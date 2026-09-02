param(
    [string]$HostIP = "62.234.84.145",
    [int]$Port = 8080
)

$BaseUrl = "http://${HostIP}:${Port}"
Write-Host "=======================================================" -ForegroundColor Cyan
Write-Host "   电动汽车充电桩管理平台 —— 远程服务端连通性测试 (PS)   " -ForegroundColor Cyan
Write-Host "   目标服务器: $BaseUrl" -ForegroundColor Cyan
Write-Host "======================================================="

# 1. TCP 端口连通性
Write-Host "`n>>> 1. 检测 TCP 端口连通性..." -ForegroundColor Yellow
try {
    $tcp = New-Object System.Net.Sockets.TcpClient
    $tcp.Connect($HostIP, $Port)
    $tcp.Close()
    Write-Host "  [PASS] TCP 端口 $Port 连接成功！" -ForegroundColor Green
} catch {
    Write-Host "  [FAIL] 无法连接到 $BaseUrl : $_" -ForegroundColor Red
    Write-Host "  提示: 请检查远程服务器 server 进程是否启动，以及腾讯云/阿里云安全组是否放行 8080 端口。" -ForegroundColor Yellow
    exit 1
}

# 2. 空间搜桩接口
Write-Host "`n>>> 2. 测试空间搜桩 (/api/v1/stations/nearby)..." -ForegroundColor Yellow
try {
    $res = Invoke-RestMethod -Uri "$BaseUrl/api/v1/stations/nearby?latitude=31.2304&longitude=121.4737&radius_km=15&limit=3" -Method Get -TimeoutSec 5
    Write-Host "  [PASS] 搜桩成功! 匹配到 $($res.data.total) 个电站" -ForegroundColor Green
} catch {
    Write-Host "  [FAIL] 请求失败: $_" -ForegroundColor Red
}

# 3. 车主免密登录
Write-Host "`n>>> 3. 测试车主免密登录 (/api/v1/auth/login)..." -ForegroundColor Yellow
try {
    $loginBody = '{"phone":"13877779999","auth_type":"passwordless"}'
    $loginRes = Invoke-RestMethod -Uri "$BaseUrl/api/v1/auth/login" -Method Post -Body $loginBody -ContentType "application/json" -TimeoutSec 5
    $token = $loginRes.data.access_token
    Write-Host "  [PASS] 车主登录成功! 获取到 Token: $($token.Substring(0, 20))..." -ForegroundColor Green
} catch {
    Write-Host "  [FAIL] 车主登录失败: $_" -ForegroundColor Red
}

# 4. 管理员登录与大盘
Write-Host "`n>>> 4. 测试管理员登录与大盘 (/api/v1/admin/*)..." -ForegroundColor Yellow
try {
    $adminBody = '{"account":"13900000000","password":"123456"}'
    $adminLogin = Invoke-RestMethod -Uri "$BaseUrl/api/v1/admin/auth/login" -Method Post -Body $adminBody -ContentType "application/json" -TimeoutSec 5
    $adminToken = $adminLogin.data.access_token
    Write-Host "  [PASS] 管理员登录成功!" -ForegroundColor Green

    $adminHeaders = @{ "Authorization" = "Bearer $adminToken" }
    $summary = Invoke-RestMethod -Uri "$BaseUrl/api/v1/admin/dashboard/summary" -Method Get -Headers $adminHeaders -TimeoutSec 5
    Write-Host "  [PASS] 获取大盘数据成功! 累计营收: $($summary.data.total_revenue) 元, 用户数: $($summary.data.total_user_count)" -ForegroundColor Green
} catch {
    Write-Host "  [FAIL] 管理员测试失败: $_" -ForegroundColor Red
}

Write-Host "`n=======================================================" -ForegroundColor Cyan
Write-Host "   >>> 远程服务器基础功能验证完成 <<<   " -ForegroundColor Cyan
Write-Host "=======================================================`n"
