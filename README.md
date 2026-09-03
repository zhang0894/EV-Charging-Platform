# EV-Charging-Platform
北理工大三小学期｜东软电动汽车充电桩应用管理平台｜基于 Linux + Qt(C++) 的电动汽车充电桩管理平台，含用户端、管理后台、数据库存储、Web数据大屏与机器学习负荷预测

## 项目结构 (服务端 Server)
```
server/
├── CMakeLists.txt                      # 构建脚本 (跨平台兼容 Windows MSYS2/MinGW64 与 Linux GCC/Clang，C++23)
├── main.cpp                            # 服务端主入口程序 (多线程 Asio 协程事件循环、启动交互提示、组件生命周期管理、优雅退出)
├── cache/                              # 外部高速缓存组件
│   └── redis_cache.hpp / .cpp          # 基于 hiredis 的 Redis 实时/TTL 缓存组件，支持热点只读微缓存 (Write-Invalidated Cache)
├── common/                             # 领域实体模型与公共组件
│   ├── types.hpp                       # 全局枚举 (PileStatus, OrderStatus, FlowType, Role) 与分/元金额转换
│   ├── error.hpp                       # std::expected 错误模型、10001~50006 业务错误码与 HTTP 状态码映射
│   ├── models.hpp                      # 领域实体模型与全部接口请求/响应 DTO (Glaze 编译期反射序列化)
│   ├── auth_token.hpp                  # Bearer Token 签名生成与基于 std::string_view 的零拷贝鉴权校验器
│   └── response.hpp                    # 统一 HTTP JSON 响应组装封装
├── db/                                 # PostgreSQL 18 数据存储与访问持久层
│   ├── schema.sql                      # DDL 建表脚本 (用户表、钱包表、流水表、电站表、电桩表、订单表)
│   ├── db_pool.hpp / .cpp              # 基于 libpq 的动态扩容非阻塞连接池 (支持预编译语句 Prepared Statements 与事务隔离)
│   ├── db_repository.hpp / .cpp        # 业务仓储层 (预编译查询、热点微缓存、行级锁资金扣划、幂等入账、退款审计)
│   ├── async_flow_persister.hpp        # 环形双缓冲异步批量流水持久化引擎 (Batch Flush，高频流水与主业务解耦)
│   └── seed_data.hpp / .cpp            # 超大规模测试数据预置器 (10,000 北京充电站、100,000 充电桩、超大规模数据) 与一键清空
├── memory/                             # 内存状态池与空间索引 (L1 级存储)
│   ├── rtree_index.hpp                 # Boost.Geometry R-Tree 2D 空间几何索引与站点元数据常驻内存表 (搜桩 0 次查库)
│   └── state_pool.hpp                  # 高频电桩遥测与状态缓存池 (双缓冲读写保护与活跃电桩增量索引)
├── simulation/                         # 真实充电桩动态模拟引擎
│   └── simulator.hpp / .cpp            # Asio 定时器驱动的高频充电推演引擎 (基于活跃桩增量模拟 CC-CV 曲线与阶梯超时占位费)
├── websocket/                          # 实时长连接与高频流分发
│   ├── ws_session.hpp / .cpp           # Boost.Beast C++20 协程 WebSocket 会话管理
│   └── ws_manager.hpp                  # 充电遥测流、导航监控流、全局告警广播流 Pub/Sub 管理器
├── server/                             # 网络协议会话层
│   └── http_session.hpp                # Boost.Beast HTTP/1.1 会话协程处理器 (基于 Session Strand 严格串行防竞争)
├── controllers/                        # 业务控制器
│   ├── auth_controller.hpp             # 手机号免密登录(未注册返回10001)、账号密码注册(自动登录/手机号唯一校验)、手机号密码登录、修改密码、管理员登录、Token刷新
│   ├── user_controller.hpp             # 个人中心资料、修改密码、钱包资产查询、幂等充值、资金变动流水明细
│   ├── station_controller.hpp          # 附近电站搜桩 (空间索引+内存零查库)、电站与枪位详情、单站销售业绩多维统计
│   ├── charging_controller.hpp         # 充电前校验、启动充电、主动停止、行级排他锁资金结算、历史订单分页
│   └── admin_controller.hpp            # 运营态势大盘、电站/电桩 CRUD、远程管控指令、用户风控/调账、一键退款
├── router/                             # 路由分发层
│   └── http_router.hpp / .cpp          # 静态化正则预编译、Token 零拷贝解析与 RESTful 路由分发中心
└── tests/                              # 单元测试与端到端集成测试集
    ├── test_db_pool.cpp                # 数据库连接池、行锁扣款与业务仓储测试
    ├── test_rtree_and_sim.cpp          # R-Tree 空间检索与超时占位费阶梯计算测试
    └── test_integration.cpp            # 覆盖全部接口与全业务链路的端到端集成测试
```