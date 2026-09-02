-- ============================================================
-- 东软电动汽车充电桩应用管理平台 —— 数据库设计
-- 子系统（3）数据库端
--
-- 依据：01.项目说明书-东软电动汽车充电桩应用管理平台.doc
-- 数据库：SQLite 3（说明书 1.6 关键技术：QSQLlite）
-- 编码：UTF-8
--
-- ⚠️ 表结构是全组的「契约」，只能由数据库负责人修改，改动必须通知全组
-- ============================================================

PRAGMA foreign_keys = ON;   -- SQLite 默认不检查外键，必须每次连接都打开

-- ------------------------------------------------------------
-- 1. 管理员表  admins
--    对应功能：PC服务器端 ▲管理员登录（默认 admin / 123456）
-- ------------------------------------------------------------
DROP TABLE IF EXISTS admins;
CREATE TABLE admins (
    id          INTEGER PRIMARY KEY AUTOINCREMENT,
    username    TEXT    NOT NULL UNIQUE,              -- 登录账号
    pwd_hash    TEXT    NOT NULL,                     -- 密码的 SHA-256，禁止存明文
    real_name   TEXT,                                 -- 姓名，用于界面右上角显示
    role        TEXT    NOT NULL DEFAULT 'admin',     -- admin=超级管理员 staff=普通运维
    last_login  TEXT,                                 -- 最后登录时间
    created_at  TEXT    NOT NULL DEFAULT (datetime('now','localtime'))
);

-- ------------------------------------------------------------
-- 2. 用户表  users
--    对应功能：用户端 ▲用户信息维护（手机号免密登录 / 头像 / 昵称 / 余额）
--             服务端 ▲用户管理（冻结解冻 / 手机号模糊搜索）
-- ------------------------------------------------------------
DROP TABLE IF EXISTS users;
CREATE TABLE users (
    id          INTEGER PRIMARY KEY AUTOINCREMENT,
    phone       TEXT    NOT NULL UNIQUE
                        CHECK (length(phone) = 11),   -- 说明书要求 11 位手机号
    nickname    TEXT    NOT NULL,                     -- 默认「用户」+手机号后4位
    avatar      TEXT,                                 -- 头像文件路径；NULL=默认灰色头像
    balance     REAL    NOT NULL DEFAULT 0
                        CHECK (balance >= 0),         -- 钱包余额（元），不允许为负
    status      INTEGER NOT NULL DEFAULT 0,           -- 0=正常  1=冻结（风控）
    created_at  TEXT    NOT NULL DEFAULT (datetime('now','localtime'))  -- 注册时间
);

-- ------------------------------------------------------------
-- 3. 充电站表  stations
--    对应功能：用户端 ▲附近充电站查询（区域下拉 / 距离排序 / 站卡片）
--             服务端 ▲充电站管理（ID/站名/地址/经纬度/总桩数/在线率/新增）
-- ------------------------------------------------------------
DROP TABLE IF EXISTS stations;
CREATE TABLE stations (
    id          INTEGER PRIMARY KEY AUTOINCREMENT,
    name        TEXT    NOT NULL,                     -- 站名
    region      TEXT    NOT NULL,                     -- 区域，用户端「下拉选择区域」的数据来源
    address     TEXT    NOT NULL,                     -- 详细地址
    lat         REAL    NOT NULL,                     -- 纬度（腾讯地图 API 转换得到）
    lng         REAL    NOT NULL,                     -- 经度
    price       REAL    NOT NULL DEFAULT 1.20
                        CHECK (price > 0),            -- 充电价格（元/度），站卡片要显示
    status      INTEGER NOT NULL DEFAULT 0,           -- 0=营业  1=停业
    created_at  TEXT    NOT NULL DEFAULT (datetime('now','localtime'))
);
-- 注意：「总电桩数」和「在线率」不存字段，由 piles 表实时算出来（见 v_station_overview）
--       存了就会和真实情况对不上，这是数据库设计的基本原则：不存冗余的可推导数据

-- ------------------------------------------------------------
-- 4. 充电桩表  piles
--    对应功能：服务端 ▲充电桩管理（编号/所属站/类型/功率/状态/累计次数/累计时长/远程重启）
--             服务端 ▲电桩状态（在用·闲置·故障 数量与占比）
--             用户端  点击充电站查看电桩明细
-- ------------------------------------------------------------
DROP TABLE IF EXISTS piles;
CREATE TABLE piles (
    id              INTEGER PRIMARY KEY AUTOINCREMENT,
    station_id      INTEGER NOT NULL REFERENCES stations(id) ON DELETE CASCADE,
    code            TEXT    NOT NULL UNIQUE,          -- 电桩编号，如 CD-001-01
    type            INTEGER NOT NULL DEFAULT 0,       -- 0=快充  1=慢充
    power_kw        REAL    NOT NULL CHECK (power_kw > 0),   -- 功率 kW
    status          INTEGER NOT NULL DEFAULT 0,       -- 0=闲置  1=在用  2=故障
    total_sessions  INTEGER NOT NULL DEFAULT 0,       -- 累计充电次数
    total_minutes   INTEGER NOT NULL DEFAULT 0,       -- 累计充电时长（分钟）
    last_restart_at TEXT,                             -- 最后一次「远程重启」时间
    created_at      TEXT    NOT NULL DEFAULT (datetime('now','localtime'))
);

-- ------------------------------------------------------------
-- 5. 充电订单表  orders
--    对应功能：用户端 ▲电动汽车充电（预约—充电—计费—结算 全流程）
--             用户端  充电前检查「是否存在未完成订单」
--             服务端 ▲销售业绩（营收趋势 / 今日·本月·总营收）
--             子系统5 机器学习充电负荷预测的训练数据来源
-- ------------------------------------------------------------
DROP TABLE IF EXISTS orders;
CREATE TABLE orders (
    id           INTEGER PRIMARY KEY AUTOINCREMENT,
    user_id      INTEGER NOT NULL REFERENCES users(id),
    pile_id      INTEGER NOT NULL REFERENCES piles(id),
    start_time   TEXT    NOT NULL,                    -- 开始充电时间
    end_time     TEXT,                                -- 结束时间；NULL=还在充
    duration_min INTEGER NOT NULL DEFAULT 0,          -- 本次充电时长（分钟）
    kwh          REAL    NOT NULL DEFAULT 0,          -- 本次充电量（度）
    unit_price   REAL    NOT NULL,                    -- 下单那一刻的电价快照
    amount       REAL    NOT NULL DEFAULT 0,          -- 金额 = kwh × unit_price
    status       INTEGER NOT NULL DEFAULT 0,          -- 0=充电中 1=待结算 2=已结算
    settled_at   TEXT                                 -- 结算时间
);
-- 为什么要存 unit_price（电价快照）？
--   电站以后调价了，历史订单的金额必须保持不变。
--   如果算钱时才去 join stations.price，涨价后所有旧订单金额都会跟着变 —— 这是经典的设计错误。

-- ------------------------------------------------------------
-- 6. 充值记录表  recharges  【扩展表，说明书未强制要求】
--    对应功能：用户端 ▲用户信息维护 → 余额充值
--    加分点：有流水才能对账，也是风控（冻结账号）的判断依据
-- ------------------------------------------------------------
DROP TABLE IF EXISTS recharges;
CREATE TABLE recharges (
    id         INTEGER PRIMARY KEY AUTOINCREMENT,
    user_id    INTEGER NOT NULL REFERENCES users(id),
    amount     REAL    NOT NULL CHECK (amount > 0),   -- 充值金额（元）
    method     TEXT    NOT NULL DEFAULT '模拟支付',
    created_at TEXT    NOT NULL DEFAULT (datetime('now','localtime'))
);

-- ============================================================
-- 索引：让查询快起来
-- 原则：经常出现在 WHERE / JOIN / ORDER BY 里的列才建索引
-- ============================================================
CREATE INDEX idx_users_phone      ON users(phone);          -- 手机号登录、模糊搜索
CREATE INDEX idx_stations_region  ON stations(region);      -- 按区域筛选
CREATE INDEX idx_piles_station    ON piles(station_id);     -- 查某站的所有桩
CREATE INDEX idx_piles_status     ON piles(status);         -- 电桩状态统计
CREATE INDEX idx_orders_user      ON orders(user_id);       -- 查某用户的订单
CREATE INDEX idx_orders_pile      ON orders(pile_id);
CREATE INDEX idx_orders_start     ON orders(start_time);    -- 营收趋势按时间聚合
CREATE INDEX idx_orders_status    ON orders(status);        -- 找「未完成订单」

-- ============================================================
-- 视图：把常用的复杂查询封装起来，Qt 里直接 SELECT * FROM 视图名
-- 好处：SQL 写一次，客户端、服务端、大屏、机器学习都能复用
-- ============================================================

-- 充电站总览：带总桩数、空闲数、在线率  → 服务端「充电站管理」列表
DROP VIEW IF EXISTS v_station_overview;
CREATE VIEW v_station_overview AS
SELECT  s.id, s.name, s.region, s.address, s.lat, s.lng, s.price,
        COUNT(p.id)                                    AS pile_total,
        SUM(CASE WHEN p.status = 0 THEN 1 ELSE 0 END)  AS pile_free,
        SUM(CASE WHEN p.status = 1 THEN 1 ELSE 0 END)  AS pile_busy,
        SUM(CASE WHEN p.status = 2 THEN 1 ELSE 0 END)  AS pile_fault,
        -- 在线率 = 非故障桩 / 总桩数 × 100
        ROUND(100.0 * SUM(CASE WHEN p.status IN (0,1) THEN 1 ELSE 0 END)
              / NULLIF(COUNT(p.id), 0), 1)             AS online_rate
FROM stations s
LEFT JOIN piles p ON p.station_id = s.id
GROUP BY s.id;

-- 电桩明细：把数字状态翻译成中文  → 服务端「充电桩管理」、用户端桩列表
DROP VIEW IF EXISTS v_pile_detail;
CREATE VIEW v_pile_detail AS
SELECT  p.id, p.code, s.name AS station_name, s.id AS station_id,
        CASE p.type   WHEN 0 THEN '快充' ELSE '慢充' END      AS type_text,
        p.power_kw,
        CASE p.status WHEN 0 THEN '闲置' WHEN 1 THEN '在用'
                      ELSE '故障' END                          AS status_text,
        p.status, p.total_sessions, p.total_minutes, p.last_restart_at
FROM piles p JOIN stations s ON s.id = p.station_id;

-- 订单明细：五表联查  → 服务端「销售业绩」、用户端订单历史
DROP VIEW IF EXISTS v_order_detail;
CREATE VIEW v_order_detail AS
SELECT  o.id, u.phone, u.nickname, s.name AS station_name, p.code AS pile_code,
        CASE p.type WHEN 0 THEN '快充' ELSE '慢充' END AS type_text,
        o.start_time, o.end_time, o.duration_min,
        ROUND(o.kwh, 2) AS kwh, o.unit_price, ROUND(o.amount, 2) AS amount,
        CASE o.status WHEN 0 THEN '充电中' WHEN 1 THEN '待结算'
                      ELSE '已结算' END AS status_text,
        o.status, o.user_id, o.pile_id, s.id AS station_id
FROM orders o
JOIN users u    ON u.id = o.user_id
JOIN piles p    ON p.id = o.pile_id
JOIN stations s ON s.id = p.station_id;

-- 每日营收  → 服务端「销售业绩」折线图、Web 大屏
DROP VIEW IF EXISTS v_daily_revenue;
CREATE VIEW v_daily_revenue AS
SELECT  date(start_time)      AS day,
        COUNT(*)              AS order_count,
        ROUND(SUM(kwh), 2)    AS total_kwh,
        ROUND(SUM(amount), 2) AS revenue
FROM orders
WHERE status = 2                -- 只统计已结算的
GROUP BY date(start_time);

-- 分时段负荷  → 子系统5 机器学习「充电负荷智能预测」的训练集
DROP VIEW IF EXISTS v_hourly_load;
CREATE VIEW v_hourly_load AS
SELECT  date(start_time)                       AS day,
        CAST(strftime('%H', start_time) AS INT) AS hour,
        p.station_id,
        COUNT(*)           AS session_count,
        ROUND(SUM(o.kwh),2) AS total_kwh
FROM orders o JOIN piles p ON p.id = o.pile_id
WHERE o.status = 2
GROUP BY day, hour, p.station_id;
