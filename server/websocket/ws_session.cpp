#include "ws_session.hpp"
#include "../memory/state_pool.hpp"
#include <iostream>
#include <regex>

namespace ev {

WsSession::WsSession(tcp::socket&& socket)
    : ws_(std::move(socket)) {
}

bool WsSession::is_open() const {
    return is_open_ && ws_.is_open();
}

void WsSession::send_json(const std::string& json_text) {
    if (!is_open_) return;

    std::lock_guard<std::mutex> lock(queue_mutex_);
    write_queue_.push(json_text);

    // 触发异步写
    net::post(ws_.get_executor(), [self = shared_from_this()]() {
        std::lock_guard<std::mutex> lk(self->queue_mutex_);
        if (self->write_queue_.empty() || !self->is_open_) return;

        std::string msg = std::move(self->write_queue_.front());
        self->write_queue_.pop();

        self->ws_.async_write(
            net::buffer(msg),
            [self](beast::error_code ec, std::size_t) {
                if (ec) {
                    self->is_open_ = false;
                }
            }
        );
    });
}

net::awaitable<void> WsSession::run(beast::http::request<beast::http::string_body> req) {
    try {
        ws_.set_option(websocket::stream_base::timeout::suggested(beast::role_type::server));
        co_await ws_.async_accept(req, net::use_awaitable);

        is_open_ = true;
        std::string_view target = req.target();
        std::string target_str(target);

        // 1. 充电遥测流: /ws/v1/charging/{order_id}
        std::regex charging_ws_regex(R"(^/ws/v1/charging/([^/]+)$)");
        std::cmatch match;
        if (std::regex_match(target_str.c_str(), match, charging_ws_regex)) {
            std::string order_id = match[1].str();
            WsManager::instance().subscribe_charging(order_id, shared_from_this());
        }

        // 2. 站点导航监控流: /ws/v1/stations/{station_id}/monitor
        std::regex station_ws_regex(R"(^/ws/v1/stations/(\d+)/monitor$)");
        if (std::regex_match(target_str.c_str(), match, station_ws_regex)) {
            int64_t sid = std::stoll(match[1].str());
            WsManager::instance().subscribe_station(sid, shared_from_this());

            // 立即推送初始快照帧
            auto piles = ChargingStatePool::instance().get_piles_by_station(sid);
            auto sum = ChargingStatePool::instance().get_station_pile_summary(sid);

            StationSnapshotFrame snapshot{
                .event = "STATION_SNAPSHOT",
                .station_id = sid,
                .station_name = "充电站实时状态监控",
                .data = StationSnapshotData{
                    .total_piles = sum.total_piles,
                    .idle_piles = sum.idle_piles,
                    .fast_idle_piles = sum.fast_piles_idle,
                    .slow_idle_piles = sum.slow_piles_idle,
                    .busy_piles = sum.busy_piles,
                    .fault_piles = sum.fault_piles,
                    .queueing_cars = 0,
                    .estimated_wait_minutes = (sum.idle_piles > 0 ? 0 : 15)
                },
                .timestamp = current_time_ms()
            };

            for (const auto& p : piles) {
                snapshot.data.piles.push_back(StationNavPileStatusDTO{
                    .pile_id = p.pile_id,
                    .type = p.type,
                    .status = p.status,
                    .power_kw = p.power_kw,
                    .current_soc = p.current_soc,
                    .est_remaining_mins = (p.status == "CHARGING" ? std::max(5, (100 - p.current_soc) / 2) : 0)
                });
            }

            std::string json_str;
            if (!glz::write_json(snapshot, json_str)) {
                send_json(json_str);
            }
        }

        // 3. 全局告警流: /ws/v1/events
        if (target.starts_with("/ws/v1/events")) {
            WsManager::instance().subscribe_global(shared_from_this());
        }

        // 持续读取客户端心跳
        while (is_open_) {
            buffer_.clear();
            co_await ws_.async_read(buffer_, net::use_awaitable);

            // 如果收到文本心跳 "PING"，回送 "PONG"
            std::string msg = beast::buffers_to_string(buffer_.data());
            if (msg == "PING" || msg == "ping") {
                send_json(R"({"event":"PONG"})");
            }
        }
    } catch (const std::exception& e) {
        is_open_ = false;
    }
}

} // namespace ev
