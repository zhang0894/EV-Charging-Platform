#pragma once

#include <boost/geometry.hpp>
#include <boost/geometry/geometries/point.hpp>
#include <boost/geometry/index/rtree.hpp>
#include <vector>
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

    void build_index(const std::vector<std::pair<int64_t, std::pair<double, double>>>& stations) {
        std::unique_lock<std::shared_mutex> lock(mutex_);
        std::vector<StationValue> values;
        values.reserve(stations.size());

        for (const auto& [sid, coord] : stations) {
            values.emplace_back(GeoPoint(coord.second, coord.first), sid); // bg point: (lon, lat)
        }

        rtree_ = bgi::rtree<StationValue, bgi::rstar<16>>(values.begin(), values.end());
    }

    std::vector<StationDistanceResult> search_nearby(
        double user_lat,
        double user_lon,
        double radius_km = 10.0,
        size_t limit = 20
    ) {
        std::shared_lock<std::shared_mutex> lock(mutex_);

        // 粗筛：按经纬度包围盒做 KNN 或粗查
        GeoPoint user_pt(user_lon, user_lat);
        std::vector<StationValue> candidate_values;

        // 获取 KNN 候选集 (如 limit * 3)
        rtree_.query(bgi::nearest(user_pt, static_cast<unsigned int>(std::max(limit * 3, size_t(50)))), std::back_inserter(candidate_values));

        std::vector<StationDistanceResult> results;
        results.reserve(candidate_values.size());

        for (const auto& item : candidate_values) {
            double st_lon = item.first.template get<0>();
            double st_lat = item.first.template get<1>();
            int64_t sid = item.second;
            double dist = calculate_distance_km(user_lat, user_lon, st_lat, st_lon);

            if (dist <= radius_km) {
                results.push_back(StationDistanceResult{
                    .station_id = sid,
                    .distance_km = dist,
                    .latitude = st_lat,
                    .longitude = st_lon
                });
            }
        }

        // 精确按距离排序
        std::sort(results.begin(), results.end(), [](const auto& a, const auto& b) {
            return a.distance_km < b.distance_km;
        });

        if (results.size() > limit) {
            results.resize(limit);
        }

        return results;
    }

private:
    StationRTree() = default;
    bgi::rtree<StationValue, bgi::rstar<16>> rtree_;
    std::shared_mutex mutex_;
};

} // namespace ev
