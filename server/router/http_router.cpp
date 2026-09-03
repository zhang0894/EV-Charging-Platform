#include "http_router.hpp"
#include "../controllers/auth_controller.hpp"
#include "../controllers/user_controller.hpp"
#include "../controllers/station_controller.hpp"
#include "../controllers/charging_controller.hpp"
#include "../controllers/admin_controller.hpp"
#include "../common/auth_token.hpp"
#include "../common/response.hpp"
#include <regex>
#include <iostream>

namespace ev {

std::string_view HttpRouter::extract_path_only(std::string_view target) {
    auto q_pos = target.find('?');
    if (q_pos != std::string_view::npos) {
        return target.substr(0, q_pos);
    }
    return target;
}

std::unordered_map<std::string, std::string> HttpRouter::parse_query_params(std::string_view target) {
    std::unordered_map<std::string, std::string> params;
    auto q_pos = target.find('?');
    if (q_pos == std::string_view::npos) return params;

    std::string_view query_str = target.substr(q_pos + 1);
    size_t start = 0;
    while (start < query_str.size()) {
        size_t amp_pos = query_str.find('&', start);
        if (amp_pos == std::string_view::npos) amp_pos = query_str.size();

        std::string_view pair = query_str.substr(start, amp_pos - start);
        size_t eq_pos = pair.find('=');
        if (eq_pos != std::string_view::npos) {
            std::string key(pair.substr(0, eq_pos));
            std::string val(pair.substr(eq_pos + 1));
            params[key] = val;
        }

        start = amp_pos + 1;
    }

    return params;
}

http::response<http::string_body> HttpRouter::dispatch(const http::request<http::string_body>& req) {
    // 1. CORS Preflight 跨域探测
    if (req.method() == http::verb::options) {
        return make_http_response(http::status::ok, "{\"code\":0,\"msg\":\"OK\"}");
    }

    std::string_view target = req.target();
    std::string_view path = extract_path_only(target);
    auto query = parse_query_params(target);
    auto method = req.method();

    // 辅助函数：提取 Bearer Token
    auto get_auth_claims = [&]() -> Result<TokenClaims> {
        auto it = req.find(http::field::authorization);
        if (it == req.end()) return std::unexpected(AppError::Unauthorized);
        return AuthTokenManager::extract_and_verify(it->value());
    };

    // ==========================================
    // 1. 认证模块公开接口 (无须鉴权)
    // ==========================================
    if (path == "/api/v1/auth/login" && method == http::verb::post) {
        return AuthController::handle_user_login(req);
    }
    if (path == "/api/v1/admin/auth/login" && method == http::verb::post) {
        return AuthController::handle_admin_login(req);
    }
    if (path == "/api/v1/auth/refresh" && method == http::verb::post) {
        return AuthController::handle_refresh_token(req);
    }

    // ==========================================
    // 2. 充电站附近与详情接口 (公开或带鉴权)
    // ==========================================
    if (path == "/api/v1/stations/nearby" && method == http::verb::get) {
        double lat = query.contains("latitude") ? std::stod(query["latitude"]) : 39.9042;
        double lng = query.contains("longitude") ? std::stod(query["longitude"]) : 116.4074;
        double radius = query.contains("radius_km") ? std::stod(query["radius_km"]) : 10.0;
        size_t limit = query.contains("limit") ? std::stoul(query["limit"]) : 20;
        return StationController::handle_get_nearby_stations(lat, lng, radius, limit);
    }

    // 正则路径匹配: /api/v1/stations/{station_id}
    std::regex station_detail_regex(R"(^/api/v1/stations/(\d+)$)");
    std::cmatch match;
    std::string path_str(path);

    if (method == http::verb::get && std::regex_match(path_str.c_str(), match, station_detail_regex)) {
        int64_t sid = std::stoll(match[1].str());
        return StationController::handle_get_station_detail(sid);
    }

    // ==========================================
    // 3. 用户端受保护接口 (需要 User 鉴权)
    // ==========================================
    if (path.starts_with("/api/v1/user/") || path.starts_with("/api/v1/wallet/") || path.starts_with("/api/v1/charging/") || path.starts_with("/api/v1/orders/")) {
        auto claims = get_auth_claims();
        if (!claims) return make_error_response(claims.error());

        int64_t uid = claims->user_id;

        // 用户资料
        if (path == "/api/v1/user/profile") {
            if (method == http::verb::get) return UserController::handle_get_profile(uid);
            if (method == http::verb::put) return UserController::handle_update_profile(uid, req);
        }
        if (path == "/api/v1/user/avatar" && method == http::verb::post) {
            return UserController::handle_upload_avatar(uid, req);
        }

        // 钱包资产与充值
        if (path == "/api/v1/wallet/balance" && method == http::verb::get) {
            return UserController::handle_get_wallet_balance(uid);
        }
        if (path == "/api/v1/wallet/recharge" && method == http::verb::post) {
            return UserController::handle_recharge(uid, req);
        }
        if (path == "/api/v1/user/wallet/transactions" && method == http::verb::get) {
            int page = query.contains("page") ? std::stoi(query["page"]) : 1;
            int page_size = query.contains("page_size") ? std::stoi(query["page_size"]) : 20;
            int flow_type = query.contains("flow_type") ? std::stoi(query["flow_type"]) : 0;
            return UserController::handle_get_transactions(uid, page, page_size, flow_type);
        }

        // 充电流程
        if (path == "/api/v1/charging/active-order" && method == http::verb::get) {
            return ChargingController::handle_check_active_order(uid);
        }
        if (path == "/api/v1/charging/start" && method == http::verb::post) {
            return ChargingController::handle_start_charging(uid, req);
        }
        if (path == "/api/v1/charging/stop" && method == http::verb::post) {
            return ChargingController::handle_stop_charging(uid, req);
        }
        if (path == "/api/v1/charging/settle" && method == http::verb::post) {
            return ChargingController::handle_settle_order(uid, req);
        }

        // 用户订单列表
        if (path == "/api/v1/orders/my" && method == http::verb::get) {
            int page = query.contains("page") ? std::stoi(query["page"]) : 1;
            int page_size = query.contains("page_size") ? std::stoi(query["page_size"]) : 10;
            std::string st = query.contains("status") ? query["status"] : "";
            std::string sort = query.contains("sort_order") ? query["sort_order"] : "desc";
            return ChargingController::handle_get_my_orders(uid, page, page_size, st, sort);
        }

        // 订单详情: /api/v1/charging/orders/{order_id}
        std::regex order_detail_regex(R"(^/api/v1/charging/orders/([^/]+)$)");
        if (method == http::verb::get && std::regex_match(path_str.c_str(), match, order_detail_regex)) {
            std::string oid = match[1].str();
            return ChargingController::handle_get_order_detail(uid, oid);
        }
    }

    // ==========================================
    // 4. 管理端受保护接口 (需要 Admin 鉴权)
    // ==========================================
    if (path.starts_with("/api/v1/admin/")) {
        auto claims = get_auth_claims();
        if (!claims) return make_error_response(claims.error());
        if (claims->role != "admin") {
            return make_error_response(AppError::PermissionDenied, "Admin role required");
        }

        int64_t admin_uid = claims->user_id;

        // 运营看板
        if (path == "/api/v1/admin/dashboard/summary" && method == http::verb::get) {
            return AdminController::handle_get_dashboard_summary();
        }
        if (path == "/api/v1/admin/dashboard/revenue-trend" && method == http::verb::get) {
            int days = query.contains("days") ? std::stoi(query["days"]) : 7;
            return AdminController::handle_get_revenue_trend(days);
        }
        if (path == "/api/v1/admin/dashboard/pile-status" && method == http::verb::get) {
            return AdminController::handle_get_pile_status_overview();
        }

        // 电站管理
        if (path == "/api/v1/admin/stations") {
            if (method == http::verb::get) {
                int page = query.contains("page") ? std::stoi(query["page"]) : 1;
                int page_size = query.contains("page_size") ? std::stoi(query["page_size"]) : 10;
                std::string name = query.contains("station_name") ? query["station_name"] : "";
                int st = query.contains("status") ? std::stoi(query["status"]) : 0;
                return AdminController::handle_get_stations(page, page_size, name, st);
            }
            if (method == http::verb::post) {
                return AdminController::handle_create_station(req);
            }
        }

        // 单站销售统计: /api/v1/admin/stations/{station_id}/sales-stats
        std::regex station_sales_regex(R"(^/api/v1/admin/stations/(\d+)/sales-stats$)");
        if (method == http::verb::get && std::regex_match(path_str.c_str(), match, station_sales_regex)) {
            int64_t sid = std::stoll(match[1].str());
            std::string time_range = query.contains("time_range") ? query["time_range"] : "today";
            return StationController::handle_get_sales_stats(sid, time_range);
        }

        // 电站更新 / 删除: /api/v1/admin/stations/{station_id}
        std::regex station_op_regex(R"(^/api/v1/admin/stations/(\d+)$)");
        if (std::regex_match(path_str.c_str(), match, station_op_regex)) {
            int64_t sid = std::stoll(match[1].str());
            if (method == http::verb::put) return AdminController::handle_update_station(sid, req);
            if (method == http::verb::delete_) return AdminController::handle_delete_station(sid);
        }

        // 充电桩管理
        if (path == "/api/v1/admin/piles") {
            if (method == http::verb::get) {
                int page = query.contains("page") ? std::stoi(query["page"]) : 1;
                int page_size = query.contains("page_size") ? std::stoi(query["page_size"]) : 10;
                int64_t sid = query.contains("station_id") ? std::stoll(query["station_id"]) : 0;
                std::string st = query.contains("status") ? query["status"] : "";
                std::string type = query.contains("type") ? query["type"] : "";
                return AdminController::handle_get_piles(page, page_size, sid, st, type);
            }
            if (method == http::verb::post) {
                return AdminController::handle_create_pile(req);
            }
        }

        // 充电桩远程重启: /api/v1/admin/piles/{pile_id}/restart
        std::regex pile_restart_regex(R"(^/api/v1/admin/piles/([^/]+)/restart$)");
        if (method == http::verb::post && std::regex_match(path_str.c_str(), match, pile_restart_regex)) {
            std::string pid = match[1].str();
            return AdminController::handle_restart_pile(pid, req);
        }

        // 充电桩状态切换: /api/v1/admin/piles/{pile_id}/status
        std::regex pile_status_regex(R"(^/api/v1/admin/piles/([^/]+)/status$)");
        if (method == http::verb::put && std::regex_match(path_str.c_str(), match, pile_status_regex)) {
            std::string pid = match[1].str();
            return AdminController::handle_change_pile_status(pid, req);
        }

        // 用户管理
        if (path == "/api/v1/admin/users" && method == http::verb::get) {
            int page = query.contains("page") ? std::stoi(query["page"]) : 1;
            int page_size = query.contains("page_size") ? std::stoi(query["page_size"]) : 10;
            std::string phone = query.contains("phone") ? query["phone"] : "";
            int st = query.contains("status") ? std::stoi(query["status"]) : 0;
            return AdminController::handle_get_users(page, page_size, phone, st);
        }

        // 用户状态修改: /api/v1/admin/users/{user_id}/status
        std::regex user_status_regex(R"(^/api/v1/admin/users/(\d+)/status$)");
        if (method == http::verb::put && std::regex_match(path_str.c_str(), match, user_status_regex)) {
            int64_t target_uid = std::stoll(match[1].str());
            return AdminController::handle_change_user_status(target_uid, admin_uid, req);
        }

        // 用户钱包调账: /api/v1/admin/users/{user_id}/adjust-wallet
        std::regex user_adjust_regex(R"(^/api/v1/admin/users/(\d+)/adjust-wallet$)");
        if (method == http::verb::post && std::regex_match(path_str.c_str(), match, user_adjust_regex)) {
            int64_t target_uid = std::stoll(match[1].str());
            return AdminController::handle_adjust_user_wallet(target_uid, admin_uid, req);
        }

        // 管理员全局订单搜索
        if (path == "/api/v1/admin/orders" && method == http::verb::get) {
            int page = query.contains("page") ? std::stoi(query["page"]) : 1;
            int page_size = query.contains("page_size") ? std::stoi(query["page_size"]) : 10;
            int64_t sid = query.contains("station_id") ? std::stoll(query["station_id"]) : 0;
            std::string st = query.contains("status") ? query["status"] : "";
            std::string start_d = query.contains("start_date") ? query["start_date"] : "";
            std::string end_d = query.contains("end_date") ? query["end_date"] : "";
            return AdminController::handle_get_orders(page, page_size, sid, st, start_d, end_d);
        }

        // 管理员按手机号/用户ID查询历史订单: /api/v1/admin/orders/user
        if (path == "/api/v1/admin/orders/user" && method == http::verb::get) {
            int64_t target_uid = query.contains("user_id") ? std::stoll(query["user_id"]) : 0;
            std::string phone = query.contains("phone") ? query["phone"] : "";
            int page = query.contains("page") ? std::stoi(query["page"]) : 1;
            int page_size = query.contains("page_size") ? std::stoi(query["page_size"]) : 10;
            std::string sort = query.contains("sort_order") ? query["sort_order"] : "asc";
            return AdminController::handle_get_user_historical_orders(target_uid, phone, page, page_size, sort);
        }

        // 管理员指定订单一键退款: /api/v1/admin/orders/{order_id}/refund
        std::regex order_refund_regex(R"(^/api/v1/admin/orders/([^/]+)/refund$)");
        if (method == http::verb::post && std::regex_match(path_str.c_str(), match, order_refund_regex)) {
            std::string oid = match[1].str();
            return AdminController::handle_refund_order(oid, admin_uid, req);
        }
    }

    return make_error_response(AppError::RouteNotFound, "API endpoint not found");
}

} // namespace ev
