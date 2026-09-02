-- ============================================================
-- 示例数据 —— 跑完就能直接看到图表和列表有内容
-- 依赖：先执行 01_schema.sql
-- ============================================================

PRAGMA foreign_keys = ON;

-- ------------------------------------------------------------
-- 管理员：说明书指定的默认账号 admin / 123456
-- 密码存的是 SHA-256，不是明文
-- 验证：printf '123456' | sha256sum
-- ------------------------------------------------------------
INSERT INTO admins (username, pwd_hash, real_name, role) VALUES
('admin', '8d969eef6ecad3c29a3a629280e686cf0c3f5d5a86aff3ca12020c923adc6c92', '系统管理员', 'admin'),
('yunwei','8d969eef6ecad3c29a3a629280e686cf0c3f5d5a86aff3ca12020c923adc6c92', '运维小李',  'staff');

-- ------------------------------------------------------------
-- 充电站：8 个站，分布在 4 个区（用户端「区域下拉框」的数据来源）
-- 经纬度是北京的真实坐标，方便接腾讯地图 API 时对得上
-- ------------------------------------------------------------
INSERT INTO stations (name, region, address, lat, lng, price) VALUES
('中关村科技园充电站', '海淀区', '海淀区中关村大街1号',      39.9847, 116.3076, 1.35),
('北理工南门充电站',   '海淀区', '海淀区中关村南大街5号',    39.9614, 116.3181, 1.20),
('魏公村地铁站充电站', '海淀区', '海淀区魏公村路口西',        39.9536, 116.3222, 1.15),
('国家图书馆充电站',   '海淀区', '海淀区中关村南大街33号',   39.9455, 116.3308, 1.45),
('三里屯太古里充电站', '朝阳区', '朝阳区三里屯路19号',        39.9337, 116.4551, 1.60),
('望京SOHO充电站',     '朝阳区', '朝阳区望京街10号',          39.9962, 116.4740, 1.50),
('西单大悦城充电站',   '西城区', '西城区西单北大街131号',     39.9130, 116.3740, 1.55),
('亦庄经开区充电站',   '大兴区', '大兴区荣京东街12号',        39.7950, 116.5060, 1.05);

-- ------------------------------------------------------------
-- 充电桩：每站 3~5 个，前 2 个是快充，其余是慢充
-- 编号规则 CD-站号-桩号，例如 CD-002-03
-- ------------------------------------------------------------
WITH RECURSIVE cnt(i) AS (SELECT 1 UNION ALL SELECT i + 1 FROM cnt WHERE i < 5)
INSERT INTO piles (station_id, code, type, power_kw, status)
SELECT  s.id,
        'CD-' || printf('%03d', s.id) || '-' || printf('%02d', c.i),
        CASE WHEN c.i <= 2 THEN 0 ELSE 1 END,                    -- 0快充 1慢充
        CASE WHEN c.i <= 2
             THEN (CASE s.id % 3 WHEN 0 THEN 120.0 WHEN 1 THEN 60.0 ELSE 180.0 END)
             ELSE (CASE s.id % 2 WHEN 0 THEN 7.0 ELSE 11.0 END) END,
        0
FROM stations s, cnt c
WHERE c.i <= 3 + (s.id % 3)
ORDER BY s.id, c.i;   -- 让 id 顺序和站号顺序一致，列表看起来才整齐

-- ------------------------------------------------------------
-- 用户：20 个。昵称用说明书规定的默认规则「用户+手机号后4位」
-- ------------------------------------------------------------
WITH RECURSIVE cnt(i) AS (SELECT 1 UNION ALL SELECT i + 1 FROM cnt WHERE i < 20)
INSERT INTO users (phone, nickname, balance)
SELECT  '138' || printf('%08d', 20250000 + i),
        '用户' || substr('138' || printf('%08d', 20250000 + i), 8, 4),
        ROUND(abs(random()) % 50000 / 100.0, 2)
FROM cnt;

-- 几个用户改过昵称和头像（模拟真实使用）
UPDATE users SET nickname = '张伟',   avatar = ':/avatar/a1.png' WHERE id = 1;
UPDATE users SET nickname = '李娜',   avatar = ':/avatar/a2.png' WHERE id = 2;
UPDATE users SET nickname = '王强'                                WHERE id = 3;
UPDATE users SET nickname = '刘洋'                                WHERE id = 5;
-- 两个被风控冻结的账号，用来演示服务端「冻结/解冻」
UPDATE users SET status = 1 WHERE id IN (7, 13);

-- ------------------------------------------------------------
-- 充值流水
-- ------------------------------------------------------------
WITH RECURSIVE cnt(i) AS (SELECT 1 UNION ALL SELECT i + 1 FROM cnt WHERE i < 45)
INSERT INTO recharges (user_id, amount, created_at)
SELECT  abs(random()) % 20 + 1,
        (abs(random()) % 5 + 1) * 50.0,
        datetime('now', 'localtime', '-' || (abs(random()) % 30) || ' day')
FROM cnt;

-- ------------------------------------------------------------
-- 历史订单：最近 30 天 800 条，带早晚高峰
--   30% 落在早高峰 7:00-10:59
--   35% 落在晚高峰 17:00-21:59
--   35% 随机分布在全天
-- 这样「销售业绩」折线图和子系统5的负荷预测才有规律可学
-- ------------------------------------------------------------

-- 步骤 1：先造订单骨架（谁、哪个桩、什么时候、充多久）
WITH RECURSIVE seq(n) AS (SELECT 1 UNION ALL SELECT n + 1 FROM seq WHERE n < 800),
pick AS (
    SELECT  abs(random()) % (SELECT COUNT(*) FROM users) + 1 AS uid,
            abs(random()) % (SELECT COUNT(*) FROM piles) + 1 AS pid,
            abs(random()) % 30                               AS d,
            abs(random()) % 100                              AS r,
            abs(random()) % 60                               AS mi,
            20 + abs(random()) % 100                          AS dur
    FROM seq
),
timed AS (
    SELECT  uid, pid, dur,
            datetime('now', 'localtime',
                     '-' || d || ' day', 'start of day',
                     '+' || (CASE WHEN r < 30 THEN 7  + abs(random()) % 4
                                  WHEN r < 65 THEN 17 + abs(random()) % 5
                                  ELSE              abs(random()) % 24 END) || ' hour',
                     '+' || mi || ' minute') AS st
    FROM pick
)
INSERT INTO orders (user_id, pile_id, start_time, end_time, duration_min,
                    kwh, unit_price, amount, status, settled_at)
SELECT  uid, pid, st,
        datetime(st, '+' || dur || ' minute'), dur,
        0, 0, 0, 2,
        datetime(st, '+' || dur || ' minute')
FROM timed;

-- 步骤 2：按电桩功率算充电量（充电效率取 55%~94%，模拟没充满的情况）
UPDATE orders
SET kwh = ROUND((SELECT p.power_kw FROM piles p WHERE p.id = orders.pile_id)
                * duration_min / 60.0
                * (0.55 + (abs(random()) % 40) / 100.0), 2)
WHERE kwh = 0;

-- 步骤 3：取下单时的电价快照
UPDATE orders
SET unit_price = (SELECT s.price FROM piles p JOIN stations s ON s.id = p.station_id
                  WHERE p.id = orders.pile_id)
WHERE unit_price = 0;

-- 步骤 4：算钱
UPDATE orders SET amount = ROUND(kwh * unit_price, 2) WHERE amount = 0;

-- ------------------------------------------------------------
-- 制造「当前正在发生的事」，让界面一打开就有动态内容
-- ------------------------------------------------------------

-- 3 个桩正在充电（status=1），并生成对应的「充电中」订单（end_time 为 NULL）
UPDATE piles SET status = 1 WHERE id IN (1, 8, 15);

INSERT INTO orders (user_id, pile_id, start_time, end_time, duration_min,
                    kwh, unit_price, amount, status)
SELECT  CASE p.id WHEN 1 THEN 1 WHEN 8 THEN 4 ELSE 9 END,
        p.id,
        datetime('now', 'localtime', '-' || (10 + p.id) || ' minute'),
        NULL, 0, 0,
        (SELECT s.price FROM stations s WHERE s.id = p.station_id),
        0, 0
FROM piles p WHERE p.id IN (1, 8, 15);

-- 2 个桩故障（status=2），用来演示服务端的「远程重启」按钮
UPDATE piles SET status = 2 WHERE id IN (5, 20);

-- 1 个用户有「待结算」订单 —— 用来演示用户端的「充电前检查」强制跳转
UPDATE orders SET status = 1, settled_at = NULL
WHERE id = (SELECT id FROM orders WHERE user_id = 2 AND status = 2
            ORDER BY start_time DESC LIMIT 1);

-- ------------------------------------------------------------
-- 回填电桩的累计统计（累计充电次数 / 累计充电时长）
-- 说明书「充电桩管理」列表要显示这两列
-- ------------------------------------------------------------
UPDATE piles SET
    total_sessions = (SELECT COUNT(*)               FROM orders o WHERE o.pile_id = piles.id AND o.status = 2),
    total_minutes  = (SELECT IFNULL(SUM(o.duration_min), 0) FROM orders o WHERE o.pile_id = piles.id AND o.status = 2);
