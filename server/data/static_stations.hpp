#pragma once

#include <array>
#include <string_view>
#include <cstdint>
#include <optional>
#include <span>
#include <iostream>
#include <glaze/glaze.hpp>

namespace ev {

// 北京市 16 个行政区编码规范 (0 ~ 15)
inline constexpr std::string_view DISTRICT_NAMES[16] = {
    "东城区", // 0
    "西城区", // 1
    "朝阳区", // 2
    "海淀区", // 3
    "丰台区", // 4
    "石景山区", // 5
    "门头沟区", // 6
    "房山区", // 7
    "通州区", // 8
    "顺义区", // 9
    "昌平区", // 10
    "大兴区", // 11
    "怀柔区", // 12
    "平谷区", // 13
    "密云区", // 14
    "延庆区", // 15
};

inline std::optional<uint8_t> get_district_code_by_name(std::string_view name) {
    for (uint8_t i = 0; i < 16; ++i) {
        if (DISTRICT_NAMES[i] == name) return i;
    }
    return std::nullopt;
}

inline std::string_view get_district_name_by_code(uint8_t code) {
    if (code < 16) return DISTRICT_NAMES[code];
    return "未知";
}

struct StaticStation {
    int32_t station_id{};    // 唯一高效连续整数 ID (1 ~ 8565)
    uint8_t district_code{}; // 0 ~ 15 编号
    double latitude{};
    double longitude{};
    std::string_view name{};
    std::string_view address{};
};

constexpr size_t STATIC_STATION_COUNT = 8565;

#include <fstream>
#include <sstream>
#include <vector>

#if defined(__has_embed)
#if __has_embed("stations_processed.json")
#define EV_HAS_STATIONS_EMBED 1
inline constexpr unsigned char STATIONS_JSON_EMBED[] = {
#embed "stations_processed.json"
    , 0
};
#endif
#endif

namespace detail {
inline const std::array<StaticStation, STATIC_STATION_COUNT>& load_static_stations() {
    static const auto stations = [] {
        alignas(StaticStation) static std::array<StaticStation, STATIC_STATION_COUNT> arr{};
#if defined(EV_HAS_STATIONS_EMBED)
        std::string_view json_str(reinterpret_cast<const char*>(STATIONS_JSON_EMBED), sizeof(STATIONS_JSON_EMBED) - 1);
#else
        static std::string static_json_str;
        const std::vector<std::string> candidate_paths = {
            "data/stations_processed.json",
            "server/data/stations_processed.json",
            "../data/stations_processed.json",
            "../../data/stations_processed.json",
            "E:/EV-Charging-Platform/server/data/stations_processed.json"
        };
        for (const auto& path : candidate_paths) {
            std::ifstream file(path, std::ios::binary);
            if (file.is_open()) {
                std::stringstream ss;
                ss << file.rdbuf();
                static_json_str = ss.str();
                break;
            }
        }
        if (static_json_str.empty()) {
            std::cerr << "[Fatal Error] Could not locate stations_processed.json in any candidate path!\n";
            std::abort();
        }
        std::string_view json_str(static_json_str);
#endif
        auto ec = glz::read_json(arr, json_str);
        if (ec) {
            std::cerr << "[Fatal Error] Failed to parse stations JSON via Glaze: "
                      << glz::format_error(ec, json_str) << std::endl;
            std::abort();
        }
        return arr;
    }();
    return stations;
}
} // namespace detail

inline const std::array<StaticStation, STATIC_STATION_COUNT>& STATIC_STATIONS = detail::load_static_stations();

inline const StaticStation* find_static_station(int32_t id) {
    if (id >= 1 && static_cast<size_t>(id) <= STATIC_STATION_COUNT) {
        return &STATIC_STATIONS[id - 1];
    }
    return nullptr;
}

} // namespace ev
