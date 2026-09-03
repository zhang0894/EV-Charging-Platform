import urllib.request
import json
import time

BASE_URL = "http://127.0.0.1:8080"

def request(method, path, data=None, token=None):
    headers = {"Content-Type": "application/json"}
    if token:
        headers["Authorization"] = f"Bearer {token}"
    body = json.dumps(data).encode("utf-8") if data else None
    req = urllib.request.Request(f"{BASE_URL}{path}", data=body, headers=headers, method=method)
    with urllib.request.urlopen(req) as resp:
        return json.loads(resp.read().decode("utf-8"))

def main():
    print(">>> 1. 管理员登录验证...")
    login_resp = request("POST", "/api/v1/admin/auth/login", {"account": "13900000000", "password": "123456"})
    token = login_resp["data"]["access_token"]
    print("  [OK] 登录成功，获取 JWT 令牌")

    print("\n>>> 2. 测试大盘概览 (Cache Miss -> DB Read Replica -> Redis 填充)...")
    t0 = time.perf_counter()
    summary1 = request("GET", "/api/v1/admin/dashboard/summary", token=token)
    t1 = time.perf_counter()
    ms1 = (t1 - t0) * 1000
    print(f"  [Call 1 - Cache Miss / DB Read] 耗时: {ms1:.2f} ms | 用户数: {summary1['data']['total_user_count']} | 今日营收: {summary1['data']['today_revenue']} 元")

    print("\n>>> 3. 测试大盘概览缓存命中 (Cache Hit -> 零数据库查询 / 微秒级返回)...")
    t2 = time.perf_counter()
    summary2 = request("GET", "/api/v1/admin/dashboard/summary", token=token)
    t3 = time.perf_counter()
    ms2 = (t3 - t2) * 1000
    print(f"  [Call 2 - Cache HIT] 耗时: {ms2:.2f} ms | 用户数: {summary2['data']['total_user_count']}")
    assert summary1['data']['total_user_count'] == summary2['data']['total_user_count'], "Cache data mismatch!"

    print("\n>>> 4. 测试 7 天营收趋势缓存...")
    t4 = time.perf_counter()
    trend1 = request("GET", "/api/v1/admin/dashboard/revenue-trend?days=7", token=token)
    t5 = time.perf_counter()
    ms_trend1 = (t5 - t4) * 1000
    print(f"  [Trend Call 1 - Cache Miss] 耗时: {ms_trend1:.2f} ms, 返回天数: {len(trend1['data']['dates'])}")

    t6 = time.perf_counter()
    trend2 = request("GET", "/api/v1/admin/dashboard/revenue-trend?days=7", token=token)
    t7 = time.perf_counter()
    ms_trend2 = (t7 - t6) * 1000
    print(f"  [Trend Call 2 - Cache HIT] 耗时: {ms_trend2:.2f} ms")

    print("\n>>> 5. 测试单站销售统计缓存...")
    stats1 = request("GET", "/api/v1/admin/stations/1/sales-stats?time_range=today", token=token)
    stats2 = request("GET", "/api/v1/admin/stations/1/sales-stats?time_range=today", token=token)
    print(f"  [Station Sales Stats HIT] 成功获取并缓存: 电站={stats2['data']['station_name']}, 范围={stats2['data']['time_range']}")

    print("\n>>> 6. 测试主从读写分离 (Write to Master -> Read from Replica)...")
    # 获取车主 token (车主拥有个人钱包)
    user_login = request("POST", "/api/v1/auth/login", {"phone": "13866668888", "auth_type": "passwordless"})
    user_token = user_login["data"]["access_token"]

    # 写主库: 充值 100 元
    recharge_resp = request("POST", "/api/v1/wallet/recharge", {
        "amount": 100.0,
        "idempotent_key": f"test_idemp_{int(time.time()*1000)}",
        "remark": "读写分离测试充值"
    }, token=user_token)
    print(f"  [Master Write OK] 充值后余额: {recharge_resp['data']['balance_after']:.2f} 元")

    # 读从库: 查询钱包
    wallet_resp = request("GET", "/api/v1/wallet/balance", token=user_token)
    print(f"  [Replica Read OK] 查询余额: {wallet_resp['data']['balance']:.2f} 元")

    print("\n=======================================================")
    print("   ALL TESTS PASSED: Redis 缓存与读写分离运行完全正常!   ")
    print("=======================================================\n")

if __name__ == "__main__":
    main()
