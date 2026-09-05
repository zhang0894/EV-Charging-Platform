#pragma once

#include "../common/models.hpp"
#include <boost/beast/websocket.hpp>
#include <glaze/glaze.hpp>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>
#include <mutex>
#include <functional>

namespace ev {

class IWebSocketSession {
public:
    virtual ~IWebSocketSession() = default;
    virtual void send_json(const std::string& json_text) = 0;
    virtual bool is_open() const = 0;
};

class WsManager {
public:
    static WsManager& instance() {
        static WsManager mgr;
        return mgr;
    }

    // 订阅充电会话流 (/ws/v1/charging/{order_id})
    void subscribe_charging(const std::string& order_id, std::shared_ptr<IWebSocketSession> session) {
        std::lock_guard<std::mutex> lock(mutex_);
        charging_subs_[order_id].push_back(session);
    }

    // 订阅站点导航动态流 (/ws/v1/stations/{station_id}/monitor)
    void subscribe_station(int64_t station_id, std::shared_ptr<IWebSocketSession> session) {
        std::lock_guard<std::mutex> lock(mutex_);
        station_subs_[station_id].push_back(session);
    }

    // 订阅全局事件与告警广播 (/ws/v1/events)
    void subscribe_global(std::shared_ptr<IWebSocketSession> session) {
        std::lock_guard<std::mutex> lock(mutex_);
        global_subs_.push_back(session);
    }

    bool has_charging_subscribers(const std::string& order_id) {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = charging_subs_.find(order_id);
        return it != charging_subs_.end() && !it->second.empty();
    }

    // 广播充电遥测
    void broadcast_telemetry(const TelemetryFrame& frame) {
        std::string json_str;
        if (glz::write_json(frame, json_str)) return;

        std::lock_guard<std::mutex> lock(mutex_);
        auto it = charging_subs_.find(frame.order_id);
        if (it != charging_subs_.end()) {
            clean_and_send(it->second, json_str);
        }
    }

    // 广播充电完成
    void broadcast_charging_finished(const ChargingFinishedFrame& frame) {
        std::string json_str;
        if (glz::write_json(frame, json_str)) return;

        std::lock_guard<std::mutex> lock(mutex_);
        auto it = charging_subs_.find(frame.order_id);
        if (it != charging_subs_.end()) {
            clean_and_send(it->second, json_str);
        }
    }

    // 广播电站导航动态变动
    void broadcast_station_update(const StationDynamicUpdateFrame& frame) {
        std::string json_str;
        if (glz::write_json(frame, json_str)) return;

        std::lock_guard<std::mutex> lock(mutex_);
        auto it = station_subs_.find(frame.station_id);
        if (it != station_subs_.end()) {
            clean_and_send(it->second, json_str);
        }
    }

    // 广播全局设备状态
    void broadcast_pile_status(const PileStatusChangedBroadcastFrame& frame) {
        std::string json_str;
        if (glz::write_json(frame, json_str)) return;

        std::lock_guard<std::mutex> lock(mutex_);
        clean_and_send(global_subs_, json_str);
    }

    // 广播全局故障报警
    void broadcast_fault_alarm(const DeviceFaultAlarmBroadcastFrame& frame) {
        std::string json_str;
        if (glz::write_json(frame, json_str)) return;

        std::lock_guard<std::mutex> lock(mutex_);
        clean_and_send(global_subs_, json_str);
    }

private:
    WsManager() = default;

    void clean_and_send(std::vector<std::shared_ptr<IWebSocketSession>>& sessions, const std::string& json_str) {
        sessions.erase(
            std::remove_if(sessions.begin(), sessions.end(), [&](const std::shared_ptr<IWebSocketSession>& s) {
                if (!s || !s->is_open()) return true;
                s->send_json(json_str);
                return false;
            }),
            sessions.end()
        );
    }

    std::unordered_map<std::string, std::vector<std::shared_ptr<IWebSocketSession>>> charging_subs_;
    std::unordered_map<int64_t, std::vector<std::shared_ptr<IWebSocketSession>>> station_subs_;
    std::vector<std::shared_ptr<IWebSocketSession>> global_subs_;
    std::mutex mutex_;
};

} // namespace ev
