#pragma once

#include "../common/types.hpp"
#include "../common/models.hpp"
#include "../data/static_stations.hpp"
#include "../db/db_pool.hpp"
#include "station_status_manager.hpp"
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
#include <iostream>
#include <cmath>
#include <print>

namespace ev {

struct PileRuntimeState {
    std::string pile_id;
    int64_t station_id{0};
    std::string pile_name;
    std::string type{"FAST"}; // FAST, SLOW
    double max_power_kw{120.0};
    std::string status{"IDLE"}; // IDLE, CHARGING, FAULT, OFFLINE
    std::string pre_station_offline_status{"IDLE"}; // 记录电站下线前各个充电桩的状态 (IDLE / FAULT / OFFLINE)

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

    // 历史累计与心跳指标
    int64_t total_charge_count{0};
    double total_charge_hours{0.0};
    int64_t last_heartbeat_at{0};
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

inline std::string normalize_pile_id(std::string_view pid) {
    size_t first = pid.find_first_not_of(" \t\r\n\"'");
    if (first == std::string_view::npos) return "";
    size_t last = pid.find_last_not_of(" \t\r\n\"'");
    std::string s(pid.substr(first, last - first + 1));
    if (!s.empty() && (s[0] == 'p' || s[0] == 'P')) {
        s[0] = 'P';
    }
    return s;
}

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
            int64_t total_charge_count{};
            double total_charge_hours{};
            int64_t last_heartbeat_at{};
            int64_t created_at{};
            int64_t updated_at{};
        };

        std::vector<JsonPile> piles;
        auto ec = glz::read<glz::opts{.error_on_unknown_keys = false}>(piles, buf);
        if (ec || piles.empty()) {
            std::cerr << "[StatePool] Warning: Failed to parse seed_piles.json (" 
                      << glz::format_error(ec, buf) << "), falling back to static stations.\n";
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
                .pre_station_offline_status = (st == "FAULT" || st == "OFFLINE") ? st : "IDLE",
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
                .reservation_expire_time = 0,
                .total_charge_count = p.total_charge_count,
                .total_charge_hours = p.total_charge_hours,
                .last_heartbeat_at = p.last_heartbeat_at > 0 ? p.last_heartbeat_at : now
            };

            if (is_chg) {
                active_charging_pile_ids_.insert(p.pile_id);
            }
        }
        lock.unlock();
        sync_missing_piles_from_db();
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
                    .pre_station_offline_status = (st == "FAULT" || st == "OFFLINE") ? st : "IDLE",
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
                    .reservation_expire_time = 0,
                    .total_charge_count = 0,
                    .total_charge_hours = 0.0,
                    .last_heartbeat_at = now
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
                .reservation_expire_time = 0,
                .total_charge_count = p.total_charge_count,
                .total_charge_hours = p.total_charge_hours,
                .last_heartbeat_at = p.last_heartbeat_at > 0 ? p.last_heartbeat_at : now
            };
            if (is_chg) {
                active_charging_pile_ids_.insert(p.pile_id);
            }
        }
    }

    std::optional<PileRuntimeState> get_pile_state(std::string_view pile_id) const {
        std::string norm = normalize_pile_id(pile_id);
        if (norm.empty()) return std::nullopt;

        {
            std::shared_lock<std::shared_mutex> lock(mutex_);
            auto it = piles_.find(norm);
            if (it != piles_.end()) {
                PileRuntimeState p = it->second;
                if (!StationStatusManager::instance().is_online(p.station_id)) {
                    p.status = "OFFLINE";
                    p.voltage_v = 0.0;
                    p.current_a = 0.0;
                    p.power_kw = 0.0;
                }
                return p;
            }
        }

        // 内存未命中，触发从数据库按需懒加载兜底
        auto db_state = sync_single_pile_from_db(norm);
        if (db_state) {
            if (!StationStatusManager::instance().is_online(db_state->station_id)) {
                db_state->status = "OFFLINE";
                db_state->voltage_v = 0.0;
                db_state->current_a = 0.0;
                db_state->power_kw = 0.0;
            }
            return db_state;
        }

        return std::nullopt;
    }

    std::string_view get_pile_status(std::string_view pile_id) const {
        std::string norm = normalize_pile_id(pile_id);
        if (norm.empty()) return "IDLE";

        {
            std::shared_lock<std::shared_mutex> lock(mutex_);
            auto it = piles_.find(norm);
            if (it != piles_.end()) {
                if (!StationStatusManager::instance().is_online(it->second.station_id)) {
                    return "OFFLINE";
                }
                return it->second.status;
            }
        }

        sync_single_pile_from_db(norm);
        {
            std::shared_lock<std::shared_mutex> lock(mutex_);
            auto it = piles_.find(norm);
            if (it != piles_.end()) {
                if (!StationStatusManager::instance().is_online(it->second.station_id)) {
                    return "OFFLINE";
                }
                return it->second.status;
            }
        }
        return "IDLE";
    }

    std::vector<PileRuntimeState> get_piles_by_station(int64_t station_id) const {
        std::shared_lock<std::shared_mutex> lock(mutex_);
        std::vector<PileRuntimeState> result;
        bool station_online = StationStatusManager::instance().is_online(station_id);

        auto add_pile = [&](const PileRuntimeState& src) {
            PileRuntimeState p = src;
            if (!station_online) {
                p.status = "OFFLINE";
                p.voltage_v = 0.0;
                p.current_a = 0.0;
                p.power_kw = 0.0;
            }
            result.push_back(std::move(p));
        };

        if (station_id >= 1 && static_cast<size_t>(station_id) < station_pile_ids_.size()) {
            const auto& pids = station_pile_ids_[station_id];
            result.reserve(pids.size());
            for (const auto& pid : pids) {
                auto it = piles_.find(pid);
                if (it != piles_.end()) {
                    add_pile(it->second);
                }
            }
        } else {
            for (const auto& [_, p] : piles_) {
                if (p.station_id == station_id) {
                    add_pile(p);
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
        bool station_online = StationStatusManager::instance().is_online(station_id);

        auto check_pile = [&](const PileRuntimeState& p) {
            sum.total_piles++;
            if (p.type == "FAST") sum.has_fast_pile = true;

            std::string st = station_online ? p.status : "OFFLINE";

            if (st == "IDLE") {
                sum.idle_piles++;
                if (p.type == "FAST") sum.fast_piles_idle++;
                else sum.slow_piles_idle++;
            } else if (st == "CHARGING" || st == "PREPARING" || st == "FINISHING") {
                sum.busy_piles++;
            } else if (st == "RESERVED") {
                sum.busy_piles++;
                sum.reserved_piles++;
            } else if (st == "FAULT" || st == "OFFLINE") {
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

    bool has_fast_pile(int64_t station_id) const {
        std::shared_lock<std::shared_mutex> lock(mutex_);
        if (station_id >= 1 && static_cast<size_t>(station_id) < station_pile_ids_.size()) {
            for (const auto& pid : station_pile_ids_[station_id]) {
                auto it = piles_.find(pid);
                if (it != piles_.end() && it->second.type == "FAST") {
                    return true;
                }
            }
        }
        return false;
    }

    void set_station_piles_offline(int64_t station_id) {
        std::unique_lock<std::shared_mutex> lock(mutex_);
        int64_t now = current_time_ms();
        if (station_id >= 1 && static_cast<size_t>(station_id) < station_pile_ids_.size()) {
            for (const auto& pid : station_pile_ids_[station_id]) {
                auto it = piles_.find(pid);
                if (it != piles_.end()) {
                    // 保真记录下线前状态 (IDLE / FAULT / OFFLINE)
                    if (it->second.status == "FAULT") {
                        it->second.pre_station_offline_status = "FAULT";
                    } else if (it->second.status == "OFFLINE") {
                        it->second.pre_station_offline_status = "OFFLINE";
                    } else {
                        it->second.pre_station_offline_status = "IDLE";
                    }
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
                    std::string restored = it->second.pre_station_offline_status;
                    if (restored != "FAULT" && restored != "OFFLINE") {
                        restored = "IDLE";
                    }
                    it->second.status = restored;
                    it->second.last_update_time = now;
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
        std::string norm = normalize_pile_id(pile_id);
        std::unique_lock<std::shared_mutex> lock(mutex_);
        auto it = piles_.find(norm);
        if (it == piles_.end()) {
            lock.unlock();
            sync_single_pile_from_db(norm);
            lock.lock();
            it = piles_.find(norm);
            if (it == piles_.end()) return false;
        }
        if (it->second.status != "IDLE" && (it->second.status != "RESERVED" || it->second.reserved_user_id != user_id)) return false;

        it->second.status = "RESERVED";
        it->second.is_simulated = false;
        it->second.reserved_user_id = user_id;
        it->second.reservation_id = std::string(reservation_id);
        it->second.reservation_expire_time = expire_time;
        it->second.last_update_time = current_time_ms();
        return true;
    }

    bool release_reserved_pile(std::string_view pile_id) {
        std::string norm = normalize_pile_id(pile_id);
        std::unique_lock<std::shared_mutex> lock(mutex_);
        auto it = piles_.find(norm);
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
                if (!StationStatusManager::instance().is_online(station_cursor)) continue;

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
            std::string norm = normalize_pile_status(status);
            std::string final_st = norm.empty() ? std::string(status) : norm;
            it->second.status = final_st;
            it->second.last_update_time = current_time_ms();
            if (final_st == "CHARGING") {
                active_charging_pile_ids_.insert(std::string(pile_id));
            } else {
                active_charging_pile_ids_.erase(std::string(pile_id));
                it->second.voltage_v = 0.0;
                it->second.current_a = 0.0;
                it->second.power_kw = 0.0;
            }
            if (final_st == "IDLE" || final_st == "FAULT" || final_st == "OFFLINE") {
                it->second.pre_station_offline_status = final_st;
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

    void register_pile(std::string_view pile_id, int64_t station_id, std::string_view pile_name, std::string_view type, double max_power_kw) {
        std::unique_lock<std::shared_mutex> lock(mutex_);
        if (station_id >= 0) {
            if (static_cast<size_t>(station_id) >= station_pile_ids_.size()) {
                station_pile_ids_.resize(station_id + 1);
            }
            station_pile_ids_[station_id].push_back(std::string(pile_id));
        }
        int64_t now = current_time_ms();
        piles_[std::string(pile_id)] = PileRuntimeState{
            .pile_id = std::string(pile_id),
            .station_id = station_id,
            .pile_name = std::string(pile_name),
            .type = std::string(type),
            .max_power_kw = max_power_kw,
            .status = "IDLE",
            .voltage_v = 0.0,
            .current_a = 0.0,
            .power_kw = 0.0,
            .current_soc = 0,
            .temperature_celsius = 25.0,
            .charged_energy_kwh = 0.0,
            .electricity_price = 1.45,
            .service_price = 0.35,
            .last_update_time = now,
            .is_simulated = false,
            .total_charge_count = 0,
            .total_charge_hours = 0.0,
            .last_heartbeat_at = now
        };
    }

    void increment_charge_stats(std::string_view pile_id, double hours) {
        std::unique_lock<std::shared_mutex> lock(mutex_);
        auto it = piles_.find(std::string(pile_id));
        if (it != piles_.end()) {
            it->second.total_charge_count++;
            it->second.total_charge_hours += hours;
            it->second.last_heartbeat_at = current_time_ms();
        }
    }

    AdminPileStatusOverviewData get_pile_status_overview() const {
        std::shared_lock<std::shared_mutex> lock(mutex_);
        int in_use = 0;
        int idle = 0;
        int fault = 0;
        int offline = 0;
        int total = static_cast<int>(piles_.size());

        for (const auto& [_, p] : piles_) {
            bool st_online = StationStatusManager::instance().is_online(p.station_id);
            std::string st = st_online ? p.status : "OFFLINE";
            if (st == "CHARGING" || st == "PREPARING" || st == "FINISHING" || st == "RESERVED") {
                in_use++;
            } else if (st == "IDLE") {
                idle++;
            } else if (st == "FAULT") {
                fault++;
            } else if (st == "OFFLINE") {
                offline++;
            } else {
                offline++;
            }
        }

        auto round2 = [](double v) {
            return std::round(v * 100.0) / 100.0;
        };

        double in_use_pct = total > 0 ? (static_cast<double>(in_use) / total * 100.0) : 0.0;
        double idle_pct = total > 0 ? (static_cast<double>(idle) / total * 100.0) : 0.0;
        double fault_pct = total > 0 ? (static_cast<double>(fault) / total * 100.0) : 0.0;
        double offline_pct = total > 0 ? (static_cast<double>(offline) / total * 100.0) : 0.0;
        double online_rate = total > 0 ? (static_cast<double>(in_use + idle) / total * 100.0) : 0.0;

        return AdminPileStatusOverviewData{
            .total_piles = total,
            .in_use_count = in_use,
            .in_use_percentage = round2(in_use_pct),
            .idle_count = idle,
            .idle_percentage = round2(idle_pct),
            .fault_count = fault,
            .fault_percentage = round2(fault_pct),
            .offline_count = offline,
            .offline_percentage = round2(offline_pct),
            .online_rate = round2(online_rate)
        };
    }

    std::optional<PileRuntimeState> sync_single_pile_from_db(std::string_view pile_id) const {
        if (!DbPool::instance().is_initialized()) return std::nullopt;
        auto conn = DbPool::instance().acquire_reader();
        if (!conn) return std::nullopt;

        std::string clean_id = normalize_pile_id(pile_id);
        if (clean_id.empty()) return std::nullopt;

        std::string sql = std::format(
            "SELECT pile_id, station_id, pile_name, type, max_power_kw, total_charge_count, total_charge_hours, last_heartbeat_at "
            "FROM piles WHERE pile_id = '{}' OR LOWER(pile_id) = LOWER('{}') LIMIT 1;",
            clean_id, clean_id
        );
        PgResultGuard res(conn->exec(sql.c_str()));
        if (!res.is_ok() || res.rows() == 0) return std::nullopt;

        std::string pid = res.value(0, 0);
        int64_t st_id = 0;
        try { st_id = std::stoll(res.value(0, 1)); } catch (...) {}
        std::string pname = res.value(0, 2);
        std::string ptype = res.value(0, 3);
        double pwr = 120.0;
        try { pwr = std::stod(res.value(0, 4)); } catch (...) {}
        int64_t chg_cnt = 0;
        try { chg_cnt = std::stoll(res.value(0, 5)); } catch (...) {}
        double chg_hrs = 0.0;
        try { chg_hrs = std::stod(res.value(0, 6)); } catch (...) {}
        int64_t hb = 0;
        try { hb = std::stoll(res.value(0, 7)); } catch (...) {}
        int64_t now = current_time_ms();

        std::unique_lock<std::shared_mutex> lock(mutex_);
        if (st_id >= 1) {
            if (static_cast<size_t>(st_id) >= station_pile_ids_.size()) {
                station_pile_ids_.resize(st_id + 1);
            }
            if (std::find(station_pile_ids_[st_id].begin(), station_pile_ids_[st_id].end(), pid) == station_pile_ids_[st_id].end()) {
                station_pile_ids_[st_id].push_back(pid);
            }
        }

        PileRuntimeState state{
            .pile_id = pid,
            .station_id = st_id,
            .pile_name = pname,
            .type = ptype,
            .max_power_kw = pwr,
            .status = "IDLE",
            .pre_station_offline_status = "IDLE",
            .voltage_v = 0.0,
            .current_a = 0.0,
            .power_kw = 0.0,
            .current_soc = 0,
            .temperature_celsius = 25.0,
            .charged_energy_kwh = 0.0,
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
            .active_order_id = "",
            .user_id = 0,
            .start_time = 0,
            .last_update_time = now,
            .is_simulated = false,
            .reserved_user_id = 0,
            .reservation_id = "",
            .reservation_expire_time = 0,
            .total_charge_count = chg_cnt,
            .total_charge_hours = chg_hrs,
            .last_heartbeat_at = hb > 0 ? hb : now
        };
        piles_[pid] = state;
        return state;
    }

    void sync_missing_piles_from_db() {
        if (!DbPool::instance().is_initialized()) return;
        auto conn = DbPool::instance().acquire_reader();
        if (!conn) return;

        std::string sql = "SELECT pile_id, station_id, pile_name, type, max_power_kw, total_charge_count, total_charge_hours, last_heartbeat_at FROM piles;";
        PgResultGuard res(conn->exec(sql.c_str()));
        if (!res.is_ok()) return;

        int rows = res.rows();
        int added = 0;
        int64_t now = current_time_ms();
        std::unique_lock<std::shared_mutex> lock(mutex_);
        for (int i = 0; i < rows; ++i) {
            std::string pid = res.value(i, 0);
            if (piles_.find(pid) == piles_.end()) {
                int64_t st_id = 0;
                try { st_id = std::stoll(res.value(i, 1)); } catch (...) {}
                std::string pname = res.value(i, 2);
                std::string ptype = res.value(i, 3);
                double pwr = 120.0;
                try { pwr = std::stod(res.value(i, 4)); } catch (...) {}
                int64_t chg_cnt = 0;
                try { chg_cnt = std::stoll(res.value(i, 5)); } catch (...) {}
                double chg_hrs = 0.0;
                try { chg_hrs = std::stod(res.value(i, 6)); } catch (...) {}
                int64_t hb = 0;
                try { hb = std::stoll(res.value(i, 7)); } catch (...) {}

                if (st_id >= 1) {
                    if (static_cast<size_t>(st_id) >= station_pile_ids_.size()) {
                        station_pile_ids_.resize(st_id + 1);
                    }
                    station_pile_ids_[st_id].push_back(pid);
                }

                piles_[pid] = PileRuntimeState{
                    .pile_id = pid,
                    .station_id = st_id,
                    .pile_name = pname,
                    .type = ptype,
                    .max_power_kw = pwr,
                    .status = "IDLE",
                    .pre_station_offline_status = "IDLE",
                    .voltage_v = 0.0,
                    .current_a = 0.0,
                    .power_kw = 0.0,
                    .current_soc = 0,
                    .temperature_celsius = 25.0,
                    .charged_energy_kwh = 0.0,
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
                    .active_order_id = "",
                    .user_id = 0,
                    .start_time = 0,
                    .last_update_time = now,
                    .is_simulated = false,
                    .reserved_user_id = 0,
                    .reservation_id = "",
                    .reservation_expire_time = 0,
                    .total_charge_count = chg_cnt,
                    .total_charge_hours = chg_hrs,
                    .last_heartbeat_at = hb > 0 ? hb : now
                };
                added++;
            }
        }
        if (added > 0) {
            std::println("  [OK] 同步补齐数据库中新增的 {} 个充电桩，当前状态池全量充电桩: {}", added, piles_.size());
        }
    }

    void load_active_reservations_from_db() {
        if (!DbPool::instance().is_initialized()) return;
        auto conn = DbPool::instance().acquire_reader();
        if (!conn) return;

        int64_t now = current_time_ms();
        std::string sql = std::format(
            "SELECT pile_id, user_id, reservation_id, expire_at FROM pile_reservations "
            "WHERE status = 'ACTIVE' AND expire_at > {};",
            now
        );
        PgResultGuard res(conn->exec(sql.c_str()));
        if (res.is_ok()) {
            std::unique_lock<std::shared_mutex> lock(mutex_);
            int rows = res.rows();
            for (int i = 0; i < rows; ++i) {
                std::string pid = res.value(i, 0);
                int64_t uid = std::stoll(res.value(i, 1));
                std::string rid = res.value(i, 2);
                int64_t exp = std::stoll(res.value(i, 3));

                auto it = piles_.find(pid);
                if (it != piles_.end()) {
                    it->second.status = "RESERVED";
                    it->second.is_simulated = false;
                    it->second.reserved_user_id = uid;
                    it->second.reservation_id = rid;
                    it->second.reservation_expire_time = exp;
                    it->second.last_update_time = now;
                    active_charging_pile_ids_.erase(pid);
                }
            }
            if (rows > 0) {
                std::cout << "  [OK] Successfully loaded " << rows << " active pile reservations into memory.\n" << std::flush;
            }
        }
    }

    PileListResponseData get_piles_paged(
        int page,
        int page_size,
        int64_t station_id_filter,
        std::string_view status_filter,
        std::string_view type_filter
    ) const {
        std::shared_lock<std::shared_mutex> lock(mutex_);

        std::string norm_status = normalize_pile_status(status_filter);
        std::string norm_type = normalize_pile_type(type_filter);

        auto to_dto = [](const PileRuntimeState& p, bool station_online) -> PileAdminItemDTO {
            std::string st_name;
            const StaticStation* s = find_static_station(static_cast<int32_t>(p.station_id));
            if (s) {
                st_name = s->name;
            }
            std::string eff_st = station_online ? p.status : "OFFLINE";
            int st_code = pile_status_to_code(eff_st);
            return PileAdminItemDTO{
                .pile_id = p.pile_id,
                .station_id = p.station_id,
                .station_name = st_name,
                .pile_name = p.pile_name,
                .type = p.type,
                .power_kw = p.max_power_kw,
                .current_status = eff_st,
                .current_status_code = st_code,
                .status = eff_st,
                .status_code = st_code,
                .status_desc = std::string(pile_status_to_desc(eff_st)),
                .total_charge_count = p.total_charge_count,
                .total_charge_hours = p.total_charge_hours,
                .last_heartbeat_at = p.last_heartbeat_at
            };
        };

        if (station_id_filter > 0) {
            bool station_online = StationStatusManager::instance().is_online(station_id_filter);
            std::vector<const PileRuntimeState*> matched;
            if (station_id_filter < static_cast<int64_t>(station_pile_ids_.size())) {
                for (const auto& pid : station_pile_ids_[station_id_filter]) {
                    auto it = piles_.find(pid);
                    if (it == piles_.end()) continue;
                    const auto& p = it->second;
                    std::string eff_st = station_online ? p.status : "OFFLINE";
                    if (!norm_status.empty() && eff_st != norm_status) continue;
                    if (!norm_type.empty() && p.type != norm_type) continue;
                    matched.push_back(&p);
                }
            }

            int64_t total = matched.size();
            int64_t offset = static_cast<int64_t>(page - 1) * page_size;

            PileListResponseData data;
            data.total = total;
            data.page = page;
            data.page_size = page_size;

            if (offset < total) {
                int64_t end_idx = std::min<int64_t>(offset + page_size, total);
                for (int64_t i = offset; i < end_idx; ++i) {
                    data.piles.push_back(to_dto(*matched[i], station_online));
                }
            }
            return data;
        }

        // 全网查询: 按 station_pile_ids_ 从站 1 到 8569 遍历，天然保证 pile_id ASC 严格递增排序
        int64_t match_count = 0;
        int64_t offset = static_cast<int64_t>(page - 1) * page_size;
        int64_t end_idx = offset + page_size;

        PileListResponseData data;
        data.page = page;
        data.page_size = page_size;

        for (size_t sid = 1; sid < station_pile_ids_.size(); ++sid) {
            bool station_online = StationStatusManager::instance().is_online(sid);
            for (const auto& pid : station_pile_ids_[sid]) {
                auto it = piles_.find(pid);
                if (it == piles_.end()) continue;
                const auto& p = it->second;
                std::string eff_st = station_online ? p.status : "OFFLINE";
                if (!norm_status.empty() && eff_st != norm_status) continue;
                if (!norm_type.empty() && p.type != norm_type) continue;

                if (match_count >= offset && match_count < end_idx) {
                    data.piles.push_back(to_dto(p, station_online));
                }
                match_count++;
            }
        }

        data.total = match_count;
        return data;
    }

private:
    ChargingStatePool() = default;
    mutable std::unordered_map<std::string, PileRuntimeState> piles_;
    std::unordered_set<std::string> active_charging_pile_ids_;
    mutable std::vector<std::vector<std::string>> station_pile_ids_;
    mutable std::shared_mutex mutex_;
};

} // namespace ev
