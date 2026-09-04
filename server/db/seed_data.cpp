#include "seed_data.hpp"
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

// Glaze 反序列化结构体定义
struct JsonStation {
    int32_t station_id{};
    uint8_t district_code{};
    double latitude{};
    double longitude{};
    std::string name{};
    std::string address{};
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

struct JsonOrder {
    std::string order_id{};
    int64_t user_id{};
    int32_t station_id{};
    std::string pile_id{};
    std::string strategy_type{};
    double strategy_value{};
    std::string order_status{};
    int64_t start_time{};
    int64_t end_time{};
    int32_t start_soc{};
    int32_t end_soc{};
    double charged_energy_kwh{};
    double electricity_price{};
    int64_t electricity_fee_cents{};
    double service_price{};
    int64_t service_fee_cents{};
    int32_t overtime_grace_minutes{};
    int32_t overtime_duration_minutes{};
    double overtime_rate_per_15min{};
    int64_t overtime_fee_cents{};
    int64_t total_fee_cents{};
    std::string stop_reason{};
    int64_t settled_at{};
    int64_t created_at{};
    int64_t updated_at{};
};

} // namespace

bool SeedDataGenerator::clear_database() {
    auto conn = DbPool::instance().acquire();
    if (!conn) {
        std::cerr << "[Seed Error] Cannot acquire connection from DbPool to clear database\n";
        return false;
    }
    std::cout << "[Seed] Truncating all business tables and resetting sequences...\n";
    PgResultGuard res(conn->exec(
        "TRUNCATE TABLE charging_orders, wallet_transaction_flows, piles, user_wallets, stations, users "
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
            sql += std::format("({}, '{}', '{}', {:.6f}, {:.6f}, '010-88889999', '00:00 - 24:00', 1.45, 0.35, 5.00, 15, 1, {}, {})",
                               s.station_id, sql_escape(s.name), sql_escape(s.address), s.latitude, s.longitude, now - 864000000LL, now);
        }
        sql += " ON CONFLICT (station_id) DO NOTHING;";

        DbPool::instance().with_transaction([&](DbConnection& tx_conn) -> Result<void> {
            tx_conn.exec(sql.c_str());
            return {};
        });
    }

    // 修复自增序列
    DbPool::instance().with_transaction([&](DbConnection& tx_conn) -> Result<void> {
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

        DbPool::instance().with_transaction([&](DbConnection& tx_conn) -> Result<void> {
            tx_conn.exec(u_sql.c_str());
            tx_conn.exec(w_sql.c_str());
            return {};
        });
    }

    // 修复自增序列
    DbPool::instance().with_transaction([&](DbConnection& tx_conn) -> Result<void> {
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
        std::string p_sql = "INSERT INTO piles (pile_id, station_id, pile_name, type, gun_type, max_power_kw, voltage_range, status, total_charge_count, total_charge_hours, last_heartbeat_at, created_at, updated_at) VALUES ";

        for (size_t j = i; j < end_idx; ++j) {
            const auto& p = piles[j];
            if (j > i) p_sql += ", ";
            p_sql += std::format("('{}', {}, '{}', '{}', '{}', {:.1f}, '{}', '{}', {}, {:.1f}, {}, {}, {})",
                                 sql_escape(p.pile_id), p.station_id, sql_escape(p.pile_name),
                                 sql_escape(p.type), sql_escape(p.gun_type), p.max_power_kw,
                                 sql_escape(p.voltage_range), sql_escape(p.status),
                                 p.total_charge_count, p.total_charge_hours, p.last_heartbeat_at,
                                 p.created_at, p.updated_at);
        }
        p_sql += " ON CONFLICT (pile_id) DO NOTHING;";

        DbPool::instance().with_transaction([&](DbConnection& tx_conn) -> Result<void> {
            tx_conn.exec(p_sql.c_str());
            return {};
        });
    }
    std::cout << "  [OK] Successfully imported " << piles.size() << " piles.\n";

    // 4. 读取并导入历史订单 (charging_orders)
    std::string ord_path = resolve_data_path(data_dir, "seed_orders.json");
    std::cout << "  -> Loading orders from: " << ord_path << "\n";
    std::string ord_buf = read_file_content(ord_path);
    if (ord_buf.empty()) {
        std::cerr << "[Seed Error] Could not read orders JSON file: " << ord_path << "\n";
        return false;
    }

    std::vector<JsonOrder> orders;
    auto ec_ord = glz::read_json(orders, ord_buf);
    if (ec_ord) {
        std::cerr << "[Seed Error] Glaze failed to parse orders JSON: "
                  << glz::format_error(ec_ord, ord_buf) << "\n";
        return false;
    }
    std::cout << "     Parsed " << orders.size() << " orders. Bulk inserting...\n";

    constexpr int ORDER_BATCH = 2000;
    for (size_t i = 0; i < orders.size(); i += ORDER_BATCH) {
        size_t end_idx = std::min(i + ORDER_BATCH, orders.size());
        std::string o_sql = "INSERT INTO charging_orders (order_id, user_id, station_id, pile_id, strategy_type, strategy_value, order_status, start_time, end_time, start_soc, end_soc, charged_energy_kwh, electricity_price, electricity_fee_cents, service_price, service_fee_cents, overtime_grace_minutes, overtime_duration_minutes, overtime_rate_per_15min, overtime_fee_cents, total_fee_cents, stop_reason, settled_at, created_at, updated_at) VALUES ";

        for (size_t j = i; j < end_idx; ++j) {
            const auto& o = orders[j];
            if (j > i) o_sql += ", ";
            o_sql += std::format("('{}', {}, {}, '{}', '{}', {:.1f}, '{}', {}, {}, {}, {}, {:.2f}, {:.2f}, {}, {:.2f}, {}, {}, {}, {:.2f}, {}, {}, '{}', {}, {}, {})",
                                 sql_escape(o.order_id), o.user_id, o.station_id, sql_escape(o.pile_id),
                                 sql_escape(o.strategy_type), o.strategy_value, sql_escape(o.order_status),
                                 o.start_time, o.end_time, o.start_soc, o.end_soc, o.charged_energy_kwh,
                                 o.electricity_price, o.electricity_fee_cents, o.service_price,
                                 o.service_fee_cents, o.overtime_grace_minutes, o.overtime_duration_minutes,
                                 o.overtime_rate_per_15min, o.overtime_fee_cents, o.total_fee_cents,
                                 sql_escape(o.stop_reason), o.settled_at, o.created_at, o.updated_at);
        }
        o_sql += " ON CONFLICT (order_id) DO NOTHING;";

        DbPool::instance().with_transaction([&](DbConnection& tx_conn) -> Result<void> {
            tx_conn.exec(o_sql.c_str());
            return {};
        });
    }
    std::cout << "  [OK] Successfully imported " << orders.size() << " orders.\n";

    auto elapsed_ms = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - t_start).count();
    std::cout << "[Seed] >>> Successfully finished importing dataset in " << elapsed_ms << " ms!\n"
              << "       - Stations: " << stations.size() << "\n"
              << "       - Users:    " << users.size() << "\n"
              << "       - Piles:    " << piles.size() << "\n"
              << "       - Orders:   " << orders.size() << "\n";

    return true;
}

bool SeedDataGenerator::populate_if_empty(const std::string& data_dir) {
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
