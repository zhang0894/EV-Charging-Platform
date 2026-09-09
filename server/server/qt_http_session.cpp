#include "qt_http_session.hpp"
#include "business_thread_pool.hpp"
#include "../websocket/qt_ws_session.hpp"
#include "../router/http_router.hpp"
#include "../db/db_repository.hpp"
#include <boost/beast/websocket.hpp>
#include <QPointer>
#include <sstream>
#include <iostream>
#include <print>
#include <chrono>
#include <charconv>
#include <shared_mutex>
#include <unordered_map>

namespace ev {

namespace {

struct CallerInfo {
    int64_t user_id{0};
    std::string nickname;

    std::string to_string() const {
        if (user_id > 0) {
            return std::format("{} (ID: {})", nickname.empty() ? "未知用户" : nickname, user_id);
        }
        return "访客 (ID: -)";
    }
};

std::shared_mutex s_nick_cache_mutex;
std::unordered_map<int64_t, std::string> s_nick_cache;

std::string get_user_nickname(int64_t uid) {
    if (uid <= 0) return "访客";
    {
        std::shared_lock lock(s_nick_cache_mutex);
        auto it = s_nick_cache.find(uid);
        if (it != s_nick_cache.end()) {
            return it->second;
        }
    }

    std::string nick;
    // 优先从 DbRepository 获取 (内部自带 Redis/内存缓存)
    auto user_res = DbRepository::instance().get_user_by_id(uid);
    if (user_res && !user_res->nickname.empty()) {
        nick = user_res->nickname;
    } else if (uid == 1) {
        nick = "超级管理员";
    } else {
        nick = std::format("车主_{:05d}", uid);
    }

    {
        std::unique_lock lock(s_nick_cache_mutex);
        s_nick_cache[uid] = nick;
    }
    return nick;
}

CallerInfo resolve_caller(const http::request<http::string_body>& req, const http::response<http::string_body>& res) {
    // 1. 优先从 Authorization 请求头提取 Bearer Token
    std::string_view token_str;
    auto auth_it = req.find(http::field::authorization);
    if (auth_it != req.end()) {
        token_str = auth_it->value();
        if (token_str.starts_with("Bearer ")) {
            token_str.remove_prefix(7);
        }
    }

    // 2. 若无头，尝试从 URL Query 中检索 (?token=... 或 ?access_token=...)
    if (token_str.empty()) {
        std::string_view target = req.target();
        auto q_pos = target.find('?');
        if (q_pos != std::string_view::npos) {
            std::string_view query = target.substr(q_pos + 1);
            while (!query.empty()) {
                auto amp_pos = query.find('&');
                std::string_view param = (amp_pos == std::string_view::npos) ? query : query.substr(0, amp_pos);
                if (param.starts_with("token=")) {
                    token_str = param.substr(6);
                    break;
                } else if (param.starts_with("access_token=")) {
                    token_str = param.substr(13);
                    break;
                }
                if (amp_pos == std::string_view::npos) break;
                query.remove_prefix(amp_pos + 1);
            }
        }
    }

    // 3. 从 Token 中提取 user_id (格式: EV_TOKEN.<uid>.<role>...)
    if (!token_str.empty() && token_str.starts_with("EV_TOKEN.")) {
        std::string_view rem = token_str.substr(9);
        auto dot_pos = rem.find('.');
        if (dot_pos != std::string_view::npos) {
            std::string_view uid_sv = rem.substr(0, dot_pos);
            int64_t uid = 0;
            auto [ptr, ec] = std::from_chars(uid_sv.data(), uid_sv.data() + uid_sv.size(), uid);
            if (ec == std::errc() && uid > 0) {
                return CallerInfo{
                    .user_id = uid,
                    .nickname = get_user_nickname(uid)
                };
            }
        }
    }

    // 4. 若为登录 / 注册成功响应，从 Response Body 解析刚登录成功的用户
    if (res.result() == http::status::ok && !res.body().empty()) {
        std::string_view target = req.target();
        if (target.find("/auth/login") != std::string_view::npos || target.find("/auth/register") != std::string_view::npos) {
            std::string_view body = res.body();
            auto uid_pos = body.find("\"user_id\":");
            if (uid_pos != std::string_view::npos) {
                uid_pos += 10;
                while (uid_pos < body.size() && (body[uid_pos] == ' ' || body[uid_pos] == '\t')) uid_pos++;
                int64_t uid = 0;
                auto [ptr, ec] = std::from_chars(body.data() + uid_pos, body.data() + body.size(), uid);
                if (ec == std::errc() && uid > 0) {
                    std::string nick;
                    auto nick_pos = body.find("\"nickname\":\"", uid_pos);
                    if (nick_pos == std::string_view::npos) {
                        nick_pos = body.find("\"nickname\": \"", uid_pos);
                    }
                    if (nick_pos != std::string_view::npos) {
                        auto val_start = body.find('"', nick_pos + 11);
                        if (val_start != std::string_view::npos) {
                            val_start += 1;
                            auto val_end = body.find('"', val_start);
                            if (val_end != std::string_view::npos) {
                                nick = std::string(body.substr(val_start, val_end - val_start));
                            }
                        }
                    }
                    if (nick.empty()) {
                        nick = get_user_nickname(uid);
                    } else {
                        std::unique_lock lock(s_nick_cache_mutex);
                        s_nick_cache[uid] = nick;
                    }
                    return CallerInfo{
                        .user_id = uid,
                        .nickname = nick
                    };
                }
            }
        }
    }

    // 5. 访客 / 未携带凭证请求
    return CallerInfo{
        .user_id = 0,
        .nickname = "访客"
    };
}

} // anonymous namespace

template <bool EnableLogging>
QtHttpSession<EnableLogging>::QtHttpSession(qintptr socketDescriptor, QObject* parent)
    : QObject(parent) {
    socket_ = new QTcpSocket(this);
    if (!socket_->setSocketDescriptor(socketDescriptor)) {
        this->deleteLater();
        return;
    }
    parser_ = std::make_unique<http::request_parser<http::string_body>>();
    parser_->body_limit(2 * 1024 * 1024); // 2MB 上限，由业务层校验 <1MB 并返回 413
    connect(socket_, &QTcpSocket::readyRead, this, &QtHttpSession<EnableLogging>::onReadyRead);
    connect(socket_, &QTcpSocket::disconnected, this, &QtHttpSession<EnableLogging>::onDisconnected);
}

template <bool EnableLogging>
QtHttpSession<EnableLogging>::~QtHttpSession() {
    if (socket_) {
        socket_->disconnect(this);
    }
}

template <bool EnableLogging>
void QtHttpSession<EnableLogging>::onReadyRead() {
    if (!socket_) return;
    QByteArray chunk = socket_->readAll();
    if (!chunk.isEmpty()) {
        raw_buffer_.append(chunk);
    }
    if (!is_processing_) {
        process_pipeline();
    }
}

template <bool EnableLogging>
void QtHttpSession<EnableLogging>::onDisconnected() {
    this->deleteLater();
}

template <bool EnableLogging>
void QtHttpSession<EnableLogging>::process_pipeline() {
    while (!raw_buffer_.isEmpty() && parser_ && !is_processing_) {
        beast::error_code ec;
        size_t bytes_used = parser_->put(
            boost::asio::buffer(raw_buffer_.constData(), raw_buffer_.size()),
            ec
        );

        if (ec) {
            // 解析错误或请求内容超限
            if (socket_) {
                socket_->disconnectFromHost();
            }
            return;
        }

        if (bytes_used > 0) {
            raw_buffer_.remove(0, static_cast<qsizetype>(bytes_used));
        }

        if (parser_->is_done()) {
            auto req = parser_->release();

            // 1. 判断是否为 WebSocket 升级请求
            if (beast::websocket::is_upgrade(req)) {
                if constexpr (EnableLogging) {
                    http::response<http::string_body> dummy_res;
                    auto caller = resolve_caller(req, dummy_res);
                    std::println("[DEMO-WS]   [{}] UPGRADE {:<45} -> 101 Switching Protocols",
                                 caller.to_string(),
                                 std::string_view(req.target()));
                    std::fflush(stdout);
                }

                disconnect(socket_, &QTcpSocket::readyRead, this, &QtHttpSession<EnableLogging>::onReadyRead);
                disconnect(socket_, &QTcpSocket::disconnected, this, &QtHttpSession<EnableLogging>::onDisconnected);

                // 移交套接字至 QtWsSession (由 std::shared_ptr 独立管理生命周期)
                auto ws = std::make_shared<QtWsSession>(socket_, nullptr);
                ws->start(req);

                socket_ = nullptr; // 套接字所有权移交给 QtWsSession
                this->deleteLater();
                return;
            }

            // 2. 快路径判定 (纯内存只读请求：R-Tree 空间搜桩、单站静态详情、电桩内存状态池、CORS 嗅探)
            if (HttpRouter::is_fast_path(req)) {
                if constexpr (EnableLogging) {
                    auto start_tp = std::chrono::steady_clock::now();
                    auto res = HttpRouter::instance().dispatch(req);
                    bool keep_alive = res.keep_alive();

                    auto elapsed_us = std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now() - start_tp).count();
                    double elapsed_ms = elapsed_us / 1000.0;
                    auto caller = resolve_caller(req, res);
                    std::println("[DEMO-HTTP] [{}] {:<6} {:<40} -> {} {} ({:.2f}ms, {}B) [FastPath]",
                                 caller.to_string(),
                                 std::string_view(req.method_string()),
                                 std::string_view(req.target()),
                                 res.result_int(),
                                 std::string_view(res.reason()),
                                 elapsed_ms,
                                 res.body().size());
                    std::fflush(stdout);

                    std::ostringstream oss;
                    oss << res;
                    std::string res_bytes = oss.str();

                    if (socket_ && socket_->isOpen()) {
                        socket_->write(res_bytes.data(), static_cast<qint64>(res_bytes.size()));
                    }

                    if (!keep_alive) {
                        if (socket_) {
                            socket_->disconnectFromHost();
                        }
                        return;
                    }

                    // Keep-Alive 保持连接：重置解析器并继续推进管道中下一条请求
                    parser_ = std::make_unique<http::request_parser<http::string_body>>();
                    parser_->body_limit(2 * 1024 * 1024);
                    continue;
                } else {
                    auto res = HttpRouter::instance().dispatch(req);
                    bool keep_alive = res.keep_alive();

                    std::ostringstream oss;
                    oss << res;
                    std::string res_bytes = oss.str();

                    if (socket_ && socket_->isOpen()) {
                        socket_->write(res_bytes.data(), static_cast<qint64>(res_bytes.size()));
                    }

                    if (!keep_alive) {
                        if (socket_) {
                            socket_->disconnectFromHost();
                        }
                        return;
                    }

                    // Keep-Alive 保持连接：重置解析器并继续推进管道中下一条请求
                    parser_ = std::make_unique<http::request_parser<http::string_body>>();
                    parser_->body_limit(2 * 1024 * 1024);
                    continue;
                }
            }

            // 3. 慢路径/DB操作解耦：异步投递至业务工作线程池，彻底解放 Reactor I/O 事件循环
            is_processing_ = true;
            if constexpr (EnableLogging) {
                auto start_tp = std::chrono::steady_clock::now();
                BusinessThreadPool::instance().submit([self = QPointer<QtHttpSession<EnableLogging>>(this), req = std::move(req), start_tp]() {
                    if (!self) return;

                    auto res = HttpRouter::instance().dispatch(req);
                    bool keep_alive = res.keep_alive();

                    auto elapsed_us = std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now() - start_tp).count();
                    double elapsed_ms = elapsed_us / 1000.0;
                    auto caller = resolve_caller(req, res);
                    std::println("[DEMO-HTTP] [{}] {:<6} {:<40} -> {} {} ({:.2f}ms, {}B) [WorkerPool]",
                                 caller.to_string(),
                                 std::string_view(req.method_string()),
                                 std::string_view(req.target()),
                                 res.result_int(),
                                 std::string_view(res.reason()),
                                 elapsed_ms,
                                 res.body().size());
                    std::fflush(stdout);

                    std::ostringstream oss;
                    oss << res;
                    std::string res_bytes = oss.str();

                    if (!self) return;
                    QMetaObject::invokeMethod(self, [self, res_bytes = std::move(res_bytes), keep_alive]() {
                        if (!self) return;
                        self->on_async_response_ready(std::move(res_bytes), keep_alive);
                    }, Qt::QueuedConnection);
                });
            } else {
                BusinessThreadPool::instance().submit([self = QPointer<QtHttpSession<EnableLogging>>(this), req = std::move(req)]() {
                    if (!self) return;

                    auto res = HttpRouter::instance().dispatch(req);
                    bool keep_alive = res.keep_alive();

                    std::ostringstream oss;
                    oss << res;
                    std::string res_bytes = oss.str();

                    if (!self) return;
                    QMetaObject::invokeMethod(self, [self, res_bytes = std::move(res_bytes), keep_alive]() {
                        if (!self) return;
                        self->on_async_response_ready(std::move(res_bytes), keep_alive);
                    }, Qt::QueuedConnection);
                });
            }

            break; // 跳出当前解析循环，等待该请求异步完成并写回后，再按序推进 Keep-Alive 下一条请求
        } else if (bytes_used == 0) {
            // 缓冲区内无足够字节推进解析状态，等待后续 readyRead 信号
            break;
        }
    }
}

template <bool EnableLogging>
void QtHttpSession<EnableLogging>::on_async_response_ready(std::string res_bytes, bool keep_alive) {
    if (socket_ && socket_->isOpen()) {
        socket_->write(res_bytes.data(), static_cast<qint64>(res_bytes.size()));
    }

    if (!keep_alive) {
        if (socket_) {
            socket_->disconnectFromHost();
        }
        return;
    }

    // Keep-Alive 连接复用：重置解析器并解除处理中标记
    parser_ = std::make_unique<http::request_parser<http::string_body>>();
    parser_->body_limit(2 * 1024 * 1024);
    is_processing_ = false;

    // 唤醒继续处理可能已缓存在 raw_buffer_ 中的后续请求
    process_pipeline();
}

// 显式模板实例化，生成无日志与展示日志两套独立的特化机器码
template class QtHttpSession<false>;
template class QtHttpSession<true>;

} // namespace ev
