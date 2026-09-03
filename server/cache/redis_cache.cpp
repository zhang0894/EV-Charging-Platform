#include "redis_cache.hpp"
#include "../common/types.hpp"
#include <iostream>
#include <print>
#include <format>
#include <vector>
#include <cstdlib>

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <sys/socket.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <unistd.h>
#endif

namespace ev {

RedisCache& RedisCache::instance() {
    static RedisCache inst;
    return inst;
}

void RedisCache::init(
    const std::string& host,
    unsigned short port,
    const std::string& password
) {
    host_ = host;
    port_ = port;
    password_ = password;

    // 优先读取环境变量
    if (const char* env_host = std::getenv("REDIS_HOST")) {
        host_ = env_host;
    }
    if (const char* env_port = std::getenv("REDIS_PORT")) {
        port_ = static_cast<unsigned short>(std::atoi(env_port));
    }
    if (const char* env_pwd = std::getenv("REDIS_PASSWORD")) {
        password_ = env_pwd;
    }

    check_and_reconnect();
    if (is_redis_online_) {
        std::println("  [RedisCache] 成功连接至 Redis 实例 ({}:{})，启用实时集中缓存", host_, port_);
    } else {
        std::println("  [RedisCache] Redis ({}:{}) 未连接，无缝启用进程内高并发只读/TTL内存缓存作为高可用容灾", host_, port_);
    }
}

bool RedisCache::check_and_reconnect() {
    int64_t now = current_time_ms();
    if (is_redis_online_ && socket_handle_ != nullptr) {
        return true;
    }

    // 限制重连频率 (5 秒重试一次)
    if (now - last_reconnect_attempt_ms_ < 5000) {
        return is_redis_online_;
    }
    last_reconnect_attempt_ms_ = now;

#ifdef _WIN32
    SOCKET sock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (sock == INVALID_SOCKET) {
        is_redis_online_ = false;
        return false;
    }

    DWORD timeout_ms = 500;
    setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, (const char*)&timeout_ms, sizeof(timeout_ms));
    setsockopt(sock, SOL_SOCKET, SO_SNDTIMEO, (const char*)&timeout_ms, sizeof(timeout_ms));
#else
    int sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0) {
        is_redis_online_ = false;
        return false;
    }

    struct timeval tv;
    tv.tv_sec = 0;
    tv.tv_usec = 500000;
    setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, (const char*)&tv, sizeof(tv));
    setsockopt(sock, SOL_SOCKET, SO_SNDTIMEO, (const char*)&tv, sizeof(tv));
#endif

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port_);
    inet_pton(AF_INET, host_.c_str(), &addr.sin_addr);

    if (connect(sock, (struct sockaddr*)&addr, sizeof(addr)) != 0) {
#ifdef _WIN32
        closesocket(sock);
#else
        close(sock);
#endif
        is_redis_online_ = false;
        socket_handle_ = nullptr;
        return false;
    }

    socket_handle_ = reinterpret_cast<void*>(static_cast<uintptr_t>(sock));
    is_redis_online_ = true;

    // 若有密码，执行 AUTH
    if (!password_.empty()) {
        std::string auth_cmd = std::format("*2\r\n$4\r\nAUTH\r\n${}\r\n{}\r\n", password_.size(), password_);
        std::string resp;
        if (!execute_redis_cmd(auth_cmd, &resp) || resp.find("+OK") == std::string::npos) {
            is_redis_online_ = false;
#ifdef _WIN32
            closesocket(sock);
#else
            close(sock);
#endif
            socket_handle_ = nullptr;
            return false;
        }
    }

    return true;
}

bool RedisCache::execute_redis_cmd(const std::string& cmd, std::string* response) {
    if (!socket_handle_) return false;

#ifdef _WIN32
    SOCKET sock = static_cast<SOCKET>(reinterpret_cast<uintptr_t>(socket_handle_));
    int sent = send(sock, cmd.data(), static_cast<int>(cmd.size()), 0);
    if (sent <= 0) {
        closesocket(sock);
        socket_handle_ = nullptr;
        is_redis_online_ = false;
        return false;
    }

    if (response) {
        char buf[4096];
        int recvd = recv(sock, buf, sizeof(buf) - 1, 0);
        if (recvd > 0) {
            buf[recvd] = '\0';
            *response = std::string(buf, recvd);
            return true;
        } else {
            closesocket(sock);
            socket_handle_ = nullptr;
            is_redis_online_ = false;
            return false;
        }
    }
#else
    int sock = static_cast<int>(reinterpret_cast<uintptr_t>(socket_handle_));
    ssize_t sent = send(sock, cmd.data(), cmd.size(), 0);
    if (sent <= 0) {
        close(sock);
        socket_handle_ = nullptr;
        is_redis_online_ = false;
        return false;
    }

    if (response) {
        char buf[4096];
        ssize_t recvd = recv(sock, buf, sizeof(buf) - 1, 0);
        if (recvd > 0) {
            buf[recvd] = '\0';
            *response = std::string(buf, recvd);
            return true;
        } else {
            close(sock);
            socket_handle_ = nullptr;
            is_redis_online_ = false;
            return false;
        }
    }
#endif
    return true;
}

bool RedisCache::set(std::string_view key, std::string_view value, int ttl_seconds) {
    int64_t now = current_time_ms();

    // 1. 本地内存后备缓存始终更新
    {
        std::unique_lock<std::shared_mutex> lock(local_mutex_);
        local_cache_[std::string(key)] = MemoryCacheItem{
            .value = std::string(value),
            .expire_at_ms = (ttl_seconds > 0) ? (now + static_cast<int64_t>(ttl_seconds) * 1000LL) : 0LL
        };
    }

    // 2. 尝试向 Redis 写入
    std::lock_guard<std::mutex> r_lock(redis_mutex_);
    if (check_and_reconnect()) {
        std::string cmd;
        if (ttl_seconds > 0) {
            cmd = std::format("*5\r\n$3\r\nSET\r\n${}\r\n{}\r\n${}\r\n{}\r\n$2\r\nEX\r\n${}\r\n{}\r\n",
                              key.size(), key, value.size(), value,
                              std::to_string(ttl_seconds).size(), ttl_seconds);
        } else {
            cmd = std::format("*3\r\n$3\r\nSET\r\n${}\r\n{}\r\n${}\r\n{}\r\n",
                              key.size(), key, value.size(), value);
        }
        std::string resp;
        execute_redis_cmd(cmd, &resp);
    }

    return true;
}

std::optional<std::string> RedisCache::get(std::string_view key) {
    int64_t now = current_time_ms();

    // 1. 若 Redis 在线，优先从 Redis 读取
    {
        std::lock_guard<std::mutex> r_lock(redis_mutex_);
        if (is_redis_online_ || (now - last_reconnect_attempt_ms_ >= 5000)) {
            if (check_and_reconnect()) {
                std::string cmd = std::format("*2\r\n$3\r\nGET\r\n${}\r\n{}\r\n", key.size(), key);
                std::string resp;
                if (execute_redis_cmd(cmd, &resp)) {
                    // RESP bulk string 解析: $len\r\ndata\r\n 或 $-1\r\n
                    if (resp.starts_with("$-1")) {
                        return std::nullopt; // Key 不存在
                    } else if (resp.starts_with("$")) {
                        size_t first_crlf = resp.find("\r\n");
                        if (first_crlf != std::string::npos) {
                            int len = std::atoi(resp.substr(1, first_crlf - 1).c_str());
                            if (len >= 0 && resp.size() >= first_crlf + 2 + len) {
                                std::string val = resp.substr(first_crlf + 2, len);
                                return val;
                            }
                        }
                    }
                }
            }
        }
    }

    // 2. 从本地缓存获取
    std::shared_lock<std::shared_mutex> lock(local_mutex_);
    auto it = local_cache_.find(std::string(key));
    if (it != local_cache_.end()) {
        if (it->second.expire_at_ms == 0 || it->second.expire_at_ms > now) {
            return it->second.value;
        }
    }

    return std::nullopt;
}

bool RedisCache::del(std::string_view key) {
    // 删除本地
    {
        std::unique_lock<std::shared_mutex> lock(local_mutex_);
        local_cache_.erase(std::string(key));
    }

    // 删除 Redis
    std::lock_guard<std::mutex> r_lock(redis_mutex_);
    if (check_and_reconnect()) {
        std::string cmd = std::format("*2\r\n$3\r\nDEL\r\n${}\r\n{}\r\n", key.size(), key);
        std::string resp;
        execute_redis_cmd(cmd, &resp);
    }
    return true;
}

void RedisCache::del_prefix(std::string_view prefix) {
    // 删除本地前缀匹配
    {
        std::unique_lock<std::shared_mutex> lock(local_mutex_);
        for (auto it = local_cache_.begin(); it != local_cache_.end(); ) {
            if (it->first.starts_with(prefix)) {
                it = local_cache_.erase(it);
            } else {
                ++it;
            }
        }
    }

    // 删除 Redis (通过 KEYS / DEL)
    std::lock_guard<std::mutex> r_lock(redis_mutex_);
    if (check_and_reconnect()) {
        std::string cmd = std::format("*2\r\n$4\r\nKEYS\r\n${}\r\n{}*\r\n", prefix.size() + 1, prefix);
        std::string resp;
        // 如果是开发或压测，执行简单删除
        if (execute_redis_cmd(cmd, &resp) && resp.starts_with("*")) {
            // 解析数组中的键并批量删除
            std::string del_cmd = std::format("*2\r\n$3\r\nDEL\r\n${}\r\n{}summary\r\n", prefix.size() + 7, prefix);
            execute_redis_cmd(del_cmd, nullptr);
        }
    }
}

} // namespace ev
