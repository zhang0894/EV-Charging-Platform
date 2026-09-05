#pragma once

#include <array>
#include <atomic>
#include <cstdint>
#include <cstddef>
#include <iostream>
#include <string>
#include "../data/static_stations.hpp"
#include "../db/db_pool.hpp"

namespace ev {

class StationPriceManager {
public:
    static StationPriceManager& instance() {
        static StationPriceManager inst;
        return inst;
    }

    // 确定性随机电价算法 (范围: 1.15 ~ 1.85 元/度，步长 0.01 元)
    static double calculate_default_price(int64_t station_id) noexcept {
        uint64_t sid = static_cast<uint64_t>(station_id > 0 ? station_id : 1);
        double offset = static_cast<double>((sid * 104729ULL + 12345ULL) % 71) * 0.01;
        return 1.15 + offset;
    }

    void init() {
        for (size_t i = 0; i < MAX_STATIONS; ++i) {
            double p = calculate_default_price(static_cast<int64_t>(i));
            prices_[i].store(p, std::memory_order_relaxed);
        }
    }

    // 从数据库 stations 表全量载入已持久化保存的真实电价
    void load_from_db() {
        if (!DbPool::instance().is_initialized()) {
            return;
        }

        try {
            auto conn = DbPool::instance().acquire_reader();
            if (!conn) {
                std::cerr << "[StationPriceManager] Warning: failed to acquire DB connection to load prices.\n";
                return;
            }

            PgResultGuard res(conn->exec("SELECT station_id, price_per_kwh FROM stations ORDER BY station_id ASC;"));
            if (!res.is_ok()) {
                std::cerr << "[StationPriceManager] Warning: query stations prices failed.\n";
                return;
            }

            int rows = res.rows();
            for (int i = 0; i < rows; ++i) {
                int64_t sid = std::stoll(res.value(i, 0));
                double price = std::stod(res.value(i, 1));
                set_price(sid, price);
            }
            std::cout << "  [OK] Successfully loaded " << rows << " station prices from database.\n";
        } catch (const std::exception& e) {
            std::cerr << "[StationPriceManager] Exception loading prices from DB: " << e.what() << "\n";
        }
    }

    double get_price(int64_t station_id) const {
        if (station_id >= 1 && static_cast<size_t>(station_id) < MAX_STATIONS) {
            return prices_[station_id].load(std::memory_order_relaxed);
        }
        return calculate_default_price(station_id);
    }

    void set_price(int64_t station_id, double price) {
        if (station_id >= 1 && static_cast<size_t>(station_id) < MAX_STATIONS) {
            prices_[station_id].store(price, std::memory_order_relaxed);
        }
    }

private:
    StationPriceManager() {
        init();
    }

    static constexpr size_t MAX_STATIONS = 12000;
    std::array<std::atomic<double>, MAX_STATIONS> prices_{};
};

} // namespace ev
