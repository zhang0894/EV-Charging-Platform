#pragma once

#include "../common/types.hpp"
#include "../common/models.hpp"
#include "../data/static_stations.hpp"
#include <string>
#include <vector>
#include <unordered_map>
#include <unordered_set>
#include <shared_mutex>
#include <optional>
#include <algorithm>
#include <fstream>
#include <filesystem>
#include <random>

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

    // 预约与模拟标记
    bool is_simulated{false};
    int64_t reserved_user_id{0};
    std::string reservation_id;
    int64_t reservation_expire_time{0};
};

struct StationPileSummary {
    int total_piles{0};
    int idle_piles{0};
    int fast_piles_idle{0};
    int slow_piles_idle{0};
    int busy_piles{0};
    int fault_piles{0};
    int reserved_piles{0};
    bool has_fast_pile{false};
};

class ChargingStatePool {
public:
    static ChargingStatePool& instance() {
        static ChargingStatePool pool;
        return pool;
    }

    // 优先从 seed_piles.json 读取全量充电桩数据（与数据库保持 100% 同步），若不存在则降级为程序化生成
    bool init_from_seed_piles(const std::string& data_dir = "data") {
        std::vector<std::string> candidates = {
            data_dir + "/seed_piles.json",
            "server/data/seed_piles.json",
            "data/seed_piles.json",
            "../data/seed_piles.json",
            "../server/data/seed_piles.json",
            "../../server/data/seed_piles.json",
            "e:/EV-Charging-Platform/server/data/seed_piles.json"
        };
        std::string p_path;
        for (const auto& p : candidates) {
            if (std::filesystem::exists(p)) {
                p_path = p;
                break;
            }
        }
        if (p_path.empty()) {
            init_from_static_stations();
            return false;
        }

        std::ifstream ifs(p_path, std::ios::binary);
        if (!ifs) {
            init_from_static_stations();
            return false;
        }
        ifs.seekg(0, std::ios::end);
        size_t sz = ifs.tellg();
        ifs.seekg(0, std::ios::beg);
        std::string buf(sz, '\0');
        ifs.read(buf.data(), sz);

        struct JsonPile {
            std::string pile_id{};
            int32_t station_id{};
            std::string pile_name{};
            std::string type{};
            std::string gun_type{};
            double max_power_kw{};
            std::string voltage_range{};
            std::string status{};
        };

        std::vector<JsonPile> piles;
        auto ec = glz::read_json(piles, buf);
        if (ec || piles.empty()) {
            init_from_static_stations();
            return false;
        }

        std::unique_lock<std::shared_mutex> lock(mutex_);
        piles_.clear();
        piles_.reserve(piles.size());
        active_charging_pile_ids_.clear();
        station_pile_ids_.assign(STATIC_STATION_COUNT + 1, {});

        int64_t now = current_time_ms();
        for (const auto& p : piles) {
            if (p.station_id >= 1 && static_cast<size_t>(p.station_id) < station_pile_ids_.size()) {
                station_pile_ids_[p.station_id].push_back(p.pile_id);
            }

            // 智能数据模拟：各站首桩始终保持 IDLE 方便测试；其余桩模拟约 25% 占用率
            std::string st = p.status;
            bool is_first_pile = p.pile_id.ends_with("_01");
            if (is_first_pile) {
                st = "IDLE";
            } else if (st == "IDLE") {
                size_t h = std::hash<std::string>{}(p.pile_id);
                if (h % 100 < 25) {
                    st = "CHARGING";
                }
            }

            bool is_chg = (st == "CHARGING");
            int init_soc = is_chg ? (30 + static_cast<int>(std::hash<std::string>{}(p.pile_id) % 55)) : 0;
            double chg_power = is_chg ? (p.type == "FAST" ? (60.0 + (init_soc % 60)) : 7.0) : 0.0;
            double volt = is_chg ? (380.0 + init_soc * 0.4) : 0.0;
            double curr = (is_chg && volt > 0) ? (chg_power * 1000.0 / volt) : 0.0;
            std::string sim_order = is_chg ? std::format("SIM_ORD_{}_{}", p.pile_id, now) : "";

            piles_[p.pile_id] = PileRuntimeState{
                .pile_id = p.pile_id,
                .station_id = p.station_id,
                .pile_name = p.pile_name,
                .type = p.type,
                .max_power_kw = p.max_power_kw,
                .status = st,
                .voltage_v = volt,
                .current_a = curr,
                .power_kw = chg_power,
                .current_soc = init_soc,
                .temperature_celsius = is_chg ? (30.0 + (init_soc * 0.15)) : 25.0,
                .charged_energy_kwh = is_chg ? (15.0 + (init_soc * 0.3)) : 0.0,
                .electricity_price = 1.45,
                .electricity_fee_cents = 0,
                .service_price = 0.35,
                .service_fee_cents = 0,
                .is_full = false,
                .full_timestamp = 0,
                .overtime_grace_minutes = 15,
                .overtime_rate_per_15min = 5.00,
                .overtime_duration_minutes = 0,
                .overtime_fee_cents = 0,
                .total_fee_cents = 0,
                .active_order_id = sim_order,
                .user_id = 0,
                .start_time = is_chg ? (now - 1200000) : 0,
                .last_update_time = now,
                .is_simulated = is_chg,
                .reserved_user_id = 0,
                .reservation_id = "",
                .reservation_expire_time = 0
            };

            if (is_chg) {
                active_charging_pile_ids_.insert(p.pile_id);
            }
        }
        return true;
    }

    // 根据真实电站数据 (8,569座)，为每座充电站随机分配 5 ~ 30 个充电桩
    void init_from_static_stations() {
        std::unique_lock<std::shared_mutex> lock(mutex_);
        piles_.clear();
        piles_.reserve(STATIC_STATION_COUNT * 20);
        active_charging_pile_ids_.clear();
        station_pile_ids_.assign(STATIC_STATION_COUNT + 1, {});

        int64_t now = current_time_ms();

        for (const auto& s : STATIC_STATIONS) {
            std::mt19937 rng(static_cast<uint32_t>(10007 + s.station_id));
            std::uniform_int_distribution<int> pile_count_dist(5, 30);
            int count = pile_count_dist(rng);

            auto& s_piles = station_pile_ids_[s.station_id];
            s_piles.reserve(count);

            for (int i = 1; i <= count; ++i) {
                std::string pid = std::format("P{:05d}_{:02d}", s.station_id, i);
                // 保证第1个桩以及多数桩为快充，其余为慢充
                bool is_fast = (i == 1) || (i % 3 != 0); // 约 70% 直流快充
                std::string ptype = is_fast ? "FAST" : "SLOW";
                double power = is_fast ? 120.0 : 7.0;
                std::string pname = std::format("{}-{}号{}", s.name, i, (is_fast ? "快充桩" : "慢充桩"));

                std::string st = "IDLE";
                if (i == 1) {
                    st = "IDLE"; // 1号桩保持空闲
                } else if (i == 4 && count > 10) {
                    st = "FAULT";
                } else if (i % 4 == 0 || i % 7 == 0) {
                    st = "CHARGING";
                }

                bool is_chg = (st == "CHARGING");
                int init_soc = is_chg ? (35 + (i * 7) % 50) : 0;
                double chg_power = is_chg ? (is_fast ? 90.0 : 7.0) : 0.0;
                double volt = is_chg ? (380.0 + init_soc * 0.4) : 0.0;
                double curr = (is_chg && volt > 0) ? (chg_power * 1000.0 / volt) : 0.0;
                std::string sim_order = is_chg ? std::format("SIM_ORD_{}_{}", pid, now) : "";

                piles_[pid] = PileRuntimeState{
                    .pile_id = pid,
                    .station_id = s.station_id,
                    .pile_name = pname,
                    .type = ptype,
                    .max_power_kw = power,
                    .status = st,
                    .voltage_v = volt,
                    .current_a = curr,
                    .power_kw = chg_power,
                    .current_soc = init_soc,
                    .temperature_celsius = is_chg ? (30.0 + (init_soc * 0.15)) : 25.0,
                    .charged_energy_kwh = is_chg ? (12.0 + (init_soc * 0.25)) : 0.0,
                    .electricity_price = 1.45,
                    .electricity_fee_cents = 0,
                    .service_price = 0.35,
                    .service_fee_cents = 0,
                    .is_full = false,
                    .full_timestamp = 0,
                    .overtime_grace_minutes = 15,
                    .overtime_rate_per_15min = 5.00,
                    .overtime_duration_minutes = 0,
                    .overtime_fee_cents = 0,
                    .total_fee_cents = 0,
                    .active_order_id = sim_order,
                    .user_id = 0,
                    .start_time = is_chg ? (now - 1200000) : 0,
                    .last_update_time = now,
                    .is_simulated = is_chg,
                    .reserved_user_id = 0,
                    .reservation_id = "",
                    .reservation_expire_time = 0
                };

                if (is_chg) {
                    active_charging_pile_ids_.insert(pid);
                }

                s_piles.push_back(pid);
            }
        }
    }

    void init_from_piles(const std::vector<PileModel>& piles) {
        std::unique_lock<std::shared_mutex> lock(mutex_);
        piles_.clear();
        piles_.reserve(piles.size());
        active_charging_pile_ids_.clear();
        station_pile_ids_.clear();

        int64_t now = current_time_ms();
        for (const auto& p : piles) {
            if (p.station_id >= 0) {
                if (static_cast<size_t>(p.station_id) >= station_pile_ids_.size()) {
                    station_pile_ids_.resize(p.station_id + 1);
                }
                station_pile_ids_[p.station_id].push_back(p.pile_id);
            }

            std::string st = p.status;
            bool is_first_pile = p.pile_id.ends_with("_01");
            if (is_first_pile) {
                st = "IDLE";
            } else if (st == "IDLE") {
                size_t h = std::hash<std::string>{}(p.pile_id);
                if (h % 100 < 25) {
                    st = "CHARGING";
                }
            }

            bool is_chg = (st == "CHARGING");
            int init_soc = is_chg ? (30 + static_cast<int>(std::hash<std::string>{}(p.pile_id) % 55)) : 0;
            double chg_power = is_chg ? (p.type == "FAST" ? 80.0 : 7.0) : 0.0;
            double volt = is_chg ? (380.0 + init_soc * 0.4) : 0.0;
            double curr = (is_chg && volt > 0) ? (chg_power * 1000.0 / volt) : 0.0;
            std::string sim_order = is_chg ? std::format("SIM_ORD_{}_{}", p.pile_id, now) : "";

            piles_[p.pile_id] = PileRuntimeState{
                .pile_id = p.pile_id,
                .station_id = p.station_id,
                .pile_name = p.pile_name,
                .type = p.type,
                .max_power_kw = p.max_power_kw,
                .status = st,
                .voltage_v = volt,
                .current_a = curr,
                .power_kw = chg_power,
                .current_soc = init_soc,
                .temperature_celsius = 25.0,
                .charged_energy_kwh = is_chg ? 15.0 : 0.0,
                .active_order_id = sim_order,
                .start_time = is_chg ? (now - 1200000) : 0,
                .last_update_time = now,
                .is_simulated = is_chg,
                .reserved_user_id = 0,
                .reservation_id = "",
                .reservation_expire_time = 0
            };
            if (is_chg) {
                active_charging_pile_ids_.insert(p.pile_id);
            }
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

        if (station_id >= 1 && static_cast<size_t>(station_id) < station_pile_ids_.size()) {
            const auto& pids = station_pile_ids_[station_id];
            result.reserve(pids.size());
            for (const auto& pid : pids) {
                auto it = piles_.find(pid);
                if (it != piles_.end()) {
                    result.push_back(it->second);
                }
            }
        } else {
            for (const auto& [_, p] : piles_) {
                if (p.station_id == station_id) {
                    result.push_back(p);
                }
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

        auto check_pile = [&](const PileRuntimeState& p) {
            sum.total_piles++;
            if (p.type == "FAST") sum.has_fast_pile = true;

            if (p.status == "IDLE") {
                sum.idle_piles++;
                if (p.type == "FAST") sum.fast_piles_idle++;
                else sum.slow_piles_idle++;
            } else if (p.status == "CHARGING" || p.status == "PREPARING" || p.status == "FINISHING") {
                sum.busy_piles++;
            } else if (p.status == "RESERVED") {
                sum.busy_piles++;
                sum.reserved_piles++;
            } else if (p.status == "FAULT" || p.status == "MAINTENANCE" || p.status == "OFFLINE") {
                sum.fault_piles++;
            }
        };

        if (station_id >= 1 && static_cast<size_t>(station_id) < station_pile_ids_.size()) {
            const auto& pids = station_pile_ids_[station_id];
            for (const auto& pid : pids) {
                auto it = piles_.find(pid);
                if (it != piles_.end()) {
                    check_pile(it->second);
                }
            }
        } else {
            for (const auto& [_, p] : piles_) {
                if (p.station_id == station_id) {
                    check_pile(p);
                }
            }
        }
        return sum;
    }

    void set_station_piles_offline(int64_t station_id) {
        std::unique_lock<std::shared_mutex> lock(mutex_);
        int64_t now = current_time_ms();
        if (station_id >= 1 && static_cast<size_t>(station_id) < station_pile_ids_.size()) {
            for (const auto& pid : station_pile_ids_[station_id]) {
                auto it = piles_.find(pid);
                if (it != piles_.end()) {
                    it->second.status = "OFFLINE";
                    it->second.voltage_v = 0.0;
                    it->second.current_a = 0.0;
                    it->second.power_kw = 0.0;
                    it->second.last_update_time = now;
                    active_charging_pile_ids_.erase(pid);
                }
            }
        }
    }

    void set_station_piles_online(int64_t station_id) {
        std::unique_lock<std::shared_mutex> lock(mutex_);
        int64_t now = current_time_ms();
        if (station_id >= 1 && static_cast<size_t>(station_id) < station_pile_ids_.size()) {
            for (const auto& pid : station_pile_ids_[station_id]) {
                auto it = piles_.find(pid);
                if (it != piles_.end()) {
                    if (it->second.status == "OFFLINE") {
                        it->second.status = "IDLE";
                        it->second.last_update_time = now;
                    }
                }
            }
        }
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
        if (p.status != "IDLE") {
            // 如果是被当前用户预约的桩，允许开枪；他人或其他状态不可用
            if (p.status == "RESERVED" && p.reserved_user_id == user_id) {
                // 预约车主到场开枪
            } else {
                return false;
            }
        }

        int64_t now = current_time_ms();
        p.status = "CHARGING";
        p.is_simulated = false;
        p.reserved_user_id = 0;
        p.reservation_id.clear();
        p.reservation_expire_time = 0;
        active_charging_pile_ids_.insert(std::string(pile_id));
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
        p.is_simulated = false;
        p.reserved_user_id = 0;
        p.reservation_id.clear();
        p.reservation_expire_time = 0;
        active_charging_pile_ids_.erase(std::string(pile_id));
        p.voltage_v = 0.0;
        p.current_a = 0.0;
        p.power_kw = 0.0;
        p.last_update_time = current_time_ms();

        return p;
    }

    bool reserve_pile(std::string_view pile_id, int64_t user_id, std::string_view reservation_id, int64_t expire_time) {
        std::unique_lock<std::shared_mutex> lock(mutex_);
        auto it = piles_.find(std::string(pile_id));
        if (it == piles_.end()) return false;
        if (it->second.status != "IDLE") return false;

        it->second.status = "RESERVED";
        it->second.is_simulated = false;
        it->second.reserved_user_id = user_id;
        it->second.reservation_id = std::string(reservation_id);
        it->second.reservation_expire_time = expire_time;
        it->second.last_update_time = current_time_ms();
        return true;
    }

    bool release_reserved_pile(std::string_view pile_id) {
        std::unique_lock<std::shared_mutex> lock(mutex_);
        auto it = piles_.find(std::string(pile_id));
        if (it == piles_.end()) return false;
        if (it->second.status == "RESERVED") {
            it->second.status = "IDLE";
            it->second.reserved_user_id = 0;
            it->second.reservation_id.clear();
            it->second.reservation_expire_time = 0;
            it->second.last_update_time = current_time_ms();
            return true;
        }
        return false;
    }

    void maintain_simulation(int64_t now) {
        std::unique_lock<std::shared_mutex> lock(mutex_);
        if (active_charging_pile_ids_.size() < 1500) {
            static uint32_t station_cursor = 1;
            int added = 0;
            for (size_t i = 0; i < 30 && added < 3; ++i) {
                station_cursor = (station_cursor % STATIC_STATION_COUNT) + 1;
                if (station_cursor >= station_pile_ids_.size()) continue;

                const auto& pids = station_pile_ids_[station_cursor];
                for (size_t j = 1; j < pids.size() && added < 3; ++j) {
                    auto it = piles_.find(pids[j]);
                    if (it != piles_.end() && it->second.status == "IDLE") {
                        auto& p = it->second;
                        p.status = "CHARGING";
                        p.is_simulated = true;
                        p.current_soc = 25 + static_cast<int>(now % 30);
                        p.power_kw = (p.type == "FAST" ? 90.0 : 7.0);
                        p.voltage_v = 380.0;
                        p.current_a = p.power_kw * 1000.0 / p.voltage_v;
                        p.charged_energy_kwh = 5.0;
                        p.is_full = false;
                        p.full_timestamp = 0;
                        p.active_order_id = std::format("SIM_ORD_{}_{}", p.pile_id, now);
                        p.start_time = now;
                        p.last_update_time = now;
                        active_charging_pile_ids_.insert(p.pile_id);
                        added++;
                        break;
                    }
                }
            }
        }
    }

    void set_pile_status(std::string_view pile_id, std::string_view status) {
        std::unique_lock<std::shared_mutex> lock(mutex_);
        auto it = piles_.find(std::string(pile_id));
        if (it != piles_.end()) {
            it->second.status = std::string(status);
            it->second.last_update_time = current_time_ms();
            if (status == "CHARGING") {
                active_charging_pile_ids_.insert(std::string(pile_id));
            } else {
                active_charging_pile_ids_.erase(std::string(pile_id));
                it->second.voltage_v = 0.0;
                it->second.current_a = 0.0;
                it->second.power_kw = 0.0;
            }
        }
    }

    // O(Active) 高性能增量扫描：仅遍历活跃充电桩，消除 100,000 次哈希桶遍历与读写锁争用
    std::vector<PileRuntimeState> get_all_active_charging_piles() const {
        std::shared_lock<std::shared_mutex> lock(mutex_);
        std::vector<PileRuntimeState> active;
        active.reserve(active_charging_pile_ids_.size());
        for (const auto& pid : active_charging_pile_ids_) {
            auto it = piles_.find(pid);
            if (it != piles_.end() && it->second.status == "CHARGING") {
                active.push_back(it->second);
            }
        }
        return active;
    }

    void update_pile_state(const PileRuntimeState& state) {
        std::unique_lock<std::shared_mutex> lock(mutex_);
        piles_[state.pile_id] = state;
        if (state.status == "CHARGING") {
            active_charging_pile_ids_.insert(state.pile_id);
        } else {
            active_charging_pile_ids_.erase(state.pile_id);
        }
    }

private:
    ChargingStatePool() = default;
    std::unordered_map<std::string, PileRuntimeState> piles_;
    std::unordered_set<std::string> active_charging_pile_ids_;
    std::vector<std::vector<std::string>> station_pile_ids_;
    mutable std::shared_mutex mutex_;
};

} // namespace ev
