#pragma once

#include <array>
#include <atomic>
#include <cstdint>
#include <cstddef>
#include "../data/static_stations.hpp"

namespace ev {

class StationStatusManager {
public:
    static StationStatusManager& instance() {
        static StationStatusManager inst;
        return inst;
    }

    void init() {
        for (size_t i = 0; i < MAX_STATIONS; ++i) {
            status_table_[i].store(true, std::memory_order_relaxed);
        }
        online_count_.store(STATIC_STATION_COUNT, std::memory_order_relaxed);
    }

    bool is_online(int64_t station_id) const {
        if (station_id >= 1 && static_cast<size_t>(station_id) < MAX_STATIONS) {
            return status_table_[station_id].load(std::memory_order_relaxed);
        }
        return false;
    }

    void set_online(int64_t station_id, bool online) {
        if (station_id >= 1 && static_cast<size_t>(station_id) < MAX_STATIONS) {
            bool prev = status_table_[station_id].exchange(online, std::memory_order_relaxed);
            if (prev != online) {
                if (online) {
                    online_count_.fetch_add(1, std::memory_order_relaxed);
                } else {
                    online_count_.fetch_sub(1, std::memory_order_relaxed);
                }
            }
        }
    }

    size_t get_online_count() const {
        return online_count_.load(std::memory_order_relaxed);
    }

private:
    StationStatusManager() {
        init();
    }

    static constexpr size_t MAX_STATIONS = 12000;
    std::array<std::atomic<bool>, MAX_STATIONS> status_table_{};
    std::atomic<size_t> online_count_{STATIC_STATION_COUNT};
};

} // namespace ev
