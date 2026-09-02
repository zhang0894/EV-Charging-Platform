#pragma once

#include "../common/types.hpp"
#include "../common/models.hpp"
#include <string>
#include <vector>
#include <unordered_map>
#include <shared_mutex>
#include <optional>
#include <algorithm>

namespace ev {

struct PileRuntimeState {
    std::string pile_id;
    int64_t station_id{0};
    std::string pile_name;
    std::string type{"FAST"}; // FAST, SLOW
    double max_power_kw{120.0};
    std::string status{"IDLE"}; // IDLE, CHARGING, FAULT, MAINTENANCE, OFFLINE

    // 实时遥测指标
    double voltage_v{0.0};
    double current_a{0.0};
    double power_kw{0.0};
    int current_soc{0};
    double temperature_celsius{25.0};

    // 计费与电量累计
    double charged_energy_kwh{0.0};
    double electricity_price{1.45};
    int64_t electricity_fee_cents{0};
    double service_price{0.35};
    int64_t service_fee_cents{0};

    // 超时占位费机制
    bool is_full{false};
    int64_t full_timestamp{0};
    int overtime_grace_minutes{15};
    double overtime_rate_per_15min{5.00};
    int overtime_duration_minutes{0};
    int64_t overtime_fee_cents{0};
    int64_t total_fee_cents{0};

    // 会话与关联订单
    std::string active_order_id;
    int64_t user_id{0};
    int64_t start_time{0};
    int64_t last_update_time{0};
};

struct StationPileSummary {
    int total_piles{0};
    int idle_piles{0};
    int fast_piles_idle{0};
    int slow_piles_idle{0};
    int busy_piles{0};
    int fault_piles{0};
};

class ChargingStatePool {
public:
    static ChargingStatePool& instance() {
        static ChargingStatePool pool;
        return pool;
    }

    void init_from_piles(const std::vector<PileModel>& piles) {
        std::unique_lock<std::shared_mutex> lock(mutex_);
        piles_.clear();
        piles_.reserve(piles.size());

        for (const auto& p : piles) {
            piles_[p.pile_id] = PileRuntimeState{
                .pile_id = p.pile_id,
                .station_id = p.station_id,
                .pile_name = p.pile_name,
                .type = p.type,
                .max_power_kw = p.max_power_kw,
                .status = p.status,
                .voltage_v = (p.status == "CHARGING" ? 398.0 : 0.0),
                .current_a = (p.status == "CHARGING" ? 150.0 : 0.0),
                .power_kw = (p.status == "CHARGING" ? p.max_power_kw * 0.8 : 0.0),
                .current_soc = (p.status == "CHARGING" ? 50 : 0),
                .temperature_celsius = 25.0,
                .charged_energy_kwh = 0.0,
                .last_update_time = current_time_ms()
            };
        }
    }

    std::optional<PileRuntimeState> get_pile_state(std::string_view pile_id) const {
        std::shared_lock<std::shared_mutex> lock(mutex_);
        auto it = piles_.find(std::string(pile_id));
        if (it != piles_.end()) {
            return it->second;
        }
        return std::nullopt;
    }

    std::vector<PileRuntimeState> get_piles_by_station(int64_t station_id) const {
        std::shared_lock<std::shared_mutex> lock(mutex_);
        std::vector<PileRuntimeState> result;
        for (const auto& [_, p] : piles_) {
            if (p.station_id == station_id) {
                result.push_back(p);
            }
        }
        std::sort(result.begin(), result.end(), [](const auto& a, const auto& b) {
            return a.pile_id < b.pile_id;
        });
        return result;
    }

    StationPileSummary get_station_pile_summary(int64_t station_id) const {
        std::shared_lock<std::shared_mutex> lock(mutex_);
        StationPileSummary sum;

        for (const auto& [_, p] : piles_) {
            if (p.station_id == station_id) {
                sum.total_piles++;
                if (p.status == "IDLE") {
                    sum.idle_piles++;
                    if (p.type == "FAST") sum.fast_piles_idle++;
                    else sum.slow_piles_idle++;
                } else if (p.status == "CHARGING" || p.status == "PREPARING" || p.status == "FINISHING") {
                    sum.busy_piles++;
                } else if (p.status == "FAULT" || p.status == "MAINTENANCE" || p.status == "OFFLINE") {
                    sum.fault_piles++;
                }
            }
        }
        return sum;
    }

    bool start_charging(
        std::string_view pile_id,
        std::string_view order_id,
        int64_t user_id,
        int initial_soc,
        double elec_price,
        double serv_price,
        double overtime_rate,
        int grace_mins
    ) {
        std::unique_lock<std::shared_mutex> lock(mutex_);
        auto it = piles_.find(std::string(pile_id));
        if (it == piles_.end()) return false;

        auto& p = it->second;
        if (p.status != "IDLE") return false;

        int64_t now = current_time_ms();
        p.status = "CHARGING";
        p.active_order_id = std::string(order_id);
        p.user_id = user_id;
        p.start_time = now;
        p.last_update_time = now;
        p.current_soc = initial_soc;
        p.charged_energy_kwh = 0.0;
        p.electricity_price = elec_price;
        p.service_price = serv_price;
        p.overtime_rate_per_15min = overtime_rate;
        p.overtime_grace_minutes = grace_mins;
        p.is_full = false;
        p.full_timestamp = 0;
        p.overtime_duration_minutes = 0;
        p.overtime_fee_cents = 0;
        p.electricity_fee_cents = 0;
        p.service_fee_cents = 0;
        p.total_fee_cents = 0;

        // 初始电压电流
        p.voltage_v = 380.0;
        p.current_a = (p.type == "FAST" ? 150.0 : 32.0);
        p.power_kw = (p.voltage_v * p.current_a) / 1000.0;

        return true;
    }

    std::optional<PileRuntimeState> stop_charging(std::string_view pile_id) {
        std::unique_lock<std::shared_mutex> lock(mutex_);
        auto it = piles_.find(std::string(pile_id));
        if (it == piles_.end()) return std::nullopt;

        auto& p = it->second;
        p.status = "IDLE";
        p.voltage_v = 0.0;
        p.current_a = 0.0;
        p.power_kw = 0.0;
        p.last_update_time = current_time_ms();

        return p;
    }

    void set_pile_status(std::string_view pile_id, std::string_view status) {
        std::unique_lock<std::shared_mutex> lock(mutex_);
        auto it = piles_.find(std::string(pile_id));
        if (it != piles_.end()) {
            it->second.status = std::string(status);
            it->second.last_update_time = current_time_ms();
            if (status != "CHARGING") {
                it->second.voltage_v = 0.0;
                it->second.current_a = 0.0;
                it->second.power_kw = 0.0;
            }
        }
    }

    std::vector<PileRuntimeState> get_all_active_charging_piles() const {
        std::shared_lock<std::shared_mutex> lock(mutex_);
        std::vector<PileRuntimeState> active;
        for (const auto& [_, p] : piles_) {
            if (p.status == "CHARGING") {
                active.push_back(p);
            }
        }
        return active;
    }

    void update_pile_state(const PileRuntimeState& state) {
        std::unique_lock<std::shared_mutex> lock(mutex_);
        piles_[state.pile_id] = state;
    }

private:
    ChargingStatePool() = default;
    std::unordered_map<std::string, PileRuntimeState> piles_;
    mutable std::shared_mutex mutex_;
};

} // namespace ev
