#include "seed_data.hpp"
#include "schema_migrator.hpp"
#include "../common/types.hpp"
#include <iostream>
#include <fstream>
#include <filesystem>
#include <format>
#include <vector>
#include <string>
#include <chrono>
#include <glaze/glaze.hpp>

namespace ev {

namespace {

// 安全转义 SQL 字符串中的单引号
std::string sql_escape(std::string_view s) {
    std::string out;
    out.reserve(s.size() + 4);
    for (char c : s) {
        if (c == '\'') out.push_back('\'');
        out.push_back(c);
    }
    return out;
}

// 跨不同运行工作目录智能定位数据文件
std::string resolve_data_path(const std::string& data_dir, const std::string& filename) {
    std::vector<std::string> candidates = {
        data_dir + "/" + filename,
        "server/data/" + filename,
        "data/" + filename,
        "../data/" + filename,
        "../server/data/" + filename,
        "../../server/data/" + filename,
        "e:/EV-Charging-Platform/server/data/" + filename
    };
    for (const auto& p : candidates) {
        if (std::filesystem::exists(p)) return p;
    }
    return data_dir + "/" + filename;
}

std::string read_file_content(const std::string& path) {
    std::ifstream ifs(path, std::ios::binary);
    if (!ifs) return "";
    ifs.seekg(0, std::ios::end);
    size_t sz = ifs.tellg();
    ifs.seekg(0, std::ios::beg);
    std::string buf(sz, '\0');
    ifs.read(buf.data(), sz);
    return buf;
}

} // namespace

namespace seed_internal {

// Glaze 反序列化结构体定义 (具名命名空间以支持 MSVC Glaze 反射)
struct JsonStation {
    int32_t station_id{};
    uint8_t district_code{};
    double latitude{};
    double longitude{};
    std::string name{};
    std::string address{};
    bool is_online{true};
};

struct JsonUser {
    int64_t user_id{};
    std::string phone{};
    std::string password_hash{};
    std::string nickname{};
    std::string avatar_url{};
    std::string role{};
    int16_t status{};
    int64_t balance_cents{};
    int64_t frozen_cents{};
    int64_t created_at{};
    int64_t updated_at{};
};

struct JsonPile {
    std::string pile_id{};
    int32_t station_id{};
    std::string pile_name{};
    std::string type{};
    std::string gun_type{};
    double max_power_kw{};
    std::string voltage_range{};
    std::string status{};
    int64_t total_charge_count{};
    double total_charge_hours{};
    int64_t last_heartbeat_at{};
    int64_t created_at{};
    int64_t updated_at{};
};

} // namespace seed_internal

using namespace seed_internal;

bool SeedDataGenerator::clear_database() {
    auto conn = DbPool::instance().acquire();
    if (!conn) {
        std::cerr << "[Seed Error] Cannot acquire connection from DbPool to clear database\n";
        return false;
    }
    std::cout << "[Seed] Truncating all business tables and resetting sequences...\n";
    PgResultGuard res(conn->exec(
        "TRUNCATE TABLE platform_metrics, pile_reservations, user_avatars, charging_orders, wallet_transaction_flows, piles, user_wallets, stations, users "
        "RESTART IDENTITY CASCADE;"
    ));
    if (!res.is_ok()) {
        std::cerr << "[Seed Error] Failed to truncate tables: " << conn->last_error() << "\n";
        return false;
    }
    std::cout << "[Seed] Successfully truncated all tables.\n";
    return true;
}

bool SeedDataGenerator::import_from_json(const std::string& data_dir) {
    auto t_start = std::chrono::steady_clock::now();
    std::cout << "[Seed] >>> Starting batch database import from JSON files...\n";

    // 1. 读取并导入充电站 (stations)
    std::string st_path = resolve_data_path(data_dir, "stations_processed.json");
    std::cout << "  -> Loading stations from: " << st_path << "\n";
    std::string st_buf = read_file_content(st_path);
    if (st_buf.empty()) {
        std::cerr << "[Seed Error] Could not read stations JSON file: " << st_path << "\n";
        return false;
    }

    std::vector<JsonStation> stations;
    auto ec_st = glz::read_json(stations, st_buf);
    if (ec_st) {
        std::cerr << "[Seed Error] Glaze failed to parse stations JSON: "
                  << glz::format_error(ec_st, st_buf) << "\n";
        return false;
    }
    std::cout << "     Parsed " << stations.size() << " stations. Bulk inserting...\n";

    int64_t now = current_time_ms();
    constexpr int STATION_BATCH = 1000;
    for (size_t i = 0; i < stations.size(); i += STATION_BATCH) {
        size_t end_idx = std::min(i + STATION_BATCH, stations.size());
        std::string sql = "INSERT INTO stations (station_id, station_name, address, latitude, longitude, contact_phone, operating_hours, price_per_kwh, service_fee_per_kwh, overtime_fee_per_15min, overtime_grace_minutes, status, created_at, updated_at) VALUES ";
        for (size_t j = i; j < end_idx; ++j) {
            const auto& s = stations[j];
            if (j > i) sql += ", ";
            double st_price = 1.15 + static_cast<double>((static_cast<uint64_t>(s.station_id) * 104729ULL + 12345ULL) % 71) * 0.01;
            int st_status = s.is_online ? 1 : 2;
            sql += std::format("({}, '{}', '{}', {:.6f}, {:.6f}, '010-88889999', '00:00 - 24:00', {:.2f}, 0.35, 5.00, 15, {}, {}, {})",
                               s.station_id, sql_escape(s.name), sql_escape(s.address), s.latitude, s.longitude, st_price, st_status, now - 864000000LL, now);
        }
        sql += " ON CONFLICT (station_id) DO NOTHING;";

        (void)DbPool::instance().with_transaction([&](DbConnection& tx_conn) -> Result<void> {
            tx_conn.exec(sql.c_str());
            return {};
        });
    }

    // 修复自增序列
    (void)DbPool::instance().with_transaction([&](DbConnection& tx_conn) -> Result<void> {
        tx_conn.exec("SELECT setval('stations_station_id_seq', (SELECT COALESCE(MAX(station_id), 1) FROM stations));");
        return {};
    });
    std::cout << "  [OK] Successfully imported " << stations.size() << " stations.\n";

    // 2. 读取并导入用户与钱包 (users & user_wallets)
    std::string u_path = resolve_data_path(data_dir, "seed_users.json");
    std::cout << "  -> Loading users from: " << u_path << "\n";
    std::string u_buf = read_file_content(u_path);
    if (u_buf.empty()) {
        std::cerr << "[Seed Error] Could not read users JSON file: " << u_path << "\n";
        return false;
    }

    std::vector<JsonUser> users;
    auto ec_u = glz::read_json(users, u_buf);
    if (ec_u) {
        std::cerr << "[Seed Error] Glaze failed to parse users JSON: "
                  << glz::format_error(ec_u, u_buf) << "\n";
        return false;
    }
    std::cout << "     Parsed " << users.size() << " users. Bulk inserting...\n";

    constexpr int USER_BATCH = 1000;
    for (size_t i = 0; i < users.size(); i += USER_BATCH) {
        size_t end_idx = std::min(i + USER_BATCH, users.size());
        std::string u_sql = "INSERT INTO users (user_id, phone, password_hash, nickname, avatar_url, role, status, created_at, updated_at) VALUES ";
        std::string w_sql = "INSERT INTO user_wallets (user_id, balance_cents, frozen_cents, status, updated_at) VALUES ";

        for (size_t j = i; j < end_idx; ++j) {
            const auto& u = users[j];
            if (j > i) {
                u_sql += ", ";
                w_sql += ", ";
            }
            u_sql += std::format("({}, '{}', '{}', '{}', '{}', '{}', {}, {}, {})",
                                 u.user_id, sql_escape(u.phone), sql_escape(u.password_hash),
                                 sql_escape(u.nickname), sql_escape(u.avatar_url),
                                 sql_escape(u.role), u.status, u.created_at, u.updated_at);

            w_sql += std::format("({}, {}, {}, {}, {})",
                                 u.user_id, u.balance_cents, u.frozen_cents, u.status, u.updated_at);
        }
        u_sql += " ON CONFLICT (user_id) DO NOTHING;";
        w_sql += " ON CONFLICT (user_id) DO NOTHING;";

        (void)DbPool::instance().with_transaction([&](DbConnection& tx_conn) -> Result<void> {
            tx_conn.exec(u_sql.c_str());
            tx_conn.exec(w_sql.c_str());
            return {};
        });
    }

    // 修复自增序列
    (void)DbPool::instance().with_transaction([&](DbConnection& tx_conn) -> Result<void> {
        tx_conn.exec("SELECT setval('users_user_id_seq', (SELECT COALESCE(MAX(user_id), 1) FROM users));");
        return {};
    });
    std::cout << "  [OK] Successfully imported " << users.size() << " users and wallets.\n";

    // 3. 读取并导入充电桩 (piles)
    std::string p_path = resolve_data_path(data_dir, "seed_piles.json");
    std::cout << "  -> Loading piles from: " << p_path << "\n";
    std::string p_buf = read_file_content(p_path);
    if (p_buf.empty()) {
        std::cerr << "[Seed Error] Could not read piles JSON file: " << p_path << "\n";
        return false;
    }

    std::vector<JsonPile> piles;
    auto ec_p = glz::read_json(piles, p_buf);
    if (ec_p) {
        std::cerr << "[Seed Error] Glaze failed to parse piles JSON: "
                  << glz::format_error(ec_p, p_buf) << "\n";
        return false;
    }
    std::cout << "     Parsed " << piles.size() << " piles. Bulk inserting...\n";

    constexpr int PILE_BATCH = 1000;
    for (size_t i = 0; i < piles.size(); i += PILE_BATCH) {
        size_t end_idx = std::min(i + PILE_BATCH, piles.size());
        std::string p_sql = "INSERT INTO piles (pile_id, station_id, pile_name, type, gun_type, max_power_kw, voltage_range, total_charge_count, total_charge_hours, last_heartbeat_at, created_at, updated_at) VALUES ";

        for (size_t j = i; j < end_idx; ++j) {
            const auto& p = piles[j];
            if (j > i) p_sql += ", ";
            p_sql += std::format("('{}', {}, '{}', '{}', '{}', {:.1f}, '{}', {}, {:.1f}, {}, {}, {})",
                                 sql_escape(p.pile_id), p.station_id, sql_escape(p.pile_name),
                                 sql_escape(p.type), sql_escape(p.gun_type), p.max_power_kw,
                                 sql_escape(p.voltage_range),
                                 p.total_charge_count, p.total_charge_hours, p.last_heartbeat_at,
                                 p.created_at, p.updated_at);
        }
        p_sql += " ON CONFLICT (pile_id) DO NOTHING;";

        (void)DbPool::instance().with_transaction([&](DbConnection& tx_conn) -> Result<void> {
            tx_conn.exec(p_sql.c_str());
            return {};
        });
    }
    std::cout << "  [OK] Successfully imported " << piles.size() << " piles.\n";

    // 4. 纯 C++ 动态生成过去 30 天的真实充电订单 (不再依赖 seed_orders.json，自动关联真实电站与电桩)
    std::cout << "  -> Dynamically generating 30-day historical orders using C++ generator...\n";
    int64_t cst_offset = 8 * 3600 * 1000LL;
    int64_t current_day = (now + cst_offset) / 86400000LL;

    std::unordered_map<int32_t, std::vector<const JsonPile*>> station_piles_map;
    station_piles_map.reserve(stations.size());
    for (const auto& p : piles) {
        station_piles_map[p.station_id].push_back(&p);
    }

    constexpr int ORDER_BATCH = 2000;
    std::string o_sql;
    o_sql.reserve(1024 * 512);
    int batch_count = 0;
    size_t total_generated_orders = 0;

    auto flush_batch = [&]() {
        if (batch_count == 0) return;
        o_sql += " ON CONFLICT (order_id) DO NOTHING;";
        (void)DbPool::instance().with_transaction([&](DbConnection& tx_conn) -> Result<void> {
            tx_conn.exec(o_sql.c_str());
            return {};
        });
        o_sql.clear();
        batch_count = 0;
    };

    for (int d = 29; d >= 0; --d) {
        int64_t day_idx = current_day - d;
        int64_t day_start_ms = day_idx * 86400000LL - cst_offset;

        for (const auto& s : stations) {
            if (!s.is_online) continue; // 下线/暂停营业电站不生成订单

            auto it = station_piles_map.find(s.station_id);
            if (it == station_piles_map.end() || it->second.empty()) continue;
            const auto& p_list = it->second;

            double elec_price = 1.15 + static_cast<double>((static_cast<uint64_t>(s.station_id) * 104729ULL + 12345ULL) % 71) * 0.01;
            double serv_price = 0.35;

            uint64_t seed = (static_cast<uint64_t>(s.station_id) * 314159ULL) ^ (static_cast<uint64_t>(day_idx) * 271828ULL);
            int station_tier = static_cast<int>((static_cast<uint64_t>(s.station_id) * 1337ULL) % 100);
            int orders_today = 0;
            if (station_tier < 15) {
                // 繁忙站点: 每日 1 ~ 4 单
                orders_today = 1 + static_cast<int>(seed % 4);
            } else if (station_tier < 70) {
                // 普通站点: 每日 0 ~ 2 单
                orders_today = (seed % 3 == 0) ? 0 : (1 + static_cast<int>(seed % 2));
            } else {
                // 较冷门站点: 隔日 0 ~ 1 单
                orders_today = (seed % 2 == 0) ? 0 : 1;
            }

            for (int k = 0; k < orders_today; ++k) {
                uint64_t ord_seed = seed ^ (static_cast<uint64_t>(k) * 65537ULL);
                const auto* selected_pile = p_list[ord_seed % p_list.size()];
                bool is_fast = (selected_pile->type == "FAST");

                int hour = (ord_seed % 100 < 75) ? (8 + static_cast<int>(ord_seed % 14)) : static_cast<int>(ord_seed % 24);
                int minute = static_cast<int>((ord_seed / 24) % 60);
                int second = static_cast<int>((ord_seed / 1440) % 60);
                int64_t start_time = day_start_ms + (hour * 3600LL + minute * 60LL + second) * 1000LL;
                if (start_time > now) start_time = now - 1800000LL;

                int dur_mins = is_fast ? (30 + static_cast<int>(ord_seed % 45)) : (120 + static_cast<int>(ord_seed % 240));
                int64_t end_time = start_time + dur_mins * 60 * 1000LL;
                if (end_time > now) end_time = now;

                int start_soc = 15 + static_cast<int>(ord_seed % 25);
                int end_soc = 85 + static_cast<int>(ord_seed % 15);
                if (end_soc > 100) end_soc = 100;

                double battery_cap = 50.0 + static_cast<double>(ord_seed % 35);
                double charged_energy = std::round(battery_cap * (end_soc - start_soc) / 100.0 * 100.0) / 100.0;
                int64_t elec_cents = static_cast<int64_t>(charged_energy * elec_price * 100.0);
                int64_t serv_cents = static_cast<int64_t>(charged_energy * serv_price * 100.0);
                int overtime_mins = (ord_seed % 100 < 12) ? (15 + static_cast<int>(ord_seed % 4) * 15) : 0;
                int64_t overtime_cents = (overtime_mins / 15) * 500LL;
                int64_t total_cents = elec_cents + serv_cents + overtime_cents;

                int64_t uid = 1 + static_cast<int64_t>(ord_seed % (users.empty() ? 20000 : users.size()));
                std::string st_status = (ord_seed % 100 < 97) ? "COMPLETED" : "UNSETTLED";
                std::string ord_id = std::format("ORD_{}_{:05d}_{:02d}", start_time, s.station_id, k);
                std::string stop_rsn = (ord_seed % 2 == 0) ? "USER_MANUAL_STOP" : "TARGET_SOC_REACHED";
                int64_t settled_at = (st_status == "COMPLETED") ? end_time : 0;

                if (batch_count == 0) {
                    o_sql = "INSERT INTO charging_orders (order_id, user_id, station_id, pile_id, strategy_type, strategy_value, order_status, start_time, end_time, start_soc, end_soc, charged_energy_kwh, electricity_price, electricity_fee_cents, service_price, service_fee_cents, overtime_grace_minutes, overtime_duration_minutes, overtime_rate_per_15min, overtime_fee_cents, total_fee_cents, stop_reason, settled_at, created_at, updated_at) VALUES ";
                } else {
                    o_sql += ", ";
                }

                o_sql += std::format("('{}', {}, {}, '{}', 'FULL', 0.0, '{}', {}, {}, {}, {}, {:.2f}, {:.2f}, {}, {:.2f}, {}, 15, {}, 5.00, {}, {}, '{}', {}, {}, {})",
                                     sql_escape(ord_id), uid, s.station_id, sql_escape(selected_pile->pile_id),
                                     st_status, start_time, end_time, start_soc, end_soc, charged_energy,
                                     elec_price, elec_cents, serv_price, serv_cents,
                                     overtime_mins, overtime_cents, total_cents,
                                     stop_rsn, settled_at, start_time, end_time);

                batch_count++;
                total_generated_orders++;

                if (batch_count >= ORDER_BATCH) {
                    flush_batch();
                }
            }
        }
    }
    flush_batch();
    std::cout << "  [OK] Successfully generated and imported " << total_generated_orders << " orders for the last 30 days.\n";

    // 5. 初始化全盘指标平台基底 (让全盘统计数值远大于当月数据，且记录最后模拟日)
    int64_t hist_baseline_cents = 8865000000LL; // 8865 万元历史营收基底
    (void)DbPool::instance().with_transaction([&](DbConnection& tx_conn) -> Result<void> {
        tx_conn.exec(std::format(
            "INSERT INTO platform_metrics (metric_key, metric_val, updated_at) VALUES "
            "('historical_revenue_cents', {}, {}), "
            "('last_simulated_day', {}, {}) "
            "ON CONFLICT (metric_key) DO UPDATE SET metric_val = EXCLUDED.metric_val, updated_at = EXCLUDED.updated_at;",
            hist_baseline_cents, now, current_day, now
        ).c_str());
        return {};
    });
    std::cout << "  [OK] Successfully initialized platform metrics baseline (historical revenue: "
              << (hist_baseline_cents / 100.0) << " yuan, last_simulated_day: " << current_day << ").\n";

    auto elapsed_ms = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - t_start).count();
    std::cout << "[Seed] >>> Successfully finished importing dataset in " << elapsed_ms << " ms!\n"
              << "       - Stations: " << stations.size() << "\n"
              << "       - Users:    " << users.size() << "\n"
              << "       - Piles:    " << piles.size() << "\n"
              << "       - Orders:   " << total_generated_orders << "\n";

    return true;
}

bool SeedDataGenerator::populate_if_empty(const std::string& data_dir) {
    // 确保数据表结构完整
    if (!SchemaMigrator::ensure_schema()) {
        std::cerr << "[Seed Error] Failed to verify or migrate database schema\n";
        return false;
    }

    auto conn = DbPool::instance().acquire();
    if (!conn) {
        std::cerr << "[Seed Error] Cannot acquire connection from DbPool\n";
        return false;
    }

    PgResultGuard st_chk(conn->exec("SELECT COUNT(*) FROM stations;"));
    PgResultGuard u_chk(conn->exec("SELECT COUNT(*) FROM users;"));

    bool has_stations = (st_chk.is_ok() && st_chk.rows() > 0 && std::stoll(st_chk.value(0, 0)) >= 8000);
    bool has_users = (u_chk.is_ok() && u_chk.rows() > 0 && std::stoll(u_chk.value(0, 0)) >= 20000);

    if (has_stations && has_users) {
        std::cout << "[Seed] Large-scale dataset already exists in database. Skipping seed.\n";
        return true;
    }

    std::cout << "[Seed] Database missing initial data. Importing from JSON...\n";
    return import_from_json(data_dir);
}

} // namespace ev
