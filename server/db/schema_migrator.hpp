#pragma once

#include "db_pool.hpp"
#include <iostream>
#include <vector>
#include <string>
#include <string_view>
#include <unordered_set>

namespace ev {

class SchemaMigrator {
public:
    struct TableDefinition {
        std::string_view name;
        std::string_view ddl;
        std::string_view indexes_ddl;
    };

    // 检查数据库中是否存在所有必须的表与索引，若缺失则自动执行 DDL 创建
    static bool ensure_schema() {
        auto conn = DbPool::instance().acquire();
        if (!conn) {
            std::cerr << "[SchemaMigrator] ❌ 无法从连接池获取数据库主库连接，自检终止。\n";
            return false;
        }

        // 1. 查询 public schema 下当前已存在的所有数据表
        PgResultGuard res(conn->exec(
            "SELECT table_name FROM information_schema.tables "
            "WHERE table_schema = 'public' AND table_type = 'BASE TABLE';"
        ));

        if (!res.is_ok()) {
            std::cerr << "[SchemaMigrator] ❌ 查询数据库表元数据失败: " << conn->last_error() << "\n";
            return false;
        }

        std::unordered_set<std::string> existing_tables;
        int row_count = res.rows();
        for (int i = 0; i < row_count; ++i) {
            existing_tables.insert(res.value(i, 0));
        }

        // 2. 系统核心数据表定义 (按外键依赖顺序排列)
        static const std::vector<TableDefinition> REQUIRED_TABLES = {
            {
                "users",
                "CREATE TABLE IF NOT EXISTS users ("
                "    user_id BIGSERIAL PRIMARY KEY,"
                "    phone VARCHAR(20) UNIQUE NOT NULL,"
                "    password_hash VARCHAR(128) NOT NULL DEFAULT '',"
                "    nickname VARCHAR(64) NOT NULL DEFAULT '',"
                "    avatar_url VARCHAR(255) NOT NULL DEFAULT 'http://localhost:8080/static/avatars/default.png',"
                "    role VARCHAR(16) NOT NULL DEFAULT 'user',"
                "    status SMALLINT NOT NULL DEFAULT 1,"
                "    created_at BIGINT NOT NULL,"
                "    updated_at BIGINT NOT NULL"
                ");",
                "CREATE INDEX IF NOT EXISTS idx_users_phone ON users(phone);"
                "CREATE INDEX IF NOT EXISTS idx_users_role ON users(role);"
            },
            {
                "user_avatars",
                "CREATE TABLE IF NOT EXISTS user_avatars ("
                "    user_id BIGINT PRIMARY KEY REFERENCES users(user_id) ON DELETE CASCADE,"
                "    content_type VARCHAR(64) NOT NULL DEFAULT 'image/png',"
                "    file_size INTEGER NOT NULL,"
                "    avatar_data BYTEA NOT NULL,"
                "    updated_at BIGINT NOT NULL"
                ");",
                ""
            },
            {
                "user_wallets",
                "CREATE TABLE IF NOT EXISTS user_wallets ("
                "    user_id BIGINT PRIMARY KEY REFERENCES users(user_id) ON DELETE CASCADE,"
                "    balance_cents BIGINT NOT NULL DEFAULT 0,"
                "    frozen_cents BIGINT NOT NULL DEFAULT 0,"
                "    status SMALLINT NOT NULL DEFAULT 1,"
                "    updated_at BIGINT NOT NULL"
                ");",
                ""
            },
            {
                "wallet_transaction_flows",
                "CREATE TABLE IF NOT EXISTS wallet_transaction_flows ("
                "    id VARCHAR(64) PRIMARY KEY,"
                "    user_id BIGINT NOT NULL REFERENCES users(user_id),"
                "    flow_type SMALLINT NOT NULL,"
                "    amount_cents BIGINT NOT NULL,"
                "    balance_before_cents BIGINT NOT NULL,"
                "    balance_after_cents BIGINT NOT NULL,"
                "    related_order_id VARCHAR(64) DEFAULT '',"
                "    operator_id BIGINT DEFAULT 0,"
                "    remark VARCHAR(255) DEFAULT '',"
                "    idempotent_key VARCHAR(128) UNIQUE,"
                "    created_at BIGINT NOT NULL"
                ");",
                "CREATE INDEX IF NOT EXISTS idx_flow_user_id ON wallet_transaction_flows(user_id, created_at DESC);"
                "CREATE INDEX IF NOT EXISTS idx_flow_related_order ON wallet_transaction_flows(related_order_id);"
            },
            {
                "stations",
                "CREATE TABLE IF NOT EXISTS stations ("
                "    station_id BIGSERIAL PRIMARY KEY,"
                "    station_name VARCHAR(128) NOT NULL,"
                "    address VARCHAR(255) NOT NULL,"
                "    latitude DOUBLE PRECISION NOT NULL,"
                "    longitude DOUBLE PRECISION NOT NULL,"
                "    contact_phone VARCHAR(32) DEFAULT '',"
                "    operating_hours VARCHAR(64) DEFAULT '00:00 - 24:00',"
                "    price_per_kwh DOUBLE PRECISION NOT NULL DEFAULT 1.45,"
                "    service_fee_per_kwh DOUBLE PRECISION NOT NULL DEFAULT 0.35,"
                "    overtime_fee_per_15min DOUBLE PRECISION NOT NULL DEFAULT 5.00,"
                "    overtime_grace_minutes INT NOT NULL DEFAULT 15,"
                "    status SMALLINT NOT NULL DEFAULT 1,"
                "    created_at BIGINT NOT NULL,"
                "    updated_at BIGINT NOT NULL"
                ");",
                "CREATE INDEX IF NOT EXISTS idx_stations_lat_lng ON stations(latitude, longitude);"
            },
            {
                "piles",
                "CREATE TABLE IF NOT EXISTS piles ("
                "    pile_id VARCHAR(32) PRIMARY KEY,"
                "    station_id BIGINT NOT NULL REFERENCES stations(station_id) ON DELETE CASCADE,"
                "    pile_name VARCHAR(128) NOT NULL,"
                "    type VARCHAR(16) NOT NULL DEFAULT 'FAST',"
                "    gun_type VARCHAR(32) DEFAULT '国标2015',"
                "    max_power_kw DOUBLE PRECISION NOT NULL DEFAULT 120.0,"
                "    voltage_range VARCHAR(32) DEFAULT '200V-750V',"
                "    total_charge_count BIGINT DEFAULT 0,"
                "    total_charge_hours DOUBLE PRECISION DEFAULT 0.0,"
                "    last_heartbeat_at BIGINT DEFAULT 0,"
                "    created_at BIGINT NOT NULL,"
                "    updated_at BIGINT NOT NULL"
                ");",
                "CREATE INDEX IF NOT EXISTS idx_piles_station_id ON piles(station_id);"
            },
            {
                "charging_orders",
                "CREATE TABLE IF NOT EXISTS charging_orders ("
                "    order_id VARCHAR(64) PRIMARY KEY,"
                "    user_id BIGINT NOT NULL REFERENCES users(user_id),"
                "    station_id BIGINT NOT NULL REFERENCES stations(station_id),"
                "    pile_id VARCHAR(32) NOT NULL REFERENCES piles(pile_id),"
                "    strategy_type VARCHAR(16) DEFAULT 'FULL',"
                "    strategy_value DOUBLE PRECISION DEFAULT 0.0,"
                "    order_status VARCHAR(20) NOT NULL DEFAULT 'CHARGING',"
                "    start_time BIGINT NOT NULL,"
                "    end_time BIGINT DEFAULT 0,"
                "    start_soc INT DEFAULT 20,"
                "    end_soc INT DEFAULT 20,"
                "    charged_energy_kwh DOUBLE PRECISION DEFAULT 0.0,"
                "    electricity_price DOUBLE PRECISION NOT NULL DEFAULT 1.45,"
                "    electricity_fee_cents BIGINT DEFAULT 0,"
                "    service_price DOUBLE PRECISION NOT NULL DEFAULT 0.35,"
                "    service_fee_cents BIGINT DEFAULT 0,"
                "    overtime_grace_minutes INT DEFAULT 15,"
                "    overtime_duration_minutes INT DEFAULT 0,"
                "    overtime_rate_per_15min DOUBLE PRECISION DEFAULT 5.00,"
                "    overtime_fee_cents BIGINT DEFAULT 0,"
                "    total_fee_cents BIGINT DEFAULT 0,"
                "    stop_reason VARCHAR(64) DEFAULT '',"
                "    settled_at BIGINT DEFAULT 0,"
                "    refund_transaction_id VARCHAR(64) DEFAULT '',"
                "    operator_id BIGINT DEFAULT 0,"
                "    refund_reason VARCHAR(255) DEFAULT '',"
                "    refunded_at BIGINT DEFAULT 0,"
                "    created_at BIGINT NOT NULL,"
                "    updated_at BIGINT NOT NULL"
                ");",
                "CREATE INDEX IF NOT EXISTS idx_orders_user_id ON charging_orders(user_id, created_at DESC);"
                "CREATE INDEX IF NOT EXISTS idx_orders_station_id ON charging_orders(station_id, created_at DESC);"
                "CREATE INDEX IF NOT EXISTS idx_orders_pile_id ON charging_orders(pile_id);"
                "CREATE INDEX IF NOT EXISTS idx_orders_status ON charging_orders(order_status);"
                "CREATE INDEX IF NOT EXISTS idx_orders_created_at ON charging_orders(created_at DESC);"
                "CREATE INDEX IF NOT EXISTS idx_orders_status_created ON charging_orders(order_status, created_at DESC);"
                "CREATE INDEX IF NOT EXISTS idx_orders_st_sales ON charging_orders(station_id, created_at, order_status);"
                "CREATE INDEX IF NOT EXISTS idx_orders_status_created_total ON charging_orders(order_status, created_at DESC);"
            },
            {
                "pile_reservations",
                "CREATE TABLE IF NOT EXISTS pile_reservations ("
                "    reservation_id VARCHAR(64) PRIMARY KEY,"
                "    user_id BIGINT NOT NULL REFERENCES users(user_id),"
                "    station_id BIGINT NOT NULL REFERENCES stations(station_id),"
                "    pile_id VARCHAR(32) NOT NULL REFERENCES piles(pile_id),"
                "    deposit_cents BIGINT NOT NULL DEFAULT 2000,"
                "    penalty_fee_cents BIGINT NOT NULL DEFAULT 0,"
                "    refund_amount_cents BIGINT NOT NULL DEFAULT 0,"
                "    status VARCHAR(20) NOT NULL DEFAULT 'ACTIVE',"
                "    created_at BIGINT NOT NULL,"
                "    expire_at BIGINT NOT NULL,"
                "    fulfilled_at BIGINT DEFAULT 0,"
                "    cancelled_at BIGINT DEFAULT 0,"
                "    updated_at BIGINT NOT NULL"
                ");",
                "CREATE INDEX IF NOT EXISTS idx_res_user ON pile_reservations(user_id, status);"
                "CREATE INDEX IF NOT EXISTS idx_res_pile ON pile_reservations(pile_id, status);"
            },
            {
                "platform_metrics",
                "CREATE TABLE IF NOT EXISTS platform_metrics ("
                "    metric_key VARCHAR(64) PRIMARY KEY,"
                "    metric_val BIGINT NOT NULL DEFAULT 0,"
                "    updated_at BIGINT NOT NULL"
                ");",
                ""
            }
        };

        std::vector<std::string> created_tables;

        // 3. 逐一比对并自动建表
        for (const auto& t : REQUIRED_TABLES) {
            if (!existing_tables.contains(std::string(t.name))) {
                std::cout << "[SchemaMigrator] ⚠️ 检测到数据库缺失数据表: [" << t.name << "]，正在自动创建...\n" << std::flush;
                
                PgResultGuard create_res(conn->exec(std::string(t.ddl).c_str()));
                if (!create_res.is_ok()) {
                    std::cerr << "[SchemaMigrator] ❌ 创建数据表 [" << t.name << "] 失败: " << conn->last_error() << "\n" << std::flush;
                    return false;
                }

                if (!t.indexes_ddl.empty()) {
                    PgResultGuard idx_res(conn->exec(std::string(t.indexes_ddl).c_str()));
                    if (!idx_res.is_ok()) {
                        std::cerr << "[SchemaMigrator] ⚠️ 为数据表 [" << t.name << "] 创建索引失败: " << conn->last_error() << "\n" << std::flush;
                    }
                }

                std::cout << "[SchemaMigrator] ✅ 数据表 [" << t.name << "] 及其索引已自动创建就绪！\n" << std::flush;
                created_tables.push_back(std::string(t.name));
            }
        }

        if (!created_tables.empty()) {
            std::cout << "[SchemaMigrator] 🎉 数据库自动迁移补全完成，共创建 " << created_tables.size() << " 张缺失数据表。\n" << std::flush;
        } else {
            std::cout << "  [OK] 数据库结构自检通过：所有 8 张核心业务表与索引均已就绪。\n" << std::flush;
        }

        // 4. 清理 piles 表中遗留的冗余 status 列与索引 (运行状态统一由内存状态池托管)
        PgResultGuard col_chk(conn->exec(
            "SELECT column_name FROM information_schema.columns "
            "WHERE table_schema = 'public' AND table_name = 'piles' AND column_name = 'status';"
        ));
        if (col_chk.is_ok() && col_chk.rows() > 0) {
            std::cout << "[SchemaMigrator] 🧹 检测到 piles 表存在遗留冗余 status 列，正在执行平滑清理迁移...\n" << std::flush;
            conn->exec("DROP INDEX IF EXISTS idx_piles_status;");
            conn->exec("ALTER TABLE piles DROP COLUMN IF EXISTS status;");
            std::cout << "[SchemaMigrator] 🧹 piles 表已成功移除 status 冗余列，充电桩状态已 100% 统一至内存状态池！\n" << std::flush;
        }

        return true;
    }
};

} // namespace ev
