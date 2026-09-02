#include "server/http_session.hpp"
#include "db/db_pool.hpp"
#include "db/db_repository.hpp"
#include "db/seed_data.hpp"
#include "memory/rtree_index.hpp"
#include "memory/state_pool.hpp"
#include "simulation/simulator.hpp"

#include <boost/asio/ip/tcp.hpp>
#include <boost/asio/signal_set.hpp>
#include <boost/asio/co_spawn.hpp>
#include <boost/asio/detached.hpp>
#include <iostream>
#include <print>
#include <thread>

namespace net = boost::asio;
using tcp = boost::asio::ip::tcp;

net::awaitable<void> listener(tcp::acceptor& acceptor) {
    for (;;) {
        tcp::socket socket = co_await acceptor.async_accept(net::use_awaitable);
        auto executor = socket.get_executor();
        net::co_spawn(
            executor,
            ev::handle_session(std::move(socket)),
            net::detached
        );
    }
}

int main(int argc, char* argv[]) {
    std::println("\n=======================================================");
    std::println("   电动汽车充电桩管理平台 (EV Charging Platform) 服务端   ");
    std::println("   C++23 | Boost.Asio/Beast | PostgreSQL 18 | Glaze     ");
    std::println("=======================================================\n");

    const std::string host = "0.0.0.0";
    const unsigned short port = 8080;
    const std::string db_conninfo = "host=127.0.0.1 port=5432 dbname=postgres user=postgres password=Express1.";

    // 1. 初始化数据库连接池
    std::println(">>> 1. 正在初始化 PostgreSQL 18 数据库连接池...");
    ev::DbPool::instance().init(db_conninfo, 4, 16);

    // 2. 预置中等规模测试数据 (25 电站, 250 充电桩, 50 用户, 历史订单)
    std::println(">>> 2. 检查并预置业务基础数据...");
    ev::SeedDataGenerator::populate_if_empty();

    // 3. 构建内存 R-Tree 空间索引与实时状态池
    std::println(">>> 3. 加载电站地理坐标至 R-Tree 空间索引与内存状态池...");
    auto all_stations = ev::DbRepository::instance().get_all_stations();
    if (all_stations) {
        std::vector<std::pair<int64_t, std::pair<double, double>>> coords;
        std::vector<ev::PileModel> all_piles;
        for (const auto& s : *all_stations) {
            coords.emplace_back(s.station_id, std::make_pair(s.latitude, s.longitude));
            auto piles = ev::DbRepository::instance().get_piles_by_station(s.station_id);
            if (piles) {
                all_piles.insert(all_piles.end(), piles->begin(), piles->end());
            }
        }
        ev::ChargingStatePool::instance().init_from_piles(all_piles);
        ev::StationRTree::instance().build_index(coords);
        std::println("  [OK] 成功构建 {} 个充电站 R-Tree 空间索引与 {} 个电桩状态池", coords.size(), all_piles.size());
    }

    try {
        // 4. 初始化 Asio 网络与协程事件循环 (Core 0)
        net::io_context ioc(1);

        // 5. 启动动态充电模拟引擎 (Core 1 / 独立调度)
        std::println(">>> 4. 启动充电桩动态模拟与占位费引擎 (500ms 刷新周期)...");
        ev::ChargingSimulator::instance().start(ioc, 500);

        // 6. 绑定并监听 HTTP / WebSocket 端口 8080
        auto const address = net::ip::make_address(host);
        tcp::acceptor acceptor{ioc, {address, port}};

        std::println("\n🚀 服务端启动就绪，监听于: http://{}:{}", host, port);
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
            ev::DbPool::instance().shutdown();
            ioc.stop();
        });

        // 8. 启动网络协程接收器
        net::co_spawn(ioc, listener(acceptor), net::detached);

        ioc.run();
    } catch (const std::exception& e) {
        std::cerr << "[Server Fatal Error] " << e.what() << "\n";
        return 1;
    }

    std::println("[Server] 服务端已安全停止。\n");
    return 0;
}