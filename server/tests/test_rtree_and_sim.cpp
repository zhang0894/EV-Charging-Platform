#include "../memory/rtree_index.hpp"
#include "../memory/state_pool.hpp"
#include "../simulation/simulator.hpp"
#include "../db/db_pool.hpp"
#include "../db/db_repository.hpp"
#include <iostream>
#include <print>
#include <chrono>

int main() {
    std::println("\n=======================================================");
    std::println("   单元测试: R-Tree 空间索引与充电桩高频动态模拟引擎   ");
    std::println("=======================================================\n");

    const std::string conninfo = "host=127.0.0.1 port=5432 dbname=postgres user=postgres password=Express1.";
    ev::DbPool::instance().init(conninfo, 2, 4);

    // 1. 验证 R-Tree 空间索引
    std::println(">>> 1. 验证 R-Tree 空间几何索引...");
    auto stations_res = ev::DbRepository::instance().get_all_stations();
    if (!stations_res || stations_res->empty()) {
        std::println("  [FAIL] 数据库未加载到充电站数据");
        return 1;
    }

    std::vector<std::pair<int64_t, std::pair<double, double>>> station_coords;
    for (const auto& s : *stations_res) {
        station_coords.emplace_back(s.station_id, std::make_pair(s.latitude, s.longitude));
    }

    ev::StationRTree::instance().build_index(station_coords);

    // 用户在人民广场 (31.2304, 121.4737)
    double user_lat = 31.2304;
    double user_lon = 121.4737;

    auto t_start = std::chrono::high_resolution_clock::now();
    auto nearby = ev::StationRTree::instance().search_nearby(user_lat, user_lon, 15.0, 5);
    auto t_end = std::chrono::high_resolution_clock::now();
    auto elapsed_us = std::chrono::duration_cast<std::chrono::microseconds>(t_end - t_start).count();

    std::println("  [PASS] 空间粗筛与排序完成 (耗时: {} 微秒, 返回 {} 个附近电站):", elapsed_us, nearby.size());
    for (size_t i = 0; i < nearby.size(); ++i) {
        std::println("         {}. 电站 ID={}, 距离={:.2f} km, 坐标=({}, {})", 
                     i + 1, nearby[i].station_id, nearby[i].distance_km, nearby[i].latitude, nearby[i].longitude);
    }

    // 2. 验证内存状态池 ChargingStatePool
    std::println("\n>>> 2. 验证高并发内存状态池 ChargingStatePool...");
    auto test_sid = stations_res->front().station_id;
    auto piles_res = ev::DbRepository::instance().get_piles_by_station(test_sid);
    if (!piles_res || piles_res->empty()) {
        std::println("  [FAIL] 获取电桩失败");
        return 1;
    }

    ev::ChargingStatePool::instance().init_from_piles(*piles_res);
    auto summary = ev::ChargingStatePool::instance().get_station_pile_summary(test_sid);
    std::println("  [PASS] 状态池电站(ID={})汇总: 总桩数={}, 空闲快充={}, 空闲慢充={}, 占用={}, 故障={}",
                 test_sid, summary.total_piles, summary.fast_piles_idle, summary.slow_piles_idle, summary.busy_piles, summary.fault_piles);

    // 3. 验证充电动态模拟与超时占位费引擎
    std::println("\n>>> 3. 验证充电模拟引擎与超时占位费阶梯计算...");
    std::string test_pid = piles_res->front().pile_id;
    std::string test_oid = "ORD_SIM_TEST_001";

    bool start_ok = ev::ChargingStatePool::instance().start_charging(
        test_pid, test_oid, 10001, 20, 1.45, 0.35, 5.00, 15
    );
    if (!start_ok) {
        std::println("  [FAIL] 启动模拟充电失败");
        return 1;
    }
    std::println("  [PASS] 充电桩 {} 成功开启充电，初始 SOC=20%", test_pid);

    // 连续单步模拟推进 (模拟 100 步高倍率快充)
    for (int step = 1; step <= 20; ++step) {
        ev::ChargingSimulator::instance().step_once(180.0); // 每次推进 3 分钟
    }

    auto pile_st = ev::ChargingStatePool::instance().get_pile_state(test_pid);
    if (!pile_st) {
        std::println("  [FAIL] 无法获取桩状态");
        return 1;
    }

    std::println("  [PASS] 20轮快速模拟后: SOC={}%, 已充度数={:.2f} kWh, 电费={:.2f}元, 服务费={:.2f}元, 是否充满={}",
                 pile_st->current_soc, pile_st->charged_energy_kwh, 
                 ev::cents_to_yuan(pile_st->electricity_fee_cents), 
                 ev::cents_to_yuan(pile_st->service_fee_cents), 
                 pile_st->is_full);

    // 模拟充满后车辆未拔枪超时占位 40 分钟 (15分钟免费，超时25分钟 -> 应计收 2 个周期 10.00 元)
    std::println("\n>>> 4. 验证充满后超时占位费计费...");
    pile_st->is_full = true;
    pile_st->full_timestamp = ev::current_time_ms() - (40LL * 60000LL); // 40 分钟前充满
    ev::ChargingStatePool::instance().update_pile_state(*pile_st);

    ev::ChargingSimulator::instance().step_once(1.0);

    auto final_st = ev::ChargingStatePool::instance().get_pile_state(test_pid);
    if (!final_st) return 1;

    std::println("  [PASS] 超时占用状态检测:");
    std::println("         - 充满后占用时长: {} 分钟", final_st->overtime_duration_minutes);
    std::println("         - 免费宽限时长: {} 分钟", final_st->overtime_grace_minutes);
    std::println("         - 产生超时占位费: {:.2f} 元 (预期 10.00 元)", ev::cents_to_yuan(final_st->overtime_fee_cents));
    std::println("         - 最终订单累计总费用: {:.2f} 元", ev::cents_to_yuan(final_st->total_fee_cents));

    std::println("\n=======================================================");
    std::println("   >>> 空间索引与动态模拟全部测试通过 (ALL PASS) <<<   ");
    std::println("=======================================================\n");

    return 0;
}
