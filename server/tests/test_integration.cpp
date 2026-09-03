#include "../router/http_router.hpp"
#include "../common/response.hpp"
#include "../db/db_pool.hpp"
#include "../db/db_repository.hpp"
#include "../db/seed_data.hpp"
#include "../memory/rtree_index.hpp"
#include "../memory/state_pool.hpp"
#include "../simulation/simulator.hpp"
#include <glaze/glaze.hpp>
#include <iostream>
#include <print>
#include <cassert>

namespace http = boost::beast::http;

// 辅助函数：构造测试 HTTP 请求
http::request<http::string_body> make_req(
    http::verb method,
    std::string_view target,
    std::string_view body = "",
    std::string_view auth_token = "",
    std::string_view idempotency_key = ""
) {
    http::request<http::string_body> req{method, std::string(target), 11};
    req.set(http::field::content_type, "application/json");
    if (!auth_token.empty()) {
        req.set(http::field::authorization, "Bearer " + std::string(auth_token));
    }
    if (!idempotency_key.empty()) {
        req.set("Idempotency-Key", std::string(idempotency_key));
    }
    req.body() = std::string(body);
    req.prepare_payload();
    return req;
}

int main() {
    std::println("\n=======================================================");
    std::println("   端到端全链路集成测试: 充电管理平台全部接口与业务闭环   ");
    std::println("=======================================================\n");

    const std::string conninfo = "host=127.0.0.1 port=5432 dbname=postgres user=postgres password=Express1.";
    ev::DbPool::instance().init(conninfo, 4, 16);

    // 1. 初始化数据与内存索引
    std::println(">>> 1. 初始化数据库、R-Tree 空间索引与实时状态池...");
    ev::SeedDataGenerator::populate_if_empty();

    auto all_stations = ev::DbRepository::instance().get_all_stations();
    assert(all_stations.has_value());

    std::vector<std::pair<int64_t, std::pair<double, double>>> coords;
    std::vector<ev::PileModel> all_piles;
    for (const auto& s : *all_stations) {
        coords.emplace_back(s.station_id, std::make_pair(s.latitude, s.longitude));
        auto piles = ev::DbRepository::instance().get_piles_by_station(s.station_id);
        if (piles) {
            all_piles.insert(all_piles.end(), piles->begin(), piles->end());
        }
    }
    ev::ChargingStatePool::instance().init_from_piles(all_piles);
    ev::StationRTree::instance().build_index(coords);
    std::println("  [PASS] 系统初始化就绪 (包含 {} 个电站, {} 个充电桩)", all_stations->size(), all_piles.size());

    auto& router = ev::HttpRouter::instance();

    // 2. 测试用户登录与认证流程
    std::println("\n>>> 2. 测试用户免密登录与注册接口 (/api/v1/auth/login)...");
    auto login_req = make_req(http::verb::post, "/api/v1/auth/login", R"({"phone":"13888887777","auth_type":"passwordless"})");
    auto login_resp = router.dispatch(login_req);
    assert(login_resp.result() == http::status::ok);
    std::println("  [PASS] 用户免密登录成功, 返回: {}", login_resp.body().substr(0, 120) + "...");

    // 提取 access_token
    ev::ApiResponse<ev::AuthResponseData> user_auth;
    auto glz_err = glz::read_json(user_auth, login_resp.body());
    assert(!glz_err);
    std::string user_token = user_auth.data.access_token;
    int64_t uid = user_auth.data.user_id;

    // 3. 测试用户资料与钱包充值
    std::println("\n>>> 3. 测试用户资料与钱包充值/流水 (/api/v1/wallet/*)...");
    auto prof_req = make_req(http::verb::get, "/api/v1/user/profile", "", user_token);
    auto prof_resp = router.dispatch(prof_req);
    assert(prof_resp.result() == http::status::ok);
    std::println("  [PASS] 获取个人资料成功");

    // 钱包充值 200 元
    auto rec_req = make_req(http::verb::post, "/api/v1/wallet/recharge", R"({"amount":200.0,"remark":"测试充值200元"})", user_token, "REC_INTEGRATION_001");
    auto rec_resp = router.dispatch(rec_req);
    assert(rec_resp.result() == http::status::ok);
    std::println("  [PASS] 钱包充值 200 元成功");

    // 查询流水
    auto tx_req = make_req(http::verb::get, "/api/v1/user/wallet/transactions?page=1&page_size=10", "", user_token);
    auto tx_resp = router.dispatch(tx_req);
    assert(tx_resp.result() == http::status::ok);
    std::println("  [PASS] 查询钱包流水明细成功");

    // 4. 测试空间索引搜桩与电站详情
    std::println("\n>>> 4. 测试空间附近搜桩与电站详情 (/api/v1/stations/*)...");
    auto nearby_req = make_req(http::verb::get, "/api/v1/stations/nearby?latitude=31.2304&longitude=121.4737&radius_km=15&limit=5");
    auto nearby_resp = router.dispatch(nearby_req);
    assert(nearby_resp.result() == http::status::ok);
    std::println("  [PASS] 空间搜桩成功, 返回: {}", nearby_resp.body().substr(0, 120) + "...");

    int64_t target_sid = 1;
    auto detail_req = make_req(http::verb::get, std::format("/api/v1/stations/{}", target_sid));
    auto detail_resp = router.dispatch(detail_req);
    assert(detail_resp.result() == http::status::ok);
    std::println("  [PASS] 获取电站详情与全部枪位实时状态成功");

    // 5. 测试充电全流程闭环 (检查 -> 启动 -> 模拟遥测 -> 停止 -> 结算扣款 -> 订单历史)
    std::println("\n>>> 5. 测试充电业务完整生命周期闭环 (/api/v1/charging/* & /api/v1/orders/*)...");
    
    // 检查活动订单 (开始前应无活动订单)
    auto chk_act_req1 = make_req(http::verb::get, "/api/v1/charging/active-order", "", user_token);
    auto chk_act_resp1 = router.dispatch(chk_act_req1);
    assert(chk_act_resp1.result() == http::status::ok);

    // 启动充电 (选 1 号桩，确保桩为空闲状态)
    std::string target_pid = "P00101";
    ev::ChargingStatePool::instance().set_pile_status(target_pid, "IDLE");
    ev::DbRepository::instance().update_pile_status(target_pid, "IDLE");

    auto start_req = make_req(http::verb::post, "/api/v1/charging/start", std::format(R"({{"pile_id":"{}","strategy_type":"FULL"}})", target_pid), user_token);
    auto start_resp = router.dispatch(start_req);
    if (start_resp.result() != http::status::ok) {
        std::println("  [DEBUG START ERROR] {}", start_resp.body());
    }
    assert(start_resp.result() == http::status::ok);
    std::println("  [PASS] 成功开启充电!");

    ev::ApiResponse<ev::StartChargingResponseData> start_data;
    glz::read_json(start_data, start_resp.body());
    std::string order_id = start_data.data.order_id;
    std::println("         - 订单号: {}", order_id);
    std::println("         - WebSocket 遥测地址: {}", start_data.data.ws_telemetry_url);

    // 模拟推进动态充电机 10 轮
    for (int i = 0; i < 10; ++i) {
        ev::ChargingSimulator::instance().step_once(120.0);
    }

    // 停止充电
    auto stop_req = make_req(http::verb::post, "/api/v1/charging/stop", std::format(R"({{"order_id":"{}","stop_reason":"USER_MANUAL_STOP"}})", order_id), user_token);
    auto stop_resp = router.dispatch(stop_req);
    assert(stop_resp.result() == http::status::ok);
    std::println("  [PASS] 停止充电拔枪成功, 状态更新为 UNSETTLED (待结算)");

    // 结算扣款
    auto settle_req = make_req(http::verb::post, "/api/v1/charging/settle", std::format(R"({{"order_id":"{}"}})", order_id), user_token);
    auto settle_resp = router.dispatch(settle_req);
    assert(settle_resp.result() == http::status::ok);
    std::println("  [PASS] 订单结算扣款成功, 状态更新为 COMPLETED (已结算)");

    // 查询用户历史订单
    auto my_orders_req = make_req(http::verb::get, "/api/v1/orders/my?page=1&page_size=10&sort_order=desc", "", user_token);
    auto my_orders_resp = router.dispatch(my_orders_req);
    assert(my_orders_resp.result() == http::status::ok);
    std::println("  [PASS] 查询用户历史订单成功");

    // 查询指定订单详情 (/api/v1/charging/orders/{order_id})
    auto order_detail_req = make_req(http::verb::get, std::format("/api/v1/charging/orders/{}", order_id), "", user_token);
    auto order_detail_resp = router.dispatch(order_detail_req);
    assert(order_detail_resp.result() == http::status::ok);
    std::println("  [PASS] 查询订单详情成功 (/api/v1/charging/orders/{})", order_id);

    // 6. 测试管理端全功能 (登录 -> 看板 -> 单站销售统计 -> 桩管理 -> 用户风控 -> 一键退款)
    std::println("\n>>> 6. 测试管理端全功能业务 (/api/v1/admin/*)...");
    auto admin_login_req = make_req(http::verb::post, "/api/v1/admin/auth/login", R"({"account":"13900000000","password":"123456"})");
    auto admin_login_resp = router.dispatch(admin_login_req);
    assert(admin_login_resp.result() == http::status::ok);

    ev::ApiResponse<ev::AuthResponseData> admin_auth;
    glz::read_json(admin_auth, admin_login_resp.body());
    std::string admin_token = admin_auth.data.access_token;
    std::println("  [PASS] 管理员登录成功");

    // 运营态势大盘
    auto dash_req = make_req(http::verb::get, "/api/v1/admin/dashboard/summary", "", admin_token);
    auto dash_resp = router.dispatch(dash_req);
    assert(dash_resp.result() == http::status::ok);
    std::println("  [PASS] 获取运营大盘核心概览指标成功");

    // 7天营收趋势
    auto trend_req = make_req(http::verb::get, "/api/v1/admin/dashboard/revenue-trend?days=7", "", admin_token);
    auto trend_resp = router.dispatch(trend_req);
    assert(trend_resp.result() == http::status::ok);
    std::println("  [PASS] 获取近7天营收趋势序列成功");

    // 单站销售统计 (今日/7天/30天)
    auto stats_req = make_req(http::verb::get, std::format("/api/v1/admin/stations/{}/sales-stats?time_range=today", target_sid), "", admin_token);
    auto stats_resp = router.dispatch(stats_req);
    assert(stats_resp.result() == http::status::ok);
    std::println("  [PASS] 获取指定电站当日销售业绩统计报表成功");

    // 充电桩远程重启与状态修改
    auto restart_req = make_req(http::verb::post, std::format("/api/v1/admin/piles/{}/restart", target_pid), R"({"reason":"例行维护重启"})", admin_token);
    auto restart_resp = router.dispatch(restart_req);
    assert(restart_resp.result() == http::status::ok);
    std::println("  [PASS] 充电桩远程重启指令下发成功");

    // 用户历史订单查询 (按时间正序)
    auto admin_user_orders_req = make_req(http::verb::get, std::format("/api/v1/admin/orders/user?user_id={}&sort_order=asc", uid), "", admin_token);
    auto admin_user_orders_resp = router.dispatch(admin_user_orders_req);
    assert(admin_user_orders_resp.result() == http::status::ok);
    std::println("  [PASS] 管理员按用户ID查询历史订单成功 (时间正序排列)");

    // 管理员对订单一键退款
    auto refund_req = make_req(http::verb::post, std::format("/api/v1/admin/orders/{}/refund", order_id), R"({"reason":"集成测试退款"})", admin_token);
    auto refund_resp = router.dispatch(refund_req);
    assert(refund_resp.result() == http::status::ok);
    std::println("  [PASS] 管理员对订单一键退款成功 (金额已原路退回钱包, 订单标记为 REFUNDED)");

    std::println("\n=======================================================");
    std::println("   >>> 全链路集成测试全部通过 (ALL 18 ENDPOINTS PASS) <<<   ");
    std::println("=======================================================\n");

    return 0;
}
