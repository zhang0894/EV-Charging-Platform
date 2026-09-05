#include "simulator.hpp"
#include "../memory/state_pool.hpp"
#include "../websocket/ws_manager.hpp"
#include "../common/types.hpp"
#include "../db/db_repository.hpp"
#include <iostream>
#include <random>

namespace ev {

void ChargingSimulator::start(boost::asio::io_context& ioc, int interval_ms) {
    if (is_running_) return;

    ioc_ = &ioc;
    interval_ms_ = interval_ms;
    is_running_ = true;
    timer_ = std::make_unique<boost::asio::steady_timer>(*ioc_);

    schedule_tick();
    std::cout << "[Simulator] Dynamic pile simulation engine started.\n";
}

void ChargingSimulator::stop() {
    is_running_ = false;
    if (timer_) {
        timer_->cancel();
    }
    std::cout << "[Simulator] Dynamic pile simulation engine stopped.\n";
}

void ChargingSimulator::schedule_tick() {
    if (!is_running_ || !timer_) return;

    timer_->expires_after(std::chrono::milliseconds(interval_ms_));
    timer_->async_wait([this](const boost::system::error_code& ec) {
        if (!ec && is_running_) {
            step_once(static_cast<double>(interval_ms_) / 1000.0);
            schedule_tick();
        }
    });
}

void ChargingSimulator::step_once(double delta_seconds) {
    int64_t now = current_time_ms();

    // 1. 每秒扫描一次超时预约单 (2分钟超时未到场)
    static int64_t last_res_check_time = 0;
    if (now - last_res_check_time >= 1000) {
        last_res_check_time = now;
        auto timed_out_piles = DbRepository::instance().timeout_expired_reservations();
        if (timed_out_piles && !timed_out_piles->empty()) {
            for (const auto& pid : *timed_out_piles) {
                auto p_opt = ChargingStatePool::instance().get_pile_state(pid);
                int64_t sid = p_opt ? p_opt->station_id : 0;
                ChargingStatePool::instance().release_reserved_pile(pid);
                WsManager::instance().broadcast_pile_status(PileStatusChangedBroadcastFrame{
                    .event = "PILE_STATUS_CHANGED",
                    .station_id = sid,
                    .pile_id = pid,
                    .old_status = "RESERVED",
                    .new_status = "IDLE",
                    .new_status_code = 1,
                    .timestamp = now
                });
            }
        }
    }

    // 2. 定期动态补充模拟车流 (每5秒一次)
    static int64_t last_sim_maintain_time = 0;
    if (now - last_sim_maintain_time >= 5000) {
        last_sim_maintain_time = now;
        ChargingStatePool::instance().maintain_simulation(now);
    }

    // 3. 推进活跃充电桩推演
    auto active_piles = ChargingStatePool::instance().get_all_active_charging_piles();

    for (auto& pile : active_piles) {
        int64_t elapsed_sec = (now - pile.start_time) / 1000;

        // 模拟车辆充满并拔枪离开机制 (充满后15秒离场，恢复 IDLE)
        if (pile.is_simulated && pile.is_full && (now - pile.full_timestamp) >= 15000) {
            pile.status = "IDLE";
            pile.is_simulated = false;
            pile.voltage_v = 0.0;
            pile.current_a = 0.0;
            pile.power_kw = 0.0;
            pile.active_order_id.clear();
            ChargingStatePool::instance().update_pile_state(pile);

            WsManager::instance().broadcast_pile_status(PileStatusChangedBroadcastFrame{
                .event = "PILE_STATUS_CHANGED",
                .station_id = pile.station_id,
                .pile_id = pile.pile_id,
                .old_status = "CHARGING",
                .new_status = "IDLE",
                .new_status_code = 1,
                .timestamp = now
            });
            continue;
        }

        if (!pile.is_full) {
            // 恒流恒压充电模拟曲线
            pile.voltage_v = 380.0 + (pile.current_soc * 0.4); // 380V -> 420V
            double base_current = (pile.type == "FAST") ? 150.0 : 32.0;

            // SOC > 80% 时逐渐降流保护电池
            if (pile.current_soc > 80) {
                double factor = 1.0 - (static_cast<double>(pile.current_soc - 80) / 20.0) * 0.6; // 降至 40%
                pile.current_a = base_current * factor;
            } else {
                pile.current_a = base_current;
            }

            pile.power_kw = (pile.voltage_v * pile.current_a) / 1000.0;
            if (pile.power_kw > pile.max_power_kw) {
                pile.power_kw = pile.max_power_kw;
            }

            // 电量增量
            double delta_kwh = pile.power_kw * (delta_seconds / 3600.0);
            pile.charged_energy_kwh += delta_kwh;

            // SOC 增长 (模拟60度电池包)
            constexpr double BATTERY_CAPACITY_KWH = 60.0;
            int soc_inc = static_cast<int>((pile.charged_energy_kwh / BATTERY_CAPACITY_KWH) * 100.0);
            int new_soc = std::min(100, (pile.is_simulated ? pile.current_soc : 20) + (pile.is_simulated ? (soc_inc > 0 ? 1 : 0) : soc_inc));
            if (pile.is_simulated && delta_seconds > 0) {
                // 模拟车辆缓慢递增 SOC
                pile.current_soc = std::min(100, pile.current_soc + (delta_seconds >= 1.0 ? 1 : 0));
            } else {
                pile.current_soc = new_soc;
            }

            // 电费与服务费累计
            pile.electricity_fee_cents = yuan_to_cents(pile.charged_energy_kwh * pile.electricity_price);
            pile.service_fee_cents = yuan_to_cents(pile.charged_energy_kwh * pile.service_price);

            // 电池温升模拟
            pile.temperature_celsius = 25.0 + (pile.power_kw * 0.15) + (pile.current_soc * 0.05);

            if (pile.current_soc >= 100) {
                pile.is_full = true;
                pile.full_timestamp = now;
                pile.voltage_v = 0.0;
                pile.current_a = 0.0;
                pile.power_kw = 0.0;
            }
        } else {
            // 已充满，进入超时占位费计时
            int64_t full_duration_ms = now - pile.full_timestamp;
            pile.overtime_duration_minutes = static_cast<int>(full_duration_ms / 60000);

            if (pile.overtime_duration_minutes > pile.overtime_grace_minutes) {
                int excess_mins = pile.overtime_duration_minutes - pile.overtime_grace_minutes;
                int periods = (excess_mins - 1) / 15 + 1;
                pile.overtime_fee_cents = yuan_to_cents(periods * pile.overtime_rate_per_15min);
            } else {
                pile.overtime_fee_cents = 0;
            }
        }

        pile.total_fee_cents = pile.electricity_fee_cents + pile.service_fee_cents + pile.overtime_fee_cents;
        pile.last_update_time = now;

        // 更新状态池
        ChargingStatePool::instance().update_pile_state(pile);

        // 发送 WebSocket 遥测流 (仅真实用户订单或有订阅者时广播，避免浪费 CPU)
        if (!pile.is_simulated || WsManager::instance().has_charging_subscribers(pile.active_order_id)) {
            std::string warning = "";
            if (pile.is_full && pile.overtime_duration_minutes > pile.overtime_grace_minutes) {
                warning = "车辆已充满并超出15分钟免费占位期，当前持续计收超时占位费！";
            }

            TelemetryFrame frame{
                .event = "TELEMETRY_UPDATE",
                .order_id = pile.active_order_id,
                .pile_id = pile.pile_id,
                .data = TelemetryUpdateData{
                    .timestamp = now,
                    .voltage_v = pile.voltage_v,
                    .current_a = pile.current_a,
                    .power_kw = pile.power_kw,
                    .current_soc = pile.current_soc,
                    .charged_energy_kwh = pile.charged_energy_kwh,
                    .charging_fee = cents_to_yuan(pile.electricity_fee_cents),
                    .service_fee = cents_to_yuan(pile.service_fee_cents),
                    .overtime_duration_minutes = pile.overtime_duration_minutes,
                    .overtime_grace_minutes = pile.overtime_grace_minutes,
                    .overtime_fee = cents_to_yuan(pile.overtime_fee_cents),
                    .current_total_fee = cents_to_yuan(pile.total_fee_cents),
                    .temperature_celsius = pile.temperature_celsius,
                    .elapsed_seconds = elapsed_sec,
                    .is_full = pile.is_full,
                    .full_timestamp = pile.full_timestamp,
                    .warning_message = warning
                }
            };

            WsManager::instance().broadcast_telemetry(frame);
        }
    }
}

} // namespace ev
