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

namespace ev {

class StationController {
public:
    static http::response<http::string_body> handle_get_nearby_stations(
        double latitude,
        double longitude,
        double radius_km = 2.0,
        size_t limit = 20
    ) {
        auto nearby_results = StationRTree::instance().search_nearby_adaptive(latitude, longitude, radius_km, limit);

        StationNearbyListResponseData data;
        data.total = nearby_results.size();
        data.stations.reserve(nearby_results.size());

        for (const auto& item : nearby_results) {
            int32_t sid32 = static_cast<int32_t>(item.station_id);
            const StaticStation* static_st = find_static_station(sid32);

            std::string name;
            std::string addr;
            std::string district;
            uint8_t district_code = 0;
            double lat = item.latitude;
            double lon = item.longitude;

            if (static_st) {
                name = static_st->name;
                addr = static_st->address;
                district_code = static_st->district_code;
                district = std::string(get_district_name_by_code(district_code));
            } else {
                auto mem_st = StationRTree::instance().get_station(item.station_id);
                if (mem_st) {
                    name = mem_st->station_name;
                    addr = mem_st->address;
                } else {
                    auto st_res = DbRepository::instance().get_station_by_id(item.station_id);
                    if (st_res) {
                        name = st_res->station_name;
                        addr = st_res->address;
                    }
                }
            }

            auto summary = ChargingStatePool::instance().get_station_pile_summary(item.station_id);
            bool is_on = StationStatusManager::instance().is_online(item.station_id);
            int st_status = is_on ? 1 : 2;

            data.stations.push_back(StationNearbyCardDTO{
                .station_id = item.station_id,
                .id = item.station_id,
                .station_name = name,
                .district = district,
                .district_code = district_code,
                .address = addr,
                .latitude = lat,
                .longitude = lon,
                .distance_km = std::round(item.distance_km * 100.0) / 100.0,
                .price_per_kwh = StationPriceManager::instance().get_price(item.station_id),
                .service_fee_per_kwh = 0.35,
                .overtime_fee_per_15min = 5.00,
                .total_piles = summary.total_piles,
                .pile_count = summary.total_piles,
                .idle_piles = summary.idle_piles,
                .available_count = summary.idle_piles,
                .fast_piles_idle = summary.fast_piles_idle,
                .slow_piles_idle = summary.slow_piles_idle,
                .has_fast_pile = summary.has_fast_pile,
                .station_status = st_status,
                .is_online = is_on
            });
        }

        return make_success_response(data);
    }

    static http::response<http::string_body> handle_get_stations_by_district(
        std::string_view district_param,
        double latitude,
        double longitude,
        int page = 1,
        int page_size = 20
    ) {
        if (page < 1) page = 1;
        if (page_size < 1) page_size = 20;
        if (page_size > 100) page_size = 100;

        // 识别行政区 (支持 0~15 数字编码，或 "海淀区"/"海淀" 等名称)
        std::optional<uint8_t> opt_code;
        try {
            size_t idx = 0;
            int num = std::stoi(std::string(district_param), &idx);
            if (idx == district_param.size() && num >= 0 && num < 16) {
                opt_code = static_cast<uint8_t>(num);
            }
        } catch (...) {}

        if (!opt_code) {
            opt_code = get_district_code_by_name(district_param);
        }

        if (!opt_code) {
            // 前缀模糊匹配，如 "朝阳" 匹配 "朝阳区"
            for (uint8_t i = 0; i < 16; ++i) {
                if (DISTRICT_NAMES[i].starts_with(district_param) || district_param.starts_with(DISTRICT_NAMES[i].substr(0, std::min<size_t>(6, DISTRICT_NAMES[i].size())))) {
                    opt_code = i;
                    break;
                }
            }
        }

        if (!opt_code) {
            return make_error_response(AppError::InvalidJsonPayload, "Invalid district name or code");
        }

        uint8_t dcode = *opt_code;
        std::string dname = std::string(get_district_name_by_code(dcode));
        const auto& station_ids = StationRTree::instance().get_district_stations(dcode);

        struct SorterItem {
            int32_t station_id;
            double distance_km;
        };

        std::vector<SorterItem> items;
        items.reserve(station_ids.size());

        bool has_coords = (latitude != 0.0 || longitude != 0.0);

        for (int32_t sid : station_ids) {
            const StaticStation* st = find_static_station(sid);
            if (!st) continue;
            double dist = 0.0;
            if (has_coords) {
                dist = StationRTree::calculate_distance_km(latitude, longitude, st->latitude, st->longitude);
            }
            items.push_back(SorterItem{
                .station_id = sid,
                .distance_km = dist
            });
        }

        // 若提供了坐标则按距离排序，否则按 station_id 升序
        if (has_coords) {
            std::sort(items.begin(), items.end(), [](const auto& a, const auto& b) {
                return a.distance_km < b.distance_km;
            });
        } else {
            std::sort(items.begin(), items.end(), [](const auto& a, const auto& b) {
                return a.station_id < b.station_id;
            });
        }

        int64_t total = items.size();
        int64_t start_idx = static_cast<int64_t>(page - 1) * page_size;

        StationDistrictListResponseData resp{
            .total = total,
            .page = page,
            .page_size = page_size,
            .district = dname,
            .district_code = dcode,
            .stations = {}
        };

        if (start_idx < total) {
            int64_t end_idx = std::min(start_idx + page_size, total);
            resp.stations.reserve(end_idx - start_idx);

            for (int64_t i = start_idx; i < end_idx; ++i) {
                const auto& item = items[i];
                const StaticStation* st = find_static_station(item.station_id);
                if (!st) continue;

                auto summary = ChargingStatePool::instance().get_station_pile_summary(st->station_id);
                bool is_on = StationStatusManager::instance().is_online(st->station_id);

                resp.stations.push_back(StationNearbyCardDTO{
                    .station_id = st->station_id,
                    .id = st->station_id,
                    .station_name = std::string(st->name),
                    .district = dname,
                    .district_code = dcode,
                    .address = std::string(st->address),
                    .latitude = st->latitude,
                    .longitude = st->longitude,
                    .distance_km = std::round(item.distance_km * 100.0) / 100.0,
                    .price_per_kwh = StationPriceManager::instance().get_price(st->station_id),
                    .service_fee_per_kwh = 0.35,
                    .overtime_fee_per_15min = 5.00,
                    .total_piles = summary.total_piles,
                    .pile_count = summary.total_piles,
                    .idle_piles = summary.idle_piles,
                    .available_count = summary.idle_piles,
                    .fast_piles_idle = summary.fast_piles_idle,
                    .slow_piles_idle = summary.slow_piles_idle,
                    .has_fast_pile = summary.has_fast_pile,
                    .station_status = is_on ? 1 : 2,
                    .is_online = is_on
                });
            }
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
        int grace_mins = 15;
        std::string phone = "010-88889999";
        std::string hours = "00:00 - 24:00";

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
                grace_mins = mem_st->overtime_grace_minutes;
                phone = mem_st->contact_phone;
                hours = mem_st->operating_hours;
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
                    grace_mins = cached_st->overtime_grace_minutes;
                    phone = cached_st->contact_phone;
                    hours = cached_st->operating_hours;
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
                    grace_mins = st_res->overtime_grace_minutes;
                    phone = st_res->contact_phone;
                    hours = st_res->operating_hours;
                    RedisCache::instance().set_json(cache_key, *st_res, 120);
                }
            }
        }

        double price = StationPriceManager::instance().get_price(station_id);
        double dist = 0.0;
        if (user_lat != 0.0 && user_lon != 0.0 && lat != 0.0 && lon != 0.0) {
            dist = std::round(StationRTree::calculate_distance_km(user_lat, user_lon, lat, lon) * 100.0) / 100.0;
        }

        auto pool_piles = ChargingStatePool::instance().get_piles_by_station(station_id);
        auto summary = ChargingStatePool::instance().get_station_pile_summary(station_id);
        bool is_on = StationStatusManager::instance().is_online(station_id);

        StationDetailResponseData data{
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
            .overtime_grace_minutes = grace_mins,
            .total_piles = summary.total_piles,
            .pile_count = summary.total_piles,
            .idle_piles = summary.idle_piles,
            .available_count = summary.idle_piles,
            .fast_piles_idle = summary.fast_piles_idle,
            .slow_piles_idle = summary.slow_piles_idle,
            .has_fast_pile = summary.has_fast_pile,
            .station_status = is_on ? 1 : 2,
            .is_online = is_on,
            .contact_phone = phone,
            .operating_hours = hours,
            .piles = {}
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
            else if (p.status == "RESERVED") { st_code = 8; st_desc = "已预约锁定"; }

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
