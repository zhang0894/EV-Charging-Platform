#pragma once

#include <boost/asio.hpp>
#include <boost/asio/steady_timer.hpp>
#include <memory>
#include <atomic>
#include <thread>

namespace ev {

class ChargingSimulator {
public:
    static ChargingSimulator& instance() {
        static ChargingSimulator sim;
        return sim;
    }

    void start(boost::asio::io_context& ioc, int interval_ms = 500);
    void stop();

    // 单步推进模拟（供单元测试和无等待测试调用）
    void step_once(double delta_seconds = 1.0);

private:
    ChargingSimulator() = default;
    void schedule_tick();

    boost::asio::io_context* ioc_{nullptr};
    std::unique_ptr<boost::asio::steady_timer> timer_;
    int interval_ms_{500};
    std::atomic<bool> is_running_{false};
};

} // namespace ev
