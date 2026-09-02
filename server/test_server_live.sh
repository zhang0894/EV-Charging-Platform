#!/usr/bin/env bash
# =============================================================================
# 电动汽车充电桩管理平台 —— Linux Bash 快速连通性测试脚本
# Target: 62.234.84.145:8080
# =============================================================================

HOST="${1:-62.234.84.145}"
PORT="${2:-8080}"
BASE_URL="http://${HOST}:${PORT}"

echo -e "\033[1;36m=======================================================\033[0m"
echo -e "\033[1;36m   电动汽车充电桩管理平台 —— Linux 连通性测试   \033[0m"
echo -e "\033[1;36m   目标服务器: ${BASE_URL}\033[0m"
echo -e "\033[1;36m=======================================================\033[0m"

# 1. 空间搜桩
echo -e "\n\033[1;33m>>> 1. 测试空间搜桩 (/api/v1/stations/nearby)...\033[0m"
curl -s -m 5 "${BASE_URL}/api/v1/stations/nearby?latitude=31.2304&longitude=121.4737&radius_km=15&limit=3" | grep -q "success" && \
  echo -e "  \033[1;32m[PASS]\033[0m 附近搜桩接口正常" || echo -e "  \033[1;31m[FAIL]\033[0m 接口请求失败"

# 2. 车主免密登录
echo -e "\n\033[1;33m>>> 2. 测试车主免密登录 (/api/v1/auth/login)...\033[0m"
LOGIN_RESP=$(curl -s -m 5 -X POST "${BASE_URL}/api/v1/auth/login" \
  -H "Content-Type: application/json" \
  -d '{"phone":"13866667777","auth_type":"passwordless"}')

echo "${LOGIN_RESP}" | grep -q "access_token" && \
  echo -e "  \033[1;32m[PASS]\033[0m 车主免密登录成功" || echo -e "  \033[1;31m[FAIL]\033[0m 登录失败: ${LOGIN_RESP}"

# 3. 管理员登录
echo -e "\n\033[1;33m>>> 3. 测试管理员登录 (/api/v1/admin/auth/login)...\033[0m"
ADMIN_RESP=$(curl -s -m 5 -X POST "${BASE_URL}/api/v1/admin/auth/login" \
  -H "Content-Type: application/json" \
  -d '{"account":"13900000000","password":"123456"}')

echo "${ADMIN_RESP}" | grep -q "access_token" && \
  echo -e "  \033[1;32m[PASS]\033[0m 管理员登录成功" || echo -e "  \033[1;31m[FAIL]\033[0m 管理员登录失败: ${ADMIN_RESP}"

echo -e "\n\033[1;36m=======================================================\033[0m"
echo -e "\033[1;36m   测试完成   \033[0m"
echo -e "\033[1;36m=======================================================\n\033[0m"
