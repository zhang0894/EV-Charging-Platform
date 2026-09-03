#pragma once

#include <string>
#include <string_view>
#include <optional>
#include <memory>
#include <mutex>
#include <shared_mutex>
#include <unordered_map>
#include <chrono>
#include <glaze/glaze.hpp>

namespace ev {

class RedisCache {
public:
    static RedisCache& instance();

    // 初始化 Redis 连接与配置 (支持环境变量覆盖)
    void init(
        const std::string& host = "127.0.0.1",
        unsigned short port = 6379,
        const std::string& password = ""
    );

    // 基础键值存取
    bool set(std::string_view key, std::string_view value, int ttl_seconds = 0);
    std::optional<std::string> get(std::string_view key);
    bool del(std::string_view key);
    void del_prefix(std::string_view prefix);

    // 模板对象与 Glaze JSON 自动化存取
    template <typename T>
    bool set_json(std::string_view key, const T& value, int ttl_seconds = 0) {
        std::string json_str;
        auto err = glz::write_json(value, json_str);
        if (err) return false;
        return set(key, json_str, ttl_seconds);
    }

    template <typename T>
    std::optional<T> get_json(std::string_view key) {
        auto val = get(key);
        if (!val) return std::nullopt;

        T obj;
        auto err = glz::read_json(obj, *val);
        if (err) return std::nullopt;
        return obj;
    }

    bool is_redis_online() const noexcept { return is_redis_online_; }

private:
    RedisCache() = default;
    ~RedisCache() = default;

    RedisCache(const RedisCache&) = delete;
    RedisCache& operator=(const RedisCache&) = delete;

    // RESP 协议底层发送与接收
    bool execute_redis_cmd(const std::string& cmd, std::string* response = nullptr);
    bool check_and_reconnect();

    // 内存 TTL 缓存结构 (当 Redis 不可用或离线时作为透明高可用后备)
    struct MemoryCacheItem {
        std::string value;
        int64_t expire_at_ms{0}; // 0 为永不过期
    };

    std::string host_{"127.0.0.1"};
    unsigned short port_{6379};
    std::string password_;
    bool is_redis_online_{false};
    int64_t last_reconnect_attempt_ms_{0};

    std::mutex redis_mutex_;
    void* socket_handle_{nullptr}; // 基础 socket 抽象

    // 本地快速读写锁内存缓存
    mutable std::shared_mutex local_mutex_;
    std::unordered_map<std::string, MemoryCacheItem> local_cache_;
};

} // namespace ev
