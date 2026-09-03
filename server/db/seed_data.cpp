#include "seed_data.hpp"
#include "../common/types.hpp"
#include <iostream>
#include <format>
#include <vector>
#include <random>
#include <string>

namespace ev {

bool SeedDataGenerator::populate_if_empty() {
    auto conn = DbPool::instance().acquire();
    if (!conn) {
        std::cerr << "[Seed Error] Cannot acquire connection from DbPool\n";
        return false;
    }

    constexpr int TARGET_USERS = 20000;       // 超大规模: 20,000 用户
    constexpr int TARGET_STATIONS = 10000;    // 超大规模: 10,000 电站
    constexpr int TARGET_PILES = 100000;      // 超大规模: 100,000 电桩
    constexpr int TARGET_ORDERS = 200000;     // 超大规模: 200,000 订单

    // 检查电站数量是否达到超大规模 (>= 10,000)
    PgResultGuard st_chk(conn->exec("SELECT COUNT(*) FROM stations;"));
    if (st_chk.is_ok() && st_chk.rows() > 0 && std::stoll(st_chk.value(0, 0)) >= TARGET_STATIONS) {
        std::cout << "[Seed] Large-scale dataset already loaded (10,000 Stations, 100,000 Piles, 20,000 Users, 200,000 Orders). Skipping seed.\n";
        return true;
    }

    std::cout << "[Seed] >>> Initializing Super Large Dataset:\n"
              << "       - 10,000 Charging Stations\n"
              << "       - 100,000 Charging Piles\n"
              << "       - 20,000 User Accounts & Wallets\n"
              << "       - 200,000 Historical Orders & Financial Ledgers...\n";

    int64_t now = current_time_ms();

    // 1. 初始化管理员 (admin / 123456)
    std::string admin_sql = std::format(
        "INSERT INTO users (phone, password_hash, nickname, avatar_url, role, status, created_at, updated_at) "
        "VALUES ('13900000000', '123456', '超级管理员', 'http://localhost:8080/static/avatars/admin.png', 'admin', 1, {}, {}) "
        "ON CONFLICT (phone) DO UPDATE SET updated_at = {} RETURNING user_id;",
        now - 864000000LL, now - 864000000LL, now
    );
    PgResultGuard a_res(conn->exec(admin_sql.c_str()));
    if (a_res.is_ok() && a_res.rows() > 0) {
        int64_t admin_uid = std::stoll(a_res.value(0, 0));
        std::string a_w = std::format(
            "INSERT INTO user_wallets (user_id, balance_cents, frozen_cents, status, updated_at) VALUES ({}, 99999900, 0, 1, {}) "
            "ON CONFLICT (user_id) DO NOTHING;",
            admin_uid, now
        );
        conn->exec(a_w.c_str());
    }

    // 2. 批量高性能生成用户 (TARGET_USERS)
    std::cout << "  -> Bulk inserting " << TARGET_USERS << " user accounts and wallets...\n";
    constexpr int USER_BATCH = 1000;
    for (int batch = 0; batch < TARGET_USERS; batch += USER_BATCH) {
        int end_u = std::min(batch + USER_BATCH, TARGET_USERS);
        std::string u_batch_sql = "INSERT INTO users (phone, password_hash, nickname, avatar_url, role, status, created_at, updated_at) VALUES ";
        std::string w_batch_sql = "INSERT INTO user_wallets (user_id, balance_cents, frozen_cents, status, updated_at) VALUES ";

        for (int i = batch + 1; i <= end_u; ++i) {
            std::string phone = std::format("138{:08d}", i);
            std::string nick = std::format("车主_{:05d}", i);
            int64_t reg_time = now - (static_cast<int64_t>(i % 30) * 86400000LL);

            if (i > batch + 1) u_batch_sql += ", ";
            u_batch_sql += std::format("('{}', '123456', '{}', 'http://localhost:8080/static/avatars/default.png', 'user', 1, {}, {})",
                                       phone, nick, reg_time, reg_time);
        }
        u_batch_sql += " ON CONFLICT (phone) DO NOTHING;";

        DbPool::instance().with_transaction([&](DbConnection& tx_conn) -> Result<void> {
            tx_conn.exec(u_batch_sql.c_str());
            return {};
        });
    }

    // 批量初始化钱包
    std::string init_wallets_sql = std::format(
        "INSERT INTO user_wallets (user_id, balance_cents, frozen_cents, status, updated_at) "
        "SELECT user_id, 20000, 0, 1, {} FROM users WHERE role = 'user' ON CONFLICT (user_id) DO NOTHING;",
        now
    );
    DbPool::instance().with_transaction([&](DbConnection& tx_conn) -> Result<void> {
        tx_conn.exec(init_wallets_sql.c_str());
        return {};
    });

    // 3. 批量生成充电站 (TARGET_STATIONS) 与 充电桩 (TARGET_PILES)
    std::cout << "  -> Bulk inserting " << TARGET_STATIONS << " charging stations and " << TARGET_PILES << " charging piles...\n";

    std::mt19937 rng(1337);
    // 北京境内经纬度分布 (北纬 39.4400° ~ 41.0500°, 东经 115.4200° ~ 117.5000°)
    std::uniform_real_distribution<double> lat_dist(39.4400, 41.0500);
    std::uniform_real_distribution<double> lon_dist(115.4200, 117.5000);
    std::uniform_real_distribution<double> price_dist(1.20, 1.95);
    std::uniform_real_distribution<double> serv_dist(0.30, 0.50);

    const std::vector<std::string> prefix_names = {
        "高新科技园", "智慧网联", "超级快充港", "液冷超充示范站", "金融贸易区",
        "交通枢纽", "万达广场", "科创示范区", "绿色出行", "生态充电港",
        "大学城商业街", "滨江示范站", "太古里超充站", "奥特莱斯", "百联智充站"
    };

    constexpr int STATION_BATCH = 500;
    for (int batch = 0; batch < TARGET_STATIONS; batch += STATION_BATCH) {
        int end_s = std::min(batch + STATION_BATCH, TARGET_STATIONS);
        std::string s_batch_sql = "INSERT INTO stations (station_name, address, latitude, longitude, contact_phone, operating_hours, price_per_kwh, service_fee_per_kwh, overtime_fee_per_15min, overtime_grace_minutes, status, created_at, updated_at) VALUES ";
        
        for (int s = batch + 1; s <= end_s; ++s) {
            std::string sname = std::format("{}_{:05d}号超级电站", prefix_names[s % prefix_names.size()], s);
            std::string saddr = std::format("新能源大道{:05d}号", s);
            double lat = lat_dist(rng);
            double lon = lon_dist(rng);
            double price = std::round(price_dist(rng) * 100.0) / 100.0;
            double serv = std::round(serv_dist(rng) * 100.0) / 100.0;

            if (s > batch + 1) s_batch_sql += ", ";
            s_batch_sql += std::format("('{}', '{}', {:.5f}, {:.5f}, '010-88889999', '00:00 - 24:00', {:.2f}, {:.2f}, 5.00, 15, 1, {}, {})",
                                       sname, saddr, lat, lon, price, serv, now - 864000000LL, now - 864000000LL);
        }
        s_batch_sql += " ON CONFLICT DO NOTHING;";

        DbPool::instance().with_transaction([&](DbConnection& tx_conn) -> Result<void> {
            tx_conn.exec(s_batch_sql.c_str());
            return {};
        });

        // 对应生成充电桩
        std::string p_batch_sql = "INSERT INTO piles (pile_id, station_id, pile_name, type, gun_type, max_power_kw, voltage_range, status, total_charge_count, total_charge_hours, last_heartbeat_at, created_at, updated_at) VALUES ";
        bool first_p = true;
        for (int s = batch + 1; s <= end_s; ++s) {
            for (int p = 1; p <= 10; ++p) {
                std::string pid = std::format("P{:05d}{:02d}", s, p);
                std::string p_type = (p <= 7) ? "FAST" : "SLOW";
                double p_kw = (p <= 7) ? ((p <= 2) ? 240.0 : 120.0) : 7.0;
                std::string p_name = std::format("{:02d}号{}桩", p, (p <= 7 ? "直流快充" : "交流慢充"));
                std::string status = (p == 10) ? "FAULT" : ((p % 4 == 0) ? "CHARGING" : "IDLE");

                if (!first_p) p_batch_sql += ", ";
                p_batch_sql += std::format("('{}', {}, '{}', '{}', '国标2015', {:.1f}, '200V-750V', '{}', {}, {:.1f}, {}, {}, {})",
                                           pid, s, p_name, p_type, p_kw, status, (p * 50), (p * 80.5), now, now - 864000000LL, now);
                first_p = false;
            }
        }
        p_batch_sql += " ON CONFLICT (pile_id) DO NOTHING;";

        DbPool::instance().with_transaction([&](DbConnection& tx_conn) -> Result<void> {
            tx_conn.exec(p_batch_sql.c_str());
            return {};
        });
    }

    // 4. 批量生成 200,000 笔历史订单 (TARGET_ORDERS)
    std::cout << "  -> Bulk inserting " << TARGET_ORDERS << " historical charging orders...\n";
    constexpr int ORDER_BATCH = 2000;
    std::uniform_int_distribution<int> u_dist(1, TARGET_USERS);
    std::uniform_int_distribution<int> s_dist(1, TARGET_STATIONS);
    std::uniform_int_distribution<int> day_dist(0, 28);
    std::uniform_int_distribution<int> duration_dist(20, 80);

    for (int batch = 0; batch < TARGET_ORDERS; batch += ORDER_BATCH) {
        int end_o = std::min(batch + ORDER_BATCH, TARGET_ORDERS);
        std::string ord_batch_sql = "INSERT INTO charging_orders (order_id, user_id, station_id, pile_id, strategy_type, strategy_value, order_status, start_time, end_time, start_soc, end_soc, charged_energy_kwh, electricity_price, electricity_fee_cents, service_price, service_fee_cents, overtime_grace_minutes, overtime_duration_minutes, overtime_rate_per_15min, overtime_fee_cents, total_fee_cents, stop_reason, settled_at, created_at, updated_at) VALUES ";

        for (int i = batch + 1; i <= end_o; ++i) {
            int uid = u_dist(rng);
            int sid = s_dist(rng);
            std::string pid = std::format("P{:05d}{:02d}", sid, (i % 9) + 1);

            int days_ago = day_dist(rng);
            int duration_mins = duration_dist(rng);
            int64_t st = now - (static_cast<int64_t>(days_ago) * 86400000LL) - (i * 120000LL);
            int64_t et = st + (duration_mins * 60000LL);

            double kwh = 15.0 + (duration_mins * 0.45);
            double elec_price = 1.45;
            double serv_price = 0.35;
            int64_t elec_fee_cents = yuan_to_cents(kwh * elec_price);
            int64_t serv_fee_cents = yuan_to_cents(kwh * serv_price);

            int overtime_mins = (duration_mins > 60) ? (duration_mins - 60) : 0;
            int64_t overtime_fee_cents = (overtime_mins > 15) ? (yuan_to_cents(5.00 * ((overtime_mins - 1) / 15 + 1))) : 0;
            int64_t total_fee_cents = elec_fee_cents + serv_fee_cents + overtime_fee_cents;

            std::string oid = std::format("ORD_HST_{:08d}", i);
            std::string o_status = (i % 50 == 0) ? "REFUNDED" : "COMPLETED";

            if (i > batch + 1) ord_batch_sql += ", ";
            ord_batch_sql += std::format("('{}', {}, {}, '{}', 'FULL', 0, '{}', {}, {}, 20, 100, {:.2f}, {:.2f}, {}, {:.2f}, {}, 15, {}, 5.00, {}, {}, 'USER_MANUAL_STOP', {}, {}, {})",
                                         oid, uid, sid, pid, o_status, st, et, kwh, elec_price, elec_fee_cents, serv_price, serv_fee_cents, overtime_mins, overtime_fee_cents, total_fee_cents, et, st, et);
        }
        ord_batch_sql += " ON CONFLICT (order_id) DO NOTHING;";

        DbPool::instance().with_transaction([&](DbConnection& tx_conn) -> Result<void> {
            tx_conn.exec(ord_batch_sql.c_str());
            return {};
        });
    }

    std::cout << "[Seed] Successfully initialized super large dataset:\n"
              << "       - 10,000 Stations\n"
              << "       - 100,000 Piles\n"
              << "       - 20,000 Users\n"
              << "       - 200,000 Orders!\n";

    return true;
}

bool SeedDataGenerator::clear_database() {
    auto conn = DbPool::instance().acquire();
    if (!conn) {
        std::cerr << "[Seed Error] Cannot acquire connection from DbPool to clear database\n";
        return false;
    }
    std::cout << "[Seed] Truncating all business tables...\n";
    PgResultGuard res(conn->exec("TRUNCATE TABLE charging_orders, wallet_transaction_flows, piles, user_wallets, stations, users RESTART IDENTITY CASCADE;"));
    if (!res.is_ok()) {
        std::cerr << "[Seed Error] Failed to truncate tables: " << conn->last_error() << "\n";
        return false;
    }
    std::cout << "[Seed] Successfully truncated all tables.\n";
    return true;
}

} // namespace ev
