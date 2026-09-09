#include "server/qt_http_server.hpp"
#include "db/db_pool.hpp"
#include "db/db_repository.hpp"
#include "db/seed_data.hpp"
#include "db/schema_migrator.hpp"
#include "memory/rtree_index.hpp"
#include "memory/state_pool.hpp"
#include "simulation/simulator.hpp"
#include "cache/redis_cache.hpp"
#include "db/async_flow_persister.hpp"
#include "data/static_stations.hpp"
#include "memory/station_status_manager.hpp"
#include "memory/station_price_manager.hpp"
#include "memory/avatar_manager.hpp"
#include "common/auth_token.hpp"

#include <QCoreApplication>
#include <QString>
#include <iostream>
#include <print>
#include <thread>
#include <csignal>

int main(int argc, char* argv[]) {
    QCoreApplication app(argc, argv);

    std::println("\n=======================================================");
    std::println("   电动汽车充电桩管理平台 (EV Charging Platform) 服务端   ");
    std::println("      C++23 | Qt 6.11 (QTcpServer) | PostgreSQL | Glaze ");
    std::println("=======================================================\n");

    const std::string host = "0.0.0.0";
    const unsigned short port = 8080;
    const std::string default_conninfo = "host=127.0.0.1 port=5432 dbname=postgres user=postgres password=Express1. sslmode=disable";
    const char* env_conn = std::getenv("PG_CONNINFO");
    const std::string db_conninfo = env_conn ? env_conn : default_conninfo;
    const char* env_read_conn = std::getenv("PG_READ_CONNINFO");
    const std::string db_read_conninfo = env_read_conn ? env_read_conn : db_conninfo;

    // 1. 初始化数据库读写分离连接池
    std::println(">>> 1. 正在初始化 PostgreSQL 读写分离数据库连接池 (主库写池与只读副本读池)...");
#if defined(_WIN32) || defined(_WIN64)
    constexpr size_t WIN_MIN_CONN = 16;
    constexpr size_t WIN_MAX_CONN = 48;
    ev::DbPool::instance().init(db_conninfo, db_read_conninfo, WIN_MIN_CONN, WIN_MAX_CONN);
#else
    ev::DbPool::instance().init(db_conninfo, db_read_conninfo, 8, 32);
#endif
    if (!ev::DbPool::instance().is_initialized()) {
        std::cerr << ">>> [FATAL] 数据库连接失败，服务端终止启动。请检查 PostgreSQL 服务是否已启动并验证连接配置。\n" << std::flush;
        return 1;
    }

    // 1.1 自动自检数据库完整性并补齐缺失数据表与索引
    std::println(">>> 1.1 正在自检数据库表结构完整性与关系约束...");
    if (!ev::SchemaMigrator::ensure_schema()) {
        std::cerr << ">>> [FATAL] 数据库表结构自检与迁移失败，服务端终止启动。\n" << std::flush;
        return 1;
    }

    // 2. 初始化 Redis 缓存中心
    std::println(">>> 2. 正在初始化 Redis 实时/TTL 缓存组件...");
    ev::RedisCache::instance().init("127.0.0.1", 6379);

    // 3. 询问是否需要清空数据库并重新导入数据? (y/N)
    bool skip_prompt = false;
    bool do_reset_and_import = false;

    for (int i = 1; i < argc; ++i) {
        std::string_view arg = argv[i];
        if (arg == "--reset" || arg == "--reseed") {
            do_reset_and_import = true;
            skip_prompt = true;
            break;
        } else if (arg == "--no-prompt" || arg == "--keep") {
            do_reset_and_import = false;
            skip_prompt = true;
            break;
        }
    }

    if (!skip_prompt && const_cast<const char*>(std::getenv("RESET_DB"))) {
        do_reset_and_import = true;
        skip_prompt = true;
    }

    if (!skip_prompt) {
        if (const char* env_no_prompt = std::getenv("NO_PROMPT")) {
            std::string_view s(env_no_prompt);
            while (!s.empty() && std::isspace(static_cast<unsigned char>(s.front()))) s.remove_prefix(1);
            while (!s.empty() && std::isspace(static_cast<unsigned char>(s.back()))) s.remove_suffix(1);
            if (s == "1" || s == "true" || s == "TRUE" || s == "yes" || s == "YES") {
                skip_prompt = true;
            }
        }
    }

    if (!skip_prompt && !do_reset_and_import) {
        std::cout << "是否清空数据库并重新导入数据? (y/N): " << std::flush;
        std::string choice;
        if (std::getline(std::cin, choice)) {
            while (!choice.empty() && std::isspace(static_cast<unsigned char>(choice.front()))) choice.erase(choice.begin());
            while (!choice.empty() && std::isspace(static_cast<unsigned char>(choice.back()))) choice.pop_back();

            if (choice == "y" || choice == "Y" || choice == "yes" || choice == "YES") {
                do_reset_and_import = true;
            }
        }
    } else {
        std::println(">>> [NO_PROMPT] 自动化/非交互模式，保持现有数据库内容不变。");
    }

    if (do_reset_and_import) {
        std::println(">>> 正在清空数据库所有业务表与 Redis 缓存...");
        ev::SeedDataGenerator::clear_database();
        ev::RedisCache::instance().flush_all();
        ev::AvatarManager::instance().clear();
        std::println(">>> 正在读取本地 JSON 文件，通过 Glaze 解析并批量导入数据库...");
        ev::SeedDataGenerator::import_from_json("data");
        std::println(">>> [OK] 初始数据重新导入完毕。\n");
    } else {
        std::println(">>> 保持现有数据库内容不变，直接启动服务。\n");
        // 保险检查：若数据库完全没有任何数据（首次启动），自动导入
        ev::SeedDataGenerator::populate_if_empty("data");
        // 若数据库已有数据，检查并模拟补齐可能存在的跨天订单
        ev::DbRepository::instance().check_and_simulate_daily_orders();
    }

    // 4. 构建真实电站常量 R-Tree 空间索引与电桩状态内存池
    std::println(">>> 4. 正在装载北京市真实充电站编译期常量 (共 {} 座真实电站)...", ev::STATIC_STATION_COUNT);
    ev::StationRTree::instance().build_static_index();
    ev::StationStatusManager::instance().init();
    ev::StationPriceManager::instance().init();
    ev::StationPriceManager::instance().load_from_db();
    ev::ChargingStatePool::instance().init_from_seed_piles("data");
    ev::ChargingStatePool::instance().sync_missing_piles_from_db();
    ev::ChargingStatePool::instance().load_active_reservations_from_db();

    // 级联同步初始下线电站（包括暂停营业电站）的电桩状态
    size_t init_offline_stations = 0;
    for (size_t i = 0; i < ev::STATIC_STATION_COUNT; ++i) {
        int64_t sid = ev::STATIC_STATIONS[i].station_id;
        if (!ev::StationStatusManager::instance().is_online(sid)) {
            ev::ChargingStatePool::instance().set_station_piles_offline(sid);
            init_offline_stations++;
        }
    }

    // 启动时同步清洗数据库中可能存在的 (暂停营业) 脏名称并将状态标记为下线
    {
        auto db_conn = ev::DbPool::instance().acquire();
        if (db_conn) {
            db_conn->exec(
                "UPDATE stations SET station_name = REPLACE(REPLACE(station_name, '(暂停营业)', ''), '（暂停营业）', ''), status = 2 "
                "WHERE station_name LIKE '%(暂停营业)%' OR station_name LIKE '%（暂停营业）%';"
            );
        }
    }

    std::println("  [OK] 成功构建 {} 个真实充电站 R-Tree 空间几何索引与 16 个行政区索引", ev::STATIC_STATION_COUNT);
    std::println("  [OK] 成功为全量充电站装载充电桩，恢复活跃预约，初始化 {} 座下线/暂停营业电站", init_offline_stations);

    // 4.1 恢复冻结用户风控状态与 Token 吊销时间戳
    std::println(">>> 4.1 正在同步冻结用户风控名单...");
    auto frozen_res = ev::DbRepository::instance().get_frozen_users_info();
    if (frozen_res) {
        ev::AuthTokenManager::init_frozen_users(*frozen_res);
        std::println("  [OK] 成功同步 {} 个冻结用户至风控鉴权模块", frozen_res->size());
    }

    try {
        // 5. 启动动态充电模拟引擎 (500ms 刷新周期)
        std::println(">>> 5. 启动充电桩动态模拟与占位费引擎 (500ms 刷新周期)...");
        ev::ChargingSimulator::instance().start(500);

        // 6. 绑定并监听 HTTP / WebSocket 端口 8080 (Qt 现代多线程网络引擎)
        ev::QtHttpServer server;
#if defined(_WIN32) || defined(_WIN64)
        constexpr int WIN_SERVER_WORKERS = 12;
        if (!server.start(QString::fromStdString(host), port, WIN_SERVER_WORKERS)) {
#else
        if (!server.start(QString::fromStdString(host), port)) {
#endif
            std::cerr << ">>> [FATAL] Qt 网络服务器启动失败，服务端终止启动。\n" << std::flush;
            return 1;
        }

        std::println("\n🚀 服务端启动就绪 [Qt 6.11.0 现代多线程网络引擎 (Scheme 2+)]，监听于: http://{}:{}", host, port);
        std::println("📡 WebSocket 实时流通道 (Qt QTcpSocket 驱动):");
        std::println("   - 充电遥测流: ws://{}:{}/ws/v1/charging/<order_id>", host, port);
        std::println("   - 导航监控流: ws://{}:{}/ws/v1/stations/<station_id>/monitor", host, port);
        std::println("   - 全局告警流: ws://{}:{}/ws/v1/events", host, port);
        std::fflush(stdout);

        // 7. 优雅退出信号捕获
        static QCoreApplication* g_app = &app;
        static ev::QtHttpServer* g_server = &server;
        auto signal_handler = [](int sig) {
            std::println("\n[Server] 接收到退出信号 ({})，正在安全关闭服务端...", sig);
            std::fflush(stdout);
            if (g_app) g_app->quit();
        };
        std::signal(SIGINT, signal_handler);
        std::signal(SIGTERM, signal_handler);

#ifdef _WIN32
        SetConsoleCtrlHandler([](DWORD ctrl_type) -> BOOL {
            if (ctrl_type == CTRL_C_EVENT || ctrl_type == CTRL_BREAK_EVENT || ctrl_type == CTRL_CLOSE_EVENT) {
                if (g_app) g_app->quit();
                return TRUE;
            }
            return FALSE;
        }, TRUE);
#endif

        int exit_code = app.exec();

        ev::ChargingSimulator::instance().stop();
        server.stop();
        ev::AsyncFlowPersister::instance().shutdown();
        ev::DbPool::instance().shutdown();

        std::println("[Server] 服务端已安全停止。\n");
        return exit_code;
    } catch (const std::exception& e) {
        std::cerr << "[Server Fatal Error] " << e.what() << "\n";
        return 1;
    }

    return 0;
}