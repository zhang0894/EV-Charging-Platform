# 充电用户端（云端版 · 预约充电）

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

自动测试（打真实 server，全程自我清理）：`./build/test_cloudflow`
三个场景 27 项：预约生命周期 / 到站开充与计费 / 结算+小票+历史。

## 预约充电规则（与 server 完全一致）

| 动作 | 金额 |
|---|---|
| 预约成功 | 真扣押金 ¥20，桩变 已预约锁定(8)，全网可见，倒计时 120 秒 |
| 到站开充 | 押金 ¥20 全额退回，随后正常计费 |
| 主动取消 | 扣违约金 ¥5，退回 ¥15 |
| 超时未到站 | 押金 ¥20 不退（server 每秒清扫） |

充电中客户端每 2 秒拉一次 server 的实时费用；花费上限 = 钱包余额 − 1 元
（留 1 元缓冲吸收轮询间隔），到达上限自动结束充电，余额绝不为负。

## 用到的 server 接口（9/6 统一版）

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
| 钱包 | `GET /api/v1/wallet/balance` |

注意：旧的 `stations/nearby`、`admin/piles`、`orders/{id}` 已被 server 删除，不要再用。

## 规矩

- UI 层不直接发 HTTP —— 所有请求都走 `core/chargeservice` / `core/userservice`
- server 返回的英文错误在 `core/apiclient.cpp` 里统一翻译成中文
- 传文件不要带 build 目录和 `CMakeLists.txt.user`
