#include "seed_data.hpp"
#include "../common/types.hpp"
#include <iostream>
#include <format>
#include <vector>
#include <random>

namespace ev {

bool SeedDataGenerator::populate_if_empty() {
    auto conn = DbPool::instance().acquire();
    if (!conn) {
        std::cerr << "[Seed Error] Cannot acquire connection from DbPool\n";
        return false;
    }

    // 检查是否已存在管理员
    PgResultGuard chk(conn->exec("SELECT COUNT(*) FROM users WHERE role = 'admin';"));
    if (chk.is_ok() && chk.rows() > 0 && std::stoll(chk.value(0, 0)) > 0) {
        std::cout << "[Seed] Database already seeded. Skipping initial data population.\n";
        return true;
    }

    std::cout << "[Seed] Starting medium-scale seed data generation (25 Stations, 250 Piles, 50 Users, Historical Orders)...\n";

    int64_t now = current_time_ms();

    // 1. 初始化管理员 (admin / 123456)
    std::string admin_sql = std::format(
        "INSERT INTO users (phone, password_hash, nickname, avatar_url, role, status, created_at, updated_at) "
        "VALUES ('13900000000', '123456', '超级管理员', 'http://localhost:8080/static/avatars/admin.png', 'admin', 1, {}, {}) RETURNING user_id;",
        now - 864000000LL, now - 864000000LL
    );
    PgResultGuard a_res(conn->exec(admin_sql.c_str()));
    if (a_res.is_ok() && a_res.rows() > 0) {
        int64_t admin_uid = std::stoll(a_res.value(0, 0));
        std::string a_w = std::format(
            "INSERT INTO user_wallets (user_id, balance_cents, frozen_cents, status, updated_at) VALUES ({}, 99999900, 0, 1, {});",
            admin_uid, now
        );
        conn->exec(a_w.c_str());
    }

    // 2. 批量生成 50 名测试用户
    std::vector<int64_t> created_user_ids;
    for (int i = 1; i <= 50; ++i) {
        std::string phone = std::format("138{:08d}", i);
        std::string nick = std::format("车主_{:04d}", i);
        int64_t reg_time = now - (static_cast<int64_t>(i) * 3600000LL * 8);

        std::string u_sql = std::format(
            "INSERT INTO users (phone, password_hash, nickname, avatar_url, role, status, created_at, updated_at) "
            "VALUES ('{}', '123456', '{}', 'http://localhost:8080/static/avatars/default.png', 'user', 1, {}, {}) RETURNING user_id;",
            phone, nick, reg_time, reg_time
        );
        PgResultGuard u_res(conn->exec(u_sql.c_str()));
        if (u_res.is_ok() && u_res.rows() > 0) {
            int64_t uid = std::stoll(u_res.value(0, 0));
            created_user_ids.push_back(uid);

            int64_t init_balance = (100 + (i % 50) * 10) * 100; // 100 ~ 600 元
            std::string w_sql = std::format(
                "INSERT INTO user_wallets (user_id, balance_cents, frozen_cents, status, updated_at) VALUES ({}, {}, 0, 1, {});",
                uid, init_balance, reg_time
            );
            conn->exec(w_sql.c_str());

            // 初始充值流水
            std::string tx_id = std::format("TX_INIT_{}_{}", reg_time, uid);
            std::string f_sql = std::format(
                "INSERT INTO wallet_transaction_flows (id, user_id, flow_type, amount_cents, balance_before_cents, balance_after_cents, related_order_id, operator_id, remark, idempotent_key, created_at) "
                "VALUES ('{}', {}, 1, {}, 0, {}, '', 0, '系统初始化充值', 'INIT_KEY_{}', {});",
                tx_id, uid, init_balance, init_balance, uid, reg_time
            );
            conn->exec(f_sql.c_str());
        }
    }

    // 3. 批量生成 25 个充电站 (分布在城市中心与环线周围)
    struct StationPreset {
        const char* name;
        const char* address;
        double lat;
        double lng;
        double price;
        double service;
    };

    std::vector<StationPreset> preset_stations = {
        {"东软高新科技园超级充电站", "高新区软件园中路1号", 31.235000, 121.478000, 1.45, 0.35},
        {"金融中心地下智慧充电站", "陆家嘴金融贸易区88号地下B2层", 31.240100, 121.490000, 1.80, 0.50},
        {"虹桥枢纽超级快充站", "申昆路1500号虹桥枢纽P9停车场", 31.192000, 121.320000, 1.60, 0.40},
        {"张江科学城液冷超充港", "张江高科技园区博云路2号", 31.205000, 121.590000, 1.50, 0.35},
        {"徐家汇商圈地下充电港", "肇嘉浜路1111号美罗城B3", 31.195000, 121.436000, 1.75, 0.45},
        {"五角场万达广场超充站", "国宾路36号万达广场地下B2", 31.300000, 121.515000, 1.55, 0.35},
        {"临港新片区重卡与客车快充站", "临港新片区环湖西一路99号", 30.890000, 121.920000, 1.20, 0.30},
        {"静安大悦城智慧充电港", "西藏北路166号大悦城南楼B3", 31.246000, 121.474000, 1.65, 0.45},
        {"浦东国际机场P4航站楼充电站", "浦东国际机场P4长租停车场1层", 31.144000, 121.808000, 1.50, 0.40},
        {"宝山智慧湾科创园充电站", "蕰川路6号智慧湾园区南门", 31.340000, 121.430000, 1.35, 0.30},
        {"嘉定汽车城智能网联充电站", "安亭镇博园路7575号", 31.285000, 121.165000, 1.40, 0.35},
        {"松江大学城文汇路快充站", "文汇路800弄大学城商业街", 31.045000, 121.215000, 1.30, 0.30},
        {"闵行七宝万科城市充电港", "漕宝路3366号七宝万科广场B2", 31.155000, 121.355000, 1.60, 0.40},
        {"普陀环球港大型超充站", "中山北路3300号环球港地下车库B3", 31.233000, 121.412000, 1.70, 0.45},
        {"杨浦滨江绿能示范站", "杨树浦路1088号滨江国际广场", 31.258000, 121.530000, 1.45, 0.35},
        {"青浦奥特莱斯快充站", "沪青平公路2888号奥特莱斯停车场", 31.135000, 121.205000, 1.40, 0.35},
        {"奉贤南桥百联充电站", "南桥镇百齐路588号百联南桥购物中心", 30.915000, 121.465000, 1.35, 0.30},
        {"金山石化万达超充站", "龙皓路1088号万达广场地下B2", 30.745000, 121.335000, 1.25, 0.30},
        {"崇明陈家镇生态充电站", "陈家镇陈通路88号", 31.505000, 121.805000, 1.20, 0.30},
        {"外滩SOHO地下绿色充电港", "中山东二路88号外滩SOHO B3", 31.231000, 121.488000, 1.90, 0.50},
        {"北外滩白玉兰广场超充站", "东大名路501号白玉兰广场B3", 31.250000, 121.498000, 1.85, 0.50},
        {"前滩太古里液冷超充示范站", "东育路500号前滩太古里B2", 31.156000, 121.480000, 1.80, 0.45},
        {"大宁久光中心快充站", "大宁路400号久光百货地下车库", 31.272000, 121.450000, 1.65, 0.40},
        {"真如中海环宇城充电港", "铜川路699号中海环宇城MAX B2", 31.255000, 121.398000, 1.60, 0.40},
        {"莘庄龙之梦地下充电站", "沪闵路6088号莘庄龙之梦B3", 31.110000, 121.385000, 1.55, 0.35}
    };

    std::vector<int64_t> created_station_ids;
    std::vector<std::string> all_pile_ids;

    for (const auto& s : preset_stations) {
        std::string s_sql = std::format(
            "INSERT INTO stations (station_name, address, latitude, longitude, contact_phone, operating_hours, price_per_kwh, service_fee_per_kwh, overtime_fee_per_15min, overtime_grace_minutes, status, created_at, updated_at) "
            "VALUES ('{}', '{}', {}, {}, '021-88889999', '00:00 - 24:00', {}, {}, 5.00, 15, 1, {}, {}) RETURNING station_id;",
            s.name, s.address, s.lat, s.lng, s.price, s.service, now - 864000000LL, now - 864000000LL
        );
        PgResultGuard s_res(conn->exec(s_sql.c_str()));
        if (s_res.is_ok() && s_res.rows() > 0) {
            int64_t sid = std::stoll(s_res.value(0, 0));
            created_station_ids.push_back(sid);

            // 每个充电站生成 10 个充电桩 (共 25 * 10 = 250 桩)
            for (int p = 1; p <= 10; ++p) {
                std::string pid = std::format("P{:03d}{:02d}", sid, p);
                std::string p_type = (p <= 7) ? "FAST" : "SLOW";
                double p_kw = (p <= 7) ? ((p <= 2) ? 240.0 : 120.0) : 7.0;
                std::string p_name = std::format("{:02d}号{}桩", p, (p <= 7 ? "直流快充" : "交流慢充"));
                std::string status = (p == 10) ? "FAULT" : ((p % 3 == 0) ? "CHARGING" : "IDLE");

                std::string p_sql = std::format(
                    "INSERT INTO piles (pile_id, station_id, pile_name, type, gun_type, max_power_kw, voltage_range, status, total_charge_count, total_charge_hours, last_heartbeat_at, created_at, updated_at) "
                    "VALUES ('{}', {}, '{}', '{}', '国标2015', {}, '200V-750V', '{}', {}, {}, {}, {}, {});",
                    pid, sid, p_name, p_type, p_kw, status, (p * 50), (p * 80.5), now, now - 864000000LL, now
                );
                conn->exec(p_sql.c_str());
                all_pile_ids.push_back(pid);
            }
        }
    }

    // 4. 生成 100+ 条历史充电订单数据 (跨越过去 7 天与近一个月，供销售报表与历史查询测试)
    std::mt19937 rng(42);
    std::uniform_int_distribution<size_t> u_dist(0, created_user_ids.size() - 1);
    std::uniform_int_distribution<size_t> s_dist(0, created_station_ids.size() - 1);
    std::uniform_int_distribution<int> day_dist(0, 28);
    std::uniform_int_distribution<int> duration_dist(20, 80);

    for (int i = 1; i <= 120; ++i) {
        int64_t uid = created_user_ids[u_dist(rng)];
        int64_t sid = created_station_ids[s_dist(rng)];
        std::string pid = std::format("P{:03d}{:02d}", sid, (i % 9) + 1);

        int days_ago = day_dist(rng);
        int duration_mins = duration_dist(rng);
        int64_t st = now - (static_cast<int64_t>(days_ago) * 86400000LL) - (i * 360000LL);
        int64_t et = st + (duration_mins * 60000LL);

        double kwh = 15.0 + (duration_mins * 0.45);
        double elec_price = 1.45;
        double serv_price = 0.35;
        int64_t elec_fee_cents = yuan_to_cents(kwh * elec_price);
        int64_t serv_fee_cents = yuan_to_cents(kwh * serv_price);

        int overtime_mins = (duration_mins > 60) ? (duration_mins - 60) : 0;
        int64_t overtime_fee_cents = (overtime_mins > 15) ? (yuan_to_cents(5.00 * ((overtime_mins - 1) / 15 + 1))) : 0;
        int64_t total_fee_cents = elec_fee_cents + serv_fee_cents + overtime_fee_cents;

        std::string oid = std::format("ORD_{:08d}", i);
        std::string o_status = (i == 1) ? "CHARGING" : "COMPLETED";

        std::string ord_sql = std::format(
            "INSERT INTO charging_orders (order_id, user_id, station_id, pile_id, strategy_type, strategy_value, order_status, start_time, end_time, start_soc, end_soc, charged_energy_kwh, electricity_price, electricity_fee_cents, service_price, service_fee_cents, overtime_grace_minutes, overtime_duration_minutes, overtime_rate_per_15min, overtime_fee_cents, total_fee_cents, stop_reason, settled_at, created_at, updated_at) "
            "VALUES ('{}', {}, {}, '{}', 'FULL', 0, '{}', {}, {}, 20, 100, {}, {}, {}, {}, {}, 15, {}, 5.00, {}, {}, 'USER_MANUAL_STOP', {}, {}, {});",
            oid, uid, sid, pid, o_status, st, (o_status == "CHARGING" ? 0 : et), kwh, elec_price, elec_fee_cents, serv_price, serv_fee_cents, overtime_mins, overtime_fee_cents, total_fee_cents, (o_status == "CHARGING" ? 0 : et), st, et
        );
        conn->exec(ord_sql.c_str());

        if (o_status == "COMPLETED") {
            std::string tx_id = std::format("TX_ORD_{}_{}", et, uid);
            std::string flow_sql = std::format(
                "INSERT INTO wallet_transaction_flows (id, user_id, flow_type, amount_cents, balance_before_cents, balance_after_cents, related_order_id, operator_id, remark, idempotent_key, created_at) "
                "VALUES ('{}', {}, 2, {}, 50000, {}, '{}', 0, '充电历史扣费', 'ORD_IDEM_{}', {});",
                tx_id, uid, -total_fee_cents, 50000 - total_fee_cents, oid, oid, et
            );
            conn->exec(flow_sql.c_str());
        }
    }

    std::cout << "[Seed] Successfully seeded 1 Admin, 50 Users, 25 Stations, 250 Charging Piles, and 120 Historical Orders!\n";
    return true;
}

} // namespace ev
