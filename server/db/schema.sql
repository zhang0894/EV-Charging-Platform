-- ===================================================
-- 电动汽车充电桩管理平台 PostgreSQL 18 DDL Schema
-- ===================================================

-- 1. 用户表
CREATE TABLE IF NOT EXISTS users (
    user_id BIGSERIAL PRIMARY KEY,
    phone VARCHAR(20) UNIQUE NOT NULL,
    password_hash VARCHAR(128) NOT NULL DEFAULT '',
    nickname VARCHAR(64) NOT NULL DEFAULT '',
    avatar_url VARCHAR(255) NOT NULL DEFAULT 'http://localhost:8080/static/avatars/default.png',
    role VARCHAR(16) NOT NULL DEFAULT 'user', -- 'user' 或 'admin'
    status SMALLINT NOT NULL DEFAULT 1,       -- 1: 正常, 2: 冻结
    created_at BIGINT NOT NULL,
    updated_at BIGINT NOT NULL
);

CREATE INDEX IF NOT EXISTS idx_users_phone ON users(phone);
CREATE INDEX IF NOT EXISTS idx_users_role ON users(role);

-- 1.1 用户真实头像存储表 (二进制文件存储与快速定位)
CREATE TABLE IF NOT EXISTS user_avatars (
    user_id BIGINT PRIMARY KEY REFERENCES users(user_id) ON DELETE CASCADE,
    content_type VARCHAR(64) NOT NULL DEFAULT 'image/png',
    file_size INTEGER NOT NULL,
    avatar_data BYTEA NOT NULL,
    updated_at BIGINT NOT NULL
);

-- 2. 钱包账户表 (资金安全独立存储)
CREATE TABLE IF NOT EXISTS user_wallets (
    user_id BIGINT PRIMARY KEY REFERENCES users(user_id) ON DELETE CASCADE,
    balance_cents BIGINT NOT NULL DEFAULT 0,
    frozen_cents BIGINT NOT NULL DEFAULT 0,
    status SMALLINT NOT NULL DEFAULT 1, -- 1: 正常, 2: 冻结
    updated_at BIGINT NOT NULL
);

-- 3. 钱包流水变动表 (不可篡改 Append-Only 账本)
CREATE TABLE IF NOT EXISTS wallet_transaction_flows (
    id VARCHAR(64) PRIMARY KEY,
    user_id BIGINT NOT NULL REFERENCES users(user_id),
    flow_type SMALLINT NOT NULL, -- 1: 充值, 2: 充电扣费, 3: 充电退补, 4: 管理员调账
    amount_cents BIGINT NOT NULL,
    balance_before_cents BIGINT NOT NULL,
    balance_after_cents BIGINT NOT NULL,
    related_order_id VARCHAR(64) DEFAULT '',
    operator_id BIGINT DEFAULT 0,
    remark VARCHAR(255) DEFAULT '',
    idempotent_key VARCHAR(128) UNIQUE,
    created_at BIGINT NOT NULL
);

CREATE INDEX IF NOT EXISTS idx_flow_user_id ON wallet_transaction_flows(user_id, created_at DESC);
CREATE INDEX IF NOT EXISTS idx_flow_related_order ON wallet_transaction_flows(related_order_id);

-- 4. 充电站信息表
CREATE TABLE IF NOT EXISTS stations (
    station_id BIGSERIAL PRIMARY KEY,
    station_name VARCHAR(128) NOT NULL,
    address VARCHAR(255) NOT NULL,
    latitude DOUBLE PRECISION NOT NULL,
    longitude DOUBLE PRECISION NOT NULL,
    contact_phone VARCHAR(32) DEFAULT '',
    operating_hours VARCHAR(64) DEFAULT '00:00 - 24:00',
    price_per_kwh DOUBLE PRECISION NOT NULL DEFAULT 1.45,
    service_fee_per_kwh DOUBLE PRECISION NOT NULL DEFAULT 0.35,
    overtime_fee_per_15min DOUBLE PRECISION NOT NULL DEFAULT 5.00,
    overtime_grace_minutes INT NOT NULL DEFAULT 15,
    status SMALLINT NOT NULL DEFAULT 1, -- 1: 运营中, 2: 维护
    created_at BIGINT NOT NULL,
    updated_at BIGINT NOT NULL
);

CREATE INDEX IF NOT EXISTS idx_stations_lat_lng ON stations(latitude, longitude);

-- 5. 充电桩设备表
CREATE TABLE IF NOT EXISTS piles (
    pile_id VARCHAR(32) PRIMARY KEY,
    station_id BIGINT NOT NULL REFERENCES stations(station_id) ON DELETE CASCADE,
    pile_name VARCHAR(128) NOT NULL,
    type VARCHAR(16) NOT NULL DEFAULT 'FAST', -- 'FAST', 'SLOW'
    gun_type VARCHAR(32) DEFAULT '国标2015',
    max_power_kw DOUBLE PRECISION NOT NULL DEFAULT 120.0,
    voltage_range VARCHAR(32) DEFAULT '200V-750V',
    status VARCHAR(20) NOT NULL DEFAULT 'IDLE', -- 'IDLE', 'CHARGING', 'FAULT', 'MAINTENANCE', 'OFFLINE'
    total_charge_count BIGINT DEFAULT 0,
    total_charge_hours DOUBLE PRECISION DEFAULT 0.0,
    last_heartbeat_at BIGINT DEFAULT 0,
    created_at BIGINT NOT NULL,
    updated_at BIGINT NOT NULL
);

CREATE INDEX IF NOT EXISTS idx_piles_station_id ON piles(station_id);
CREATE INDEX IF NOT EXISTS idx_piles_status ON piles(status);

-- 6. 充电业务订单表
CREATE TABLE IF NOT EXISTS charging_orders (
    order_id VARCHAR(64) PRIMARY KEY,
    user_id BIGINT NOT NULL REFERENCES users(user_id),
    station_id BIGINT NOT NULL REFERENCES stations(station_id),
    pile_id VARCHAR(32) NOT NULL REFERENCES piles(pile_id),
    strategy_type VARCHAR(16) DEFAULT 'FULL',
    strategy_value DOUBLE PRECISION DEFAULT 0.0,
    order_status VARCHAR(20) NOT NULL DEFAULT 'CHARGING', -- 'CHARGING', 'UNSETTLED', 'COMPLETED', 'REFUNDED', 'CANCELLED'
    start_time BIGINT NOT NULL,
    end_time BIGINT DEFAULT 0,
    start_soc INT DEFAULT 20,
    end_soc INT DEFAULT 20,
    charged_energy_kwh DOUBLE PRECISION DEFAULT 0.0,
    electricity_price DOUBLE PRECISION NOT NULL DEFAULT 1.45,
    electricity_fee_cents BIGINT DEFAULT 0,
    service_price DOUBLE PRECISION NOT NULL DEFAULT 0.35,
    service_fee_cents BIGINT DEFAULT 0,
    overtime_grace_minutes INT DEFAULT 15,
    overtime_duration_minutes INT DEFAULT 0,
    overtime_rate_per_15min DOUBLE PRECISION DEFAULT 5.00,
    overtime_fee_cents BIGINT DEFAULT 0,
    total_fee_cents BIGINT DEFAULT 0,
    stop_reason VARCHAR(64) DEFAULT '',
    settled_at BIGINT DEFAULT 0,
    refund_transaction_id VARCHAR(64) DEFAULT '',
    operator_id BIGINT DEFAULT 0,
    refund_reason VARCHAR(255) DEFAULT '',
    refunded_at BIGINT DEFAULT 0,
    created_at BIGINT NOT NULL,
    updated_at BIGINT NOT NULL
);

CREATE INDEX IF NOT EXISTS idx_orders_user_id ON charging_orders(user_id, created_at DESC);
CREATE INDEX IF NOT EXISTS idx_orders_station_id ON charging_orders(station_id, created_at DESC);
CREATE INDEX IF NOT EXISTS idx_orders_pile_id ON charging_orders(pile_id);
CREATE INDEX IF NOT EXISTS idx_orders_status ON charging_orders(order_status);

-- 7. 充电桩预约表
CREATE TABLE IF NOT EXISTS pile_reservations (
    reservation_id VARCHAR(64) PRIMARY KEY,
    user_id BIGINT NOT NULL REFERENCES users(user_id),
    station_id BIGINT NOT NULL REFERENCES stations(station_id),
    pile_id VARCHAR(32) NOT NULL REFERENCES piles(pile_id),
    deposit_cents BIGINT NOT NULL DEFAULT 2000,
    penalty_fee_cents BIGINT NOT NULL DEFAULT 0,
    refund_amount_cents BIGINT NOT NULL DEFAULT 0,
    status VARCHAR(20) NOT NULL DEFAULT 'ACTIVE', -- 'ACTIVE', 'FULFILLED', 'CANCELLED', 'TIMEOUT'
    created_at BIGINT NOT NULL,
    expire_at BIGINT NOT NULL,
    fulfilled_at BIGINT DEFAULT 0,
    cancelled_at BIGINT DEFAULT 0,
    updated_at BIGINT NOT NULL
);

CREATE INDEX IF NOT EXISTS idx_res_user ON pile_reservations(user_id, status);
CREATE INDEX IF NOT EXISTS idx_res_pile ON pile_reservations(pile_id, status);

