#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
=============================================================================
电动汽车充电桩管理平台 —— 远程服务器全功能健康检查与端到端自动化测试脚本
Target: 62.234.84.145:8080 (支持自定义 --host 与 --port 参数)
依赖: Python 3.8+ (纯标准库，无需额外 pip 安装)
=============================================================================
"""

import sys
import json
import socket
import time
import urllib.request
import urllib.error
import argparse
from typing import Dict, Any, Tuple, Optional

# 兼容 Windows GBK 控制台编码
if sys.platform == "win32":
    try:
        sys.stdout.reconfigure(encoding='utf-8')
    except Exception:
        pass

# ANSI 终端颜色
GREEN = "\033[92m"
RED = "\033[91m"
YELLOW = "\033[93m"
CYAN = "\033[96m"
BOLD = "\033[1m"
RESET = "\033[0m"

class ServerTester:
    def __init__(self, host: str, port: int, timeout: float = 5.0):
        self.host = host
        self.port = port
        self.timeout = timeout
        self.base_url = f"http://{host}:{port}"
        self.pass_count = 0
        self.fail_count = 0

    def log_section(self, title: str):
        print(f"\n{BOLD}{CYAN}>>> {title}{RESET}")

    def log_pass(self, title: str, detail: str = ""):
        self.pass_count += 1
        msg = f"  {GREEN}[PASS]{RESET} {title}"
        if detail:
            msg += f" -> {detail}"
        print(msg)

    def log_fail(self, title: str, error: str):
        self.fail_count += 1
        print(f"  {RED}[FAIL]{RESET} {title} -> {RED}{error}{RESET}")

    def check_tcp_port(self) -> bool:
        """1. TCP 端口连通性检测"""
        self.log_section(f"1. 检测服务器 TCP 端口连通性 ({self.host}:{self.port})")
        sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        sock.settimeout(self.timeout)
        try:
            t0 = time.time()
            sock.connect((self.host, self.port))
            sock.close()
            latency = (time.time() - t0) * 1000
            self.log_pass("TCP 端口开放可达", f"RTT 时延: {latency:.2f} ms")
            return True
        except Exception as e:
            self.log_fail("无法连接到目标主机与端口", f"{e} (请检查服务器是否已启动或云服务器安全组/防火墙是否放行 8080 端口)")
            return False

    def http_request(
        self,
        method: str,
        path: str,
        data: Optional[Dict[str, Any]] = None,
        token: str = "",
        idempotency_key: str = ""
    ) -> Tuple[int, Dict[str, Any]]:
        """HTTP 请求封装 (纯标准库)"""
        url = f"{self.base_url}{path}"
        headers = {
            "Content-Type": "application/json",
            "User-Agent": "EV-Server-Tester/1.0"
        }
        if token:
            headers["Authorization"] = f"Bearer {token}"
        if idempotency_key:
            headers["Idempotency-Key"] = idempotency_key

        body_bytes = json.dumps(data).encode("utf-8") if data is not None else None
        req = urllib.request.Request(url, data=body_bytes, headers=headers, method=method.upper())

        try:
            with urllib.request.urlopen(req, timeout=self.timeout) as resp:
                status_code = resp.getcode()
                resp_data = json.loads(resp.read().decode("utf-8"))
                return status_code, resp_data
        except urllib.error.HTTPError as e:
            try:
                resp_data = json.loads(e.read().decode("utf-8"))
            except Exception:
                resp_data = {"code": -1, "msg": str(e)}
            return e.code, resp_data
        except Exception as e:
            return 0, {"code": -1, "msg": str(e)}

    def run_all_tests(self):
        print("=" * 65)
        print(f"{BOLD}   电动汽车充电桩管理平台 —— 远程服务端全功能自动化测试   {RESET}")
        print(f"   目标服务器: {self.base_url}")
        print("=" * 65)

        # 1. 端口连通性检查
        if not self.check_tcp_port():
            print(f"\n{RED}{BOLD}❌ TCP 连通性测试未通过，测试中断。{RESET}")
            print(f"{YELLOW}排查建议:{RESET}")
            print(f"  1. 确认服务器上已执行编译并运行: ./server (或 ./build/server)")
            print(f"  2. 确认腾讯云/阿里云控制台的安全组规则中已添加入方向规则: 允许 TCP:8080 端口")
            print(f"  3. 确认服务器系统防火墙 (ufw/iptables) 已开放 8080: sudo ufw allow 8080/tcp")
            return

        # 2. 公共电站空间与详情查询
        self.log_section("2. 空间索引搜桩与电站详情接口测试")
        status, res = self.http_request("GET", "/api/v1/stations/nearby?latitude=31.2304&longitude=121.4737&radius_km=15&limit=3")
        if status == 200 and res.get("code") == 0 and "data" in res:
            stations = res["data"].get("stations", [])
            self.log_pass(f"空间搜桩成功 (/api/v1/stations/nearby)", f"匹配到 {len(stations)} 个附近电站")
            if stations:
                st = stations[0]
                print(f"       - 示例电站: ID={st.get('station_id')}, 名称='{st.get('station_name')}', 距离={st.get('distance_km', 0):.2f}km, 空闲快充={st.get('fast_piles_idle')}")
        else:
            self.log_fail("空间搜桩失败", f"Status={status}, Resp={res}")

        status, res = self.http_request("GET", "/api/v1/stations/1")
        if status == 200 and res.get("code") == 0:
            piles = res.get("data", {}).get("piles", [])
            self.log_pass("获取1号电站详情 (/api/v1/stations/1)", f"下属 {len(piles)} 个充电桩")
        else:
            self.log_fail("获取电站详情失败", f"Status={status}, Resp={res}")

        # 3. 车主认证与钱包资产
        self.log_section("3. 车主免密登录、个人资料与钱包充值测试")
        test_phone = "13866668888"
        status, res = self.http_request("POST", "/api/v1/auth/login", {"phone": test_phone, "auth_type": "passwordless"})
        user_token = ""
        user_id = 0
        if status == 200 and res.get("code") == 0:
            user_data = res.get("data", {})
            user_token = user_data.get("access_token", "")
            user_id = user_data.get("user_id", 0)
            self.log_pass("车主免密登录/注册成功 (/api/v1/auth/login)", f"UID={user_id}, Phone={user_data.get('phone')}, Token获取成功")
        else:
            self.log_fail("车主登录失败", f"Status={status}, Resp={res}")

        if user_token:
            status, res = self.http_request("GET", "/api/v1/user/profile", token=user_token)
            if status == 200 and res.get("code") == 0:
                prof = res.get("data", {})
                self.log_pass("获取用户个人资料 (/api/v1/user/profile)", f"昵称={prof.get('nickname')}, 当前余额={prof.get('balance'):.2f}元")
            else:
                self.log_fail("获取个人资料失败", f"Status={status}, Resp={res}")

            # 钱包充值
            rec_key = f"REC_PY_TEST_{int(time.time()*1000)}"
            status, res = self.http_request("POST", "/api/v1/wallet/recharge", {"amount": 100.0, "remark": "脚本在线充值100元"}, token=user_token, idempotency_key=rec_key)
            if status == 200 and res.get("code") == 0:
                rec_data = res.get("data", {})
                self.log_pass("钱包充值 100 元成功 (/api/v1/wallet/recharge)", f"流水号={rec_data.get('transaction_id')}, 充值后余额={rec_data.get('balance_after'):.2f}元")
            else:
                self.log_fail("钱包充值失败", f"Status={status}, Resp={res}")

            # 查询流水
            status, res = self.http_request("GET", "/api/v1/wallet/transactions?page=1&page_size=5", token=user_token)
            if status == 200 and res.get("code") == 0:
                txs = res.get("data", {}).get("records", [])
                self.log_pass("查询钱包流水记录 (/api/v1/wallet/transactions)", f"流水记录数={len(txs)}")
            else:
                self.log_fail("查询钱包流水失败", f"Status={status}, Resp={res}")

        # 4. 充电全流程生命周期闭环
        self.log_section("4. 充电业务流程测试 (活动检查 -> 启动 -> 停止 -> 结算扣款)")
        if user_token:
            # 检查进行中订单
            status, res = self.http_request("GET", "/api/v1/charging/active-order", token=user_token)
            if status == 200 and res.get("code") == 0:
                has_act = res.get("data", {}).get("has_active_order", False)
                self.log_pass("检查当前活动充电订单 (/api/v1/charging/active-order)", f"是否存在未结订单={has_act}")

            # 启动充电 (选 P00102)
            target_pile = "P00102"
            status, res = self.http_request("POST", "/api/v1/charging/start", {"pile_id": target_pile, "strategy_type": "FULL"}, token=user_token)
            order_id = ""
            if status == 200 and res.get("code") == 0:
                start_data = res.get("data", {})
                order_id = start_data.get("order_id", "")
                self.log_pass("启动充电成功 (/api/v1/charging/start)", f"订单号={order_id}, 电桩={target_pile}")
                print(f"       - 实时遥测 WebSocket 地址: {start_data.get('ws_telemetry_url')}")
            else:
                # 若桩正忙，记录提示
                self.log_fail("启动充电失败", f"Status={status}, Msg={res.get('msg')}")

            if order_id:
                time.sleep(1.0)
                # 停止充电
                status, res = self.http_request("POST", "/api/v1/charging/stop", {"order_id": order_id, "stop_reason": "USER_MANUAL_STOP"}, token=user_token)
                if status == 200 and res.get("code") == 0:
                    stop_data = res.get("data", {})
                    self.log_pass("停止充电拔枪成功 (/api/v1/charging/stop)", f"状态={stop_data.get('order_status')}, 结算金额={stop_data.get('total_amount'):.2f}元")
                else:
                    self.log_fail("停止充电失败", f"Status={status}, Resp={res}")

                # 结算扣费
                status, res = self.http_request("POST", "/api/v1/charging/settle", {"order_id": order_id}, token=user_token)
                if status == 200 and res.get("code") == 0:
                    settle_data = res.get("data", {})
                    self.log_pass("订单钱包行锁扣费成功 (/api/v1/charging/settle)", f"扣款={settle_data.get('wallet_deducted'):.2f}元, 钱包新余额={settle_data.get('new_balance'):.2f}元")
                else:
                    self.log_fail("订单结算扣款失败", f"Status={status}, Resp={res}")

            # 查询历史订单
            status, res = self.http_request("GET", "/api/v1/orders/my?page=1&page_size=5&sort_order=desc", token=user_token)
            if status == 200 and res.get("code") == 0:
                orders = res.get("data", {}).get("orders", [])
                self.log_pass("查询用户个人历史订单 (/api/v1/orders/my)", f"订单列表数={len(orders)}")
            else:
                self.log_fail("查询个人订单失败", f"Status={status}, Resp={res}")

        # 5. 管理端核心功能与一键退款
        self.log_section("5. 管理端运营看板、单站报表与退款测试")
        status, res = self.http_request("POST", "/api/v1/admin/auth/login", {"account": "13900000000", "password": "123456"})
        admin_token = ""
        if status == 200 and res.get("code") == 0:
            admin_token = res.get("data", {}).get("access_token", "")
            self.log_pass("管理员登录成功 (/api/v1/admin/auth/login)", "超级管理员凭证验证通过")
        else:
            self.log_fail("管理员登录失败", f"Status={status}, Resp={res}")

        if admin_token:
            # 运营态势大盘
            status, res = self.http_request("GET", "/api/v1/admin/dashboard/summary", token=admin_token)
            if status == 200 and res.get("code") == 0:
                d = res.get("data", {})
                self.log_pass("获取运营大盘核心概览 (/api/v1/admin/dashboard/summary)", f"累计营收={d.get('total_revenue'):.2f}元, 注册用户={d.get('total_user_count')}, 活跃会话={d.get('active_charging_sessions')}")
            else:
                self.log_fail("获取大盘概览失败", f"Status={status}, Resp={res}")

            # 7天营收趋势
            status, res = self.http_request("GET", "/api/v1/admin/dashboard/revenue-trend?days=7", token=admin_token)
            if status == 200 and res.get("code") == 0:
                trend = res.get("data", {})
                self.log_pass("获取近7天营收趋势曲线 (/api/v1/admin/dashboard/revenue-trend)", f"包含 {len(trend.get('dates', []))} 天数据点")
            else:
                self.log_fail("获取营收趋势失败", f"Status={status}, Resp={res}")

            # 单站今日销售报表
            status, res = self.http_request("GET", "/api/v1/admin/stations/1/sales-stats?time_range=today", token=admin_token)
            if status == 200 and res.get("code") == 0:
                stats = res.get("data", {})
                self.log_pass("获取单站销售业绩统计 (/api/v1/admin/stations/1/sales-stats)", f"电站='{stats.get('station_name')}', 统计范围='{stats.get('time_range')}'")
            else:
                self.log_fail("获取单站销售报表失败", f"Status={status}, Resp={res}")

            # 管理员按用户ID查询历史订单 (时间正序)
            if user_id > 0:
                status, res = self.http_request("GET", f"/api/v1/admin/orders/user?user_id={user_id}&sort_order=asc", token=admin_token)
                if status == 200 and res.get("code") == 0:
                    u_orders = res.get("data", {}).get("orders", [])
                    self.log_pass("管理员按用户查询历史订单 (/api/v1/admin/orders/user)", f"用户UID={user_id}, 订单数={len(u_orders)}, 排序=正序")
                else:
                    self.log_fail("管理员查询用户订单失败", f"Status={status}, Resp={res}")

        # 汇总报告
        print("\n" + "=" * 65)
        total_tests = self.pass_count + self.fail_count
        if self.fail_count == 0:
            print(f"{BOLD}{GREEN}>> [SUCCESS] 全部测试通过! (通过: {self.pass_count}/{total_tests}, 失败: 0){RESET}")
            print(f"{GREEN}目标服务器 {self.base_url} 运行完全正常，所有业务接口均已就绪。{RESET}")
        else:
            print(f"{BOLD}{YELLOW}>> [WARNING] 测试完成: 通过 {self.pass_count}/{total_tests}, 失败 {self.fail_count}/{total_tests}{RESET}")
            print(f"{YELLOW}请根据上述 [FAIL] 项输出排查具体业务或网络问题。{RESET}")
        print("=" * 65 + "\n")

if __name__ == "__main__":
    parser = argparse.ArgumentParser(description="EV Charging Platform Remote Server Live Test")
    parser.add_argument("--host", type=str, default="62.234.84.145", help="Target server host (default: 62.234.84.145)")
    parser.add_argument("--port", type=int, default=8080, help="Target server port (default: 8080)")
    parser.add_argument("--timeout", type=float, default=5.0, help="HTTP/TCP timeout in seconds (default: 5.0)")
    args = parser.parse_args()

    tester = ServerTester(host=args.host, port=args.port, timeout=args.timeout)
    tester.run_all_tests()
