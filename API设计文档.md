# 电动汽车充电桩管理平台 —— 服务端接口与端口设计规范文档 (API Specification)

> **版本**：v1.1.0  
> **协议支持**：HTTP/1.1、HTTP/2 (RESTful JSON)、WebSocket (RFC 6455)  
> **字符编码**：UTF-8  
> **基础路径 (Base URL)**：`http://<server-host>:8080/api/v1`  
> **长连接路径 (WebSocket URL)**：`ws://<server-host>:8080/ws/v1`  
> **服务技术栈**：Modern C++20/23 (Boost.Asio + Boost.Beast + Glaze + PostgreSQL 18 + Redis)

---

## 目录

- [一、 全局规范与约定](#一-全局规范与约定)
  - [1.1 统一返回数据结构](#11-统一返回数据结构)
  - [1.2 HTTP 状态码与业务错误码定义](#12-http-状态码与业务错误码定义)
  - [1.3 身份认证与权限控制 (RBAC)](#13-身份认证与权限控制-rbac)
  - [1.4 数据精度与单位规约](#14-数据精度与单位规约)
  - [1.5 计费模型与规则（电费 + 超时占位费）](#15-计费模型与规则电费--超时占位费)
  - [1.6 资金安全与幂等性控制](#16-资金安全与幂等性控制)
- [二、 充电用户端 API (Driver Client)](#二-充电用户端-api-driver-client)
  - [2.1 用户认证与个人中心](#21-用户认证与个人中心)
  - [2.2 钱包账户与资金交易](#22-钱包账户与资金交易)
  - [2.3 充电站查询与详情](#23-充电站查询与详情)
  - [2.4 充电核心业务流程 (预约-检查-启动-停止-结算)](#24-充电核心业务流程-预约-检查-启动-停止-结算)
  - [2.5 历史订单与账单查询](#25-历史订单与账单查询)
- [三、 PC 运营管理端 API (Admin Management)](#三-pc-运营管理端-api-admin-management)
  - [3.1 管理员认证](#31-管理员认证)
  - [3.2 平台综合运营态势与销售业绩](#32-平台综合运营态势与销售业绩)
  - [3.3 充电站运维管理与单站销售分析 (上下线 & 销售统计)](#33-充电站运维管理与单站销售分析-上下线--销售统计)
  - [3.4 充电桩监控与远程管控 (CRUD & 远程指令)](#34-充电桩监控与远程管控-crud--远程指令)
  - [3.5 平台用户管理与风控处置](#35-平台用户管理与风控处置)
  - [3.6 平台全局订单审计、用户历史订单查询与一键退款](#36-平台全局订单审计用户历史订单查询与一键退款)
- [四、 实时数据流与长连接 (WebSocket Streams)](#四-实时数据流与长连接-websocket-streams)
  - [4.1 充电过程高频遥测数据流 (唯一实时监控途径)](#41-充电过程高频遥测数据流-唯一实时监控途径)
  - [4.2 目标充电站导航动态监控流 (占用与排队情况推送)](#42-目标充电站导航动态监控流-占用与排队情况推送)
  - [4.3 全网设备状态与系统告警广播流](#43-全网设备状态与系统告警广播流)
- [五、 总结与开发落地指引](#五-总结与开发落地指引)

---

## 一、 全局规范与约定

### 1.1 统一返回数据结构

服务端所有 RESTful 接口均返回 `application/json` 格式数据，统一遵循以下封装模板：

```json
{
  "code": 0,
  "msg": "success",
  "data": {},
  "timestamp": 1772607600000
}
```

| 字段名 | 类型 | 必填 | 描述说明 |
| :--- | :--- | :--- | :--- |
| `code` | `Integer` | 是 | 业务状态码。`0` 表示操作成功，非 `0` 表示业务异常或错误。 |
| `msg` | `String` | 是 | 状态提示信息，成功时为 `"success"`，失败时返回具体错误描述。 |
| `data` | `Object / Array / Null` | 是 | 业务响应主体载荷。无数据返回时为 `{}` 或 `null`。 |
| `timestamp` | `Long` | 是 | 服务端响应时的毫秒级 Unix 时间戳。 |

---

### 1.2 HTTP 状态码与业务错误码定义

#### HTTP 传输层状态码

| HTTP Status | 语义 | 使用场景 |
| :--- | :--- | :--- |
| `200 OK` | 请求成功 | 接口处理成功，业务结果由 JSON 内部 `code` 区分。 |
| `400 Bad Request` | 客户端参数错误 | JSON 解析失败、必填字段缺失、数据格式不合法。 |
| `401 Unauthorized` | 身份未认证 | 缺少 Authorization 请求头、Token 过期或签名无效。 |
| `403 Forbidden` | 权限不足 | 用户试图访问管理员接口，或被冻结账号发起受限操作。 |
| `404 Not Found` | 资源未找到 | 请求路径不存在、查询的目标电站/电桩/订单不存在。 |
| `409 Conflict` | 状态冲突 | 桩已被占用、存在未完成订单冲突、幂等键冲突、订单已退款。 |
| `422 Unprocessable Entity` | 业务语义校验不通过 | 余额不足无法启动充电、充电桩处于故障维护状态。 |
| `500 Internal Server Error` | 服务端内部异常 | 数据库连接超时、内部异步协程执行异常。 |

#### 业务错误码矩阵 (Business Code Matrix)

| 业务 `code` | 提示信息 (`msg`) | 场景说明 |
| :--- | :--- | :--- |
| `0` | `success` | 请求处理成功 |
| `10001` | `User not found` | 用户不存在 |
| `10002` | `User account frozen` | 用户已被冻结，禁止交易与充电 |
| `10003` | `Invalid phone format` | 手机号格式错误（必须为11位合法手机号） |
| `10004` | `Invalid credentials` | 用户名或密码错误 |
| `10005` | `User phone already registered` | 手机号已注册，请直接登录 |
| `20001` | `Station not found` | 充电站不存在或已下线 |
| `20002` | `Charging pile not found` | 充电桩不存在 |
| `20003` | `Charging pile is busy or reserved` | 充电桩正被占用或已预约 |
| `20004` | `Charging pile in fault state` | 充电桩当前处于故障/离线维护状态 |
| `20005` | `Active charging order exists` | 用户存在未结算/进行中的充电订单，禁止重复开枪 |
| `20006` | `No active charging order found` | 未找到对应的活跃充电订单 |
| `30001` | `Insufficient wallet balance` | 钱包余额不足以启动充电预冻结 |
| `30002` | `Duplicate transaction key (Idempotent)` | 重复的交易请求（幂等性拦截） |
| `30003` | `Invalid recharge amount` | 充值金额非法（必须大于0） |
| `30004` | `Order already refunded` | 订单已经退款，不可重复退款 |
| `30005` | `Invalid refund amount` | 退款金额非法或超过订单实际支付金额 |
| `40001` | `Unauthorized token` | Token 缺失或非法 |
| `40002` | `Token expired` | Token 已过期，需刷新 |
| `40003` | `Permission denied` | 角色越权访问拒绝 |
| `50001` | `Internal database error` | 数据库执行或连接池错误 |
| `50002` | `Hardware communication timeout` | 电桩底层/模拟器通讯超时 |

---

### 1.3 身份认证与权限控制 (RBAC)

- **Header 格式**：受保护接口需在请求头携带 Bearer Token：
  ```http
  Authorization: Bearer <access_token>
  ```
- **角色划分**：
  - `user`：普通车主客户端。
  - `admin`：PC 运营管理后台。
- **用户登录与注册机制**：
  - **手机号登录** (`POST /api/v1/auth/login`)：仅供已注册用户快捷登录。若数据库中不存在该手机号，系统返回 `10001` 业务错误码 (`User not found`)，不再自动注册。
  - **账号注册与自动登录** (`POST /api/v1/auth/register`)：新用户通过手机号与密码注册，校验手机号唯一性。注册成功后视同登录，直接返回包含 Token 与用户信息的完整凭证。
  - **手机号+密码登录** (`POST /api/v1/auth/login-password`)：支持已注册车主通过手机号与密码进行双因子认证登录。
- **Token 有效期与即时风控机制**：
  - **Access Token 有效期**：`120` 秒（2 分钟，登录响应返回 `expires_in: 120`），用于所有受保护业务接口的高频鉴权。
  - **Refresh Token 有效期**：`604800` 秒（7 天），用于短期 Access Token 过期后安全置换新凭证 (`POST /api/v1/auth/refresh`)。
  - **账号冻结即时失效**：管理端将用户账号置为冻结（`status = 2`）后，系统立即生效：
    1. 对应账号已签发的 Access Token 与 Refresh Token **即刻失效**，后续携带该凭证访问任何受限 API 均直接阻断，返回 HTTP 403 Forbidden（业务码 `10002 UserAccountFrozen`）；
    2. 该账号历史签发的 Token 彻底吊销，即使账号后续解冻，旧 Token 亦永久失效（返回 HTTP 401 Unauthorized），强制用户重新登录获取全新凭证；
    3. 冻结账号发起任何登录请求（免密登录、密码登录、管理员登录）均直接被拒绝，返回 HTTP 403 Forbidden（业务码 `10002 UserAccountFrozen`）。

---

### 1.4 数据精度与单位规约

| 数据类别 | 单位 | 传输与存储类型 | 示例 / 说明 |
| :--- | :--- | :--- | :--- |
| **资金金额** | 分 (Cent) & 元 (Yuan) | `Long` (分) + `Double` (元) | 内部计算和数据库必须以 **分** (`Long`) 计算；对外展示返回双字段，如 `amount_cents: 1550`, `amount: 15.50`。 |
| **地理坐标** | 经纬度 (GCJ-02 / WGS-84) | `Double` | `latitude: 31.230416`, `longitude: 121.473701`（保留 6 位小数）。 |
| **电量度数** | 度 (kWh) | `Double` | `energy_kwh: 42.50`（保留 2 位小数）。 |
| **充电功率** | 千瓦 (kW) | `Double` | `power_kw: 120.0`。 |
| **电压 / 电流** | 伏特 (V) / 安培 (A) | `Double` | `voltage_v: 380.5`, `current_a: 150.2`。 |
| **电池百分比** | 百分比 (%) | `Integer` | `soc: 78`（范围 0 ~ 100）。 |
| **时间戳** | 毫秒 (ms) | `Long` | `1772607600000` (Unix Epoch Milliseconds)。 |

---

### 1.5 计费模型与规则（差异化电价 + 超时占位费）

为了高度模拟真实充电场景，平台的订单结算采用**“站点差异化电费 + 充满后超时占位费”**组合计费体系：

1. **基础充电费用 (Charging Electricity & Service Fee)**：
   $$\text{充电总电费} = \text{充电量 (kWh)} \times (\text{基础电价 } P_{elec} + \text{服务费单价 } P_{service})$$
   - **差异化电价体系**：各充电站基础电价（$P_{elec}$）在 **1.15 ~ 1.85 元/度**（步长 0.01 元）区间内随机生成并持久化保存至数据库 `stations.price_per_kwh` 字段，全网不同充电站呈现多样化的真实电价。
   - **服务费单价**：默认标准服务费 $P_{service} = 0.35$ 元/度。
   - 例如：某电站电费 1.62 元/度，服务费 0.35 元/度，充 30 度电，充电费 $= 30 \times (1.62 + 0.35) = 59.10$ 元。

2. **充满后超时占位费 (Overtime Occupancy Fee)**：
   - **触发条件**：车辆电池达到 100%（或达到用户设定充电阈值，枪机停止输电）且进入 `CHARGED_FULL` 状态后，**用户未及时拔枪驶离**。
   - **免费宽限期**：充满后的 **前 15 分钟内为免费宽限期**（不收取占位费）。
   - **阶梯计费**：超过 15 分钟后，**每 15 分钟计收 $X$ 元**（例如 5.00 元/15分钟，不足 15 分钟向上取整为一个计费周期）：
     $$\text{超时分钟数} T_{over} = \max(0, T_{unplug} - T_{full})$$
     $$\text{计费周期数} N = \begin{cases} 0, & T_{over} \le 15\text{ 分钟} \\ \lceil \frac{T_{over} - 15}{15} \rceil, & T_{over} > 15\text{ 分钟} \end{cases}$$
     $$\text{超时占位费} = N \times \text{单周期占位费 (如 5.00 元)}$$
   - **封顶保护**：系统支持设置单次占位费最高上限（如封顶 100 元）。

3. **订单总应付金额**：
   $$\text{订单总额} = \text{基础充电电费} + \text{充电服务费} + \text{超时占位费}$$

---

### 1.6 资金安全与幂等性控制

- 充值、充电扣费、订单退款必须在请求头携带 `Idempotency-Key`（全局唯一 UUID / 雪花 ID）。
- 资金操作遵循 **DB First + 关系数据库行级排他锁 (`FOR UPDATE`)**，保证强 ACID 事务与防超卖、防并发冲突。

---

## 二、 充电用户端 API (Driver Client)

### 2.1 用户认证与个人中心

#### 1. 用户免密登录

- **接口路径**：`POST /api/v1/auth/login`
- **认证方式**：公开接口 (无需 Token)
- **业务逻辑**：根据 11 位手机号校验。若用户已存在则直接返回登录凭证；若用户不存在，**返回 `10001` 业务错误码（`User not found`），不再自动注册**。
- **请求参数**：
  - Request Headers: `Content-Type: application/json`
  - Request Body:
    ```json
    {
      "phone": "13800138000",
      "auth_type": "passwordless"
    }
    ```
- **成功响应 (`200 OK`)**：
  ```json
  {
    "code": 0,
    "msg": "success",
    "data": {
      "user_id": 10001,
      "phone": "13800138000",
      "nickname": "用户8000",
      "balance": 0.00,
      "balance_cents": 0,
      "is_new_user": false,
      "access_token": "mock_user_token_abc123",
      "refresh_token": "mock_refresh_token_999",
      "role": "user",
      "expires_in": 120
    },
    "timestamp": 1772607600000
  }
  ```
- **异常响应**：
  - `404 Not Found` (`code: 10001`)：`"User not found"` (用户不存在)
  - `403 Forbidden` (`code: 10002`)：`"User account frozen, operations restricted"`
  - `400 Bad Request` (`code: 10003`)：`"Invalid phone format: must be 11 digits"`

---

#### 2. 账号注册与自动登录

- **接口路径**：`POST /api/v1/auth/register`
- **认证方式**：公开接口
- **业务逻辑**：校验手机号在数据库中的唯一性。若手机号已注册，返回 `10005` 错误码；若未注册则创建新用户与初始钱包，**注册成功后视作自动登录，直接返回包含 Token 与用户信息的完整登录数据包**。
- **请求参数**：
  - Request Body:
    ```json
    {
      "phone": "13800138000",
      "password": "user_secure_password_hash",
      "nickname": "极速车手"
    }
    ```
- **成功响应 (`200 OK`)**：
  ```json
  {
    "code": 0,
    "msg": "success",
    "data": {
      "user_id": 10001,
      "phone": "13800138000",
      "nickname": "极速车手",
      "balance": 0.00,
      "balance_cents": 0,
      "is_new_user": true,
      "access_token": "EV_TOKEN.10001.user.xxxx",
      "refresh_token": "EV_TOKEN.10001.user.yyyy",
      "role": "user",
      "expires_in": 120
    },
    "timestamp": 1772607600000
  }
  ```
- **异常响应**：
  - `409 Conflict` (`code: 10005`)：`"Phone number already registered"` (手机号已被占用)
  - `400 Bad Request` (`code: 10003`)：`"Invalid phone format: must be 11 digits"`

---

#### 3. 手机号 + 密码登录 (新增)

- **接口路径**：`POST /api/v1/auth/login-password` (兼容 `POST /api/v1/auth/login/password`)
- **认证方式**：公开接口
- **业务逻辑**：根据 11 位手机号与密码进行验证。用户不存在返回 `10001`，密码错误返回 `10004`，账号冻结返回 `10002`。验证通过后返回完整登录凭证。
- **请求参数**：
  - Request Body:
    ```json
    {
      "phone": "13800138000",
      "password": "user_secure_password_hash"
    }
    ```
- **成功响应 (`200 OK`)**：
  ```json
  {
    "code": 0,
    "msg": "success",
    "data": {
      "user_id": 10001,
      "phone": "13800138000",
      "nickname": "极速车手",
      "balance": 50.00,
      "balance_cents": 5000,
      "is_new_user": false,
      "access_token": "EV_TOKEN.10001.user.xxxx",
      "refresh_token": "EV_TOKEN.10001.user.yyyy",
      "role": "user",
      "expires_in": 120
    },
    "timestamp": 1772607600000
  }
  ```
- **异常响应**：
  - `404 Not Found` (`code: 10001`)：`"User not found"`
  - `401 Unauthorized` (`code: 10004`)：`"Incorrect password"`
  - `403 Forbidden` (`code: 10002`)：`"User account frozen"`

---

#### 4. 用户修改密码 (新增)

- **接口路径**：`POST /api/v1/user/password` (需要 Token 鉴权，亦兼容公开接口 `POST /api/v1/auth/change-password`)
- **认证方式**：需要 User Token 鉴权 (`Authorization: Bearer <access_token>`)
- **业务逻辑**：校验旧密码是否正确，验证通过后将用户密码更新为新密码，并主动失效用户缓存。
- **请求参数**：
  - Request Body:
    ```json
    {
      "old_password": "current_password",
      "new_password": "new_secure_password"
    }
    ```
- **成功响应 (`200 OK`)**：
  ```json
  {
    "code": 0,
    "msg": "success",
    "data": {},
    "timestamp": 1772607600000
  }
  ```
- **异常响应**：
  - `401 Unauthorized` (`code: 10004`)：`"Incorrect old password"`
  - `403 Forbidden` (`code: 10002`)：`"User account frozen"`

---

#### 5. 手机号是否已注册快速核验 (无需 Token)

- **接口路径**：`POST /api/v1/auth/check-phone` 与 `GET /api/v1/auth/check-phone`
- **认证方式**：公开接口 (无需携带 Authorization Token)
- **业务逻辑**：用于用户注册、免密登录或页面表单失焦时，快速校验输入的 11 位手机号是否已经在系统中注册。未注册返回 `is_registered: false`，已注册返回 `true`。
- **请求参数**：
  - **GET 方式**：Query 参数 `?phone=13800138000`
  - **POST 方式**：
    - Request Body:
      ```json
      {
        "phone": "13800138000"
      }
      ```
- **成功响应 (`200 OK` - 已注册)**：
  ```json
  {
    "code": 0,
    "msg": "success",
    "data": {
      "phone": "13800138000",
      "is_registered": true,
      "is_exists": true
    },
    "timestamp": 1772607600000
  }
  ```
- **成功响应 (`200 OK` - 未注册)**：
  ```json
  {
    "code": 0,
    "msg": "success",
    "data": {
      "phone": "13800138000",
      "is_registered": false,
      "is_exists": false
    },
    "timestamp": 1772607600000
  }
  ```
- **异常响应**：
  - `400 Bad Request` (`code: 10003`)：`"Invalid phone format: must be 11 digits"` (非11位手机号)

---

#### 6. 刷新 Access Token

- **接口路径**：`POST /api/v1/auth/refresh`
- **认证方式**：公开接口
- **请求参数**：
  
  - Request Body:
    ```json
    {
      "refresh_token": "mock_refresh_token_999"
    }
    ```
- **成功响应 (`200 OK`)**：
  ```json
  {
    "code": 0,
    "msg": "success",
    "data": {
      "access_token": "mock_user_token_new_refreshed",
      "expires_in": 120
    },
    "timestamp": 1772607600000
  }
  ```

---

#### 7. 查询当前用户个人资料

- **接口路径**：`GET /api/v1/user/profile`
- **认证方式**：`Bearer <user_token>`
- **成功响应 (`200 OK`)**：
  
  ```json
  {
    "code": 0,
    "msg": "success",
    "data": {
      "user_id": 10001,
      "phone": "13800138000",
      "nickname": "极速车主_10001",
      "avatar_url": "http://server:8080/static/avatars/user_10001.png",
      "balance": 168.50,
      "balance_cents": 16850,
      "frozen_amount": 0.00,
      "frozen_cents": 0,
      "status": 1,
      "status_desc": "NORMAL",
      "has_active_order": false,
      "created_at": 1772600000000
    },
    "timestamp": 1772607600000
  }
  ```

---

#### 8. 修改个人基本信息 (修改昵称)

- **接口路径**：`PUT /api/v1/user/profile`
- **认证方式**：`Bearer <user_token>`
- **请求参数**：
  - Request Body:
    ```json
    {
      "nickname": "特斯拉超充玩家"
    }
    ```
- **成功响应 (`200 OK`)**：
  ```json
  {
    "code": 0,
    "msg": "success",
    "data": {
      "user_id": 10001,
      "nickname": "特斯拉超充玩家",
      "updated_at": 1772607600000
    },
    "timestamp": 1772607600000
  }
  ```

---

#### 9. 上传 / 更换用户真实头像文件

- **接口路径**：`POST /api/v1/user/avatar`
- **认证方式**：`Bearer <user_token>`
- **请求参数**：
  - Request Headers:
    - `Content-Type: image/png` (或 `image/jpeg`、`image/webp`、`multipart/form-data`)
  - Request Body: 真实头像原始二进制文件流（**严格限制文件体积 < 1MB / 1,048,576 字节**）
- **业务逻辑**：
  - 校验图片体积，若大于等于 1MB 返回 `413 Payload Too Large`；若为空返回 `400 Bad Request`；
  - 自动识别图像 MIME 类型，并将真实文件二进制流持久化写入 PostgreSQL 数据库 `user_avatars` 存储表；
  - 同步载入服务端高速内存缓存，使后续查询极速响应。
- **成功响应 (`200 OK`)**：
  ```json
  {
    "code": 0,
    "msg": "success",
    "data": {
      "user_id": 10001,
      "content_type": "image/png",
      "file_size": 24576,
      "updated_at": 1772607600000
    },
    "timestamp": 1772607600000
  }
  ```
- **异常响应**：
  - `413 Payload Too Large` (`code: 10007`)：`"Avatar file size exceeds 1MB limit"` (头像文件大小超过1MB限制)
  - `400 Bad Request` (`code: 10003`)：`"Avatar file content is empty or invalid"` (文件为空或数据损坏)
  - `401 Unauthorized` (`code: 10004`)：`"Missing or invalid access token"`

---

#### 10. 获取当前用户真实头像文件 (新增)

- **接口路径**：`GET /api/v1/user/avatar`
- **认证方式**：`Bearer <user_token>` (必填)
- **业务逻辑**：
  - 从数据库与服务端内存缓存中获取当前登录用户的真实头像二进制文件；
  - 若用户从未上传过头像，系统返回 `404 Not Found`（业务码 `10006`，不会为 2 万名虚拟生成用户预存头像）；
  - **高性能传输与缓存协商**：支持 HTTP 标准协商缓存，服务端返回 `ETag`。客户端在后续请求携带 `If-None-Match` 时，若头像未发生变动，直接返回 `304 Not Modified`（0 流量传输），最大化节约带宽与提升前端加载性能。
- **成功响应 (`200 OK`)**：
  - Response Headers:
    - `Content-Type: image/png` (或用户上传的实际图片格式)
    - `Content-Length: 24576`
    - `ETag: "av_10001_1772607600000"`
    - `Cache-Control: private, no-cache`
  - Response Body: 头像图片的真实原始二进制数据流
- **协商缓存响应 (`304 Not Modified`)**：
  - 客户端携带 `If-None-Match: "av_10001_1772607600000"` 命中缓存时返回，无 Body。
- **异常响应**：
  - `404 Not Found` (`code: 10006`)：`"User avatar not found"` (该用户尚未上传过个人头像)
  - `401 Unauthorized` (`code: 10004`)：`"Missing or invalid access token"`

---

### 2.2 钱包账户与资金交易

#### 1. 查询钱包余额
- **接口路径**：`GET /api/v1/wallet/balance`
- **认证方式**：`Bearer <user_token>`
- **成功响应 (`200 OK`)**：
  ```json
  {
    "code": 0,
    "msg": "success",
    "data": {
      "user_id": 10001,
      "balance": 168.50,
      "balance_cents": 16850,
      "frozen_amount": 0.00,
      "frozen_cents": 0,
      "available_amount": 168.50,
      "currency": "CNY"
    },
    "timestamp": 1772607600000
  }
  ```

---

#### 2. 用户钱包充值 (模拟支付)
- **接口路径**：`POST /api/v1/wallet/recharge`
- **认证方式**：`Bearer <user_token>`
- **请求参数**：
  - Request Headers:
    - `Idempotency-Key: REC-UUID-20260902-10001-001` (必填)
  - Request Body:
    ```json
    {
      "amount": 100.00,
      "amount_cents": 10000,
      "payment_method": "MOCK_PAY",
      "remark": "客户端钱包充值100元"
    }
    ```
- **成功响应 (`200 OK`)**：
  ```json
  {
    "code": 0,
    "msg": "success",
    "data": {
      "transaction_id": "TX_REC_20260902143000_10001",
      "user_id": 10001,
      "amount": 100.00,
      "balance_before": 168.50,
      "new_balance": 268.50,
      "status": "SUCCESS",
      "created_at": 1772607600000
    },
    "timestamp": 1772607600000
  }
  ```

---

#### 3. 查询钱包资金变动流水明细
- **接口路径**：`GET /api/v1/user/wallet/transactions`
- **认证方式**：`Bearer <user_token>`
- **查询参数 (Query Params)**：
  - `page` (可选, 默认 `1`): 页码
  - `page_size` (可选, 默认 `20`): 每页数量
  - `flow_type` (可选): `1`-充值, `2`-充电扣费, `3`-充电退补, `4`-管理员调账
- **成功响应 (`200 OK`)**：
  ```json
  {
    "code": 0,
    "msg": "success",
    "data": {
      "total": 3,
      "page": 1,
      "page_size": 20,
      "records": [
        {
          "transaction_id": "TX_RF_20260902150000_10001",
          "flow_type": 3,
          "flow_type_desc": "REFUND",
          "amount": 55.40,
          "amount_cents": 5540,
          "balance_before": 213.10,
          "balance_after": 268.50,
          "related_order_id": "ORD_20260902_1001",
          "remark": "充电故障订单管理员全额退款",
          "created_at": 1772609900000
        },
        {
          "transaction_id": "TX_REC_20260902143000_10001",
          "flow_type": 1,
          "flow_type_desc": "RECHARGE",
          "amount": 100.00,
          "amount_cents": 10000,
          "balance_before": 168.50,
          "balance_after": 268.50,
          "related_order_id": "",
          "remark": "客户端钱包充值100元",
          "created_at": 1772607600000
        }
      ]
    },
    "timestamp": 1772607600000
  }
  ```

---

### 2.3 充电站与充电桩综合查询 (全平台统一接口规范)

> **接口精简与架构重构说明**：  
> 1. **充电站查询统一**：原有的 `GET /api/v1/stations/nearby`（附近粗筛）、`GET /api/v1/stations/district`（行政区查询）以及 `GET /api/v1/admin/stations`（管理端电站列表）三个接口已全量废弃并删除，合并升级为统一且高效的 **`GET /api/v1/stations/inquire`**。单站信息由 **`GET /api/v1/stations/{station_id}`** 提供，其返回格式与 `inquire` 严格对齐，移除了冗余的桩位数组。  
> 2. **充电桩查询统一**：原有的 `GET /api/v1/admin/piles`（管理端桩查询）接口已废弃并删除，合并升级为用户端与管理端统一通用的 **`GET /api/v1/piles`** 接口。该接口支持按电站 ID、快慢充类型、全量 8 种实时桩状态多维筛选，并与内存实时遥测状态池和预约锁闭状态 100% 动态同步。  
> 
> 平台服务端标准化查询接口如下：
> 1. `GET /api/v1/stations/inquire`：电站多维综合查询（名称模糊、行政区限定、经纬度距离排序、严格分页）
> 2. `GET /api/v1/stations/{station_id}`：单站卡片信息查询（格式与 inquire 严格对齐）
> 3. `GET /api/v1/piles`：充电桩综合分页查询（全网/单站、快慢充类型、8 种状态多格式兼容与实时同步）

---

#### 1. 充电站综合多维检索 (统一替代附近、行政区与管理端列表查询)
- **接口路径**：`GET /api/v1/stations/inquire`
- **认证方式**：`Bearer <token>` (必填，普通用户与管理员均可调用)
- **查询参数 (Query Params)**：
  - `name` (可选, 字符串): 充电站名称模糊筛选关键字（不区分大小写，支持中英文子串匹配，如 `"特来电"`, `"中关村"`）
  - `district` (可选, 字符串): 北京市 16 个行政区限定查询，支持行政区编码（`0` ~ `15`）或行政区全称/简称（如 `"海淀区"`, `"海淀"`, `"朝阳区"`, `"朝阳"`）
    - 编码映射：0=东城区, 1=西城区, 2=朝阳区, 3=海淀区, 4=丰台区, 5=石景山区, 6=门头沟区, 7=房山区, 8=通州区, 9=顺义区, 10=昌平区, 11=大兴区, 12=怀柔区, 13=平谷区, 14=密云区, 15=延庆区
  - `latitude` (可选, 浮点数): 用户当前纬度 (例如 `39.904200`)
  - `longitude` (可选, 浮点数): 用户当前经度 (例如 `116.407400`)
  - `status` (可选, 整数/字符串): 充电站运营状态筛选。`1` / `"ONLINE"`（营业中/上线），`2` / `"OFFLINE"`（暂停营业/下线）
  - `fast_pile` (可选, 布尔值): 站点是否配置快充桩筛选。`true` / `1`（包含快充桩的站点），`false` / `0`（不包含快充桩的纯慢充站点）
  - `page` (可选, 默认 `1`, 最小 `1`): 当前分页页码
  - `page_size` (可选, 默认 `20`, 上限 `20`): 每页条数（**服务端强制截断上限为 20**，若传入大于 20 则按 20 返回）
- **参数约束与校验逻辑**：
  - **经纬度对等原则**：`latitude` 与 `longitude` 必须**同时提供或均不提供**。若仅提供其中一个，服务端直接拒绝并返回 `400 Bad Request`（业务码 `10003`，错误提示 `"Latitude and longitude must both be provided"`）。
- **结果排序规则**：
  - **坐标排序**：若提供了经纬度坐标，服务端自动计算球面距离 `distance_km`，并**按照距离由近到远升序排列**（近距离站点优先展现）；
  - **默认排序**：若未提供经纬度坐标（包括 `name`、`district`、`status`、`fast_pile`、`latitude`、`longitude` 四个可选参数均未提供），服务端一律**按照电站 `station_id` / `id` 升序排列**（从 1 开始递增返回）。
- **高性能架构设计**：
  - 基于全北京市 8,569 座真实充电站编译期静态常量与 2D 空间几何索引，微秒级在内存中完成全维度（名称、行政区、在线状态、快充桩能力）过滤与排序；
  - 仅对当前分页窗口内的至多 20 个站点聚合实时桩位状态池（总桩数、空闲数、快慢充分布）与持久化差异电价，杜绝全表遍历，查询延迟稳定在微秒至毫秒级。
- **成功响应 (`200 OK`)**：
  ```json
  {
    "code": 0,
    "msg": "success",
    "data": {
      "total": 841,
      "page": 1,
      "page_size": 20,
      "stations": [
        {
          "station_id": 5130,
          "id": 5130,
          "station_name": "开迈斯充电站(北京中关村一桥地下超充站)",
          "district": "海淀区",
          "district_code": 3,
          "address": "中关村大街1号院地下停车场",
          "latitude": 39.982000,
          "longitude": 116.315000,
          "distance_km": 0.18,
          "price_per_kwh": 1.78,
          "service_fee_per_kwh": 0.35,
          "overtime_fee_per_15min": 5.00,
          "total_piles": 18,
          "pile_count": 18,
          "idle_piles": 14,
          "available_count": 14,
          "fast_piles_idle": 12,
          "slow_piles_idle": 2,
          "has_fast_pile": true,
          "is_online": true
        },
        {
          "station_id": 3442,
          "id": 3442,
          "station_name": "特来电充电站(北京侨福芳草地购物中心站)",
          "district": "朝阳区",
          "district_code": 2,
          "address": "东大桥路9号侨福芳草地地下B2层D区",
          "latitude": 39.918500,
          "longitude": 116.448000,
          "distance_km": 1.25,
          "price_per_kwh": 1.71,
          "service_fee_per_kwh": 0.35,
          "overtime_fee_per_15min": 5.00,
          "total_piles": 26,
          "pile_count": 26,
          "idle_piles": 20,
          "available_count": 20,
          "fast_piles_idle": 14,
          "slow_piles_idle": 6,
          "has_fast_pile": true,
          "is_online": true
        }
      ]
    },
    "timestamp": 1772607600000
  }
  ```
- **异常响应**：
  - `400 Bad Request` (`code: 10003`)：`"Latitude and longitude must both be provided"` (经纬度仅提供单边)
  - `401 Unauthorized` (`code: 40001`)：`"Missing or invalid access token"` (未携带认证 Token)

---

#### 2. 查询指定充电站详情

- **接口路径**：`GET /api/v1/stations/{station_id}`
- **认证方式**：公开接口 / `Bearer <token>` 均可
- **路径参数**：
  - `station_id` (必填, 整数): 电站唯一 ID (1 ~ 8565)
- **查询参数 (Query Params)**：
  - `latitude` (可选, 浮点数): 用户当前纬度，用于动态计算用户与该电站的距离 `distance_km`
  - `longitude` (可选, 浮点数): 用户当前经度，用于动态计算用户与该电站的距离 `distance_km`
- **业务逻辑与模型规范**：
  - **返回值严格对齐电站卡片结构**：彻底剥离全部桩位数组（`piles`）、电站客服座机（`contact_phone`）、营业时间（`operating_hours`）、超时宽限期（`overtime_grace_minutes`），数据格式与 `inquire` 返回列表中每座电站的格式保持 100% 相同；
  - **差异化电价返回**：返回保存在数据库中的该站点独立基础电价 `price_per_kwh`（`1.15 ~ 1.85` 元/度区间）；
  - **实时可用数动态聚合**：返回该电站最新可用快充、慢充总数及在线运营状态。
- **成功响应 (`200 OK`)**：
  ```json
  {
    "code": 0,
    "msg": "success",
    "data": {
      "station_id": 3442,
      "id": 3442,
      "station_name": "特来电充电站(北京侨福芳草地购物中心站)",
      "district": "朝阳区",
      "district_code": 2,
      "address": "东大桥路9号侨福芳草地地下B2层D区",
      "latitude": 39.918500,
      "longitude": 116.448000,
      "distance_km": 1.25,
      "price_per_kwh": 1.71,
      "service_fee_per_kwh": 0.35,
      "overtime_fee_per_15min": 5.00,
      "total_piles": 26,
      "pile_count": 26,
      "idle_piles": 20,
      "available_count": 20,
      "fast_piles_idle": 14,
      "slow_piles_idle": 6,
      "has_fast_pile": true,
      "is_online": true
    },
    "timestamp": 1772607600000
  }
  ```
- **异常响应**：
  - `404 Not Found` (`code: 20001`)：`"Station not found"` (电站 ID 不存在)

---

#### 3. 充电桩综合多维检索与实时状态查询 (全平台统一桩位接口)
- **接口路径**：`GET /api/v1/piles`
- **认证方式**：`Bearer <token>` (必填，普通用户与管理员均可调用)
- **查询参数 (Query Params)**：
  - `station_id` (可选, 整数): 指定充电站 ID。
    - 若**不提供**：返回全平台所有充电桩列表，严格按 `pile_id ASC` 字典序递增分页返回；
    - 若**提供**：返回该充电站所属的充电桩列表（单站充电桩生成数量为 5~30 根）。
  - `page` (可选, 默认 `1`, 最小 `1`): 当前分页页码。
  - `page_size` (可选, 默认 `30`, 上限 `30`): 每页条数（根据电站充电桩生成规模规则，单站最多 30 根桩，服务端强制截断上限为 30，若传入大于 30 则按 30 返回）。
  - `status` (可选, 字符串或数字编码): 按充电桩实时状态过滤。支持系统全量 7 种有效状态，支持多种参数格式传入：
    - 状态全集覆盖（已彻底移除冗余的 MAINTENANCE 维护状态）：
      - `1` / `"IDLE"` / `"空闲"`：空闲可用（可直接启动充电或发起预约）
      - `2` / `"PREPARING"` / `"准备中"`：车辆已插枪准备中
      - `3` / `"CHARGING"` / `"充电中"`：正在充电中
      - `4` / `"FINISHING"` / `"充电完成"`：充电结束待拔枪
      - `5` / `"FAULT"` / `"故障"`：桩机故障停用
      - `7` / `"OFFLINE"` / `"离线"`：下线停用、通讯离线或电站整体下线
      - `8` / `"RESERVED"` / `"已预约"` / `"已预约锁定"`：已被用户预约锁定
  - `type` (可选, 字符串或数字编码): 按充电桩类型过滤。支持快充与慢充：
    - 快充：`"FAST"`、`"快充"`、`1`
    - 慢充：`"SLOW"`、`"慢充"`、`2`
- **业务逻辑与实时状态同步**：
  - **动态状态同步**：接口返回的充电桩状态与内存实时遥测状态池（`ChargingStatePool`）以及预约锁定表实时联动。若桩位被用户预约（状态变为 `RESERVED`）或进入充电（状态变为 `CHARGING`），查询结果和空闲过滤即刻精确反映最新动态，杜绝脏读。
  - **电站下线级联感知**：若指定充电站处于下线状态（`is_online = false`），则查询该电站下所有充电桩一律呈现为 `OFFLINE`（状态码 7），空闲可用桩数统计自动降为 0；在电站恢复上线后，各个充电桩将保真恢复至下线前的各自独立状态（`IDLE`、`FAULT`、`OFFLINE`）。
  - **返回格式对齐**：返回格式与原 `/api/v1/admin/piles` 保持 100% 格式对齐，兼容前端现有字段解析。
- **成功响应 (`200 OK`)**：
  ```json
  {
    "code": 0,
    "msg": "success",
    "data": {
      "total": 25,
      "page": 1,
      "page_size": 30,
      "piles": [
        {
          "pile_id": "P00001_01",
          "station_id": 1,
          "pile_name": "清华大学科技园P+R停车充电站-01号桩",
          "type": "FAST",
          "power_kw": 120.0,
          "status": "IDLE",
          "status_code": 1,
          "status_desc": "空闲可用",
          "current_status": "IDLE",
          "current_status_code": 1,
          "voltage_v": 0.0,
          "current_a": 0.0,
          "soc_pct": 0,
          "total_charge_count": 0,
          "total_charge_hours": 0.0,
          "last_heartbeat_at": 1772607600000
        }
      ]
    },
    "timestamp": 1772607600000
  }
  ```
- **异常响应**：
  - `401 Unauthorized` (`code: 40001`)：`"Unauthorized: missing or invalid authentication token"` (未携带认证 Token 或已失效)

---

### 2.4 充电核心业务流程 (检查-启动-停止-结算)

#### 1. 充电前状态检查 (是否有未完成订单)
- **接口路径**：`GET /api/v1/charging/active-order`
- **认证方式**：`Bearer <user_token>`
- **业务逻辑**：车主点击“进入充电”或扫码前，客户端调用此接口检查是否存在未结算/进行中的订单。如有未完成订单，客户端必须强制提示并跳转至结算页。
- **成功响应 (`200 OK` - 无未完成订单)**：
  ```json
  {
    "code": 0,
    "msg": "success",
    "data": {
      "has_active_order": false,
      "active_order": null
    },
    "timestamp": 1772607600000
  }
  ```
- **成功响应 (`200 OK` - 存在未结算订单)**：
  ```json
  {
    "code": 0,
    "msg": "success",
    "data": {
      "has_active_order": true,
      "active_order": {
        "order_id": "ORD_20260902_1001",
        "station_id": 101,
        "station_name": "东软高新科技园超级充电站",
        "pile_id": "P10101",
        "order_status": "CHARGING",
        "start_time": 1772604000000,
        "charged_energy_kwh": 18.50,
        "current_cost": 33.30,
        "soc": 65
      }
    },
    "timestamp": 1772607600000
  }
  ```

---

#### 2. 开始充电 / 启动插枪
- **接口路径**：`POST /api/v1/charging/start`
- **认证方式**：`Bearer <user_token>`
- **请求参数**：
  - Request Headers:
    - `Idempotency-Key: CHG-START-UUID-20260902-10001-01`
  - Request Body:
    ```json
    {
      "pile_id": "P10101",
      "strategy_type": "FULL",
      "strategy_value": 0,
      "pre_freeze_amount": 50.00
    }
    ```
    - `strategy_type` 可选值：
      - `"FULL"`: 充满自停 (`strategy_value: 0`)
      - `"MONEY"`: 按金额充 (`strategy_value: 50.00`)
      - `"ENERGY"`: 按度数充 (`strategy_value: 30.0`)
      - `"TIME"`: 按分钟充 (`strategy_value: 60`)
- **成功响应 (`200 OK`)**：
  ```json
  {
    "code": 0,
    "msg": "success",
    "data": {
      "order_id": "ORD_20260902_1001",
      "pile_id": "P10101",
      "station_id": 101,
      "station_name": "东软高新科技园超级充电站",
      "order_status": "CHARGING",
      "start_time": 1772607600000,
      "initial_soc": 20,
      "unit_price": 1.45,
      "service_price": 0.35,
      "overtime_fee_per_15min": 5.00,
      "ws_telemetry_url": "ws://server:8080/ws/v1/charging/ORD_20260902_1001"
    },
    "timestamp": 1772607600000
  }
  ```
- **异常响应**：
  - `409 Conflict` (`code: 20005`)：`"Active charging order exists"`
  - `409 Conflict` (`code: 20003`)：`"Charging pile is busy or occupied"`
  - `422 Unprocessable Entity` (`code: 30001`)：`"Insufficient wallet balance, please recharge first"`

> **注意**：启动成功后，客户端通过返回的 `ws_telemetry_url` 建立 **WebSocket 长连接**（参见第 4.1 节），实时接收电压、电流、功率、SOC、电费及充满后的超时占位费推送，**不使用 HTTP 轮询**。
> 
> **预约车主到场核销机制 (押金全额退回)**：
> 若用户此前预约了目标充电桩（桩状态为 `RESERVED` 且由当前账号锁定），相同账号在该桩调用此接口开始充电时视作**车主按约到场**：
> 1. 系统首先自动触发履约核销，**将此前预约时支付的 20 元押金全额原路退还至用户钱包余额**，生成财务流水（`flow_type: 6, RESERVATION_REFUND`）；
> 2. 随后校验钱包余额门槛（$\ge 20$ 元）并无缝转入正式充电流程；
> 3. 内存状态池与数据库中的桩位状态从 `RESERVED` 变更为 `CHARGING` 并全网广播。

---

#### 3. 结束充电 / 主动拔枪
- **接口路径**：`POST /api/v1/charging/stop`
- **认证方式**：`Bearer <user_token>`
- **请求参数**：
  - Request Body:
    ```json
    {
      "order_id": "ORD_20260902_1001",
      "stop_reason": "USER_MANUAL_STOP"
    }
    ```
- **成功响应 (`200 OK`)**：
  ```json
  {
    "code": 0,
    "msg": "success",
    "data": {
      "order_id": "ORD_20260902_1001",
      "order_status": "UNSETTLED",
      "end_time": 1772609400000,
      "duration_seconds": 1800,
      "charged_energy_kwh": 30.78,
      "final_soc": 100,
      "electricity_fee": 44.63,
      "service_fee": 10.77,
      "overtime_minutes": 25,
      "overtime_fee": 5.00,
      "total_amount": 60.40,
      "total_amount_cents": 6040,
      "need_settle": true
    },
    "timestamp": 1772609400000
  }
  ```

---

#### 4. 订单结算与钱包扣费
- **接口路径**：`POST /api/v1/charging/settle`
- **认证方式**：`Bearer <user_token>`
- **请求参数**：
  
  - Request Headers:
    - `Idempotency-Key: SETTLE-ORD_20260902_1001`
  - Request Body:
    ```json
    {
      "order_id": "ORD_20260902_1001"
    }
    ```
- **成功响应 (`200 OK`)**：
  ```json
  {
    "code": 0,
    "msg": "success",
    "data": {
      "order_id": "ORD_20260902_1001",
      "order_status": "COMPLETED",
      "electricity_fee": 44.63,
      "service_fee": 10.77,
      "overtime_fee": 5.00,
      "total_fee": 60.40,
      "total_fee_cents": 6040,
      "wallet_deducted": 60.40,
      "new_balance": 208.10,
      "new_balance_cents": 20810,
      "settled_at": 1772609420000
    },
    "timestamp": 1772609420000
  }
  ```

---

#### 5. 预约空闲充电桩 (预扣 20 元押金锁定)

- **接口路径**：`POST /api/v1/charging/reserve` (亦兼容 `POST /api/v1/charging/reservation`)
- **认证方式**：`Bearer <user_token>`
- **业务规则**：
  1. 只能预约当前处于空闲状态 (`IDLE`) 且电站在线的充电桩；
  2. 预约时从用户钱包扣除 **20.00 元 (2000 分) 预约押金**，生成财务流水 (`flow_type: 5, RESERVATION_DEPOSIT`)；
  3. 预约成功后，该电桩进入 `RESERVED` (状态码 `8`，"已预约锁定") 状态，他人无法使用或再次预约；
  4. 预约保留时长为 **2 分钟 (120 秒)**，提供倒计时；
  5. 若相同账号在到期前于该电桩启动充电，视同按约到场，**20 元押金全额退回钱包**；
  6. 若超时 (超过 120 秒) 未到场，后台定时推演引擎自动**没收全部 20 元押金**，将预约单置为 `TIMEOUT`，并将充电桩自动恢复为空闲 `IDLE` 供他人使用。
- **请求参数**：
  - Request Body:
    ```json
    {
      "pile_id": "P00001_01"
    }
    ```
- **成功响应 (`200 OK`)**：
  ```json
  {
    "code": 0,
    "msg": "success",
    "data": {
      "reservation_id": "RES_1772607600000_10001",
      "pile_id": "P00001_01",
      "station_id": 1,
      "station_name": "科学园超级充电站",
      "deposit": 20.00,
      "deposit_cents": 2000,
      "status": "ACTIVE",
      "created_at": 1772607600000,
      "expire_at": 1772607720000,
      "timeout_seconds": 120,
      "wallet_balance": 80.00,
      "wallet_balance_cents": 8000
    },
    "timestamp": 1772607600000
  }
  ```
- **异常响应**：
  - `409 Conflict` (`code: 20003`)：`"Charging pile is not idle"` (电桩已被占用或被他人预约)
  - `409 Conflict` (`code: 20005`)：`"You already have an active pile reservation"` (已有进行中预约或订单)
  - `422 Unprocessable Entity` (`code: 30001`)：`"Insufficient wallet balance, please recharge"` (钱包余额不足 20 元)

---

#### 6. 主动取消充电桩预约 (扣除 5 元手续费，退还 15 元押金)

- **接口路径**：`POST /api/v1/charging/cancel-reservation` (亦兼容 `POST /api/v1/charging/reservation/cancel`)
- **认证方式**：`Bearer <user_token>`
- **业务规则**：
  - 用户在 2 分钟倒计时内可主动取消预约；
  - 扣除 **5.00 元违约手续费**，剩余 **15.00 元原路退还至用户钱包余额**；
  - 生成押金退还流水 (`flow_type: 6, RESERVATION_REFUND`)；
  - 预约单状态置为 `CANCELLED`，充电桩立即解除锁定恢复为空闲 `IDLE`。
- **请求参数**：
  - Request Body (reservation_id 可选，若留空则自动取消当前用户的生效中预约):
    ```json
    {
      "reservation_id": "RES_1772607600000_10001"
    }
    ```
- **成功响应 (`200 OK`)**：
  ```json
  {
    "code": 0,
    "msg": "success",
    "data": {
      "reservation_id": "RES_1772607600000_10001",
      "pile_id": "P00001_01",
      "status": "CANCELLED",
      "deposit": 20.00,
      "deposit_cents": 2000,
      "penalty_fee": 5.00,
      "penalty_fee_cents": 500,
      "refund_amount": 15.00,
      "refund_amount_cents": 1500,
      "new_balance": 95.00,
      "new_balance_cents": 9500,
      "cancelled_at": 1772607630000
    },
    "timestamp": 1772607630000
  }
  ```
- **异常响应**：
  - `404 Not Found` (`code: 20006`)：`"No active reservation found"` (无有效预约单)
  - `422 Unprocessable Entity` (`code: 20008`)：`"Order cannot be stopped or already expired"`

---

#### 7. 查询当前生效中的预约状态 (含倒计时)

- **接口路径**：`GET /api/v1/charging/active-reservation` (亦兼容 `GET /api/v1/charging/reservation/active`)
- **认证方式**：`Bearer <user_token>`
- **成功响应 (`200 OK` - 存在有效预约)**：
  
  ```json
  {
    "code": 0,
    "msg": "success",
    "data": {
      "has_active_reservation": true,
      "active_reservation": {
        "reservation_id": "RES_1772607600000_10001",
        "user_id": 10001,
        "station_id": 1,
        "station_name": "科学园超级充电站",
        "pile_id": "P00001_01",
        "pile_name": "1号直流快充桩",
        "pile_type": "FAST",
        "deposit": 20.00,
        "deposit_cents": 2000,
        "status": "ACTIVE",
        "created_at": 1772607600000,
        "expire_at": 1772607720000,
        "remaining_seconds": 95
      }
    },
    "timestamp": 1772607625000
  }
  ```
- **成功响应 (`200 OK` - 无有效预约)**：
  ```json
  {
    "code": 0,
    "msg": "success",
    "data": {
      "has_active_reservation": false,
      "active_reservation": null
    },
    "timestamp": 1772607600000
  }
  ```

---

### 2.5 历史订单与账单查询

#### 1. 查询当前用户历史充电订单列表
- **接口路径**：`GET /api/v1/orders/my`
- **认证方式**：`Bearer <user_token>`
- **查询参数 (Query Params)**：
  - `page` (可选, 默认 `1`)
  - `page_size` (可选, 默认 `10`)
  - `status` (可选): `CHARGING`, `UNSETTLED`, `COMPLETED`, `REFUNDED`
  - `sort_order` (可选, 默认 `desc`): `asc` (按时间正序), `desc` (按时间倒序)
- **成功响应 (`200 OK`)**：
  
  ```json
  {
    "code": 0,
    "msg": "success",
    "data": {
      "total": 1,
      "page": 1,
      "page_size": 10,
      "orders": [
        {
          "order_id": "ORD_20260902_1001",
          "station_id": 101,
          "station_name": "东软高新科技园超级充电站",
          "pile_id": "P10101",
          "order_status": "COMPLETED",
          "start_time": 1772607600000,
          "end_time": 1772609400000,
          "duration_minutes": 30,
          "charged_energy_kwh": 30.78,
          "overtime_minutes": 25,
          "overtime_fee": 5.00,
          "total_fee": 60.40,
          "total_fee_cents": 6040
        }
      ]
    },
    "timestamp": 1772609500000
  }
  ```

---

#### 2. 查询特定订单计费详情
- **接口路径**：`GET /api/v1/charging/orders/{order_id}`
- **认证方式**：`Bearer <user_token>`
- **成功响应 (`200 OK`)**：
  
  ```json
  {
    "code": 0,
    "msg": "success",
    "data": {
      "order_id": "ORD_20260902_1001",
      "user_id": 10001,
      "station_id": 101,
      "station_name": "东软高新科技园超级充电站",
      "pile_id": "P10101",
      "pile_type": "FAST",
      "order_status": "COMPLETED",
      "start_time": 1772607600000,
      "end_time": 1772609400000,
      "duration_seconds": 1800,
      "start_soc": 20,
      "end_soc": 100,
      "charged_energy_kwh": 30.78,
      "electricity_price": 1.45,
      "electricity_fee": 44.63,
      "service_price": 0.35,
      "service_fee": 10.77,
      "overtime_grace_minutes": 15,
      "overtime_duration_minutes": 25,
      "overtime_rate_per_15min": 5.00,
      "overtime_fee": 5.00,
      "total_amount": 60.40,
      "total_amount_cents": 6040,
      "stop_reason": "USER_MANUAL_STOP",
      "settled_at": 1772609420000
    },
    "timestamp": 1772609500000
  }
  ```

---

## 三、 PC 运营管理端 API (Admin Management)

### 3.1 管理员认证

#### 1. 管理员登录
- **接口路径**：`POST /api/v1/admin/auth/login`
- **认证方式**：公开接口
- **请求参数**：
  - Request Body:
    ```json
    {
      "account": "admin",
      "password": "admin_password"
    }
    ```
- **成功响应 (`200 OK`)**：
  ```json
  {
    "code": 0,
    "msg": "success",
    "data": {
      "admin_id": 99999,
      "username": "admin",
      "role": "admin",
      "access_token": "mock_admin_token_xyz888",
      "refresh_token": "mock_admin_refresh_999",
      "expires_in": 120
    },
    "timestamp": 1772607600000
  }
  ```

---

### 3.2 平台综合运营态势与销售业绩

#### 1. 运营核心指标看板 (大盘统计)
- **接口路径**：`GET /api/v1/admin/dashboard/summary`
- **认证方式**：`Bearer <admin_token>`
- **成功响应 (`200 OK`)**：
  
  ```json
  {
    "code": 0,
    "msg": "success",
    "data": {
      "today_revenue": 12850.60,
      "today_revenue_cents": 1285060,
      "month_revenue": 348920.00,
      "month_revenue_cents": 34892000,
      "total_revenue": 2189400.50,
      "total_revenue_cents": 218940050,
      "today_energy_kwh": 8750.40,
      "today_order_count": 312,
      "total_user_count": 5280,
      "active_charging_sessions": 48
    },
    "timestamp": 1772607600000
  }
  ```

---

#### 2. 全平台销售业绩与营收趋势分析
- **接口路径**：`GET /api/v1/admin/dashboard/revenue-trend`
- **认证方式**：`Bearer <admin_token>`
- **查询参数 (Query Params)**：
  - `days` (可选, 默认 `7`): `7` (近7日) 或 `30` (近30日)
- **成功响应 (`200 OK`)**：
  ```json
  {
    "code": 0,
    "msg": "success",
    "data": {
      "time_range": "LAST_7_DAYS",
      "dates": ["2026-08-27", "2026-08-28", "2026-08-29", "2026-08-30", "2026-08-31", "2026-09-01", "2026-09-02"],
      "revenue_series": [10240.5, 11500.0, 14200.8, 15800.0, 13400.2, 12900.0, 12850.6],
      "energy_kwh_series": [6900.0, 7800.2, 9500.4, 10600.0, 9100.5, 8700.0, 8750.4],
      "order_count_series": [250, 278, 340, 380, 315, 305, 312]
    },
    "timestamp": 1772607600000
  }
  ```

---

#### 3. 全网电桩健康与状态分布统计
- **接口路径**：`GET /api/v1/admin/dashboard/pile-status-overview`（别名兼容：`GET /api/v1/admin/dashboard/pile-status`）
- **认证方式**：`Bearer <admin_token>`
- **功能说明**：提供全网充电桩运行状态分布统计与健康在线率计算。准确区分硬件故障（`fault_count`）与下线维护（`offline_count`，包括电站下线级联所有电桩以及管理员手动下线的电桩），并实时计算在网可用在线率 `online_rate`（`(in_use + idle) / total * 100%`）。
- **成功响应 (`200 OK`)**：
  
  ```json
  {
    "code": 0,
    "msg": "success",
    "data": {
      "total_piles": 150888,
      "in_use_count": 35848,
      "in_use_percentage": 23.76,
      "idle_count": 107550,
      "idle_percentage": 71.28,
      "fault_count": 7490,
      "fault_percentage": 4.96,
      "offline_count": 0,
      "offline_percentage": 0.0,
      "online_rate": 95.04
    },
    "timestamp": 1772607600000
  }
  ```

---

### 3.3 充电站运维管理与单站销售分析 (上下线 & 销售统计)

> **接口说明**：  
> 原有的管理员分页查询电站接口 `GET /api/v1/admin/stations` 已废弃删除。管理员查询电站统一使用 **`GET /api/v1/stations/inquire`**（携带管理员 Token 即可完成站名模糊检索、行政区过滤、经纬度距离排序等全部功能）。  
> 本节保留单站销售统计与电站上下线运维管控接口。

---

#### 1. 查询指定充电站的当日 / 7天 / 一个月销售业绩情况
- **接口路径**：`GET /api/v1/admin/stations/{station_id}/sales-stats`
- **认证方式**：`Bearer <admin_token>`
- **查询参数 (Query Params)**：
  
  - `time_range` (必填): 可选 `today` (当日), `7d` (近7天), `30d` (近一个月/30天)
- **成功响应 (`200 OK` - 当日销售明细示例)**：
  ```json
  {
    "code": 0,
    "msg": "success",
    "data": {
      "station_id": 101,
      "station_name": "东软高新科技园超级充电站",
      "time_range": "today",
      "summary": {
        "total_revenue": 3250.80,
        "total_revenue_cents": 325080,
        "electricity_fee_total": 2420.50,
        "service_fee_total": 680.30,
        "overtime_fee_total": 150.00,
        "total_energy_kwh": 1820.5,
        "total_order_count": 56,
        "average_order_amount": 58.05
      },
      "timeline": {
        "time_slots": ["00:00", "04:00", "08:00", "12:00", "16:00", "20:00"],
        "revenue_series": [120.0, 80.5, 650.0, 980.3, 820.0, 600.0],
        "energy_series": [80.0, 50.2, 390.5, 560.8, 450.0, 289.0],
        "order_series": [2, 1, 12, 18, 14, 9]
      }
    },
    "timestamp": 1772607600000
  }
  ```
- **成功响应 (`200 OK` - 近7天/近30天销售示例)**：
  ```json
  {
    "code": 0,
    "msg": "success",
    "data": {
      "station_id": 101,
      "station_name": "东软高新科技园超级充电站",
      "time_range": "7d",
      "summary": {
        "total_revenue": 22450.00,
        "total_revenue_cents": 2245000,
        "electricity_fee_total": 16800.00,
        "service_fee_total": 4550.00,
        "overtime_fee_total": 1100.00,
        "total_energy_kwh": 12500.0,
        "total_order_count": 390,
        "average_order_amount": 57.56
      },
      "timeline": {
        "time_slots": ["2026-08-27", "2026-08-28", "2026-08-29", "2026-08-30", "2026-08-31", "2026-09-01", "2026-09-02"],
        "revenue_series": [2800.0, 3100.5, 3600.0, 3950.0, 3200.0, 2548.7, 3250.8],
        "energy_series": [1550.0, 1720.0, 2010.5, 2200.0, 1800.5, 1398.5, 1820.5],
        "order_series": [48, 52, 62, 68, 55, 49, 56]
      }
    },
    "timestamp": 1772607600000
  }
  ```

---

#### 2. 充电站上线 (恢复运营)

- **接口路径**：`POST /api/v1/admin/stations/{station_id}/online`
- **认证方式**：`Bearer <admin_token>`
- **路径参数**：
  - `station_id` (必填, 整数): 电站唯一 ID (1 ~ 8565)
- **业务逻辑**：
  - 将指定的静态电站在线状态原子性置为上线 (`is_online = true`)；
  - 自动将该电站下处于 `OFFLINE` 状态的所有充电桩恢复为 `IDLE` 空闲可用状态；
  - 返回电站最新状态。
- **成功响应 (`200 OK`)**：
  
  ```json
  {
    "code": 0,
    "msg": "success",
    "data": {
      "station_id": 5130,
      "status": 1,
      "is_online": true,
      "terminated_orders": 0,
      "message": "Station brought online successfully"
    },
    "timestamp": 1772607600000
  }
  ```

---

#### 3. 充电站下线 (故障停运 / 维护及在途订单同步终止结算)

- **接口路径**：`POST /api/v1/admin/stations/{station_id}/offline`
- **认证方式**：`Bearer <admin_token>`
- **路径参数**：
  - `station_id` (必填, 整数): 电站唯一 ID (1 ~ 8565)
- **业务逻辑**：
  - 将指定电站在线状态置为下线 (`is_online = false`)，电站是否下线单独原子存储；
  - **核心保障**：若下线时该电站下属的充电桩有用户正在充电（存在进行中的在途订单），服务端将**同步强制结束该订单**，自动按实际充电度数、时长计算电费与服务费，并**同步发起钱包扣款结算**；
  - 同步向 WebSocket 遥测流广播 `CHARGING_FINISHED`（结束原因为 `STATION_OFFLINE`）；
  - 同步将该电站所有充电桩状态切换为 `OFFLINE`（离线），防止后续用户发起充电；
  - 返回受影响并强制结单的订单数量。
- **成功响应 (`200 OK`)**：
  
  ```json
  {
    "code": 0,
    "msg": "success",
    "data": {
      "station_id": 5130,
      "status": 2,
      "is_online": false,
      "terminated_orders": 1,
      "message": "Station taken offline, active orders terminated and settled"
    },
    "timestamp": 1772607600000
  }
  ```

---

### 3.4 充电桩监控与远程管控 (CRUD & 远程指令)

#### 1. 分页查询全网充电桩列表 (接口已升级统一)

> **接口变更通知**：  
> 原有的管理端私有接口 `GET /api/v1/admin/piles` 已全面废弃并删除（调用返回 `404 Not Found`）。  
> 充电桩查询功能已统一升级为全平台通用的 **`GET /api/v1/piles`**（详见 [2.3.3 充电桩综合多维检索与实时状态查询](#3-充电桩综合多维检索与实时状态查询-全平台统一桩位接口)）。  
> 管理员携带管理员 Bearer Token 同样可完整调用该通用接口，支持 `station_id`、`page`、`page_size`（上限 30）、`type`、`status`（全量 8 种状态多格式映射）等筛选条件，实时同步遥测状态与预约锁定状态。

---

#### 2. 新增充电桩

- **接口路径**：`POST /api/v1/admin/piles`
- **认证方式**：`Bearer <admin_token>`
- **请求参数**：
  - Request Body:
    ```json
    {
      "pile_id": "P10105",
      "station_id": 101,
      "pile_name": "5号超级快充桩",
      "type": "FAST",
      "power_kw": 240.0,
      "gun_type": "国标2015"
    }
    ```
- **成功响应 (`200 OK`)**：
  ```json
  {
    "code": 0,
    "msg": "success",
    "data": {
      "pile_id": "P10105",
      "station_id": 101,
      "status": "IDLE"
    },
    "timestamp": 1772607600000
  }
  ```

---

#### 3. 远程下发重启指令 (模拟处理死机异常)
- **接口路径**：`POST /api/v1/admin/piles/{pile_id}/restart`
- **认证方式**：`Bearer <admin_token>`
- **业务约束**：
  - 若充电桩所属的充电站处于下线状态（`is_online = false`），服务端严格阻断重启指令，返回 HTTP 404，业务错误码 `20001 StationNotFound`，错误提示 `"充电站已下线，禁止重启充电桩"`。
- **请求参数**：
  - Request Body:
    ```json
    {
      "reason": "管理员远程处理遥测无响应"
    }
    ```
- **成功响应 (`200 OK`)**：
  ```json
  {
    "code": 0,
    "msg": "success",
    "data": {
      "pile_id": "P10104",
      "command": "REBOOT",
      "execution_status": "SUCCESS",
      "new_status": "IDLE",
      "message": "Remote reboot command executed successfully"
    },
    "timestamp": 1772607600000
  }
  ```

---

#### 4. 远程设备状态切换 (单桩下线停用 / 上线就绪)
- **接口路径**：`POST /api/v1/admin/piles/{pile_id}/set-status`
- **认证方式**：`Bearer <admin_token>`
- **业务约束与状态切换规则**：
  - **允许目标状态**：仅允许将充电桩设置为 **`OFFLINE`**（下线停用）或恢复为 **`IDLE`**（上线就绪）。若传入已被移除的 `MAINTENANCE` 或其他无效状态，服务端直接拒绝并返回 HTTP 400 (`code: 40003 InvalidJsonPayload` 或 `"Invalid target status. Only OFFLINE or IDLE are permitted"`)。
  - **所属电站下线保护**：若该充电桩所属的充电站处于下线状态，服务端**严格拒绝**修改其下任何充电桩状态，返回 HTTP 404，业务错误码 `20001 StationNotFound`，错误提示 `"充电站已下线，禁止修改其下充电桩状态"`。
  - **订单与预约优雅终止**：当管理员将充电桩置为 `OFFLINE` 时，若桩端存在进行中的充电订单，服务端将自动强制停止充电、计算电费与占位费并扣划钱包完成结算，同时广播 `CHARGING_FINISHED`；若存在进行中的预约，服务端将自动解除预约锁定并退还押金。
- **请求参数**：
  - Request Body (支持 `target_status` 或 `status` 字段):
    ```json
    {
      "target_status": "OFFLINE",
      "reason": "桩位定期绝缘检修与系统维护"
    }
    ```
- **成功响应 (`200 OK`)**：
  ```json
  {
    "code": 0,
    "msg": "success",
    "data": {
      "pile_id": "P10101",
      "previous_status": "IDLE",
      "current_status": "OFFLINE"
    },
    "timestamp": 1772607600000
  }
  ```

---

### 3.5 平台用户管理与风控处置

#### 1. 分页查询用户列表 (支持手机号模糊搜索)
- **接口路径**：`GET /api/v1/admin/users`
- **认证方式**：`Bearer <admin_token>`
- **查询参数 (Query Params)**：
  - `page` (可选, 默认 `1`)
  - `page_size` (可选, 默认 `10`)
  - `phone` (可选): 手机号模糊匹配
  - `status` (可选): `1`-正常, `2`-已冻结
- **成功响应 (`200 OK`)**：
  
  ```json
  {
    "code": 0,
    "msg": "success",
    "data": {
      "total": 2,
      "page": 1,
      "page_size": 10,
      "users": [
        {
          "user_id": 10001,
          "phone": "13800138000",
          "nickname": "极速车主_10001",
          "balance": 168.50,
          "balance_cents": 16850,
          "status": 1,
          "status_desc": "NORMAL",
          "created_at": 1772521200000
        },
        {
          "user_id": 10002,
          "phone": "13911112222",
          "nickname": "恶意占用车主",
          "balance": 5.00,
          "balance_cents": 500,
          "status": 2,
          "status_desc": "FROZEN",
          "created_at": 1772434800000
        }
      ]
    },
    "timestamp": 1772607600000
  }
  ```

---

#### 2. 用户账号冻结 / 解冻 (风控操作)
- **接口路径**：`PUT /api/v1/admin/users/{user_id}/status`
- **认证方式**：`Bearer <admin_token>`
- **业务逻辑**：
  - 更新目标用户的账号状态（`1`: 正常解冻, `2`: 风控冻结），并同步更新关联钱包账户状态 (`user_wallets.status`)；
  - **冻结即时失效保障**：当账号被置为 `status = 2` (冻结) 时：
    1. 对应账号当前所有的 Access Token 与 Refresh Token **立即失效**，任何后续接口请求携带该 Token 均即时阻断，返回 HTTP 403 Forbidden（业务码 `10002 UserAccountFrozen`）；
    2. 系统在内存中吊销该用户冻结前签发的所有 Token（记录吊销时间戳）。即便未来解除冻结，旧 Token 亦永久失效（返回 HTTP 401 Unauthorized），强制用户必须重新登录；
    3. 冻结账号发起任何登录请求（快捷免密登录、手机号密码登录、管理员登录）直接被拒绝，返回 HTTP 403 Forbidden（业务码 `10002 UserAccountFrozen`）。
- **请求参数**：
  - Request Body:
    ```json
    {
      "status": 2,
      "reason": "涉嫌频繁占用车位未充电，风控冻结"
    }
    ```
- **成功响应 (`200 OK`)**：
  ```json
  {
    "code": 0,
    "msg": "success",
    "data": {
      "user_id": 10002,
      "status": 2,
      "status_desc": "FROZEN",
      "operator_id": 99999,
      "updated_at": 1772607600000
    },
    "timestamp": 1772607600000
  }
  ```

---

#### 3. 管理员手动调账 / 余额补偿
- **接口路径**：`POST /api/v1/admin/users/{user_id}/adjust-wallet`
- **认证方式**：`Bearer <admin_token>`
- **请求参数**：
  - Request Headers:
    - `Idempotency-Key: ADJ-UUID-20260902-10001-01`
  - Request Body:
    ```json
    {
      "amount": 20.00,
      "amount_cents": 2000,
      "remark": "充电桩意外断电客诉补偿"
    }
    ```
- **成功响应 (`200 OK`)**：
  ```json
  {
    "code": 0,
    "msg": "success",
    "data": {
      "transaction_id": "TX_ADJ_20260902_99999_01",
      "user_id": 10001,
      "adjust_amount": 20.00,
      "balance_before": 168.50,
      "balance_after": 188.50,
      "operator_id": 99999,
      "created_at": 1772607600000
    },
    "timestamp": 1772607600000
  }
  ```

---

### 3.6 平台全局订单审计与一键退款

#### 1. 分页检索全平台充电订单 (唯一订单查询接口)
- **接口路径**：`GET /api/v1/admin/orders`
- **认证方式**：`Bearer <admin_token>`
- **查询参数 (Query Params)**：
  - `page` (可选, 默认 `1`): 页码
  - `page_size` (可选, 默认 `10`, 最大 `100`): 每页数量
  - `station_id` (可选): 充电站 ID
  - `status` (可选): 订单状态筛选，如 `CHARGING`, `COMPLETED`, `UNSETTLED`, `REFUNDED`（兼容旧参数名 `order_status`）
  - `user_id` (可选): 按指定用户 ID 筛选订单
  - `phone` (可选): 按指定用户手机号筛选订单。**重要约束**：若同时提供 `user_id` 与 `phone`，必须指向同一用户，若产生矛盾则直接返回 `400 Bad Request` 错误码
  - `start_date` (可选): 筛选订单创建时间晚于/等于该时间的订单，支持格式：`YYYY-MM-DD` (自动规约为当日 00:00:00.000)、`YYYY-MM-DD HH:MM:SS`、或 Unix 时间戳 (秒/毫秒)
  - `end_date` (可选): 筛选订单创建时间早于/等于该时间的订单，支持格式：`YYYY-MM-DD` (自动规约为当日 23:59:59.999)、`YYYY-MM-DD HH:MM:SS`、或 Unix 时间戳 (秒/毫秒)
  - `sort_order` (可选, 默认 `desc`): `desc` (按创建时间从晚到早倒序排列), `asc` (按创建时间从早到晚升序排列)
- **成功响应 (`200 OK`)**：
  ```json
  {
    "code": 0,
    "msg": "success",
    "data": {
      "total": 312,
      "page": 1,
      "page_size": 10,
      "orders": [
        {
          "order_id": "ORD_20260902_1001",
          "user_id": 10001,
          "user_phone": "13800138000",
          "station_id": 101,
          "station_name": "东软高新科技园超级充电站",
          "pile_id": "P10101",
          "pile_type": "FAST",
          "order_status": "COMPLETED",
          "start_time": 1772607600000,
          "end_time": 1772609400000,
          "duration_minutes": 30,
          "charged_energy_kwh": 30.78,
          "electricity_fee": 44.63,
          "service_fee": 10.77,
          "overtime_minutes": 10,
          "overtime_fee": 5.00,
          "total_fee": 60.40,
          "total_fee_cents": 6040,
          "settled_at": 1772609420000
        }
      ]
    },
    "timestamp": 1772609500000
  }
  ```
- **异常响应**：
  - `400 Bad Request` (`code: 50006`)：
    - `"Provided user_id and phone are contradictory"`（提供的 `user_id` 与 `phone` 矛盾）
    - `"start_date cannot be greater than end_date"`（起始时间晚于结束时间）
    - `"Invalid start_date format"` / `"Invalid end_date format"`（时间格式错误）
  - `404 Not Found` (`code: 10001`)：
    - `"User not found with provided phone"` / `"User not found with provided user_id"`（单传 `phone` 或 `user_id` 但用户不存在）

---

#### 2. 管理员对指定订单一键退款
- **接口路径**：`POST /api/v1/admin/orders/{order_id}/refund`
- **认证方式**：`Bearer <admin_token>`
- **业务逻辑**：
  1. 校验目标订单是否存在且状态为 `COMPLETED`。
  2. 开启数据库事务，行级锁锁定用户钱包表记录。
  3. 将订单状态置为 `REFUNDED`，记录退款操作人和退款时间。
  4. 将退款金额返还至用户可用余额 (`user_wallet.balance += refund_amount_cents`)。
  5. 向 `wallet_transaction_flow` 写入一条 `flow_type: 3 (充电退补)` 的不可篡改流水记录。
- **请求参数**：
  - Request Headers:
    - `Idempotency-Key: RF-UUID-20260902-99999-ORD1001`
  - Request Body:
    ```json
    {
      "refund_amount": 60.40,
      "refund_amount_cents": 6040,
      "reason": "桩位输出异常致电量计量争议，管理员执行全额退款"
    }
    ```
- **成功响应 (`200 OK`)**：
  ```json
  {
    "code": 0,
    "msg": "success",
    "data": {
      "refund_transaction_id": "TX_RF_20260902150000_10001",
      "order_id": "ORD_20260902_1001",
      "user_id": 10001,
      "refund_amount": 60.40,
      "refund_amount_cents": 6040,
      "user_balance_before": 208.10,
      "user_balance_after": 268.50,
      "order_status": "REFUNDED",
      "operator_id": 99999,
      "refunded_at": 1772609900000
    },
    "timestamp": 1772609900000
  }
  ```
- **异常响应**：
  - `409 Conflict` (`code: 30004`)：`"Order has already been refunded"`
  - `422 Unprocessable Entity` (`code: 30005`)：`"Refund amount exceeds the actual order paid amount"`

---

#### 4. 查询特定订单计费与退款审计详情
- **接口路径**：`GET /api/v1/admin/orders/{order_id}`
- **认证方式**：`Bearer <admin_token>`
- **成功响应 (`200 OK`)**：
  ```json
  {
    "code": 0,
    "msg": "success",
    "data": {
      "order_id": "ORD_20260902_1001",
      "user_id": 10001,
      "user_phone": "13800138000",
      "station_id": 101,
      "station_name": "东软高新科技园超级充电站",
      "pile_id": "P10101",
      "pile_type": "FAST",
      "order_status": "REFUNDED",
      "start_time": 1772607600000,
      "end_time": 1772609400000,
      "duration_seconds": 1800,
      "charged_energy_kwh": 30.78,
      "electricity_fee": 44.63,
      "service_fee": 10.77,
      "overtime_minutes": 25,
      "overtime_fee": 5.00,
      "total_amount": 60.40,
      "refund_info": {
        "refund_transaction_id": "TX_RF_20260902150000_10001",
        "refund_amount": 60.40,
        "operator_id": 99999,
        "reason": "桩位输出异常致电量计量争议，管理员执行全额退款",
        "refunded_at": 1772609900000
      }
    },
    "timestamp": 1772609900000
  }
  ```

---

## 四、 实时数据流与长连接 (WebSocket Streams)

基于 Boost.Beast WebSocket 协议实现双向与订阅推送，解决高频通信与实时状态感知需求。

### 4.1 充电过程高频遥测数据流 (唯一实时监控途径)

- **连接端点**：`ws://<host>:8080/ws/v1/charging/{order_id}`
- **鉴权方式**：可在建立握手时携带 Header `Authorization: Bearer <token>` 或 Query 参数 `?token=<access_token>`
- **推送频率**：每 1000ms / 500ms 服务端主动下发 1 帧
- **帧消息格式 1 (正常充电中推送 - Server -> Client)**：
  ```json
  {
    "event": "TELEMETRY_UPDATE",
    "order_id": "ORD_20260902_1001",
    "pile_id": "P10101",
    "data": {
      "timestamp": 1772607601000,
      "voltage_v": 398.5,
      "current_a": 150.2,
      "power_kw": 59.85,
      "current_soc": 66,
      "charged_energy_kwh": 30.82,
      "charging_fee": 44.69,
      "service_fee": 10.79,
      "overtime_fee": 0.00,
      "current_total_fee": 55.48,
      "temperature_celsius": 42.5,
      "elapsed_seconds": 1821,
      "is_full": false
    }
  }
  ```
- **帧消息格式 2 (充满后进入占位状态推送 - Server -> Client)**：
  ```json
  {
    "event": "TELEMETRY_UPDATE",
    "order_id": "ORD_20260902_1001",
    "pile_id": "P10101",
    "data": {
      "timestamp": 1772609000000,
      "voltage_v": 0.0,
      "current_a": 0.0,
      "power_kw": 0.0,
      "current_soc": 100,
      "charged_energy_kwh": 30.78,
      "charging_fee": 44.63,
      "service_fee": 10.77,
      "is_full": true,
      "full_timestamp": 1772607800000,
      "overtime_duration_minutes": 20,
      "overtime_grace_minutes": 15,
      "overtime_fee": 5.00,
      "current_total_fee": 60.40,
      "warning_message": "您的爱车已充满电并超出15分钟免费宽限期，当前已产生超时占位费 5.00 元，请尽快拔枪驶离。"
    }
  }
  ```
- **充电完成/拔枪停机事件 (Server -> Client)**：
  ```json
  {
    "event": "CHARGING_FINISHED",
    "order_id": "ORD_20260902_1001",
    "pile_id": "P10101",
    "finish_reason": "USER_UNPLUGGED",
    "total_energy_kwh": 30.78,
    "electricity_fee": 44.63,
    "service_fee": 10.77,
    "overtime_fee": 5.00,
    "total_amount": 60.40,
    "timestamp": 1772609400000
  }
  ```

---

### 4.2 目标充电站导航动态监控流 (占用与排队情况推送)

用户在客户端发起导航或选定目标电站后，客户端与服务端建立该 WebSocket 连接，服务端在目标电站有桩位被占用/释放/故障，或排队人数发生变化时**主动向导航中的客户端实时下发动态**。

- **连接端点**：`ws://<host>:8080/ws/v1/stations/{station_id}/monitor`
- **鉴权方式**：`Authorization: Bearer <token>` 或 `?token=<access_token>`
- **建立连接初始帧 (Server -> Client)**：
  ```json
  {
    "event": "STATION_SNAPSHOT",
    "station_id": 101,
    "station_name": "东软高新科技园超级充电站",
    "data": {
      "total_piles": 12,
      "idle_piles": 7,
      "fast_idle_piles": 4,
      "slow_idle_piles": 3,
      "busy_piles": 4,
      "fault_piles": 1,
      "queueing_cars": 0,
      "estimated_wait_minutes": 0,
      "piles": [
        { "pile_id": "P10101", "type": "FAST", "status": "IDLE", "power_kw": 180.0 },
        { "pile_id": "P10102", "type": "FAST", "status": "CHARGING", "power_kw": 120.0, "current_soc": 85, "est_remaining_mins": 8 },
        { "pile_id": "P10103", "type": "SLOW", "status": "IDLE", "power_kw": 7.0 },
        { "pile_id": "P10104", "type": "FAST", "status": "FAULT", "power_kw": 120.0 }
      ]
    },
    "timestamp": 1772607600000
  }
  ```
- **目标站桩位状态变更/排队变化增量推送 (Server -> Client)**：
  ```json
  {
    "event": "STATION_DYNAMIC_UPDATE",
    "station_id": 101,
    "data": {
      "pile_id": "P10101",
      "change_type": "PILE_OCCUPIED",
      "old_status": "IDLE",
      "new_status": "CHARGING",
      "idle_piles_remaining": 6,
      "queueing_cars": 0,
      "alert_level": "NORMAL",
      "message": "1号直流超充桩已被其他车主接入充电"
    },
    "timestamp": 1772607615000
  }
  ```

---

### 4.3 全网设备状态与系统告警广播流

- **连接端点**：`ws://<host>:8080/ws/v1/events`
- **面向对象**：PC 运营管理端后台监控视图
- **电桩状态变更事件广播 (Server -> Admin)**：
  ```json
  {
    "event": "PILE_STATUS_CHANGED",
    "station_id": 101,
    "pile_id": "P10101",
    "old_status": "IDLE",
    "new_status": "CHARGING",
    "new_status_code": 3,
    "timestamp": 1772607600000
  }
  ```
- **硬件故障告警事件广播 (Server -> Admin)**：
  
  ```json
  {
    "event": "DEVICE_FAULT_ALARM",
    "station_id": 101,
    "pile_id": "P10104",
    "fault_code": "E_OVER_TEMP",
    "fault_message": "温度传感器阈值报警 (85℃)",
    "timestamp": 1772607500000
  }
  ```

---

## 五、 总结与开发落地指引

1. **模块划分清晰**：
   - `auth_controller`：负责车主免密注册登录、管理员登录、Token 签发与刷新。
   - `station_controller`：负责站点空间过滤粗筛、站点详情、单站当日/7天/30天销售分析。
   - `charging_controller`：负责开枪前检查、充电启动、停止与订单行锁强一致结算。
   - `admin_controller`：负责电桩/站点 CRUD、远程重启、用户冻结、根据用户手机号/ID 分页查询历史订单、指定订单一键退款。
   - `websocket_server`：基于 Boost.Beast 异步协程管理充电遥测长连接 (`/charging/{order_id}`)、导航站点动态监控 (`/stations/{station_id}/monitor`) 与全局告警广播 (`/events`)。

2. **三级存储与性能保障**：
   - **第一级（内存状态池）**：附近电站 R-Tree 筛选与桩位状态位图聚合，微秒级响应。
   - **第二级（Redis）**：Token 鉴权缓存、实时遥测数据 Pub/Sub 分发。
   - **第三级（PostgreSQL 18）**：钱包流水账本、充值、扣费、退款与订单强 ACID 事务落盘。

3. **序列化性能**：
   - 使用 **Glaze** 编译期反射库直接绑定 C++ Struct 与 JSON，消除运行时多余的内存拷贝和堆分配。
