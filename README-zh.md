# 充电用户端（云端版 · 预约充电 · 薄荷绿主题）

## ⚠️ 崩溃修复记录（2026-09-09）

朋友这版加了真实腾讯地图（`StationMapCard`/`NavigationPage` 内嵌 `QWebEngineView`），
但 `main.cpp` 里强制的 `--use-gl=swiftshader` 参数在新版 Chromium/macOS 上会直接
`qFatal` 崩溃退出（错误信息：`--use-gl=swiftshader is not supported with the
current configuration.`），一打开电站页就整个程序 abort。

ANGLE 的具体后端按操作系统区分开了（`#if defined(Q_OS_MACOS) / Q_OS_WIN / 其他`）：
macOS 用 `--use-angle=metal`（苹果芯片有真实 GPU，不需要软件渲染兜底）；
Windows 用 `--use-angle=d3d11`（ANGLE 在 Windows 上最成熟稳定的后端）；
其余平台（含机房 Linux）**不强制指定后端**，交给 Chromium 按当前显卡/驱动自己选，
避免同一段代码在不同系统上又踩到"这个后端在你机器上不受支持"的同一类坑。
本机（macOS）已连续跑 3 次 + 完整测试套件验证不再崩溃，地图正常显示真实腾讯地图瓦片
和站点标记；Windows/Linux 分支未实机测试，理论上应该能跑，如果队友那边还是崩，
把崩溃信息发回来，用环境变量 `QTWEBENGINE_CHROMIUM_FLAGS` 自己覆盖也能应急
（比如换成 `--disable-gpu --no-sandbox` 纯软件兜底，地图会退化但至少不崩）。

数据全部来自云端 server（`http://62.234.84.145:8080`），本地不开数据库。
A = 充电/预约/结算流程 + 主框架；B = 登录/电站列表/地图/个人中心。

## 怎么跑

Qt Creator 打开 `CMakeLists.txt`（机房没有 CMake 也可以开 `ncs_client_cloud.pro`）。
需要联网；server 在腾讯云，国内直连，不用挂代理（代码里已强制 NoProxy）。

| 环境变量 | 作用 |
|---|---|
| `NCS_API_BASE=http://x.x.x.x:8080` | 换 server 地址 |
| `NCS_PHONE=13800000002` | 截图/测试模式的登录手机号（默认 13800000001） |
| `NCS_LAT` / `NCS_LNG` | 模拟定位坐标（默认北京 40.0, 116.35） |
| `NCS_SHOT=a.png NCS_SHOT_PAGE=1` | 截图自检后退出（0=电站 1=充电 2=订单 3=我的） |
| `NCS_SHOT_ORDER=1` | 配合上面：自动打开第一条订单的小票并截图 |
| `NCS_DEMO_RESERVE=1` | 截图用：自动预约第一个空闲桩（会真扣押金，拍完记得取消退款） |
| `NCS_SHOT_STATION=<站id>` | 配合 NCS_SHOT_PAGE=1：直接打开该站的选桩页（填 0 = 最近的一站） |
| `NCS_SHOT_SELECT=1` | 配合上面：自动选中第一根桩（看选中态绿框） |
| `NCS_SHOT_LOGIN=1` | 只截登录页（不连 server） |
| `NCS_SHOT_DELAY=ms` / `NCS_SHOT_WAIT=ms` | 切页前等多久（默认 800）/ 切页后等多久再截图（默认 2200；看充满自动结算要给 20000 以上） |

自动测试（打真实 server，全程自我清理）：`NCS_PHONE=13800000002 NCS_PHONE_B=13800000003 ./build/test_cloudflow`
五个场景 48 项：A 预约→B 看到已预约锁定且预约被拒 / A、B 并发抢桩只成功一个 /
到站开充与计费 / 结算+小票+历史 / 管理端冻结账户（需要管理员账号，默认 13900000000/123456）。

## 预约充电规则（与 server 完全一致）

| 动作 | 金额 |
|---|---|
| 预约成功 | 真扣押金 ¥20，桩变 已预约锁定(8)，全网可见，倒计时 120 秒 |
| 到站开充 | 押金 ¥20 全额退回，随后正常计费 |
| 主动取消 | 扣违约金 ¥5，退回 ¥15 |
| 超时未到站 | 押金 ¥20 不退（server 每秒清扫） |

充电中客户端每 2 秒拉一次 server 的实时费用；花费上限 = 钱包余额 − 1 元
（留 1 元缓冲吸收轮询间隔），到达上限自动结束充电，余额绝不为负。

选桩页每 5 秒刷新一次桩状态（别人预约了会立刻变成红色「已预约锁定」）。
两次刷新都是**异步**请求，回来后**原地改文字/颜色**，桩集合没变就不重建列表，
所以不闪、选中不丢、滚动位置不动。桩名只显示「01号慢充桩」这一段（站名已在页顶）。

## 界面主题（薄荷绿）

- 所有颜色/圆角/字号集中在 `ui/theme.h`：淡绿渐变底（`QWidget#MainWindow` / `#LoginWindow` / `#SetupWindow`）、
  白色圆角卡片（`#Card`）、胶囊主按钮（默认 `QPushButton`）、灰色次要按钮（`#Secondary`）、描边小按钮（`#Ghost` / `#Small`）。
- 全局 `QWidget { background:transparent }`，所以页面直接透出渐变；需要底色的控件（输入框、列表项、对话框）在主题里单独给。
- 充电页参考小程序样式：`ui/socgauge.h`（270° 刻度弧仪表盘，显示 SOC% 和「预计还需 X 分钟充满」）+
  `ui/carart.h`（QPainter 画的车和桩，不依赖图片/SVG 模块）+ 三格统计（费用 / 时长 / 度数）+ 停止充电 / 查看详情。
- 仪表盘进度是**演示用的本地推算**（`ChargePage::demoSoc`）：每辆车从 20% 开始，按开充时间 30 秒线性到 100%，
  不读 server 的 `soc`（server 模拟电池包太大，慢充几分钟才涨 1%，课堂看不出变化）。费用/度数仍是 server 实时值。
  到 100% 时自动调用 `/charging/stop` 并直接进入结算页（`ChargePage::finishWhenFull`），不用手动点停止。
- 登录后 token 过期（40001/40002/40004）时 `core/apiclient.cpp` 自动用 refresh token 换新并重发一次（同步/异步都有，只重试一次）。
- 个人中心多了「刷新token」：清掉本机保存的登录凭证，下次启动必须验证码/密码登录（当前会话不受影响）；资料每 5 秒自动刷新。
- 管理端下线的电站：列表卡片置灰，进入后「预约充电」不可用并提示 该电站已下线。

## 定位与"距离最近"

- 首次进入用 ip-api 按公网 IP 定位，**只有城市级精度**（北京一般落在天安门附近）；输入区名（如"海淀区"）走模拟 GPS，
  输入具体地址走腾讯地图解析。
- server 数据只有北京市，所以定位结果**必须在北京范围内**（纬度 39.3~41.1，经度 115.3~117.6）：
  IP 出口在天津/河北、或开了代理变成新加坡时，直接退回默认位置海淀区并在提示里说明，否则"距离最近"排出来全是几十公里外的通州边缘站。
  测试这个分支：`NCS_IPAPI_URL=http://127.0.0.1:18765/tianjin.json`（本地放一个假 ip-api 回包）。
- server 数据里有 4 条地址在外地、坐标却在北京市中心的脏数据（id 1712/3065/3410/3418），
  `core/chargeservice.cpp` 的 `looksOutsideBeijing()` 按地址关键字剔除，后端修好后可删。

## 账户被管理端冻结

server 对冻结账户（`users.status = 2`）的登录 / 密码登录 / 改密码返回业务码 `10002`；
个人资料 `GET /user/profile` 返回 `status: 2, status_desc: "FROZEN"`。客户端统一处理：

- `core/apiclient.cpp`：任何接口回 `10002` → 翻成「账户已冻结，请联系管理员」并触发 `Api::events()->accountFrozen()`
- `core/userservice.cpp`：`profileAsync` / `accountFrozen()` 看到 `status=2` 也触发同一信号
- `ui/mainwindow.cpp`：收到信号弹一次「账户已冻结，请联系管理员」，确定后清 Session 退回登录页
- `ui/loginwindow.cpp`：本机 token 还有效但账号已冻结 → 不进主界面，登录页直接提示
- `ui/chargepage.cpp`：进入充电页、点预约、点到站开充之前各查一次资料（线上 server 对
  已登录用户的预约/充值/开充**不拦**冻结状态，源码里查的是 `user_wallets.status`，
  管理端冻结只改 `users.status` —— 已记录，待后端修）

## 用到的 server 接口（9/7 对照 GitHub 最新 server 源码核对过）

| 功能 | 接口 |
|---|---|
| 登录 | `POST /api/v1/auth/login` |
| 电站列表 | `GET /api/v1/stations/inquire`（带 lat/lng 按距离排序，page_size≤20） |
| 电站卡片 | `GET /api/v1/stations/{id}`（不再带桩列表） |
| 桩列表 | `GET /api/v1/piles?station_id=`（page_size≤30，8 种状态） |
| 预约 / 取消 / 查询 | `POST /charging/reserve` · `POST /charging/cancel-reservation` · `GET /charging/active-reservation` |
| 充电 | `POST /charging/start` · `POST /charging/stop` · `GET /charging/active-order` |
| 结算 | `POST /charging/settle`（Idempotency-Key=订单号，重复点不会扣两次） |
| 订单 | `GET /api/v1/orders/my` · 详情 `GET /api/v1/charging/orders/{id}`（拿不到自动去 my 里找） |
| 钱包 | `GET /api/v1/wallet/balance` · `POST /api/v1/wallet/recharge` |
| 账号 | `POST /auth/login` · `/auth/login-password` · `/auth/register` · `/auth/refresh` · `/auth/check-phone` · `POST /user/password` |
| 资料 | `GET/PUT /api/v1/user/profile` |
| 头像 | `POST /api/v1/user/avatar`（**请求体是图片原始二进制**，`Content-Type: image/jpeg|png|webp`，<1MB）· `GET /api/v1/user/avatar`（带 token，返回图片流，没上传过回 404/10006） |

注意：旧的 `stations/nearby`、`admin/piles`、`orders/{id}` 已被 server 删除，不要再用。
头像上传旧写法（JSON 里放 base64）server 已不认，会存成坏图。

server 业务错误码全部 32 个都在 `core/apiclient.cpp` 翻成了中文（含 4xxxx 登录失效、5xxxx 服务端异常）。

## 规矩

- UI 层不直接发 HTTP —— 所有请求都走 `core/chargeservice` / `core/userservice`
- server 返回的英文错误在 `core/apiclient.cpp` 里统一翻译成中文
- 传文件不要带 build 目录和 `CMakeLists.txt.user`
