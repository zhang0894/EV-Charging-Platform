# -*- coding: utf-8 -*-
import json
import random
import time
import os
import sys

def get_data_dir():
    # 动态定位 server/data 相对路径，确保在不同环境或部署路径下均可直接运行
    script_dir = os.path.dirname(os.path.abspath(__file__))
    candidates = [
        os.path.join(script_dir, "..", "server", "data"),
        os.path.join(script_dir, "server", "data"),
        os.path.join(os.getcwd(), "server", "data"),
        os.path.join(os.getcwd(), "data"),
    ]
    for cand in candidates:
        cand_norm = os.path.normpath(cand)
        if os.path.exists(os.path.join(cand_norm, "beijing_charging_stations.json")):
            return cand_norm
    return os.path.normpath(os.path.join(script_dir, "..", "server", "data"))

def main():
    data_dir = get_data_dir()
    src_stations_file = os.path.join(data_dir, "beijing_charging_stations.json")
    
    out_stations_file = os.path.join(data_dir, "stations_processed.json")
    out_users_file = os.path.join(data_dir, "seed_users.json")
    out_piles_file = os.path.join(data_dir, "seed_piles.json")
    out_orders_file = os.path.join(data_dir, "seed_orders.json")

    print(f">>> Loading {src_stations_file}...")
    with open(src_stations_file, "r", encoding="utf-8") as f:
        stations_raw = json.load(f)

    station_count = len(stations_raw)
    print(f">>> Total raw stations: {station_count}")

    # 1. 生成紧凑的 stations_processed.json 用于 C++23 #embed 与 Glaze 反序列化
    processed_stations = []
    for s in stations_raw:
        processed_stations.append({
            "station_id": int(s["id"]),
            "district_code": int(s["district_code"]),
            "latitude": round(float(s["latitude"]), 6),
            "longitude": round(float(s["longitude"]), 6),
            "name": s["name"].strip(),
            "address": s["address"].strip() if s.get("address") else ""
        })

    print(f">>> Writing {out_stations_file}...")
    with open(out_stations_file, "w", encoding="utf-8") as f:
        json.dump(processed_stations, f, ensure_ascii=False, indent=None, separators=(",", ":"))

    # 固定随机数种子保证数据生成的确定性与可复现性
    random.seed(42)
    now_ms = int(time.time() * 1000)
    month_ago_ms = now_ms - (30 * 86400 * 1000)

    # 2. 生成用户及钱包 (20,000 普通用户 + 1 超级管理员)
    print(">>> Generating 20,001 users and wallets...")
    users = []
    # 超级管理员 (user_id = 1)
    users.append({
        "user_id": 1,
        "phone": "13900000000",
        "password_hash": "123456",
        "nickname": "超级管理员",
        "avatar_url": "http://localhost:8080/static/avatars/admin.png",
        "role": "admin",
        "status": 1,
        "balance_cents": 99999900,
        "frozen_cents": 0,
        "created_at": month_ago_ms,
        "updated_at": now_ms
    })
    for i in range(1, 20001):
        user_reg_time = month_ago_ms + int(random.uniform(0, 25 * 86400 * 1000))
        users.append({
            "user_id": i + 1,
            "phone": f"138{i:08d}",
            "password_hash": "123456",
            "nickname": f"车主_{i:05d}",
            "avatar_url": "http://localhost:8080/static/avatars/default.png",
            "role": "user",
            "status": 1,
            "balance_cents": 20000,  # 初始余额 200.00 元
            "frozen_cents": 0,
            "created_at": user_reg_time,
            "updated_at": user_reg_time
        })

    print(f">>> Writing {out_users_file}...")
    with open(out_users_file, "w", encoding="utf-8") as f:
        json.dump(users, f, ensure_ascii=False, indent=None, separators=(",", ":"))

    # 3. 生成充电桩 (5~30 桩/站)
    # 规则：
    # - 站名包含“国家电网汽车充电站”、“超级”、“超充”、“特来电”的电站必须包含快充桩
    # - 约 20% 的电站只包含慢充桩
    # - 其余电站快慢桩比例在 [0.35, 0.85] 区间内浮动
    print(">>> Generating charging piles with intelligent distribution...")
    must_fast_keywords = ["国家电网汽车充电站", "超级", "超充", "特来电"]
    
    candidate_slow_only = []
    must_fast_stations = set()
    for s in processed_stations:
        sid = s["station_id"]
        name = s["name"]
        if any(k in name for k in must_fast_keywords):
            must_fast_stations.add(sid)
        else:
            candidate_slow_only.append(sid)

    target_slow_only_count = int(station_count * 0.20)  # 约 20% 纯慢充站 (1713 站)
    slow_only_stations = set(random.sample(candidate_slow_only, target_slow_only_count))

    piles = []
    station_pile_map = {}  # station_id -> list of pile_ids

    for s in processed_stations:
        sid = s["station_id"]
        sname = s["name"]
        pile_count = random.randint(5, 30)
        station_piles = []

        is_slow_only = (sid in slow_only_stations)
        is_must_fast = (sid in must_fast_stations)

        # 决定快充比例
        if is_slow_only:
            fast_ratio = 0.0
        else:
            # 快慢比例在 40% ~ 85% 之间随机浮动
            fast_ratio = random.uniform(0.40, 0.85)

        num_fast = int(round(pile_count * fast_ratio))
        if is_must_fast and num_fast < 1:
            num_fast = 1
        if not is_slow_only and num_fast == 0:
            num_fast = 1

        for p_idx in range(1, pile_count + 1):
            pid = f"P{sid:05d}_{p_idx:02d}"
            station_piles.append(pid)

            is_fast = (p_idx <= num_fast)
            ptype = "FAST" if is_fast else "SLOW"
            if is_fast:
                p_kw = 240.0 if p_idx <= 2 else 120.0
                v_range = "200V-750V"
                ptype_desc = "快充"
            else:
                p_kw = 7.0
                v_range = "220V"
                ptype_desc = "慢充"

            pname = f"{sname[:40]}-{p_idx:02d}号{ptype_desc}桩"
            
            # 桩状态分布
            status_roll = random.random()
            if status_roll < 0.85:
                status = "IDLE"
            elif status_roll < 0.95:
                status = "CHARGING"
            else:
                status = "FAULT"

            charge_count = random.randint(20, 200)
            charge_hours = round(charge_count * random.uniform(0.8, 2.5), 1)

            piles.append({
                "pile_id": pid,
                "station_id": sid,
                "pile_name": pname,
                "type": ptype,
                "gun_type": "国标2015",
                "max_power_kw": p_kw,
                "voltage_range": v_range,
                "status": status,
                "total_charge_count": charge_count,
                "total_charge_hours": charge_hours,
                "last_heartbeat_at": now_ms,
                "created_at": month_ago_ms,
                "updated_at": now_ms
            })

        station_pile_map[sid] = station_piles

    total_piles = len(piles)
    print(f">>> Piles generated: {total_piles} across {station_count} stations")
    print(f"    - Slow-only stations: {len(slow_only_stations)} ({len(slow_only_stations)/station_count*100:.1f}%)")
    print(f"    - Must-fast stations: {len(must_fast_stations)} ({len(must_fast_stations)/station_count*100:.1f}%)")

    print(f">>> Writing {out_piles_file}...")
    with open(out_piles_file, "w", encoding="utf-8") as f:
        json.dump(piles, f, ensure_ascii=False, indent=None, separators=(",", ":"))

    # 4. 生成 200,000 笔历史订单 (严格校验外键关联)
    print(">>> Generating 200,000 historical charging orders...")
    orders = []
    user_count = len(users) - 1  # 排除管理员

    for i in range(1, 200001):
        uid = random.randint(2, user_count + 1)
        sid = random.randint(1, station_count)
        p_list = station_pile_map[sid]
        pid = random.choice(p_list)

        duration_mins = random.randint(20, 90)
        days_ago_ms = int(random.uniform(1, 28) * 86400 * 1000)
        st = now_ms - days_ago_ms - (i * 50)
        et = st + (duration_mins * 60 * 1000)

        start_soc = random.randint(15, 35)
        end_soc = random.randint(85, 100)
        kwh = round(15.0 + (duration_mins * 0.45) + random.uniform(-2.0, 3.0), 2)
        if kwh < 5.0:
            kwh = 5.0

        elec_price = 1.45
        serv_price = 0.35
        elec_fee_cents = int(round(kwh * elec_price * 100))
        serv_fee_cents = int(round(kwh * serv_price * 100))

        overtime_mins = max(0, duration_mins - 60)
        if overtime_mins > 15:
            overtime_blocks = (overtime_mins - 1) // 15 + 1
            overtime_fee_cents = int(round(5.00 * overtime_blocks * 100))
        else:
            overtime_fee_cents = 0

        total_fee_cents = elec_fee_cents + serv_fee_cents + overtime_fee_cents
        oid = f"ORD_HST_{i:08d}"
        status = "REFUNDED" if (i % 50 == 0) else "COMPLETED"

        orders.append({
            "order_id": oid,
            "user_id": uid,
            "station_id": sid,
            "pile_id": pid,
            "strategy_type": "FULL",
            "strategy_value": 0.0,
            "order_status": status,
            "start_time": st,
            "end_time": et,
            "start_soc": start_soc,
            "end_soc": end_soc,
            "charged_energy_kwh": kwh,
            "electricity_price": elec_price,
            "electricity_fee_cents": elec_fee_cents,
            "service_price": serv_price,
            "service_fee_cents": serv_fee_cents,
            "overtime_grace_minutes": 15,
            "overtime_duration_minutes": overtime_mins,
            "overtime_rate_per_15min": 5.00,
            "overtime_fee_cents": overtime_fee_cents,
            "total_fee_cents": total_fee_cents,
            "stop_reason": "USER_MANUAL_STOP",
            "settled_at": et,
            "created_at": st,
            "updated_at": et
        })

    print(f">>> Writing {out_orders_file}...")
    with open(out_orders_file, "w", encoding="utf-8") as f:
        json.dump(orders, f, ensure_ascii=False, indent=None, separators=(",", ":"))

    print(">>> [SUCCESS] All dataset JSON files generated successfully!")

if __name__ == "__main__":
    main()
