#include <boost/asio.hpp>
#include <boost/beast.hpp>
#include <chrono>
#include <expected>
#include <format>
#include <iostream>
#include <memory>
#include <string>
#include <string_view>

namespace net = boost::asio;
namespace beast = boost::beast;
namespace http = beast::http;
using tcp = net::ip::tcp;

// ==========================================
// 1. C++23 零开销错误模型与鉴权上下文
// ==========================================
enum class AuthError {
    MissingHeader,
    InvalidFormat,
    TokenExpiredOrInvalid,
    Forbidden
};

struct AuthContext {
    uint64_t user_id{0};
    std::string role; // "user" 或 "admin"
};

// 工具函数：获取当前毫秒级时间戳
int64_t current_timestamp_ms() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()
    ).count();
}

// 模拟 JWT / Bearer Token 验证 (C++23 std::expected)
std::expected<AuthContext, AuthError> authenticate(
    std::string_view auth_header, 
    std::string_view required_role = ""
) {
    if (auth_header.empty()) {
        return std::unexpected(AuthError::MissingHeader);
    }

    constexpr std::string_view prefix = "Bearer ";
    if (!auth_header.starts_with(prefix)) {
        return std::unexpected(AuthError::InvalidFormat);
    }

    std::string_view token = auth_header.substr(prefix.size());

    // Mock 校验逻辑：
    // 如果 token 包含 "admin"，赋予 admin 权限；如果包含 "user"，赋予 user 权限
    AuthContext ctx;
    if (token.starts_with("mock_admin_token")) {
        ctx.user_id = 99999;
        ctx.role = "admin";
    } else if (token.starts_with("mock_user_token")) {
        ctx.user_id = 10001;
        ctx.role = "user";
    } else {
        return std::unexpected(AuthError::TokenExpiredOrInvalid);
    }

    // RBAC 角色检查
    if (!required_role.empty() && ctx.role != required_role) {
        return std::unexpected(AuthError::Forbidden);
    }

    return ctx;
}

// ==========================================
// 2. HTTP 响应辅助函数
// ==========================================
http::response<http::string_body> make_json_response(
    http::status status, 
    std::string_view body, 
    unsigned int version, 
    bool keep_alive
) {
    http::response<http::string_body> res{status, version};
    res.set(http::field::server, "Modern-Cpp23-Charging-Station-Server");
    res.set(http::field::content_type, "application/json");
    res.keep_alive(keep_alive);
    res.body() = body;
    res.prepare_payload();
    return res;
}

// ==========================================
// 3. 业务路由分发（用户与管理员认证模块）
// ==========================================
http::response<http::string_body> route_request(
    const http::request<http::string_body>& req
) {
    const auto version = req.version();
    const auto keep_alive = req.keep_alive();
    const auto method = req.method();
    const std::string_view target = req.target();
    const std::string_view auth_hdr = req[http::field::authorization];

    // ----------------------------------------------------
    // 开放接口 (无需登录)
    // ----------------------------------------------------

    // 1. 用户注册: POST /api/v1/auth/register
    if (method == http::verb::post && target == "/api/v1/auth/register") {
        std::string res_body = std::format(
            R"({{"code":0,"msg":"success","data":{{"user_id":10001,"created_at":{}}}}})",
            current_timestamp_ms()
        );
        return make_json_response(http::status::ok, res_body, version, keep_alive);
    }

    // 2. 登录接口: POST /api/v1/auth/login
    if (method == http::verb::post && target == "/api/v1/auth/login") {
        // 判断是否是管理员登录模拟（若请求体含有 admin 则返回 admin token）
        bool is_admin = req.body().contains("admin");
        std::string token = is_admin ? "mock_admin_token_xyz888" : "mock_user_token_abc123";
        std::string role = is_admin ? "admin" : "user";

        std::string res_body = std::format(
            R"({{"code":0,"msg":"success","data":{{"access_token":"{}","refresh_token":"mock_refresh_token_999","role":"{}","expires_in":7200}}}})",
            token, role
        );
        return make_json_response(http::status::ok, res_body, version, keep_alive);
    }

    // ----------------------------------------------------
    // 受保护接口 (必须登录鉴权)
    // ----------------------------------------------------

    // 3. 刷新 Token: POST /api/v1/auth/refresh
    if (method == http::verb::post && target == "/api/v1/auth/refresh") {
        auto auth_res = authenticate(auth_hdr);
        if (!auth_res) {
            return make_json_response(http::status::unauthorized, 
                R"({"code":401,"msg":"Unauthorized: Please login first"})", version, keep_alive);
        }
        std::string res_body = 
            R"({"code":0,"msg":"success","data":{"access_token":"mock_user_token_new_refreshed","expires_in":7200}})";
        return make_json_response(http::status::ok, res_body, version, keep_alive);
    }

    // 4. 用户信息查询: GET /api/v1/user/profile (User 或 Admin 均可访问)
    if (method == http::verb::get && target == "/api/v1/user/profile") {
        auto auth_res = authenticate(auth_hdr);
        if (!auth_res) {
            return make_json_response(http::status::unauthorized, 
                R"({"code":401,"msg":"Unauthorized: Token missing or invalid"})", version, keep_alive);
        }

        std::string res_body = std::format(
            R"({{"code":0,"msg":"success","data":{{"user_id":{},"nickname":"极速车主_{}","phone":"13800138000","balance":168.50,"status":1}}}})",
            auth_res->user_id, auth_res->user_id
        );
        return make_json_response(http::status::ok, res_body, version, keep_alive);
    }

    // 5. 钱包充值: POST /api/v1/user/wallet/recharge (普通用户专属)
    if (method == http::verb::post && target == "/api/v1/user/wallet/recharge") {
        auto auth_res = authenticate(auth_hdr, "user");
        if (!auth_res) {
            if (auth_res.error() == AuthError::Forbidden) {
                return make_json_response(http::status::forbidden, 
                    R"({"code":403,"msg":"Forbidden: Only normal users can recharge"})", version, keep_alive);
            }
            return make_json_response(http::status::unauthorized, 
                R"({"code":401,"msg":"Unauthorized: Please login first"})", version, keep_alive);
        }

        std::string res_body = std::format(
            R"({{"code":0,"msg":"success","data":{{"order_id":"REC_{}","new_balance":268.50,"status":"SUCCESS"}}}})",
            current_timestamp_ms()
        );
        return make_json_response(http::status::ok, res_body, version, keep_alive);
    }

    // 6. 管理端用户列表: GET /api/v1/admin/users (管理员专属)
    if (method == http::verb::get && target.starts_with("/api/v1/admin/users")) {
        auto auth_res = authenticate(auth_hdr, "admin");
        if (!auth_res) {
            if (auth_res.error() == AuthError::Forbidden) {
                return make_json_response(http::status::forbidden, 
                    R"({"code":403,"msg":"Forbidden: Admin privileges required"})", version, keep_alive);
            }
            return make_json_response(http::status::unauthorized, 
                R"({"code":401,"msg":"Unauthorized: Admin token required"})", version, keep_alive);
        }

        std::string res_body = std::format(
            R"({{"code":0,"msg":"success","data":{{"total":2,"users":[{{"user_id":10001,"phone":"13800138000","balance":168.50,"role":"user","created_at":{}}},{{"user_id":99999,"phone":"13900139000","balance":999.00,"role":"admin","created_at":{}}}]}}}})",
            current_timestamp_ms() - 86400000,
            current_timestamp_ms() - 172800000
        );
        return make_json_response(http::status::ok, res_body, version, keep_alive);
    }

    // 未匹配的路由返回 404
    return make_json_response(http::status::not_found, 
        R"({"code":404,"msg":"Endpoint not found"})", version, keep_alive);
}

// ==========================================
// 4. Boost.Asio 协程连接与监听处理
// ==========================================
net::awaitable<void> handle_session(tcp::socket socket) {
    beast::tcp_stream stream(std::move(socket));
    beast::flat_buffer buffer;

    try {
        while (true) {
            // 设置 30 秒超时时间，防止慢连接占用资源
            stream.expires_after(std::chrono::seconds(30));

            http::request<http::string_body> req;
            co_await http::async_read(stream, buffer, req, net::use_awaitable);

            // 处理路由并生成响应
            auto res = route_request(req);
            co_await http::async_write(stream, res, net::use_awaitable);

            if (res.need_eof()) {
                break;
            }
        }
    } catch (const beast::system_error& se) {
        if (se.code() != http::error::end_of_stream &&
            se.code() != net::error::operation_aborted &&
            se.code() != beast::error::timeout &&
            se.code() != net::error::connection_reset)
        {
            std::cerr << std::format("[Session Error]: {}\n", se.what());
        }
    } catch (const std::exception& e) {
        std::cerr << std::format("[Exception]: {}\n", e.what());
    }

    beast::error_code ec;
    stream.socket().shutdown(tcp::socket::shutdown_send, ec);
}

net::awaitable<void> listener(tcp::endpoint endpoint) {
    auto executor = co_await net::this_coro::executor;
    tcp::acceptor acceptor(executor, endpoint);

    std::cout << std::format("[Server] Listening on http://{}:{}\n", 
                 endpoint.address().to_string(), endpoint.port());

    while (true) {
        tcp::socket socket = co_await acceptor.async_accept(net::use_awaitable);
        // 为每个接入的 Socket 派发一个独立的轻量级协程
        net::co_spawn(executor, handle_session(std::move(socket)), net::detached);
    }
}

// ==========================================
// 5. 主程序入口
// ==========================================
int main() {
    try {
        const auto address = net::ip::make_address("0.0.0.0");
        const unsigned short port = 8080;

        net::io_context ioc{1};

        // 注册系统信号中断处理（Ctrl + C）
        net::signal_set signals(ioc, SIGINT, SIGTERM);
        signals.async_wait([&](auto, auto) {
            std::cout << "\n[Server] Shutting down gracefully...\n";
            ioc.stop();
        });

        // 启动主监听协程
        net::co_spawn(ioc, listener(tcp::endpoint{address, port}), net::detached);

        // 启动事件循环
        ioc.run();
    } catch (const std::exception& e) {
        std::cerr << std::format("[Fatal Error]: {}\n", e.what());
        return 1;
    }
    return 0;
}