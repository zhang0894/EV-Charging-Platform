#pragma once

#include "../common/types.hpp"
#include "../common/error.hpp"
#include "../common/models.hpp"
#include "../common/response.hpp"
#include "../db/db_repository.hpp"
#include "../memory/rtree_index.hpp"
#include "../memory/state_pool.hpp"
#include "../cache/redis_cache.hpp"

namespace ev {

class StationController {
public:
    static http::response<http::string_body> handle_get_nearby_stations(
        double latitude,
        double longitude,
        double radius_km = 10.0,
        size_t limit = 20
    ) {
        auto nearby_results = StationRTree::instance().search_nearby(latitude, longitude, radius_km, limit);

        StationNearbyListResponseData data;
        data.total = nearby_results.size();

        for (const auto& item : nearby_results) {
            auto mem_st = StationRTree::instance().get_station(item.station_id);
            StationModel st;
            if (mem_st) {
                st = *mem_st;
            } else {
                auto st_res = DbRepository::instance().get_station_by_id(item.station_id);
                if (!st_res) continue;
                st = *st_res;
            }

            auto summary = ChargingStatePool::instance().get_station_pile_summary(item.station_id);

            data.stations.push_back(StationNearbyCardDTO{
                .station_id = st.station_id,
                .station_name = st.station_name,
                .address = st.address,
                .latitude = st.latitude,
                .longitude = st.longitude,
                .distance_km = item.distance_km,
                .price_per_kwh = st.price_per_kwh,
                .service_fee_per_kwh = st.service_fee_per_kwh,
                .overtime_fee_per_15min = st.overtime_fee_per_15min,
                .total_piles = summary.total_piles,
                .idle_piles = summary.idle_piles,
                .fast_piles_idle = summary.fast_piles_idle,
                .slow_piles_idle = summary.slow_piles_idle,
                .station_status = st.status
            });
        }

        return make_success_response(data);
    }

    static http::response<http::string_body> handle_get_station_detail(int64_t station_id) {
        auto mem_st = StationRTree::instance().get_station(station_id);
        StationModel st;
        if (mem_st) {
            st = *mem_st;
        } else {
            std::string cache_key = std::format("cache:station:model:{}", station_id);
            auto cached_st = RedisCache::instance().get_json<StationModel>(cache_key);
            if (cached_st) {
                st = *cached_st;
            } else {
                auto st_res = DbRepository::instance().get_station_by_id(station_id);
                if (!st_res) {
                    return make_error_response(st_res.error());
                }
                st = *st_res;
                RedisCache::instance().set_json(cache_key, st, 120); // 120s TTL
            }
        }

        auto pool_piles = ChargingStatePool::instance().get_piles_by_station(station_id);
        auto summary = ChargingStatePool::instance().get_station_pile_summary(station_id);

        StationDetailResponseData data{
            .station_id = st.station_id,
            .station_name = st.station_name,
            .address = st.address,
            .latitude = st.latitude,
            .longitude = st.longitude,
            .contact_phone = st.contact_phone,
            .operating_hours = st.operating_hours,
            .price_per_kwh = st.price_per_kwh,
            .service_fee_per_kwh = st.service_fee_per_kwh,
            .overtime_fee_per_15min = st.overtime_fee_per_15min,
            .overtime_grace_minutes = st.overtime_grace_minutes,
            .total_piles = summary.total_piles,
            .idle_piles = summary.idle_piles
        };

        for (const auto& p : pool_piles) {
            int st_code = 1;
            std::string st_desc = "空闲中";
            if (p.status == "IDLE") { st_code = 1; st_desc = "空闲可用"; }
            else if (p.status == "PREPARING") { st_code = 2; st_desc = "插枪准备中"; }
            else if (p.status == "CHARGING") { st_code = 3; st_desc = "充电中"; }
            else if (p.status == "FINISHING") { st_code = 4; st_desc = "充电完成待拔枪"; }
            else if (p.status == "FAULT") { st_code = 5; st_desc = "设备故障"; }
            else if (p.status == "MAINTENANCE") { st_code = 6; st_desc = "锁定维护中"; }
            else if (p.status == "OFFLINE") { st_code = 7; st_desc = "离线"; }

            data.piles.push_back(PileDetailDTO{
                .pile_id = p.pile_id,
                .pile_name = p.pile_name,
                .type = p.type,
                .type_desc = (p.type == "FAST" ? "直流快充" : "交流慢充"),
                .gun_type = "国标2015",
                .max_power_kw = p.max_power_kw,
                .voltage_range = "200V-750V",
                .status = p.status,
                .status_code = st_code,
                .status_desc = st_desc
            });
        }

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
};

} // namespace ev
