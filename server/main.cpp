#include "server/http_session.hpp"
#include "db/db_pool.hpp"
#include "db/db_repository.hpp"
#include "db/seed_data.hpp"
#include "memory/rtree_index.hpp"
#include "memory/state_pool.hpp"
#include "simulation/simulator.hpp"
#include "cache/redis_cache.hpp"
#include "db/async_flow_persister.hpp"
#include "data/static_stations.hpp"
#include "memory/station_status_manager.hpp"
#include "memory/station_price_manager.hpp"

#include <boost/asio/ip/tcp.hpp>
#include <boost/asio/signal_set.hpp>
#include <boost/asio/co_spawn.hpp>
#include <boost/asio/detached.hpp>
#include <boost/asio/strand.hpp>
#include <iostream>
#include <print>
#include <thread>

namespace net = boost::asio;
using tcp = boost::asio::ip::tcp;

net::awaitable<void> listener(tcp::acceptor& acceptor) {
    auto executor = acceptor.get_executor();
    for (;;) {
        tcp::socket socket = co_await acceptor.async_accept(net::use_awaitable);
        net::co_spawn(
            net::make_strand(executor),
            ev::handle_session(std::move(socket)),
            net::detached
        );
    }
}

int main(int argc, char* argv[]) {
    std::println("\n=======================================================");
    std::println("   电动汽车充电桩管理平台 (EV Charging Platform) 服务端   ");
    std::println("      C++23 | Boost.Asio/Beast | PostgreSQL | Glaze     ");
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
    ev::DbPool::instance().init(db_conninfo, db_read_conninfo, 8, 32);
    if (!ev::DbPool::instance().is_initialized()) {
        std::cerr << ">>> [FATAL] 数据库连接失败，服务端终止启动。请检查 PostgreSQL 服务是否已启动并验证连接配置。\n" << std::flush;
        return 1;
    }

    // 2. 初始化 Redis 缓存中心
    std::println(">>> 2. 正在初始化 Redis 实时/TTL 缓存组件...");
    ev::RedisCache::instance().init("127.0.0.1", 6379);

    // 3. 询问是否需要清空数据库并重新导入数据? (y/N)
    bool skip_prompt = (std::getenv("NO_PROMPT") != nullptr && std::string_view(std::getenv("NO_PROMPT")) == "1");
    bool do_reset_and_import = false;

    if (!skip_prompt) {
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
        std::println(">>> 正在读取本地 JSON 文件，通过 Glaze 解析并批量导入数据库...");
        ev::SeedDataGenerator::import_from_json("data");
        std::println(">>> [OK] 初始数据重新导入完毕。\n");
    } else {
        std::println(">>> 保持现有数据库内容不变，直接启动服务。\n");
        // 保险检查：若数据库完全没有任何数据（首次启动），自动导入
        ev::SeedDataGenerator::populate_if_empty("data");
    }

    // 4. 构建真实电站常量 R-Tree 空间索引与电桩状态内存池
    std::println(">>> 4. 正在装载北京市真实充电站编译期常量 (共 {} 座真实电站)...", ev::STATIC_STATION_COUNT);
    ev::StationRTree::instance().build_static_index();
    ev::StationStatusManager::instance().init();
    ev::StationPriceManager::instance().init();
    ev::StationPriceManager::instance().load_from_db();
    ev::ChargingStatePool::instance().init_from_seed_piles("data");
    std::println("  [OK] 成功构建 {} 个真实充电站 R-Tree 空间几何索引与 16 个行政区索引", ev::STATIC_STATION_COUNT);
    std::println("  [OK] 成功为全量充电站装载充电桩，状态池初始化就绪");

    try {
        // 4. 初始化 Asio 网络与协程事件循环 (多线程并发驱动: 2 线程协同)
        int const threads = 2;
        net::io_context ioc{threads};

        // 5. 启动动态充电模拟引擎
        std::println(">>> 4. 启动充电桩动态模拟与占位费引擎 (500ms 刷新周期)...");
        ev::ChargingSimulator::instance().start(ioc, 500);

        // 6. 绑定并监听 HTTP / WebSocket 端口 8080
        auto const address = net::ip::make_address(host);
        tcp::acceptor acceptor{ioc, {address, port}};

        std::println("\n🚀 服务端启动就绪 [多线程事件循环架构 (Scheme A)]，监听于: http://{}:{}", host, port);
        std::println("📡 WebSocket 实时流通道:");
        std::println("   - 充电遥测流: ws://{}:{}/ws/v1/charging/<order_id>", host, port);
        std::println("   - 导航监控流: ws://{}:{}/ws/v1/stations/<station_id>/monitor", host, port);
        std::println("   - 全局告警流: ws://{}:{}/ws/v1/events", host, port);

        // 7. 优雅退出信号捕获
        net::signal_set signals(ioc, SIGINT, SIGTERM);
        signals.async_wait([&](const boost::system::error_code&, int) {
            std::println("\n[Server] 接收到退出信号，正在安全关闭服务端...");
            ev::ChargingSimulator::instance().stop();
            acceptor.close();
            ev::AsyncFlowPersister::instance().shutdown();
            ev::DbPool::instance().shutdown();
            ioc.stop();
        });

        // 8. 启动网络协程接收器
        net::co_spawn(ioc, listener(acceptor), net::detached);

        std::vector<std::thread> thread_pool;
        thread_pool.reserve(threads - 1);
        for (int i = 0; i < threads - 1; ++i) {
            thread_pool.emplace_back([&ioc] {
                ioc.run();
            });
        }
        ioc.run();

        for (auto& t : thread_pool) {
            if (t.joinable()) t.join();
        }
        ev::AsyncFlowPersister::instance().shutdown();
    } catch (const std::exception& e) {
        std::cerr << "[Server Fatal Error] " << e.what() << "\n";
        return 1;
    }

    std::println("[Server] 服务端已安全停止。\n");
    return 0;
}