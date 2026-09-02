#include "../db/db_pool.hpp"
#include "../db/db_repository.hpp"
#include "../db/seed_data.hpp"
#include <iostream>
#include <format>
#include <print>
#include <cassert>

int main() {
    std::println("\n=======================================================");
    std::println("   单元测试: PostgreSQL 18 连接池、ACID事务与业务仓储   ");
    std::println("=======================================================\n");

    const std::string conninfo = "host=127.0.0.1 port=5432 dbname=postgres user=postgres password=Express1.";
    ev::DbPool::instance().init(conninfo, 4, 16);

    // 1. 预置中等规模测试数据
    std::println(">>> 1. 验证中等规模数据自动预置...");
    bool seeded = ev::SeedDataGenerator::populate_if_empty();
    if (!seeded) {
        std::println("  [FAIL] 预置数据失败");
        return 1;
    }
    std::println("  [PASS] 预置数据完成");

    auto& repo = ev::DbRepository::instance();

    // 2. 验证用户模块
    std::println("\n>>> 2. 验证用户免密登录与注册...");
    auto user_res = repo.get_or_create_user_passwordless("13899998888");
    if (!user_res) {
        std::println("  [FAIL] 免密用户创建失败");
        return 1;
    }
    auto [user, is_new] = *user_res;
    std::println("  [PASS] 用户获取/创建成功: UID={}, Phone={}, Nickname={}, IsNew={}", 
                 user.user_id, user.phone, user.nickname, is_new);

    // 3. 验证钱包与幂等充值
    std::println("\n>>> 3. 验证钱包充值与幂等性控制...");
    int64_t recharge_cents = 10000; // 100 元
    std::string idem_key = std::format("IDEM_TEST_REC_{}", user.user_id);

    auto rec_res1 = repo.recharge_wallet(user.user_id, recharge_cents, idem_key, "测试充值100元");
    if (!rec_res1) {
        std::println("  [FAIL] 充值失败: {}", error_message(rec_res1.error()));
        return 1;
    }
    std::println("  [PASS] 第一次充值成功: TxID={}, BalanceBefore={:.2f}, BalanceAfter={:.2f}",
                 rec_res1->transaction_id, rec_res1->balance_before, rec_res1->balance_after);

    // 幂等重复请求
    auto rec_res2 = repo.recharge_wallet(user.user_id, recharge_cents, idem_key, "测试充值100元");
    if (!rec_res2 || rec_res2->balance_after != rec_res1->balance_after) {
        std::println("  [FAIL] 幂等性拦截异常");
        return 1;
    }
    std::println("  [PASS] 相同 Idempotency-Key 幂等拦截测试成功，未发生重复入账！");

    // 4. 验证充电站与枪位查询
    std::println("\n>>> 4. 验证充电站与充电桩全量检索...");
    auto stations_res = repo.get_all_stations();
    if (!stations_res || stations_res->empty()) {
        std::println("  [FAIL] 获取充电站列表失败");
        return 1;
    }
    std::println("  [PASS] 查询到全网 {} 个充电站", stations_res->size());

    int64_t test_sid = stations_res->front().station_id;
    auto piles_res = repo.get_piles_by_station(test_sid);
    if (!piles_res || piles_res->empty()) {
        std::println("  [FAIL] 获取电站桩位失败");
        return 1;
    }
    std::println("  [PASS] 充电站 ID={} 拥有 {} 个充电桩 (1号桩: ID={}, 状态={})", 
                 test_sid, piles_res->size(), piles_res->front().pile_id, piles_res->front().status);

    // 5. 验证完整充电业务闭环 (创建订单 -> 停止 -> 行锁扣费结算)
    std::println("\n>>> 5. 验证充电订单全流程 (开枪 -> 停止 -> 超时占位费 -> 钱包行锁扣费)...");
    std::string test_oid = std::format("ORD_UNIT_{}", ev::current_time_ms());
    std::string test_pid = piles_res->front().pile_id;
    int64_t start_t = ev::current_time_ms();

    ev::OrderModel order{
        .order_id = test_oid,
        .user_id = user.user_id,
        .station_id = test_sid,
        .pile_id = test_pid,
        .strategy_type = "FULL",
        .strategy_value = 0,
        .order_status = "CHARGING",
        .start_time = start_t,
        .start_soc = 20,
        .end_soc = 20,
        .electricity_price = 1.45,
        .service_price = 0.35,
        .overtime_grace_minutes = 15,
        .overtime_rate_per_15min = 5.00,
        .created_at = start_t,
        .updated_at = start_t
    };

    auto create_ord_res = repo.create_order(order);
    if (!create_ord_res) {
        std::println("  [FAIL] 创建充电订单失败: {}", error_message(create_ord_res.error()));
        return 1;
    }
    std::println("  [PASS] 充电订单创建成功: OrderID={}", test_oid);

    // 停止充电 (模拟充电 30度，充满后超时占用 25分钟 -> 产生 5元占位费)
    int64_t end_t = start_t + 1800000;
    double energy_kwh = 30.0;
    int64_t elec_cents = ev::yuan_to_cents(30.0 * 1.45); // 43.50
    int64_t serv_cents = ev::yuan_to_cents(30.0 * 0.35); // 10.50
    int overtime_mins = 25; // 超出15分钟免费期10分钟 -> 计1个周期5.00元
    int64_t overtime_cents = ev::yuan_to_cents(5.00);
    int64_t total_cents = elec_cents + serv_cents + overtime_cents; // 59.00

    auto stop_res = repo.stop_order(test_oid, end_t, 100, energy_kwh, elec_cents, serv_cents, overtime_mins, overtime_cents, total_cents, "USER_MANUAL_STOP");
    if (!stop_res) {
        std::println("  [FAIL] 停止充电失败: {}", error_message(stop_res.error()));
        return 1;
    }
    std::println("  [PASS] 停止充电成功: 状态={}, 电费={:.2f}, 服务费={:.2f}, 占位费={:.2f}, 总费用={:.2f}",
                 stop_res->order_status, stop_res->electricity_fee, stop_res->service_fee, stop_res->overtime_fee, stop_res->total_amount);

    // 结算扣款
    std::string settle_key = std::format("SETTLE_{}", test_oid);
    auto settle_res = repo.settle_order_with_wallet(test_oid, settle_key);
    if (!settle_res) {
        std::println("  [FAIL] 结算扣费失败: {}", error_message(settle_res.error()));
        return 1;
    }
    std::println("  [PASS] 钱包扣费成功: 扣款={:.2f}, 钱包新余额={:.2f}",
                 settle_res->wallet_deducted, settle_res->new_balance);

    // 6. 验证管理员一键退款功能
    std::println("\n>>> 6. 验证管理员对指定订单一键退款...");
    std::string refund_key = std::format("RF_{}", test_oid);
    auto refund_res = repo.refund_order_with_wallet(test_oid, total_cents, 99999, refund_key, "单元测试退款");
    if (!refund_res) {
        std::println("  [FAIL] 一键退款失败: {}", error_message(refund_res.error()));
        return 1;
    }
    std::println("  [PASS] 一键退款成功: 退款单号={}, 退款金额={:.2f}, 钱包恢复至={:.2f}, 订单状态={}",
                 refund_res->refund_transaction_id, refund_res->refund_amount, refund_res->user_balance_after, refund_res->order_status);

    // 7. 验证单站销售业绩统计 (今日/7天/30天)
    std::println("\n>>> 7. 验证指定电站销售报表与大盘趋势...");
    auto stats_today = repo.get_station_sales_stats(test_sid, "today");
    if (!stats_today) {
        std::println("  [FAIL] 查询电站今日销售统计失败");
        return 1;
    }
    std::println("  [PASS] 电站(ID={})今日销售报表: 营收={:.2f}元, 订单数={}, 充电量={:.2f}kWh",
                 test_sid, stats_today->summary.total_revenue, stats_today->summary.total_order_count, stats_today->summary.total_energy_kwh);

    auto stats_7d = repo.get_station_sales_stats(test_sid, "7d");
    if (!stats_7d) {
        std::println("  [FAIL] 查询电站7天销售统计失败");
        return 1;
    }
    std::println("  [PASS] 电站(ID={})近7天销售报表: 营收={:.2f}元, 时间切片数={}",
                 test_sid, stats_7d->summary.total_revenue, stats_7d->timeline.time_slots.size());

    // 8. 验证按用户查询历史订单 (按时间正序/倒序)
    std::println("\n>>> 8. 验证管理员按用户查询历史订单 (排序按时间)...");
    auto user_orders = repo.get_admin_user_orders(user.user_id, 1, 10, "asc");
    if (!user_orders) {
        std::println("  [FAIL] 查询用户历史订单失败");
        return 1;
    }
    std::println("  [PASS] 查询用户(UID={})订单列表成功: 订单总数={}, 排序={}",
                 user.user_id, user_orders->total, user_orders->sort_order);

    std::println("\n=======================================================");
    std::println("   >>> 数据库层与业务仓储全部测试通过 (ALL PASS) <<<   ");
    std::println("=======================================================\n");

    return 0;
}
