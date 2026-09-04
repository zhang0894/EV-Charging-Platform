#pragma once

#include <boost/geometry.hpp>
#include <boost/geometry/geometries/point.hpp>
#include <boost/geometry/geometries/box.hpp>
#include <boost/geometry/index/rtree.hpp>
#include "../common/models.hpp"
#include "../data/static_stations.hpp"
#include "station_status_manager.hpp"
#include <vector>
#include <array>
#include <unordered_map>
#include <optional>
#include <shared_mutex>
#include <cmath>
#include <algorithm>

namespace ev {

namespace bg = boost::geometry;
namespace bgi = boost::geometry::index;

using GeoPoint = bg::model::point<double, 2, bg::cs::cartesian>;
using StationValue = std::pair<GeoPoint, int64_t>; // <(lon, lat), station_id>

struct StationDistanceResult {
    int64_t station_id{0};
    double distance_km{0.0};
    double latitude{0.0};
    double longitude{0.0};
};

class StationRTree {
public:
    static StationRTree& instance() {
        static StationRTree tree;
        return tree;
    }

    // 地球大圆距离 (Haversine 算法，单位: km)
    static double calculate_distance_km(double lat1, double lon1, double lat2, double lon2) noexcept {
        constexpr double R = 6371.0; // 地球平均半径 (km)
        constexpr double DEG_TO_RAD = 3.14159265358979323846 / 180.0;

        double dlat = (lat2 - lat1) * DEG_TO_RAD;
        double dlon = (lon2 - lon1) * DEG_TO_RAD;
        double a = std::sin(dlat / 2.0) * std::sin(dlat / 2.0) +
                   std::cos(lat1 * DEG_TO_RAD) * std::cos(lat2 * DEG_TO_RAD) *
                   std::sin(dlon / 2.0) * std::sin(dlon / 2.0);
        double c = 2.0 * std::atan2(std::sqrt(a), std::sqrt(1.0 - a));
        return R * c;
    }

    // 基于编译期静态常量 STATIC_STATIONS (8,569 真实站点) 构建高性能空间几何索引
    void build_static_index() {
        std::unique_lock<std::shared_mutex> lock(mutex_);
        std::vector<StationValue> values;
        values.reserve(STATIC_STATION_COUNT);

        for (size_t i = 0; i < 16; ++i) {
            district_stations_[i].clear();
        }

        for (const auto& s : STATIC_STATIONS) {
            values.emplace_back(GeoPoint(s.longitude, s.latitude), s.station_id);
            if (s.district_code < 16) {
                district_stations_[s.district_code].push_back(s.station_id);
            }
        }

        rtree_ = bgi::rtree<StationValue, bgi::rstar<16>>(values.begin(), values.end());
    }

    // 获取特定行政区下的所有站点 ID 列表
    const std::vector<int32_t>& get_district_stations(uint8_t district_code) const {
        std::shared_lock<std::shared_mutex> lock(mutex_);
        static const std::vector<int32_t> empty_vec;
        if (district_code < 16) {
            return district_stations_[district_code];
        }
        return empty_vec;
    }

    // 自适应半径附近充电站查询：
    // 不考虑站点是否上线(离线站点亦按几何距离计算)，
    // 若命中结果中在线站点不足 3 个，则自适应逐级放大搜索半径直到满足至少 3 个在线站点(全域在线不足3个时除外)
    std::vector<StationDistanceResult> search_nearby_adaptive(
        double user_lat,
        double user_lon,
        double initial_radius_km = 2.0,
        size_t limit = 20
    ) const {
        std::shared_lock<std::shared_mutex> lock(mutex_);

        const size_t total_online = StationStatusManager::instance().get_online_count();
        const size_t target_online = std::min<size_t>(3, total_online);

        double current_radius = (initial_radius_km > 0.1) ? initial_radius_km : 2.0;
        constexpr double MAX_RADIUS_KM = 120.0; // 覆盖全北京市地理纵深

        std::vector<StationDistanceResult> in_radius;

        while (current_radius <= MAX_RADIUS_KM) {
            in_radius.clear();

            // 1. 根据当前搜索半径构筑空间经纬度查询包围盒 (粗筛)
            // 纬度 1度 ≈ 111km, 经度 1度 ≈ 111km * cos(40°) ≈ 85km
            double dlat = (current_radius / 111.0) * 1.15;
            double dlon = (current_radius / 85.0) * 1.15;

            bg::model::box<GeoPoint> query_box(
                GeoPoint(user_lon - dlon, user_lat - dlat),
                GeoPoint(user_lon + dlon, user_lat + dlat)
            );

            std::vector<StationValue> box_results;
            rtree_.query(bgi::within(query_box), std::back_inserter(box_results));

            size_t online_count = 0;
            for (const auto& item : box_results) {
                double st_lon = item.first.template get<0>();
                double st_lat = item.first.template get<1>();
                int64_t sid = item.second;
                double dist = calculate_distance_km(user_lat, user_lon, st_lat, st_lon);

                if (dist <= current_radius) {
                    bool is_on = StationStatusManager::instance().is_online(sid);
                    if (is_on) {
                        online_count++;
                    }
                    in_radius.push_back(StationDistanceResult{
                        .station_id = sid,
                        .distance_km = dist,
                        .latitude = st_lat,
                        .longitude = st_lon
                    });
                }
            }

            // 2. 检查当前半径内在线站点数是否达到预期
            if (online_count < target_online && current_radius < MAX_RADIUS_KM) {
                // 自适应翻倍扩展搜索半径
                current_radius = std::min(current_radius * 2.0, MAX_RADIUS_KM);
                continue;
            }

            // 已经达到目标在线数或已达最大半径
            break;
        }

        // 3. 精确按几何距离升序排序
        std::sort(in_radius.begin(), in_radius.end(), [](const auto& a, const auto& b) {
            return a.distance_km < b.distance_km;
        });

        // 4. 截断为 limit 个结果，并确保返回的结果集合中至少包含 target_online 个在线站点
        if (in_radius.size() <= limit) {
            return in_radius;
        }

        // 统计前 limit 个结果中的在线数量
        size_t online_in_top = 0;
        for (size_t i = 0; i < limit; ++i) {
            if (StationStatusManager::instance().is_online(in_radius[i].station_id)) {
                online_in_top++;
            }
        }

        std::vector<StationDistanceResult> selected(in_radius.begin(), in_radius.begin() + limit);

        // 如果前 limit 个中在线电站不足 target_online，从后续的候选中将最近的在线站点置换入选
        if (online_in_top < target_online) {
            for (size_t i = limit; i < in_radius.size() && online_in_top < target_online; ++i) {
                if (StationStatusManager::instance().is_online(in_radius[i].station_id)) {
                    // 逆序查找最远的离线电站并置换
                    for (int k = static_cast<int>(selected.size()) - 1; k >= 0; --k) {
                        if (!StationStatusManager::instance().is_online(selected[k].station_id)) {
                            selected[k] = in_radius[i];
                            online_in_top++;
                            break;
                        }
                    }
                }
            }
            // 置换后重新按距离排序
            std::sort(selected.begin(), selected.end(), [](const auto& a, const auto& b) {
                return a.distance_km < b.distance_km;
            });
        }

        return selected;
    }

    void build_index(const std::vector<std::pair<int64_t, std::pair<double, double>>>& stations) {
        std::unique_lock<std::shared_mutex> lock(mutex_);
        std::vector<StationValue> values;
        values.reserve(stations.size());

        for (const auto& [sid, coord] : stations) {
            values.emplace_back(GeoPoint(coord.second, coord.first), sid);
        }

        rtree_ = bgi::rtree<StationValue, bgi::rstar<16>>(values.begin(), values.end());
    }

    void build_index(const std::vector<StationModel>& stations) {
        std::unique_lock<std::shared_mutex> lock(mutex_);
        std::vector<StationValue> values;
        values.reserve(stations.size());
        station_map_.clear();
        station_map_.reserve(stations.size());

        for (const auto& s : stations) {
            values.emplace_back(GeoPoint(s.longitude, s.latitude), s.station_id);
            station_map_[s.station_id] = s;
        }

        rtree_ = bgi::rtree<StationValue, bgi::rstar<16>>(values.begin(), values.end());
    }

    std::optional<StationModel> get_station(int64_t station_id) const {
        std::shared_lock<std::shared_mutex> lock(mutex_);
        auto it = station_map_.find(station_id);
        if (it != station_map_.end()) {
            return it->second;
        }
        return std::nullopt;
    }

    void update_station_cache(const StationModel& station) {
        std::unique_lock<std::shared_mutex> lock(mutex_);
        station_map_[station.station_id] = station;
    }

    std::vector<StationDistanceResult> search_nearby(
        double user_lat,
        double user_lon,
        double radius_km = 10.0,
        size_t limit = 20
    ) const {
        return search_nearby_adaptive(user_lat, user_lon, radius_km, limit);
    }

private:
    StationRTree() = default;
    bgi::rtree<StationValue, bgi::rstar<16>> rtree_;
    std::array<std::vector<int32_t>, 16> district_stations_{};
    std::unordered_map<int64_t, StationModel> station_map_;
    mutable std::shared_mutex mutex_;
};

} // namespace ev
