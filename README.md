# EV-Charging-Platform
北理工大三小学期｜东软电动汽车充电桩应用管理平台｜基于 Linux + Qt(C++) 的电动汽车充电桩管理平台，含用户端、管理后台、数据库存储、Web数据大屏与机器学习负荷预测

## 项目结构
```
server/
├── CMakeLists.txt                  # 构建脚本 (C++23)
├── main.cpp                        # 服务端启动主程序 (IO 协程事件循环、状态池初始化、优雅退出)
├── common/                         # 领域模型与公共组件
│   ├── types.hpp                   # 全局枚举 (PileStatus, OrderStatus, FlowType, Role) 与金额转换
│   ├── error.hpp                   # std::expected 错误模型与 HTTP 状态码映射
│   ├── models.hpp                  # 领域实体模型与全部 18+ 接口请求/响应 DTO
│   ├── auth_token.hpp              # Bearer Token 签名与无状态鉴权管理器
│   └── response.hpp                # 统一 HTTP JSON 响应组装 (Glaze 编译期反射)
├── db/                             # PostgreSQL 18 存储与数据访问层
│   ├── schema.sql                  # DDL 建表脚本 (用户表、钱包表、流水表、电站表、电桩表、订单表)
│   ├── db_pool.hpp / .cpp          # 基于 libpq 的非阻塞线程安全连接池 (RAII 事务与行级锁)
│   ├── db_repository.hpp / .cpp    # 业务仓储层 (行锁扣费、幂等入账、单站销售统计、一键退款)
│   └── seed_data.hpp / .cpp        # 中等规模测试数据预置器 (25 电站, 250 充电桩, 50 用户, 120+ 历史订单)
├── memory/                         # 极速内存状态池与空间索引 (第一级存储)
│   ├── rtree_index.hpp             # Boost.Geometry R-Tree 2D 空间几何索引 (微秒级粗筛)
│   └── state_pool.hpp              # 高频电桩遥测与状态缓存池 (双缓冲读写保护)
├── simulation/                     # 真实充电桩高频动态模拟引擎
│   └── simulator.hpp / .cpp        # Asio 定时器动态模拟恒流恒压(CC-CV)曲线与超时占位费阶梯计算
├── websocket/                      # 实时长连接与流分发
│   ├── ws_session.hpp / .cpp       # Boost.Beast C++20 协程 WebSocket 会话
│   └── ws_manager.hpp              # 充电遥测流、导航监控流、全局告警流 Pub/Sub 管理器
├── controllers/                    # 业务控制器
│   ├── auth_controller.hpp         # 车主免密登录/自动注册、管理员登录、Token 刷新
│   ├── user_controller.hpp         # 个人资料、钱包余额、幂等充值、资金流水明细
│   ├── station_controller.hpp      # 附近电站搜桩、电站与枪位详情、单站销售业绩统计
│   ├── charging_controller.hpp     # 充电前检查、启动、停止、钱包行锁结算、历史订单
│   └── admin_controller.hpp        # 运营态势大盘、电站/电桩 CRUD、远程重启、用户风控/调账、一键退款
├── router/                         # 路由分发层
│   └── http_router.hpp / .cpp      # 基于 string_view 的 RESTful HTTP 路由与中间件拦截器
└── tests/                          # 单元测试与端到端集成测试集
    ├── test_db_pool.cpp            # 数据库连接池、行锁扣款与业务仓储测试
    ├── test_rtree_and_sim.cpp      # R-Tree 空间检索与超时占位费阶梯计算测试
    └── test_integration.cpp        # 覆盖全部 18+ 接口与全业务链路的端到端集成测试
```