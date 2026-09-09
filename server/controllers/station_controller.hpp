#pragma once

#include "../common/types.hpp"
#include "../common/error.hpp"
#include "../common/models.hpp"
#include "../common/response.hpp"
#include "../data/static_stations.hpp"
#include "../memory/station_status_manager.hpp"
#include "../db/db_repository.hpp"
#include "../memory/rtree_index.hpp"
#include "../memory/state_pool.hpp"
#include "../memory/station_price_manager.hpp"
#include "../cache/redis_cache.hpp"
#include <cmath>
#include <algorithm>
#include <format>

namespace ev {

class StationController {
public:
    static bool fuzzy_contains_icase(std::string_view target, std::string_view query) {
        if (query.empty()) return true;
        if (query.size() > target.size()) return false;

        if (target.find(query) != std::string_view::npos) return true;

        auto it = std::search(
            target.begin(), target.end(),
            query.begin(), query.end(),
            [](char ch1, char ch2) {
                return std::tolower(static_cast<unsigned char>(ch1)) ==
                       std::tolower(static_cast<unsigned char>(ch2));
            }
        );
        return it != target.end();
    }

    static http::response<http::string_body> handle_inquire_stations(
        std::string_view name_param,
        std::string_view district_param,
        std::optional<double> lat_opt,
        std::optional<double> lon_opt,
        std::optional<int> status_opt = std::nullopt,
        std::optional<bool> fast_pile_opt = std::nullopt,
        int page = 1,
        int page_size = 20
    ) {
        if (page < 1) page = 1;
        if (page_size < 1) page_size = 20;
        if (page_size > 20) page_size = 20;

        if (lat_opt.has_value() != lon_opt.has_value()) {
            return make_error_response(AppError::InvalidParameters, "Latitude and longitude must both be provided");
        }

        // 1. 识别行政区筛选条件 (可选)
        std::optional<uint8_t> opt_district_code;
        if (!district_param.empty()) {
            try {
                size_t idx = 0;
                int num = std::stoi(std::string(district_param), &idx);
                if (idx == district_param.size() && num >= 0 && num < 16) {
                    opt_district_code = static_cast<uint8_t>(num);
                }
            } catch (...) {}

            if (!opt_district_code) {
                opt_district_code = get_district_code_by_name(district_param);
            }

            if (!opt_district_code) {
                // 前缀模糊匹配，如 "朝阳" 匹配 "朝阳区"
                for (uint8_t i = 0; i < 16; ++i) {
                    if (DISTRICT_NAMES[i].starts_with(district_param) || district_param.starts_with(DISTRICT_NAMES[i].substr(0, std::min<size_t>(6, DISTRICT_NAMES[i].size())))) {
                        opt_district_code = i;
                        break;
                    }
                }
            }

            if (!opt_district_code) {
                return make_error_response(AppError::InvalidParameters, "Invalid district name or code");
            }
        }

        bool has_coords = (lat_opt.has_value() && lon_opt.has_value());
        double user_lat = has_coords ? *lat_opt : 0.0;
        double user_lon = has_coords ? *lon_opt : 0.0;

        int64_t start_idx = static_cast<int64_t>(page - 1) * page_size;
        int64_t end_idx_target = start_idx + page_size;

        int64_t total = 0;
        std::vector<std::pair<int32_t, double>> paged_items; // <station_id, distance_km>

        if (!has_coords) {
            // 未传经纬度：按 station_id 升序排列
            // STATIC_STATIONS 本身即按 station_id 严格升序存储，无须全局排序
            auto filter_and_collect = [&](int32_t sid, const StaticStation* st) {
                if (!st) return;
                if (!name_param.empty() && !fuzzy_contains_icase(st->name, name_param)) return;
                if (status_opt.has_value()) {
                    bool is_on = StationStatusManager::instance().is_online(sid);
                    if (*status_opt != (is_on ? 1 : 2)) return;
                }
                if (fast_pile_opt.has_value()) {
                    if (*fast_pile_opt != ChargingStatePool::instance().has_fast_pile(sid)) return;
                }
                if (total >= start_idx && total < end_idx_target) {
                    paged_items.emplace_back(sid, 0.0);
                }
                total++;
            };

            if (opt_district_code) {
                const auto& district_sids = StationRTree::instance().get_district_stations(*opt_district_code);
                for (int32_t sid : district_sids) {
                    filter_and_collect(sid, find_static_station(sid));
                }
            } else {
                for (const auto& s : STATIC_STATIONS) {
                    filter_and_collect(s.station_id, &s);
                }
            }
        } else {
            // 提供经纬度：按距离由近及远升序排序
            // 采用局部等距平面投影平方距离 (Equirectangular Distance Squared) 进行粗排
            // 欧氏平方距离 d2 与球面大圆距离在区域范围内严格保序单调，仅含乘加运算，效率极高
            constexpr double DEG_TO_RAD = 3.14159265358979323846 / 180.0;
            double kx = std::cos(user_lat * DEG_TO_RAD);

            struct HeapItem {
                int32_t station_id;
                double dist_sq;
                bool operator<(const HeapItem& o) const noexcept {
                    if (dist_sq != o.dist_sq) return dist_sq < o.dist_sq; // 大顶堆，最远的排在堆顶
                    return station_id < o.station_id;
                }
            };

            // 安全候选集大小 K: 取 end_idx_target + 8，确保与球面大圆距离在临界边界上 100% 严格一致
            size_t K = static_cast<size_t>(end_idx_target + 8);
            std::vector<HeapItem> max_heap;
            max_heap.reserve(std::min<size_t>(K, 256));
            double max_dist_sq = std::numeric_limits<double>::infinity();

            auto filter_and_feed = [&](int32_t sid, const StaticStation* st) {
                if (!st) return;
                if (!name_param.empty() && !fuzzy_contains_icase(st->name, name_param)) return;
                if (status_opt.has_value()) {
                    bool is_on = StationStatusManager::instance().is_online(sid);
                    if (*status_opt != (is_on ? 1 : 2)) return;
                }
                if (fast_pile_opt.has_value()) {
                    if (*fast_pile_opt != ChargingStatePool::instance().has_fast_pile(sid)) return;
                }
                total++;

                double dx = (st->longitude - user_lon) * kx;
                double dy = st->latitude - user_lat;
                double d2 = dx * dx + dy * dy;

                if (max_heap.size() < K) {
                    max_heap.push_back(HeapItem{sid, d2});
                    if (max_heap.size() == K) {
                        std::make_heap(max_heap.begin(), max_heap.end());
                        max_dist_sq = max_heap.front().dist_sq;
                    }
                } else if (d2 < max_dist_sq) {
                    std::pop_heap(max_heap.begin(), max_heap.end());
                    max_heap.back() = HeapItem{sid, d2};
                    std::push_heap(max_heap.begin(), max_heap.end());
                    max_dist_sq = max_heap.front().dist_sq;
                }
            };

            if (opt_district_code) {
                const auto& district_sids = StationRTree::instance().get_district_stations(*opt_district_code);
                for (int32_t sid : district_sids) {
                    filter_and_feed(sid, find_static_station(sid));
                }
            } else {
                for (const auto& s : STATIC_STATIONS) {
                    filter_and_feed(s.station_id, &s);
                }
            }

            // 对堆中筛选出的候选站点计算精确的 Haversine 球面大圆距离，完成最终严格排序
            struct SorterItem {
                int32_t station_id;
                double distance_km;
            };
            std::vector<SorterItem> top_candidates;
            top_candidates.reserve(max_heap.size());
            for (const auto& h : max_heap) {
                const StaticStation* s = find_static_station(h.station_id);
                if (!s) continue;
                double dist = StationRTree::calculate_distance_km(user_lat, user_lon, s->latitude, s->longitude);
                top_candidates.push_back(SorterItem{h.station_id, dist});
            }

            std::sort(top_candidates.begin(), top_candidates.end(), [](const auto& a, const auto& b) {
                if (std::abs(a.distance_km - b.distance_km) > 1e-6) {
                    return a.distance_km < b.distance_km;
                }
                return a.station_id < b.station_id;
            });

            if (start_idx < static_cast<int64_t>(top_candidates.size())) {
                int64_t end_idx = std::min(end_idx_target, static_cast<int64_t>(top_candidates.size()));
                for (int64_t i = start_idx; i < end_idx; ++i) {
                    paged_items.emplace_back(top_candidates[i].station_id, top_candidates[i].distance_km);
                }
            }
        }

        StationInquireResponseData resp{
            .total = total,
            .page = page,
            .page_size = page_size,
            .stations = {}
        };

        resp.stations.reserve(paged_items.size());
        for (const auto& [sid, dist] : paged_items) {
            const StaticStation* s = find_static_station(sid);
            if (!s) continue;

            auto summary = ChargingStatePool::instance().get_station_pile_summary(sid);
            bool is_on = StationStatusManager::instance().is_online(sid);

            resp.stations.push_back(StationNearbyCardDTO{
                .station_id = sid,
                .id = sid,
                .station_name = std::string(s->name),
                .district = std::string(get_district_name_by_code(s->district_code)),
                .district_code = s->district_code,
                .address = std::string(s->address),
                .latitude = s->latitude,
                .longitude = s->longitude,
                .distance_km = has_coords ? (std::round(dist * 100.0) / 100.0) : 0.0,
                .price_per_kwh = StationPriceManager::instance().get_price(sid),
                .service_fee_per_kwh = 0.35,
                .overtime_fee_per_15min = 5.00,
                .total_piles = summary.total_piles,
                .pile_count = summary.total_piles,
                .idle_piles = summary.idle_piles,
                .available_count = summary.idle_piles,
                .fast_piles_idle = summary.fast_piles_idle,
                .slow_piles_idle = summary.slow_piles_idle,
                .has_fast_pile = summary.has_fast_pile,
                .is_online = is_on
            });
        }

        return make_success_response(resp);
    }

    static http::response<http::string_body> handle_get_station_detail(
        int64_t station_id,
        double user_lat = 0.0,
        double user_lon = 0.0
    ) {
        std::string name;
        std::string addr;
        std::string district = "朝阳区";
        uint8_t district_code = 2;
        double lat = 0.0;
        double lon = 0.0;
        double serv = 0.35;
        double overtime_fee = 5.00;

        const StaticStation* static_st = find_static_station(static_cast<int32_t>(station_id));
        if (static_st) {
            name = static_st->name;
            addr = static_st->address;
            district_code = static_st->district_code;
            district = std::string(get_district_name_by_code(district_code));
            lat = static_st->latitude;
            lon = static_st->longitude;
        } else {
            auto mem_st = StationRTree::instance().get_station(station_id);
            if (mem_st) {
                name = mem_st->station_name;
                addr = mem_st->address;
                lat = mem_st->latitude;
                lon = mem_st->longitude;
                serv = mem_st->service_fee_per_kwh;
                overtime_fee = mem_st->overtime_fee_per_15min;
            } else {
                std::string cache_key = std::format("cache:station:model:{}", station_id);
                auto cached_st = RedisCache::instance().get_json<StationModel>(cache_key);
                if (cached_st) {
                    name = cached_st->station_name;
                    addr = cached_st->address;
                    lat = cached_st->latitude;
                    lon = cached_st->longitude;
                    serv = cached_st->service_fee_per_kwh;
                    overtime_fee = cached_st->overtime_fee_per_15min;
                } else {
                    auto st_res = DbRepository::instance().get_station_by_id(station_id);
                    if (!st_res) {
                        return make_error_response(st_res.error());
                    }
                    name = st_res->station_name;
                    addr = st_res->address;
                    lat = st_res->latitude;
                    lon = st_res->longitude;
                    serv = st_res->service_fee_per_kwh;
                    overtime_fee = st_res->overtime_fee_per_15min;
                    RedisCache::instance().set_json(cache_key, *st_res, 120);
                }
            }
        }

        double price = StationPriceManager::instance().get_price(station_id);
        double dist = 0.0;
        if (user_lat != 0.0 && user_lon != 0.0 && lat != 0.0 && lon != 0.0) {
            dist = std::round(StationRTree::calculate_distance_km(user_lat, user_lon, lat, lon) * 100.0) / 100.0;
        }

        auto summary = ChargingStatePool::instance().get_station_pile_summary(station_id);
        bool is_on = StationStatusManager::instance().is_online(station_id);

        StationNearbyCardDTO data{
            .station_id = station_id,
            .id = station_id,
            .station_name = name,
            .district = district,
            .district_code = district_code,
            .address = addr,
            .latitude = lat,
            .longitude = lon,
            .distance_km = dist,
            .price_per_kwh = price,
            .service_fee_per_kwh = serv,
            .overtime_fee_per_15min = overtime_fee,
            .total_piles = summary.total_piles,
            .pile_count = summary.total_piles,
            .idle_piles = summary.idle_piles,
            .available_count = summary.idle_piles,
            .fast_piles_idle = summary.fast_piles_idle,
            .slow_piles_idle = summary.slow_piles_idle,
            .has_fast_pile = summary.has_fast_pile,
            .is_online = is_on
        };

        return make_success_response(data);
    }

    static http::response<http::string_body> handle_get_sales_stats(int64_t station_id, std::string_view time_range) {
        std::string cache_key = std::format("cache:station:{}:sales:{}", station_id, time_range);
        auto cached = RedisCache::instance().get_json<StationSalesStatsResponseData>(cache_key);
        if (cached) {
            return make_success_response(*cached);
        }

        auto res = DbRepository::instance().get_station_sales_stats(station_id, time_range);
        if (!res) {
            return make_error_response(res.error());
        }

        RedisCache::instance().set_json(cache_key, *res, 10); // 10s TTL
        return make_success_response(*res);
    }

    static http::response<http::string_body> handle_get_piles(
        int page,
        int page_size,
        int64_t station_id_filter,
        std::string_view status_filter,
        std::string_view type_filter
    ) {
        if (page < 1) page = 1;
        if (page_size < 1) page_size = 30;
        if (page_size > 30) page_size = 30;

        auto res = DbRepository::instance().get_piles_paged(page, page_size, station_id_filter, status_filter, type_filter);
        if (!res) return make_error_response(res.error());
        return make_success_response(*res);
    }
};

} // namespace ev
