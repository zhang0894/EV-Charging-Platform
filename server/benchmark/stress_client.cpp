#include "../common/types.hpp"
#include <boost/asio.hpp>
#include <boost/asio/as_tuple.hpp>
#include <boost/beast/core.hpp>
#include <boost/beast/http.hpp>

#include <iostream>
#include <vector>
#include <string>
#include <string_view>
#include <thread>
#include <atomic>
#include <chrono>
#include <random>
#include <print>
#include <format>
#include <algorithm>
#include <cmath>
#include <numeric>
#include <memory>
#include <functional>
#include <fstream>
#include <unordered_map>

#ifdef _WIN32
#include <windows.h>
#include <psapi.h>
#include <tlhelp32.h>
#endif

namespace net = boost::asio;
namespace beast = boost::beast;
namespace http = beast::http;
using tcp = net::ip::tcp;

struct alignas(64) WorkerMetrics {
    uint64_t total_requests{0};
    uint64_t success_requests{0};
    uint64_t failed_requests{0};
    uint64_t total_bytes{0};
    std::vector<uint32_t> latency_samples; // 微秒样本
};

struct EndpointTierResult {
    int endpoint_id{0};
    std::string module_name;
    std::string endpoint_name;
    std::string http_method;
    int concurrency{0};
    double qps{0.0};
    double p50_ms{0.0};
    double p95_ms{0.0};
    double p99_ms{0.0};
    double avg_lat_ms{0.0};
    double avg_cpu{0.0};
    double max_ram_mb{0.0};
    double success_rate{0.0};
    uint64_t total_requests{0};
};

struct ApiTestCase {
    int id;
    std::string module;
    std::string name;
    std::string method;
    std::function<void(http::request<http::string_body>&, const std::string&, const std::string&, int, std::mt19937&)> build_request;
};

// 全局 42 个非 WebSocket HTTP 接口测试用例注册表
inline std::vector<ApiTestCase> create_all_test_cases() {
    std::vector<ApiTestCase> cases;

    // =========================================================================
    // 模块 A：用户认证与个人中心 (11 个)
    // =========================================================================
    cases.push_back({
        1, "用户认证与个人中心", "POST /api/v1/auth/login", "POST",
        [](http::request<http::string_body>& req, const std::string& user_token, const std::string& admin_token, int wid, std::mt19937& rng) {
            req.method(http::verb::post);
            req.target("/api/v1/auth/login");
            int uid = 1 + (wid % 20);
            req.body() = std::format("{{\"phone\":\"138{:08d}\",\"auth_type\":\"passwordless\"}}", uid);
        }
    });

    cases.push_back({
        2, "用户认证与个人中心", "POST /api/v1/auth/login-password", "POST",
        [](http::request<http::string_body>& req, const std::string& user_token, const std::string& admin_token, int wid, std::mt19937& rng) {
            req.method(http::verb::post);
            req.target("/api/v1/auth/login-password");
            int uid = 1 + (wid % 20);
            req.body() = std::format("{{\"phone\":\"138{:08d}\",\"password\":\"123456\"}}", uid);
        }
    });

    cases.push_back({
        3, "用户认证与个人中心", "POST /api/v1/auth/register", "POST",
        [](http::request<http::string_body>& req, const std::string& user_token, const std::string& admin_token, int wid, std::mt19937& rng) {
            req.method(http::verb::post);
            req.target("/api/v1/auth/register");
            uint32_t r = rng() % 90000000 + 10000000;
            req.body() = std::format("{{\"phone\":\"199{}\",\"password\":\"123456\",\"nickname\":\"BenchUser\"}}", r);
        }
    });

    cases.push_back({
        4, "用户认证与个人中心", "GET /api/v1/auth/check-phone", "GET",
        [](http::request<http::string_body>& req, const std::string& user_token, const std::string& admin_token, int wid, std::mt19937& rng) {
            req.method(http::verb::get);
            int uid = 1 + (wid % 20);
            req.target(std::format("/api/v1/auth/check-phone?phone=138{:08d}", uid));
        }
    });

    cases.push_back({
        5, "用户认证与个人中心", "POST /api/v1/auth/refresh", "POST",
        [](http::request<http::string_body>& req, const std::string& user_token, const std::string& admin_token, int wid, std::mt19937& rng) {
            req.method(http::verb::post);
            req.target("/api/v1/auth/refresh");
            req.body() = std::format("{{\"refresh_token\":\"{}\"}}", user_token);
        }
    });

    cases.push_back({
        6, "用户认证与个人中心", "POST /api/v1/auth/change-password", "POST",
        [](http::request<http::string_body>& req, const std::string& user_token, const std::string& admin_token, int wid, std::mt19937& rng) {
            req.method(http::verb::post);
            req.target("/api/v1/auth/change-password");
            int uid = 1 + (wid % 20);
            req.body() = std::format("{{\"phone\":\"138{:08d}\",\"old_password\":\"123456\",\"new_password\":\"123456\"}}", uid);
        }
    });

    cases.push_back({
        7, "用户认证与个人中心", "GET /api/v1/user/profile", "GET",
        [](http::request<http::string_body>& req, const std::string& user_token, const std::string& admin_token, int wid, std::mt19937& rng) {
            req.method(http::verb::get);
            req.target("/api/v1/user/profile");
            req.set(http::field::authorization, "Bearer " + user_token);
        }
    });

    cases.push_back({
        8, "用户认证与个人中心", "PUT /api/v1/user/profile", "PUT",
        [](http::request<http::string_body>& req, const std::string& user_token, const std::string& admin_token, int wid, std::mt19937& rng) {
            req.method(http::verb::put);
            req.target("/api/v1/user/profile");
            req.set(http::field::authorization, "Bearer " + user_token);
            req.body() = "{\"nickname\":\"Driver_Bench\"}";
        }
    });

    cases.push_back({
        9, "用户认证与个人中心", "GET /api/v1/user/avatar", "GET",
        [](http::request<http::string_body>& req, const std::string& user_token, const std::string& admin_token, int wid, std::mt19937& rng) {
            req.method(http::verb::get);
            req.target("/api/v1/user/avatar");
            req.set(http::field::authorization, "Bearer " + user_token);
        }
    });

    cases.push_back({
        10, "用户认证与个人中心", "POST /api/v1/user/avatar", "POST",
        [](http::request<http::string_body>& req, const std::string& user_token, const std::string& admin_token, int wid, std::mt19937& rng) {
            req.method(http::verb::post);
            req.target("/api/v1/user/avatar");
            req.set(http::field::authorization, "Bearer " + user_token);
            req.set(http::field::content_type, "image/png");
            req.body() = "\x89PNG\r\n\x1a\n\x00\x00\x00\rIHDR\x00\x00\x00\x01\x00\x00\x00\x01\x08\x06\x00\x00\x00\x1f\x15\xc4\x89";
        }
    });

    cases.push_back({
        11, "用户认证与个人中心", "POST /api/v1/user/change-password", "POST",
        [](http::request<http::string_body>& req, const std::string& user_token, const std::string& admin_token, int wid, std::mt19937& rng) {
            req.method(http::verb::post);
            req.target("/api/v1/user/change-password");
            req.set(http::field::authorization, "Bearer " + user_token);
            req.body() = "{\"old_password\":\"123456\",\"new_password\":\"123456\"}";
        }
    });

    // =========================================================================
    // 模块 B：钱包账户与资金交易 (3 个)
    // =========================================================================
    cases.push_back({
        12, "钱包账户与资金交易", "GET /api/v1/wallet/balance", "GET",
        [](http::request<http::string_body>& req, const std::string& user_token, const std::string& admin_token, int wid, std::mt19937& rng) {
            req.method(http::verb::get);
            req.target("/api/v1/wallet/balance");
            req.set(http::field::authorization, "Bearer " + user_token);
        }
    });

    cases.push_back({
        13, "钱包账户与资金交易", "POST /api/v1/wallet/recharge", "POST",
        [](http::request<http::string_body>& req, const std::string& user_token, const std::string& admin_token, int wid, std::mt19937& rng) {
            req.method(http::verb::post);
            req.target("/api/v1/wallet/recharge");
            req.set(http::field::authorization, "Bearer " + user_token);
            uint64_t r1 = rng();
            uint64_t r2 = rng();
            req.set("Idempotency-Key", std::format("REC-{:08X}{:08X}", r1, r2));
            req.body() = "{\"amount\":10.00,\"amount_cents\":1000,\"payment_method\":\"MOCK_PAY\",\"remark\":\"压测充值\"}";
        }
    });

    cases.push_back({
        14, "钱包账户与资金交易", "GET /api/v1/user/wallet/transactions", "GET",
        [](http::request<http::string_body>& req, const std::string& user_token, const std::string& admin_token, int wid, std::mt19937& rng) {
            req.method(http::verb::get);
            req.target("/api/v1/user/wallet/transactions?page=1&page_size=20");
            req.set(http::field::authorization, "Bearer " + user_token);
        }
    });

    // =========================================================================
    // 模块 C：充电站与充电桩综合查询 (3 个)
    // =========================================================================
    cases.push_back({
        15, "充电站与电桩综合查询", "GET /api/v1/stations/inquire", "GET",
        [](http::request<http::string_body>& req, const std::string& user_token, const std::string& admin_token, int wid, std::mt19937& rng) {
            req.method(http::verb::get);
            double lat = 39.7500 + (rng() % 4000) / 10000.0;
            double lon = 116.2000 + (rng() % 4000) / 10000.0;
            req.target(std::format("/api/v1/stations/inquire?latitude={:.4f}&longitude={:.4f}&page=1&page_size=20", lat, lon));
            req.set(http::field::authorization, "Bearer " + user_token);
        }
    });

    cases.push_back({
        16, "充电站与电桩综合查询", "GET /api/v1/stations/{station_id}", "GET",
        [](http::request<http::string_body>& req, const std::string& user_token, const std::string& admin_token, int wid, std::mt19937& rng) {
            req.method(http::verb::get);
            int sid = 1 + (rng() % 8565);
            req.target(std::format("/api/v1/stations/{}", sid));
            req.set(http::field::authorization, "Bearer " + user_token);
        }
    });

    cases.push_back({
        17, "充电站与电桩综合查询", "GET /api/v1/piles", "GET",
        [](http::request<http::string_body>& req, const std::string& user_token, const std::string& admin_token, int wid, std::mt19937& rng) {
            req.method(http::verb::get);
            int sid = 1 + (rng() % 8565);
            req.target(std::format("/api/v1/piles?station_id={}&page=1&page_size=30", sid));
            req.set(http::field::authorization, "Bearer " + user_token);
        }
    });

    // =========================================================================
    // 模块 D：充电核心业务与用户订单 (9 个)
    // =========================================================================
    cases.push_back({
        18, "充电业务与用户订单", "GET /api/v1/charging/active-order", "GET",
        [](http::request<http::string_body>& req, const std::string& user_token, const std::string& admin_token, int wid, std::mt19937& rng) {
            req.method(http::verb::get);
            req.target("/api/v1/charging/active-order");
            req.set(http::field::authorization, "Bearer " + user_token);
        }
    });

    cases.push_back({
        19, "充电业务与用户订单", "POST /api/v1/charging/start", "POST",
        [](http::request<http::string_body>& req, const std::string& user_token, const std::string& admin_token, int wid, std::mt19937& rng) {
            req.method(http::verb::post);
            req.target("/api/v1/charging/start");
            req.set(http::field::authorization, "Bearer " + user_token);
            int st_id = 1 + (rng() % 100);
            int p_idx = 1 + (rng() % 5);
            req.body() = std::format("{{\"pile_id\":\"P{:05d}_{:02d}\",\"strategy\":1,\"strategy_param\":0}}", st_id, p_idx);
        }
    });

    cases.push_back({
        20, "充电业务与用户订单", "POST /api/v1/charging/stop", "POST",
        [](http::request<http::string_body>& req, const std::string& user_token, const std::string& admin_token, int wid, std::mt19937& rng) {
            req.method(http::verb::post);
            req.target("/api/v1/charging/stop");
            req.set(http::field::authorization, "Bearer " + user_token);
            req.body() = "{\"order_id\":\"ORD_BENCH_TEST\"}";
        }
    });

    cases.push_back({
        21, "充电业务与用户订单", "POST /api/v1/charging/settle", "POST",
        [](http::request<http::string_body>& req, const std::string& user_token, const std::string& admin_token, int wid, std::mt19937& rng) {
            req.method(http::verb::post);
            req.target("/api/v1/charging/settle");
            req.set(http::field::authorization, "Bearer " + user_token);
            req.body() = "{\"order_id\":\"ORD_BENCH_TEST\"}";
        }
    });

    cases.push_back({
        22, "充电业务与用户订单", "POST /api/v1/charging/reserve", "POST",
        [](http::request<http::string_body>& req, const std::string& user_token, const std::string& admin_token, int wid, std::mt19937& rng) {
            req.method(http::verb::post);
            req.target("/api/v1/charging/reserve");
            req.set(http::field::authorization, "Bearer " + user_token);
            int st_id = 1 + (rng() % 100);
            int p_idx = 1 + (rng() % 5);
            req.body() = std::format("{{\"pile_id\":\"P{:05d}_{:02d}\"}}", st_id, p_idx);
        }
    });

    cases.push_back({
        23, "充电业务与用户订单", "POST /api/v1/charging/cancel-reservation", "POST",
        [](http::request<http::string_body>& req, const std::string& user_token, const std::string& admin_token, int wid, std::mt19937& rng) {
            req.method(http::verb::post);
            req.target("/api/v1/charging/cancel-reservation");
            req.set(http::field::authorization, "Bearer " + user_token);
            req.body() = "{\"reservation_id\":\"RSV_BENCH_TEST\"}";
        }
    });

    cases.push_back({
        24, "充电业务与用户订单", "GET /api/v1/charging/active-reservation", "GET",
        [](http::request<http::string_body>& req, const std::string& user_token, const std::string& admin_token, int wid, std::mt19937& rng) {
            req.method(http::verb::get);
            req.target("/api/v1/charging/active-reservation");
            req.set(http::field::authorization, "Bearer " + user_token);
        }
    });

    cases.push_back({
        25, "充电业务与用户订单", "GET /api/v1/orders/my", "GET",
        [](http::request<http::string_body>& req, const std::string& user_token, const std::string& admin_token, int wid, std::mt19937& rng) {
            req.method(http::verb::get);
            req.target("/api/v1/orders/my?page=1&page_size=10");
            req.set(http::field::authorization, "Bearer " + user_token);
        }
    });

    cases.push_back({
        26, "充电业务与用户订单", "GET /api/v1/charging/orders/{order_id}", "GET",
        [](http::request<http::string_body>& req, const std::string& user_token, const std::string& admin_token, int wid, std::mt19937& rng) {
            req.method(http::verb::get);
            req.target("/api/v1/charging/orders/ORD_BENCH_TEST");
            req.set(http::field::authorization, "Bearer " + user_token);
        }
    });

    // =========================================================================
    // 模块 E：PC 运营管理端综合管控 (16 个)
    // =========================================================================
    cases.push_back({
        27, "运营管理端综合管控", "POST /api/v1/admin/auth/login", "POST",
        [](http::request<http::string_body>& req, const std::string& user_token, const std::string& admin_token, int wid, std::mt19937& rng) {
            req.method(http::verb::post);
            req.target("/api/v1/admin/auth/login");
            req.body() = "{\"account\":\"admin\",\"password\":\"Express1.\"}";
        }
    });

    cases.push_back({
        28, "运营管理端综合管控", "GET /api/v1/admin/dashboard/summary", "GET",
        [](http::request<http::string_body>& req, const std::string& user_token, const std::string& admin_token, int wid, std::mt19937& rng) {
            req.method(http::verb::get);
            req.target("/api/v1/admin/dashboard/summary");
            req.set(http::field::authorization, "Bearer " + admin_token);
        }
    });

    cases.push_back({
        29, "运营管理端综合管控", "GET /api/v1/admin/dashboard/revenue-trend", "GET",
        [](http::request<http::string_body>& req, const std::string& user_token, const std::string& admin_token, int wid, std::mt19937& rng) {
            req.method(http::verb::get);
            req.target("/api/v1/admin/dashboard/revenue-trend?days=30");
            req.set(http::field::authorization, "Bearer " + admin_token);
        }
    });

    cases.push_back({
        30, "运营管理端综合管控", "GET /api/v1/admin/dashboard/pile-status-overview", "GET",
        [](http::request<http::string_body>& req, const std::string& user_token, const std::string& admin_token, int wid, std::mt19937& rng) {
            req.method(http::verb::get);
            req.target("/api/v1/admin/dashboard/pile-status-overview");
            req.set(http::field::authorization, "Bearer " + admin_token);
        }
    });

    cases.push_back({
        31, "运营管理端综合管控", "GET /api/v1/admin/stations/{station_id}/sales", "GET",
        [](http::request<http::string_body>& req, const std::string& user_token, const std::string& admin_token, int wid, std::mt19937& rng) {
            req.method(http::verb::get);
            int sid = 1 + (rng() % 100);
            req.target(std::format("/api/v1/admin/stations/{}/sales?time_range=today", sid));
            req.set(http::field::authorization, "Bearer " + admin_token);
        }
    });

    cases.push_back({
        32, "运营管理端综合管控", "POST /api/v1/admin/stations/{station_id}/online", "POST",
        [](http::request<http::string_body>& req, const std::string& user_token, const std::string& admin_token, int wid, std::mt19937& rng) {
            req.method(http::verb::post);
            int sid = 1 + (rng() % 100);
            req.target(std::format("/api/v1/admin/stations/{}/online", sid));
            req.set(http::field::authorization, "Bearer " + admin_token);
        }
    });

    cases.push_back({
        33, "运营管理端综合管控", "POST /api/v1/admin/stations/{station_id}/offline", "POST",
        [](http::request<http::string_body>& req, const std::string& user_token, const std::string& admin_token, int wid, std::mt19937& rng) {
            req.method(http::verb::post);
            int sid = 1 + (rng() % 100);
            req.target(std::format("/api/v1/admin/stations/{}/offline", sid));
            req.set(http::field::authorization, "Bearer " + admin_token);
        }
    });

    cases.push_back({
        34, "运营管理端综合管控", "POST /api/v1/admin/piles", "POST",
        [](http::request<http::string_body>& req, const std::string& user_token, const std::string& admin_token, int wid, std::mt19937& rng) {
            req.method(http::verb::post);
            req.target("/api/v1/admin/piles");
            req.set(http::field::authorization, "Bearer " + admin_token);
            req.body() = "{\"station_id\":1,\"pile_name\":\"新建测试电桩\",\"type\":\"FAST\",\"power_kw\":120.0}";
        }
    });

    cases.push_back({
        35, "运营管理端综合管控", "POST /api/v1/admin/piles/{pile_id}/restart", "POST",
        [](http::request<http::string_body>& req, const std::string& user_token, const std::string& admin_token, int wid, std::mt19937& rng) {
            req.method(http::verb::post);
            req.target("/api/v1/admin/piles/P00001_01/restart");
            req.set(http::field::authorization, "Bearer " + admin_token);
        }
    });

    cases.push_back({
        36, "运营管理端综合管控", "PUT /api/v1/admin/piles/{pile_id}/status", "PUT",
        [](http::request<http::string_body>& req, const std::string& user_token, const std::string& admin_token, int wid, std::mt19937& rng) {
            req.method(http::verb::put);
            req.target("/api/v1/admin/piles/P00001_01/status");
            req.set(http::field::authorization, "Bearer " + admin_token);
            req.body() = "{\"status\":\"IDLE\"}";
        }
    });

    cases.push_back({
        37, "运营管理端综合管控", "GET /api/v1/admin/users", "GET",
        [](http::request<http::string_body>& req, const std::string& user_token, const std::string& admin_token, int wid, std::mt19937& rng) {
            req.method(http::verb::get);
            req.target("/api/v1/admin/users?page=1&page_size=10");
            req.set(http::field::authorization, "Bearer " + admin_token);
        }
    });

    cases.push_back({
        38, "运营管理端综合管控", "PUT /api/v1/admin/users/{user_id}/status", "PUT",
        [](http::request<http::string_body>& req, const std::string& user_token, const std::string& admin_token, int wid, std::mt19937& rng) {
            req.method(http::verb::put);
            req.target("/api/v1/admin/users/10/status");
            req.set(http::field::authorization, "Bearer " + admin_token);
            req.body() = "{\"status\":1}"; // 保持为正常状态
        }
    });

    cases.push_back({
        39, "运营管理端综合管控", "POST /api/v1/admin/users/{user_id}/adjust-wallet", "POST",
        [](http::request<http::string_body>& req, const std::string& user_token, const std::string& admin_token, int wid, std::mt19937& rng) {
            req.method(http::verb::post);
            req.target("/api/v1/admin/users/10/adjust-wallet");
            req.set(http::field::authorization, "Bearer " + admin_token);
            uint64_t r1 = rng();
            uint64_t r2 = rng();
            req.set("Idempotency-Key", std::format("ADJ-{:08X}{:08X}", r1, r2));
            req.body() = "{\"amount\":1.00,\"amount_cents\":100,\"remark\":\"压测调账\"}";
        }
    });

    cases.push_back({
        40, "运营管理端综合管控", "GET /api/v1/admin/orders", "GET",
        [](http::request<http::string_body>& req, const std::string& user_token, const std::string& admin_token, int wid, std::mt19937& rng) {
            req.method(http::verb::get);
            req.target("/api/v1/admin/orders?page=1&page_size=10");
            req.set(http::field::authorization, "Bearer " + admin_token);
        }
    });

    cases.push_back({
        41, "运营管理端综合管控", "GET /api/v1/admin/orders/{order_id}", "GET",
        [](http::request<http::string_body>& req, const std::string& user_token, const std::string& admin_token, int wid, std::mt19937& rng) {
            req.method(http::verb::get);
            req.target("/api/v1/admin/orders/ORD_BENCH_TEST");
            req.set(http::field::authorization, "Bearer " + admin_token);
        }
    });

    cases.push_back({
        42, "运营管理端综合管控", "POST /api/v1/admin/orders/{order_id}/refund", "POST",
        [](http::request<http::string_body>& req, const std::string& user_token, const std::string& admin_token, int wid, std::mt19937& rng) {
            req.method(http::verb::post);
            req.target("/api/v1/admin/orders/ORD_BENCH_TEST/refund");
            req.set(http::field::authorization, "Bearer " + admin_token);
            req.body() = "{\"refund_amount\":1.00,\"refund_amount_cents\":100,\"reason\":\"压测退款\"}";
        }
    });

    return cases;
}

class SingleEndpointStressRunner {
public:
    SingleEndpointStressRunner(
        std::string host,
        unsigned short port,
        int concurrency,
        int duration_seconds,
        int server_pid,
        const ApiTestCase& test_case
    ) : host_(std::move(host)),
        port_(port),
        concurrency_(concurrency),
        duration_seconds_(duration_seconds),
        server_pid_(server_pid),
        test_case_(test_case),
        worker_metrics_(concurrency) {}

    EndpointTierResult run() {
#ifdef _WIN32
        DWORD_PTR client_affinity = 0xFFFFFFF0ULL; // 绑定到核心 4~31
        SetProcessAffinityMask(GetCurrentProcess(), client_affinity);
#endif

        init_auth_tokens();

        endpoints_.clear();
        boost::system::error_code ec;
        auto addr = net::ip::make_address(host_, ec);
        if (!ec) {
            endpoints_.emplace_back(addr, port_);
        } else {
            net::io_context res_ioc;
            tcp::resolver resolver(res_ioc);
            auto results = resolver.resolve(host_, std::to_string(port_), ec);
            if (!ec) {
                for (auto it = results.begin(); it != results.end(); ++it) {
                    endpoints_.push_back(*it);
                }
            }
        }
        if (endpoints_.empty()) {
            endpoints_.emplace_back(net::ip::make_address("127.0.0.1"), port_);
        }

        is_running_ = true;
        auto start_time = std::chrono::steady_clock::now();

        std::thread monitor_thread(&SingleEndpointStressRunner::monitor_loop, this, start_time);

        int num_threads = std::clamp(static_cast<int>(std::thread::hardware_concurrency()) - 4, 4, 16);
        std::vector<std::thread> worker_threads;
        net::io_context ioc(num_threads);

        for (int i = 0; i < concurrency_; ++i) {
            worker_metrics_[i].latency_samples.reserve(30000);
            net::co_spawn(ioc, worker_coroutine(ioc, i), net::detached);
        }

        for (int t = 0; t < num_threads; ++t) {
            worker_threads.emplace_back([&ioc]() { ioc.run(); });
        }

        std::this_thread::sleep_for(std::chrono::seconds(duration_seconds_));
        is_running_ = false;

        ioc.stop();
        for (auto& th : worker_threads) {
            if (th.joinable()) th.join();
        }
        if (monitor_thread.joinable()) monitor_thread.join();

        return calculate_report(start_time);
    }

private:
    std::vector<tcp::endpoint> endpoints_;

    void init_auth_tokens() {
        int64_t now = ev::current_time_ms();
        int64_t exp = now + 86400000LL * 7LL; // 7 天
        user_tokens_.clear();
        for (int i = 1; i <= 100; ++i) {
            user_tokens_.push_back(std::format("EV_TOKEN.{}.user.{}.{}.SIG_{}", i, exp, now, (i * 31 + exp % 9973)));
        }
        admin_token_ = std::format("EV_TOKEN.1.admin.{}.{}.SIG_{}", exp, now, (1 * 31 + exp % 9973));
    }

    net::awaitable<void> worker_coroutine(net::io_context& ioc, int worker_id) {
        std::mt19937 rng(1337 + worker_id);
        auto& wm = worker_metrics_[worker_id];

        const std::string& u_tok = user_tokens_[worker_id % user_tokens_.size()];

        while (is_running_) {
            try {
                beast::tcp_stream stream(ioc);
                stream.expires_after(std::chrono::seconds(10));
                co_await stream.async_connect(endpoints_, net::use_awaitable);
                stream.socket().set_option(tcp::no_delay(true));

                beast::flat_buffer buffer;
                http::request<http::string_body> req;
                http::response<http::string_body> resp;

                while (is_running_) {
                    req = {};
                    req.version(11);
                    req.keep_alive(true);
                    req.set(http::field::host, host_);
                    req.set(http::field::user_agent, "EV-Bench-Suite/3.0");
                    req.set(http::field::content_type, "application/json");

                    test_case_.build_request(req, u_tok, admin_token_, worker_id, rng);
                    req.prepare_payload();

                    auto req_start = std::chrono::steady_clock::now();
                    stream.expires_after(std::chrono::seconds(5));
                    co_await http::async_write(stream, req, net::use_awaitable);

                    buffer.clear();
                    resp = {};
                    co_await http::async_read(stream, buffer, resp, net::use_awaitable);

                    auto req_end = std::chrono::steady_clock::now();
                    uint32_t lat_us = static_cast<uint32_t>(
                        std::chrono::duration_cast<std::chrono::microseconds>(req_end - req_start).count()
                    );

                    wm.total_requests++;
                    // 业务上 200 为成功，状态冲突等业务异常亦记录为服务已处理响应
                    unsigned int status_val = resp.result_int();
                    if (status_val >= 200 && status_val < 300) {
                        wm.success_requests++;
                        wm.total_bytes += resp.body().size();
                    } else if (resp.result() == http::status::conflict) {
                        wm.success_requests++;
                        wm.total_bytes += resp.body().size();
                    } else {
                        wm.failed_requests++;
                    }

                    if (wm.latency_samples.size() < 30000) {
                        wm.latency_samples.push_back(lat_us);
                    }

                    if (!resp.keep_alive()) {
                        break;
                    }
                }
            } catch (...) {
                // 连接断开
            }

            if (!is_running_) break;
            boost::asio::steady_timer sleep_timer(ioc);
            sleep_timer.expires_after(std::chrono::milliseconds(20));
            co_await sleep_timer.async_wait(net::use_awaitable);
        }
    }

    void monitor_loop(std::chrono::steady_clock::time_point start_time) {
        uint64_t last_requests = 0;

        while (is_running_) {
            std::this_thread::sleep_for(std::chrono::seconds(2));
            if (!is_running_) break;

            auto now = std::chrono::steady_clock::now();
            int elapsed = static_cast<int>(std::chrono::duration_cast<std::chrono::seconds>(now - start_time).count());

            uint64_t current_reqs = 0;
            uint64_t current_succ = 0;
            for (const auto& wm : worker_metrics_) {
                current_reqs += wm.total_requests;
                current_succ += wm.success_requests;
            }

            uint64_t delta_reqs = current_reqs - last_requests;
            double interval_qps = static_cast<double>(delta_reqs) / 2.0;
            last_requests = current_reqs;

            double succ_rate = current_reqs > 0 ? (static_cast<double>(current_succ) / current_reqs * 100.0) : 100.0;

            auto [cpu_pct, ram_mb] = sample_server_resources();
            if (cpu_pct > 0.0) cpu_samples_.push_back(cpu_pct);
            if (ram_mb > 0.0) ram_samples_.push_back(ram_mb);
            qps_samples_.push_back(interval_qps);

            std::println("    [{:>3}s/{:>2}s] 瞬时QPS: {:>8.1f} | 累计请求: {:>8} | 成功率: {:>6.2f}% | 2核CPU: {:>5.1f}% | 内存: {:>6.1f} MB",
                         elapsed, duration_seconds_, interval_qps, current_reqs, succ_rate, cpu_pct, ram_mb);
        }
    }

    std::pair<double, double> sample_server_resources() {
#ifdef _WIN32
        if (server_pid_ > 0) {
            HANDLE hProcess = OpenProcess(PROCESS_QUERY_INFORMATION | PROCESS_VM_READ, FALSE, server_pid_);
            if (hProcess) {
                PROCESS_MEMORY_COUNTERS pmc;
                double ram_mb = 0.0;
                if (GetProcessMemoryInfo(hProcess, &pmc, sizeof(pmc))) {
                    ram_mb = static_cast<double>(pmc.WorkingSetSize) / (1024.0 * 1024.0);
                }

                FILETIME ftCreation, ftExit, ftKernel, ftUser;
                double cpu_pct = 0.0;
                if (GetProcessTimes(hProcess, &ftCreation, &ftExit, &ftKernel, &ftUser)) {
                    ULARGE_INTEGER kTime, uTime;
                    kTime.LowPart = ftKernel.dwLowDateTime;
                    kTime.HighPart = ftKernel.dwHighDateTime;
                    uTime.LowPart = ftUser.dwLowDateTime;
                    uTime.HighPart = ftUser.dwHighDateTime;
                    uint64_t total_proc_time = kTime.QuadPart + uTime.QuadPart;

                    auto now_time = std::chrono::steady_clock::now();
                    if (last_proc_time_ > 0 && last_sample_time_.time_since_epoch().count() > 0) {
                        double wall_time_sec = std::chrono::duration_cast<std::chrono::duration<double>>(now_time - last_sample_time_).count();
                        if (wall_time_sec > 0.0) {
                            double proc_time_sec = static_cast<double>(total_proc_time - last_proc_time_) / 10000000.0;
                            cpu_pct = (proc_time_sec / (wall_time_sec * 2.0)) * 100.0;
                            if (cpu_pct > 100.0) cpu_pct = 100.0;
                        }
                    }
                    last_proc_time_ = total_proc_time;
                    last_sample_time_ = now_time;
                }

                CloseHandle(hProcess);
                return {cpu_pct, ram_mb};
            }
        }
#endif
        return {0.0, 0.0};
    }

    EndpointTierResult calculate_report(std::chrono::steady_clock::time_point start_time) {
        auto end_time = std::chrono::steady_clock::now();
        double total_duration_sec = std::chrono::duration_cast<std::chrono::duration<double>>(end_time - start_time).count();

        uint64_t total_reqs = 0, total_succ = 0, total_fail = 0;
        std::vector<uint32_t> all_lats;

        for (const auto& wm : worker_metrics_) {
            total_reqs += wm.total_requests;
            total_succ += wm.success_requests;
            total_fail += wm.failed_requests;
            all_lats.insert(all_lats.end(), wm.latency_samples.begin(), wm.latency_samples.end());
        }

        double overall_qps = total_duration_sec > 0 ? (static_cast<double>(total_reqs) / total_duration_sec) : 0.0;
        std::sort(all_lats.begin(), all_lats.end());

        auto get_percentile = [](const std::vector<uint32_t>& sorted, double p) -> double {
            if (sorted.empty()) return 0.0;
            size_t idx = static_cast<size_t>(std::ceil(p * sorted.size())) - 1;
            idx = std::min(idx, sorted.size() - 1);
            return static_cast<double>(sorted[idx]) / 1000.0;
        };

        double p50 = get_percentile(all_lats, 0.50);
        double p95 = get_percentile(all_lats, 0.95);
        double p99 = get_percentile(all_lats, 0.99);
        double avg_lat = all_lats.empty() ? 0.0 : (std::accumulate(all_lats.begin(), all_lats.end(), 0.0) / all_lats.size() / 1000.0);

        double max_ram = ram_samples_.empty() ? 0.0 : *std::max_element(ram_samples_.begin(), ram_samples_.end());
        double avg_cpu = cpu_samples_.empty() ? 0.0 : (std::accumulate(cpu_samples_.begin(), cpu_samples_.end(), 0.0) / cpu_samples_.size());
        double succ_rate = total_reqs > 0 ? (total_succ * 100.0 / total_reqs) : 100.0;

        return EndpointTierResult{
            .endpoint_id = test_case_.id,
            .module_name = test_case_.module,
            .endpoint_name = test_case_.name,
            .http_method = test_case_.method,
            .concurrency = concurrency_,
            .qps = overall_qps,
            .p50_ms = p50,
            .p95_ms = p95,
            .p99_ms = p99,
            .avg_lat_ms = avg_lat,
            .avg_cpu = avg_cpu,
            .max_ram_mb = max_ram,
            .success_rate = succ_rate,
            .total_requests = total_reqs
        };
    }

    std::string host_;
    unsigned short port_;
    int concurrency_;
    int duration_seconds_;
    int server_pid_{0};
    ApiTestCase test_case_;

    std::atomic<bool> is_running_{false};
    std::vector<WorkerMetrics> worker_metrics_;

    std::vector<std::string> user_tokens_;
    std::string admin_token_;

    std::vector<double> cpu_samples_;
    std::vector<double> qps_samples_;
    std::vector<double> ram_samples_;

    uint64_t last_proc_time_{0};
    std::chrono::steady_clock::time_point last_sample_time_;
};

struct WeightedTestCase {
    ApiTestCase test_case;
    double weight{1.0};
};

inline std::vector<WeightedTestCase> create_client_mix_test_cases() {
    auto all_cases = create_all_test_cases();
    std::unordered_map<int, double> weights = {
        {15, 20.0}, // GET /api/v1/stations/inquire (20%)
        {16, 20.0}, // GET /api/v1/stations/{station_id} (20%)
        {17, 10.0}, // GET /api/v1/piles (10%)
        {18, 12.0}, // GET /api/v1/charging/active-order (12%)
        {12,  8.0}, // GET /api/v1/wallet/balance (8%)
        {24,  5.0}, // GET /api/v1/charging/active-reservation (5%)
        {25,  4.0}, // GET /api/v1/orders/my (4%)
        {14,  3.0}, // GET /api/v1/user/wallet/transactions (3%)
        {19,  2.0}, // POST /api/v1/charging/start (2%)
        {20,  2.0}, // POST /api/v1/charging/stop (2%)
        {21,  2.0}, // POST /api/v1/charging/settle (2%)
        {26,  1.5}, // GET /api/v1/charging/orders/{order_id} (1.5%)
        {22,  1.5}, // POST /api/v1/charging/reserve (1.5%)
        {1,   1.5}, // POST /api/v1/auth/login (1.5%)
        {5,   1.5}, // POST /api/v1/auth/refresh (1.5%)
        {23,  1.0}, // POST /api/v1/charging/cancel-reservation (1%)
        {2,   1.0}, // POST /api/v1/auth/login-password (1%)
        {13,  1.0}, // POST /api/v1/wallet/recharge (1%)
        {4,   0.5}, // GET /api/v1/auth/check-phone (0.5%)
        {3,   0.5}, // POST /api/v1/auth/register (0.5%)
        {7,   0.5}, // GET /api/v1/user/profile (0.5%)
        {9,   0.5}, // GET /api/v1/user/avatar (0.5%)
        {8,   0.4}, // PUT /api/v1/user/profile (0.4%)
        {10,  0.2}, // POST /api/v1/user/avatar (0.2%)
        {6,   0.2}, // POST /api/v1/auth/change-password (0.2%)
        {11,  0.2}  // POST /api/v1/user/change-password (0.2%)
    };

    std::vector<WeightedTestCase> res;
    for (const auto& c : all_cases) {
        auto it = weights.find(c.id);
        if (it != weights.end()) {
            res.push_back({c, it->second});
        }
    }
    return res;
}

struct MixedTierResult {
    int concurrency{0};
    double qps{0.0};
    double p50_ms{0.0};
    double p95_ms{0.0};
    double p99_ms{0.0};
    double avg_lat_ms{0.0};
    double avg_cpu{0.0};
    double max_ram_mb{0.0};
    double p99_p50_ratio{0.0};
    double success_rate{0.0};
    uint64_t total_requests{0};
};

class MixedClientStressRunner {
public:
    MixedClientStressRunner(
        std::string host,
        unsigned short port,
        int concurrency,
        int duration_seconds,
        int server_pid,
        std::vector<WeightedTestCase> weighted_cases
    ) : host_(std::move(host)),
        port_(port),
        concurrency_(concurrency),
        duration_seconds_(duration_seconds),
        server_pid_(server_pid),
        weighted_cases_(std::move(weighted_cases)),
        worker_metrics_(concurrency) {}

    MixedTierResult run() {
#ifdef _WIN32
        DWORD_PTR client_affinity = 0xFFFFFFF0ULL; // 绑定到核心 4~31
        SetProcessAffinityMask(GetCurrentProcess(), client_affinity);
#endif

        init_auth_tokens();

        endpoints_.clear();
        boost::system::error_code ec;
        auto addr = net::ip::make_address(host_, ec);
        if (!ec) {
            endpoints_.emplace_back(addr, port_);
        } else {
            net::io_context res_ioc;
            tcp::resolver resolver(res_ioc);
            auto results = resolver.resolve(host_, std::to_string(port_), ec);
            if (!ec) {
                for (auto it = results.begin(); it != results.end(); ++it) {
                    endpoints_.push_back(*it);
                }
            }
        }
        if (endpoints_.empty()) {
            endpoints_.emplace_back(net::ip::make_address("127.0.0.1"), port_);
        }

        is_running_ = true;
        auto start_time = std::chrono::steady_clock::now();

        std::thread monitor_thread(&MixedClientStressRunner::monitor_loop, this, start_time);

        int num_threads = std::clamp(static_cast<int>(std::thread::hardware_concurrency()) - 4, 4, 16);
        std::vector<std::thread> worker_threads;
        net::io_context ioc(num_threads);

        for (int i = 0; i < concurrency_; ++i) {
            worker_metrics_[i].latency_samples.reserve(15000);
            net::co_spawn(ioc, worker_coroutine(ioc, i), net::detached);
        }

        for (int t = 0; t < num_threads; ++t) {
            worker_threads.emplace_back([&ioc]() { ioc.run(); });
        }

        std::this_thread::sleep_for(std::chrono::seconds(duration_seconds_));
        is_running_ = false;

        ioc.stop();
        for (auto& th : worker_threads) {
            if (th.joinable()) th.join();
        }
        if (monitor_thread.joinable()) monitor_thread.join();

        return calculate_report(start_time);
    }

private:
    void init_auth_tokens() {
        int64_t now = ev::current_time_ms();
        int64_t exp = now + 86400000LL * 7LL; // 7 天
        user_tokens_.clear();
        for (int i = 1; i <= 200; ++i) {
            user_tokens_.push_back(std::format("EV_TOKEN.{}.user.{}.{}.SIG_{}", i, exp, now, (i * 31 + exp % 9973)));
        }
        admin_token_ = std::format("EV_TOKEN.1.admin.{}.{}.SIG_{}", exp, now, (1 * 31 + exp % 9973));
    }

    net::awaitable<void> worker_coroutine(net::io_context& ioc, int worker_id) {
        std::mt19937 rng(1337 + worker_id);
        std::vector<double> weights;
        for (const auto& wt : weighted_cases_) {
            weights.push_back(wt.weight);
        }
        std::discrete_distribution<size_t> dist(weights.begin(), weights.end());

        auto& wm = worker_metrics_[worker_id];
        const std::string& u_tok = user_tokens_[worker_id % user_tokens_.size()];

        while (is_running_) {
            try {
                beast::tcp_stream stream(ioc);
                stream.expires_after(std::chrono::seconds(10));
                co_await stream.async_connect(endpoints_, net::use_awaitable);
                stream.socket().set_option(tcp::no_delay(true));

                beast::flat_buffer buffer;
                http::request<http::string_body> req;
                http::response<http::string_body> resp;

                while (is_running_) {
                    req = {};
                    req.version(11);
                    req.keep_alive(true);
                    req.set(http::field::host, host_);
                    req.set(http::field::user_agent, "EV-Bench-Suite/3.0");
                    req.set(http::field::content_type, "application/json");

                    size_t case_idx = dist(rng);
                    const auto& tc = weighted_cases_[case_idx].test_case;
                    tc.build_request(req, u_tok, admin_token_, worker_id, rng);
                    req.prepare_payload();

                    auto req_start = std::chrono::steady_clock::now();
                    stream.expires_after(std::chrono::seconds(5));
                    co_await http::async_write(stream, req, net::use_awaitable);

                    buffer.clear();
                    resp = {};
                    co_await http::async_read(stream, buffer, resp, net::use_awaitable);

                    auto req_end = std::chrono::steady_clock::now();
                    uint32_t lat_us = static_cast<uint32_t>(
                        std::chrono::duration_cast<std::chrono::microseconds>(req_end - req_start).count()
                    );

                    wm.total_requests++;
                    unsigned int status_val = resp.result_int();
                    if (status_val >= 200 && status_val < 500) {
                        wm.success_requests++;
                        wm.total_bytes += resp.body().size();
                    } else {
                        wm.failed_requests++;
                    }

                    if (wm.latency_samples.size() < 15000) {
                        wm.latency_samples.push_back(lat_us);
                    }

                    if (!resp.keep_alive()) {
                        break;
                    }
                }
            } catch (...) {
                // connection dropped
            }

            if (!is_running_) break;
            boost::asio::steady_timer sleep_timer(ioc);
            sleep_timer.expires_after(std::chrono::milliseconds(20));
            co_await sleep_timer.async_wait(net::use_awaitable);
        }
    }

    void monitor_loop(std::chrono::steady_clock::time_point start_time) {
        uint64_t last_requests = 0;

        while (is_running_) {
            std::this_thread::sleep_for(std::chrono::seconds(2));
            if (!is_running_) break;

            auto now = std::chrono::steady_clock::now();
            int elapsed = static_cast<int>(std::chrono::duration_cast<std::chrono::seconds>(now - start_time).count());

            uint64_t current_reqs = 0;
            uint64_t current_succ = 0;
            for (const auto& wm : worker_metrics_) {
                current_reqs += wm.total_requests;
                current_succ += wm.success_requests;
            }

            uint64_t delta_reqs = current_reqs - last_requests;
            double interval_qps = static_cast<double>(delta_reqs) / 2.0;
            last_requests = current_reqs;

            double succ_rate = current_reqs > 0 ? (static_cast<double>(current_succ) / current_reqs * 100.0) : 100.0;

            auto [cpu_pct, ram_mb] = sample_server_resources();
            if (cpu_pct > 0.0) cpu_samples_.push_back(cpu_pct);
            if (ram_mb > 0.0) ram_samples_.push_back(ram_mb);
            qps_samples_.push_back(interval_qps);

            std::println("    [{:>3}s/{:>2}s] 瞬时QPS: {:>8.1f} | 累计请求: {:>8} | 成功率: {:>6.2f}% | 2核CPU: {:>5.1f}% | 内存: {:>6.1f} MB",
                         elapsed, duration_seconds_, interval_qps, current_reqs, succ_rate, cpu_pct, ram_mb);
        }
    }

    std::pair<double, double> sample_server_resources() {
#ifdef _WIN32
        if (server_pid_ > 0) {
            HANDLE hProcess = OpenProcess(PROCESS_QUERY_INFORMATION | PROCESS_VM_READ, FALSE, server_pid_);
            if (hProcess) {
                PROCESS_MEMORY_COUNTERS pmc;
                double ram_mb = 0.0;
                if (GetProcessMemoryInfo(hProcess, &pmc, sizeof(pmc))) {
                    ram_mb = static_cast<double>(pmc.WorkingSetSize) / (1024.0 * 1024.0);
                }

                FILETIME ftCreation, ftExit, ftKernel, ftUser;
                double cpu_pct = 0.0;
                if (GetProcessTimes(hProcess, &ftCreation, &ftExit, &ftKernel, &ftUser)) {
                    ULARGE_INTEGER kTime, uTime;
                    kTime.LowPart = ftKernel.dwLowDateTime;
                    kTime.HighPart = ftKernel.dwHighDateTime;
                    uTime.LowPart = ftUser.dwLowDateTime;
                    uTime.HighPart = ftUser.dwHighDateTime;
                    uint64_t total_proc_time = kTime.QuadPart + uTime.QuadPart;

                    auto now_time = std::chrono::steady_clock::now();
                    if (last_proc_time_ > 0 && last_sample_time_.time_since_epoch().count() > 0) {
                        double wall_time_sec = std::chrono::duration_cast<std::chrono::duration<double>>(now_time - last_sample_time_).count();
                        if (wall_time_sec > 0.0) {
                            double proc_time_sec = static_cast<double>(total_proc_time - last_proc_time_) / 10000000.0;
                            cpu_pct = (proc_time_sec / (wall_time_sec * 2.0)) * 100.0;
                            if (cpu_pct > 100.0) cpu_pct = 100.0;
                        }
                    }
                    last_proc_time_ = total_proc_time;
                    last_sample_time_ = now_time;
                }

                CloseHandle(hProcess);
                return {cpu_pct, ram_mb};
            }
        }
#endif
        return {0.0, 0.0};
    }

    MixedTierResult calculate_report(std::chrono::steady_clock::time_point start_time) {
        auto end_time = std::chrono::steady_clock::now();
        double total_duration_sec = std::chrono::duration_cast<std::chrono::duration<double>>(end_time - start_time).count();

        uint64_t total_reqs = 0, total_succ = 0, total_fail = 0;
        std::vector<uint32_t> all_lats;

        for (const auto& wm : worker_metrics_) {
            total_reqs += wm.total_requests;
            total_succ += wm.success_requests;
            total_fail += wm.failed_requests;
            all_lats.insert(all_lats.end(), wm.latency_samples.begin(), wm.latency_samples.end());
        }

        double overall_qps = total_duration_sec > 0 ? (static_cast<double>(total_reqs) / total_duration_sec) : 0.0;
        std::sort(all_lats.begin(), all_lats.end());

        auto get_percentile = [](const std::vector<uint32_t>& sorted, double p) -> double {
            if (sorted.empty()) return 0.0;
            size_t idx = static_cast<size_t>(std::ceil(p * sorted.size())) - 1;
            idx = std::min(idx, sorted.size() - 1);
            return static_cast<double>(sorted[idx]) / 1000.0;
        };

        double p50 = get_percentile(all_lats, 0.50);
        double p95 = get_percentile(all_lats, 0.95);
        double p99 = get_percentile(all_lats, 0.99);
        double avg_lat = all_lats.empty() ? 0.0 : (std::accumulate(all_lats.begin(), all_lats.end(), 0.0) / all_lats.size() / 1000.0);

        double max_ram = ram_samples_.empty() ? 0.0 : *std::max_element(ram_samples_.begin(), ram_samples_.end());
        double avg_cpu = cpu_samples_.empty() ? 0.0 : (std::accumulate(cpu_samples_.begin(), cpu_samples_.end(), 0.0) / cpu_samples_.size());
        double succ_rate = total_reqs > 0 ? (total_succ * 100.0 / total_reqs) : 100.0;
        double ratio = (p50 > 0.0001) ? (p99 / p50) : 0.0;

        return MixedTierResult{
            .concurrency = concurrency_,
            .qps = overall_qps,
            .p50_ms = p50,
            .p95_ms = p95,
            .p99_ms = p99,
            .avg_lat_ms = avg_lat,
            .avg_cpu = avg_cpu,
            .max_ram_mb = max_ram,
            .p99_p50_ratio = ratio,
            .success_rate = succ_rate,
            .total_requests = total_reqs
        };
    }

    std::string host_;
    unsigned short port_;
    int concurrency_;
    int duration_seconds_;
    int server_pid_{0};
    std::vector<WeightedTestCase> weighted_cases_;

    std::atomic<bool> is_running_{false};
    std::vector<WorkerMetrics> worker_metrics_;

    std::vector<std::string> user_tokens_;
    std::string admin_token_;

    std::vector<double> cpu_samples_;
    std::vector<double> qps_samples_;
    std::vector<double> ram_samples_;

    uint64_t last_proc_time_{0};
    std::chrono::steady_clock::time_point last_sample_time_;
    std::vector<tcp::endpoint> endpoints_;
};

inline int auto_detect_server_pid() {
#ifdef _WIN32
    HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snapshot != INVALID_HANDLE_VALUE) {
        PROCESSENTRY32W pe;
        pe.dwSize = sizeof(pe);
        if (Process32FirstW(snapshot, &pe)) {
            do {
                if (std::wstring(pe.szExeFile) == L"server.exe") {
                    DWORD pid = pe.th32ProcessID;
                    CloseHandle(snapshot);
                    return static_cast<int>(pid);
                }
            } while (Process32NextW(snapshot, &pe));
        }
        CloseHandle(snapshot);
    }
#endif
    return 0;
}

int main(int argc, char* argv[]) {
    std::setvbuf(stdout, nullptr, _IONBF, 0);
    std::string host = "127.0.0.1";
    unsigned short port = 8080;
    int duration_sec = 30; // 默认每轮从 25s 改回 30s
    int server_pid = 0;
    int target_endpoint_id = 0;
    int from_id = 1;
    int to_id = 42;
    std::string target_module = "";
    std::string output_file = "benchmark_results_raw.md";
    bool append_mode = false;

    std::vector<int> tiers = {128, 256, 512, 1024}; // 严格 4 档并发长连接

    bool client_mix_mode = false;

    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--client-mix") client_mix_mode = true;
        else if (arg == "--host" && i + 1 < argc) host = argv[++i];
        else if (arg == "--port" && i + 1 < argc) port = std::stoi(argv[++i]);
        else if (arg == "--duration" && i + 1 < argc) duration_sec = std::stoi(argv[++i]);
        else if (arg == "--server-pid" && i + 1 < argc) server_pid = std::stoi(argv[++i]);
        else if (arg == "--endpoint" && i + 1 < argc) target_endpoint_id = std::stoi(argv[++i]);
        else if (arg == "--from" && i + 1 < argc) from_id = std::stoi(argv[++i]);
        else if (arg == "--to" && i + 1 < argc) to_id = std::stoi(argv[++i]);
        else if (arg == "--module" && i + 1 < argc) target_module = argv[++i];
        else if (arg == "--output" && i + 1 < argc) output_file = argv[++i];
        else if (arg == "--append") append_mode = true;
        else if (arg == "--quick") duration_sec = 5;
        else if (arg == "--tiers" && i + 1 < argc) {
            tiers.clear();
            std::string t_str = argv[++i];
            size_t start = 0;
            while (start < t_str.size()) {
                auto comma = t_str.find(',', start);
                if (comma == std::string::npos) comma = t_str.size();
                std::string token = t_str.substr(start, comma - start);
                if (!token.empty()) tiers.push_back(std::stoi(token));
                start = comma + 1;
            }
        }
    }

    if (server_pid <= 0) {
        server_pid = auto_detect_server_pid();
    }

    if (client_mix_mode) {
        if (tiers.size() == 4 && tiers[0] == 128) {
            tiers = {32, 64, 128, 256, 512, 1024};
        }
        if (duration_sec == 30) {
            duration_sec = 60; // 用户要求每轮增长到 1 分钟
        }
        if (output_file == "benchmark_results_raw.md") {
            output_file = "client_mix_results.md";
        }

        auto mix_cases = create_client_mix_test_cases();

        if (!output_file.empty()) {
            std::ofstream ofs(output_file, std::ios::trunc);
            ofs << "| 并发数 | 实际吞吐量QPS (req/s) | 中位数延迟p50 (ms) | 99分位延迟p99 (ms) | 2核CPU利用率 | 峰值内存 (MB) | p99/p50比值 | 请求总数 | 成功率 |\n";
            ofs << "| :----: | :--------------------: | :----------------: | :----------------: | :----------: | :-----------: | :---------: | :------: | :----: |\n";
            ofs.flush();
        }

        std::println("\n=======================================================================================");
        std::println("       电动汽车充电桩管理平台 —— 客户端全 API 拟真混合流量极限压测                    ");
        std::println("=======================================================================================");
        std::println("目标服务地址: http://{}:{} | 监控服务端 PID: {}", host, port, server_pid);
        std::println("单轮测试时长: {} 秒 | 并发梯度: {}", duration_sec, [&]() {
            std::string s;
            for (size_t i = 0; i < tiers.size(); ++i) {
                if (i > 0) s += " -> ";
                s += std::to_string(tiers[i]);
            }
            return s;
        }());
        std::println("测试接口总数: {} 个 (客户端全量 API 权重配比混合)", mix_cases.size());
        if (!output_file.empty()) {
            std::println("实时结果写入: {}", output_file);
        }
        std::println("=======================================================================================\n");

        std::println("【客户端 26 个 API 拟真业务分布权重清单】");
        for (const auto& wt : mix_cases) {
            std::println("  - [{:>4.1f}%] {:<4} {:<40} ({})", wt.weight, wt.test_case.method, wt.test_case.name, wt.test_case.module);
        }
        std::println("---------------------------------------------------------------------------------------\n");

        std::vector<MixedTierResult> mix_results;
        for (size_t t_idx = 0; t_idx < tiers.size(); ++t_idx) {
            int concurrency = tiers[t_idx];
            std::println("\n[并发档位 {:>2}/{}] >>> 开始压测: 并发数 {} (时长 {}s)...",
                         t_idx + 1, tiers.size(), concurrency, duration_sec);

            MixedClientStressRunner runner(host, port, concurrency, duration_sec, server_pid, mix_cases);
            auto res = runner.run();
            mix_results.push_back(res);

            std::println("     [完成] QPS: {:>8.1f} | p50: {:>6.2f}ms | p99: {:>7.2f}ms | 2核CPU: {:>4.1f}% | 内存: {:>5.1f}MB | p99/p50: {:>5.2f}x",
                         res.qps, res.p50_ms, res.p99_ms, res.avg_cpu, res.max_ram_mb, res.p99_p50_ratio);

            std::string row = std::format("| {:>6} | {:>22.2f} | {:>18.2f} | {:>18.2f} | {:>11.1f}% | {:>13.1f} | {:>10.2f}x | {:>8} | {:>6.2f}% |",
                                          res.concurrency, res.qps, res.p50_ms, res.p99_ms, res.avg_cpu, res.max_ram_mb, res.p99_p50_ratio, res.total_requests, res.success_rate);
            std::println("     [TABLE_ROW] {}", row);

            if (!output_file.empty()) {
                std::ofstream ofs(output_file, std::ios::app);
                if (ofs.is_open()) {
                    ofs << row << "\n";
                    ofs.flush();
                }
            }

            std::this_thread::sleep_for(std::chrono::seconds(3)); // 3秒沉降冷却
        }

        std::println("\n\n======================================================================================================================================");
        std::println("                                    客户端全 API 拟真混合流量极限性能汇总表 (Markdown 格式)                                           ");
        std::println("======================================================================================================================================");
        std::println("| 并发数 | 实际吞吐量QPS (req/s) | 中位数延迟p50 (ms) | 99分位延迟p99 (ms) | 2核CPU利用率 | 峰值内存 (MB) | p99/p50比值 | 请求总数 | 成功率 |");
        std::println("| :----: | :--------------------: | :----------------: | :----------------: | :----------: | :-----------: | :---------: | :------: | :----: |\n");
        for (const auto& r : mix_results) {
            std::println("| {:>6} | {:>22.2f} | {:>18.2f} | {:>18.2f} | {:>11.1f}% | {:>13.1f} | {:>10.2f}x | {:>8} | {:>6.2f}% |",
                         r.concurrency, r.qps, r.p50_ms, r.p99_ms, r.avg_cpu, r.max_ram_mb, r.p99_p50_ratio, r.total_requests, r.success_rate);
        }
        std::println("======================================================================================================================================\n");

        return 0;
    }

    auto all_cases = create_all_test_cases();

    std::vector<ApiTestCase> selected_cases;
    for (const auto& c : all_cases) {
        if (target_endpoint_id > 0) {
            if (c.id == target_endpoint_id) selected_cases.push_back(c);
        } else if (!target_module.empty()) {
            if (c.module.find(target_module) != std::string::npos) selected_cases.push_back(c);
        } else {
            if (c.id >= from_id && c.id <= to_id) selected_cases.push_back(c);
        }
    }

    if (!output_file.empty()) {
        bool file_exists = false;
        {
            std::ifstream test_ifs(output_file);
            file_exists = test_ifs.good();
        }
        if (!file_exists || !append_mode) {
            std::ofstream ofs(output_file, std::ios::trunc);
            ofs << "| 序号 | 业务模块 | 接口名称 / 路径 | 方法 | 并发数 | 实际吞吐量QPS (req/s) | 中位数延迟p50 (ms) | 99分位延迟p99 (ms) | 2核CPU利用率 | 峰值内存 (MB) |\n";
            ofs << "| :--: | :--- | :--- | :--: | :----: | :--------------------: | :----------------: | :----------------: | :----------: | :-----------: |\n";
            ofs.flush();
        }
    }

    std::println("\n=======================================================================================");
    std::println("       电动汽车充电桩管理平台 —— 全接口单项极限并发阶梯压测引擎                       ");
    std::println("=======================================================================================");
    std::println("目标服务地址: http://{}:{} | 监控服务端 PID: {}", host, port, server_pid);
    std::println("单轮测试时长: {} 秒 | 并发梯度: {}", duration_sec, [&]() {
        std::string s;
        for (size_t i = 0; i < tiers.size(); ++i) {
            if (i > 0) s += " -> ";
            s += std::to_string(tiers[i]);
        }
        return s;
    }());
    std::println("待测接口总数: {} 个 (范围: #{} ~ #{})", selected_cases.size(), from_id, to_id);
    if (!output_file.empty()) {
        std::println("实时结果写入: {}", output_file);
    }
    std::println("=======================================================================================\n");

    std::vector<EndpointTierResult> results;

    for (size_t c_idx = 0; c_idx < selected_cases.size(); ++c_idx) {
        const auto& tc = selected_cases[c_idx];
        std::println("\n[接口 {:>2}/{}] >>> 开始压测: {} ({})",
                     tc.id, all_cases.size(), tc.name, tc.module);

        for (size_t t_idx = 0; t_idx < tiers.size(); ++t_idx) {
            int concurrency = tiers[t_idx];
            std::println("  -> 正在测试并发档位: {:>4} (时长 {}s)...", concurrency, duration_sec);

            SingleEndpointStressRunner runner(host, port, concurrency, duration_sec, server_pid, tc);
            auto res = runner.run();
            results.push_back(res);

            std::println("     [完成] QPS: {:>8.1f} | p50: {:>6.2f}ms | p99: {:>7.2f}ms | 2核CPU: {:>4.1f}% | 内存: {:>5.1f}MB",
                         res.qps, res.p50_ms, res.p99_ms, res.avg_cpu, res.max_ram_mb);

            std::string row = std::format("| {:>4} | {:<12} | {:<36} | {:^4} | {:>6} | {:>22.2f} | {:>18.2f} | {:>18.2f} | {:>11.1f}% | {:>13.1f} |",
                                          res.endpoint_id, res.module_name, res.endpoint_name, res.http_method, res.concurrency,
                                          res.qps, res.p50_ms, res.p99_ms, res.avg_cpu, res.max_ram_mb);
            std::println("     [TABLE_ROW] {}", row);

            if (!output_file.empty()) {
                std::ofstream ofs(output_file, std::ios::app);
                if (ofs.is_open()) {
                    ofs << row << "\n";
                    ofs.flush();
                }
            }

            std::this_thread::sleep_for(std::chrono::seconds(2)); // 短暂冷却
        }
    }

    // 格式化输出最终 Markdown 表格
    std::println("\n\n");
    std::println("======================================================================================================================================");
    std::println("                                    平台全量接口性能分析全景对比表 (Markdown 格式)                                                    ");
    std::println("======================================================================================================================================");
    std::println("| 序号 | 业务模块 | 接口名称 / 路径 | 方法 | 并发数 | 实际吞吐量QPS (req/s) | 中位数延迟p50 (ms) | 99分位延迟p99 (ms) | 2核CPU利用率 | 峰值内存 (MB) |");
    std::println("| :--: | :--- | :--- | :--: | :----: | :--------------------: | :----------------: | :----------------: | :----------: | :-----------: |");

    for (const auto& r : results) {
        std::println("| {:>4} | {:<12} | {:<36} | {:^4} | {:>6} | {:>22.2f} | {:>18.2f} | {:>18.2f} | {:>11.1f}% | {:>13.1f} |",
                     r.endpoint_id, r.module_name, r.endpoint_name, r.http_method, r.concurrency,
                     r.qps, r.p50_ms, r.p99_ms, r.avg_cpu, r.max_ram_mb);
    }
    std::println("======================================================================================================================================\n");

    return 0;
}
