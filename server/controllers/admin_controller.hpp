#pragma once

#include "../common/types.hpp"
#include "../common/error.hpp"
#include "../common/models.hpp"
#include "../common/auth_token.hpp"
#include "../common/response.hpp"
#include "../db/db_repository.hpp"
#include "../memory/state_pool.hpp"
#include "../memory/rtree_index.hpp"
#include "../websocket/ws_manager.hpp"
#include "../cache/redis_cache.hpp"
#include "../data/static_stations.hpp"
#include "../memory/station_status_manager.hpp"
#include "../memory/station_price_manager.hpp"
#include <glaze/glaze.hpp>

namespace ev {

class AdminController {
public:
    // ==========================================
    // 1. 运营态势大盘 (已接入 Redis 实时/TTL 缓存 + 防击穿 Single-Flight)
    // ==========================================

    static http::response<http::string_body> handle_get_dashboard_summary() {
        const std::string cache_key = "cache:dashboard:summary";
        auto cached = RedisCache::instance().get_json<AdminDashboardSummaryData>(cache_key);
        if (cached) {
            return make_success_response(*cached);
        }

        // 互斥锁防止高并发击穿 (Double-Checked Locking)
        static std::mutex summary_mutex;
        std::lock_guard<std::mutex> lk(summary_mutex);
        cached = RedisCache::instance().get_json<AdminDashboardSummaryData>(cache_key);
        if (cached) {
            return make_success_response(*cached);
        }

        auto res = DbRepository::instance().get_admin_dashboard_summary();
        if (!res) return make_error_response(res.error());

        RedisCache::instance().set_json(cache_key, *res, 30); // 30s TTL
        return make_success_response(*res);
    }

    static http::response<http::string_body> handle_get_revenue_trend(int days) {
        std::string cache_key = std::format("cache:dashboard:trend:{}", days);
        auto cached = RedisCache::instance().get_json<AdminRevenueTrendData>(cache_key);
        if (cached) {
            return make_success_response(*cached);
        }

        auto res = DbRepository::instance().get_admin_revenue_trend(days);
        if (!res) return make_error_response(res.error());

        RedisCache::instance().set_json(cache_key, *res, 30); // 30s TTL
        return make_success_response(*res);
    }

    static http::response<http::string_body> handle_get_pile_status_overview() {
        auto res = DbRepository::instance().get_admin_pile_status_overview();
        if (!res) return make_error_response(res.error());
        return make_success_response(*res);
    }

    // ==========================================
    // 2. 充电站管理
    // ==========================================

    static http::response<http::string_body> handle_online_station(int64_t station_id) {
        if (station_id < 1 || station_id > static_cast<int64_t>(STATIC_STATION_COUNT)) {
            return make_error_response(AppError::StationNotFound, "Station not found");
        }

        bool was_offline = !StationStatusManager::instance().is_online(station_id);
        StationStatusManager::instance().set_online(station_id, true);
        if (was_offline) {
            ChargingStatePool::instance().set_station_piles_online(station_id);
        }

        StationOnlineStatusResponseData resp{
            .station_id = station_id,
            .status = 1,
            .is_online = true,
            .terminated_orders = 0,
            .message = "Station brought online successfully"
        };
        return make_success_response(resp);
    }

    static http::response<http::string_body> handle_offline_station(int64_t station_id) {
        if (station_id < 1 || station_id > static_cast<int64_t>(STATIC_STATION_COUNT)) {
            return make_error_response(AppError::StationNotFound, "Station not found");
        }

        // 1. 检索该电站下所有充电桩，若存在进行中的订单则同步强制结单与结算，释放预约
        auto piles = ChargingStatePool::instance().get_piles_by_station(station_id);
        int terminated_count = 0;
        int64_t now = current_time_ms();

        for (const auto& p : piles) {
            if (p.status == "CHARGING" || !p.active_order_id.empty()) {
                std::string oid = p.active_order_id;
                double energy = p.charged_energy_kwh;
                int end_soc = p.current_soc;
                int64_t elec_cents = p.electricity_fee_cents;
                int64_t serv_cents = p.service_fee_cents;
                int overtime_mins = p.overtime_duration_minutes;
                int64_t overtime_cents = p.overtime_fee_cents;
                int64_t total_cents = elec_cents + serv_cents + overtime_cents;

                // 从内存池释放充电中标记
                ChargingStatePool::instance().stop_charging(p.pile_id);

                // 数据库更新订单状态为结束
                DbRepository::instance().stop_order(
                    oid,
                    now,
                    end_soc,
                    energy,
                    elec_cents,
                    serv_cents,
                    overtime_mins,
                    overtime_cents,
                    total_cents,
                    "STATION_OFFLINE"
                );

                // 钱包同步扣款扣账结算
                std::string idem = std::format("OFFLINE_SETTLE_{}_{}", oid, now);
                DbRepository::instance().settle_order_with_wallet(oid, idem);

                // 广播订单结束推送
                WsManager::instance().broadcast_charging_finished(ChargingFinishedFrame{
                    .event = "CHARGING_FINISHED",
                    .order_id = oid,
                    .pile_id = p.pile_id,
                    .finish_reason = "STATION_OFFLINE",
                    .total_energy_kwh = energy,
                    .electricity_fee = cents_to_yuan(elec_cents),
                    .service_fee = cents_to_yuan(serv_cents),
                    .overtime_fee = cents_to_yuan(overtime_cents),
                    .total_amount = cents_to_yuan(total_cents),
                    .timestamp = now
                });

                terminated_count++;
            } else if (p.status == "RESERVED") {
                ChargingStatePool::instance().release_reserved_pile(p.pile_id);
            }
        }

        // 2. 设置电站下线状态
        StationStatusManager::instance().set_online(station_id, false);

        // 3. 将电站下所有充电桩状态置为 OFFLINE 并保真记录下线前状态 (IDLE / FAULT / OFFLINE)
        ChargingStatePool::instance().set_station_piles_offline(station_id);

        StationOnlineStatusResponseData resp{
            .station_id = station_id,
            .status = 2,
            .is_online = false,
            .terminated_orders = terminated_count,
            .message = "Station taken offline, active orders terminated and settled"
        };
        return make_success_response(resp);
    }

    // ==========================================
    // 3. 充电桩管理
    // ==========================================

    static http::response<http::string_body> handle_create_pile(const http::request<http::string_body>& req) {
        CreatePileRequest p_req;
        auto err = glz::read_json(p_req, req.body());
        if (err || p_req.pile_id.empty() || p_req.station_id <= 0) {
            return make_error_response(AppError::InvalidJsonPayload);
        }

        auto res = DbRepository::instance().create_pile(p_req);
        if (!res) return make_error_response(res.error());

        // 加入内存状态池
        double pwr = (p_req.max_power_kw > 0.0) ? p_req.max_power_kw : ((p_req.power_kw > 0.0) ? p_req.power_kw : 120.0);
        ChargingStatePool::instance().register_pile(p_req.pile_id, p_req.station_id, p_req.pile_name, p_req.type, pwr);

        return make_empty_success_response();
    }

    static http::response<http::string_body> handle_restart_pile(
        std::string_view pile_id,
        const http::request<http::string_body>& req
    ) {
        PileRestartRequest r_req;
        glz::read_json(r_req, req.body());

        auto p_res = DbRepository::instance().get_pile_by_id(pile_id);
        if (!p_res) return make_error_response(p_res.error());

        // 关键校验：若所属充电站处于下线状态，拒绝重启充电桩
        if (!StationStatusManager::instance().is_online(p_res->station_id)) {
            return make_error_response(AppError::StationNotFound, "充电站已下线，禁止重启充电桩");
        }

        // 重启并重置状态为 IDLE
        ChargingStatePool::instance().set_pile_status(pile_id, "IDLE");
        DbRepository::instance().update_pile_status(pile_id, "IDLE");

        WsManager::instance().broadcast_pile_status(PileStatusChangedBroadcastFrame{
            .event = "PILE_STATUS_CHANGED",
            .station_id = p_res->station_id,
            .pile_id = std::string(pile_id),
            .old_status = p_res->status,
            .new_status = "IDLE",
            .new_status_code = 1,
            .timestamp = current_time_ms()
        });

        PileRestartResponseData data{
            .pile_id = std::string(pile_id),
            .command = "REBOOT",
            .execution_status = "SUCCESS",
            .new_status = "IDLE",
            .message = "Remote reboot command executed successfully"
        };
        return make_success_response(data);
    }

    static http::response<http::string_body> handle_change_pile_status(
        std::string_view pile_id,
        const http::request<http::string_body>& req
    ) {
        PileStatusChangeRequest sc_req;
        auto err = glz::read_json(sc_req, req.body());

        std::string raw_target = sc_req.target_status;
        if (raw_target.empty()) raw_target = sc_req.status;
        if (raw_target.empty()) raw_target = sc_req.action;

        std::string norm = normalize_pile_status(raw_target);

        // 业务约束校验：本接口专职用于充电桩进入 OFFLINE 状态或上线进入 IDLE 状态
        if (norm != "OFFLINE" && norm != "IDLE") {
            return make_error_response(AppError::InvalidParameters, "目标状态不合法，仅支持设为 OFFLINE 或 IDLE");
        }

        auto p_res = DbRepository::instance().get_pile_by_id(pile_id);
        if (!p_res) return make_error_response(p_res.error());

        // 关键业务规则：在充电站下线时，服务器拒绝修改这个充电站下的所有充电桩的状态
        if (!StationStatusManager::instance().is_online(p_res->station_id)) {
            return make_error_response(AppError::StationNotFound, "充电站已下线，禁止修改其下充电桩状态");
        }

        std::string prev_status = std::string(ChargingStatePool::instance().get_pile_status(pile_id));

        if (norm == "OFFLINE") {
            // 若该桩当前处于充电中或有订单，安全终止并结算
            auto p_state = ChargingStatePool::instance().get_pile_state(pile_id);
            if (p_state && (p_state->status == "CHARGING" || !p_state->active_order_id.empty())) {
                std::string oid = p_state->active_order_id;
                int64_t now = current_time_ms();
                ChargingStatePool::instance().stop_charging(pile_id);
                DbRepository::instance().stop_order(
                    oid, now, p_state->current_soc, p_state->charged_energy_kwh,
                    p_state->electricity_fee_cents, p_state->service_fee_cents,
                    p_state->overtime_duration_minutes, p_state->overtime_fee_cents,
                    p_state->total_fee_cents, "ADMIN_PILE_OFFLINE"
                );
                std::string idem = std::format("PILE_OFFLINE_SETTLE_{}_{}", oid, now);
                DbRepository::instance().settle_order_with_wallet(oid, idem);
            }
            ChargingStatePool::instance().release_reserved_pile(pile_id);
        }

        ChargingStatePool::instance().set_pile_status(pile_id, norm);
        DbRepository::instance().update_pile_status(pile_id, norm);

        WsManager::instance().broadcast_pile_status(PileStatusChangedBroadcastFrame{
            .event = "PILE_STATUS_CHANGED",
            .station_id = p_res->station_id,
            .pile_id = std::string(pile_id),
            .old_status = prev_status,
            .new_status = norm,
            .new_status_code = pile_status_to_code(norm),
            .timestamp = current_time_ms()
        });

        PileStatusChangeResponseData data{
            .pile_id = std::string(pile_id),
            .previous_status = prev_status,
            .current_status = norm
        };
        return make_success_response(data);
    }

    // ==========================================
    // 4. 用户风控与管理
    // ==========================================

    static http::response<http::string_body> handle_get_users(
        int page,
        int page_size,
        std::string_view phone_filter,
        int status_filter
    ) {
        auto res = DbRepository::instance().get_users_admin_paged(page, page_size, phone_filter, status_filter);
        if (!res) return make_error_response(res.error());
        return make_success_response(*res);
    }

    static http::response<http::string_body> handle_change_user_status(
        int64_t user_id,
        int64_t operator_id,
        const http::request<http::string_body>& req
    ) {
        UserStatusChangeRequest sc_req;
        auto err = glz::read_json(sc_req, req.body());
        if (err || sc_req.status <= 0) {
            return make_error_response(AppError::InvalidJsonPayload);
        }

        auto res = DbRepository::instance().update_user_status(user_id, sc_req.status);
        if (!res) return make_error_response(res.error());

        if (sc_req.status == 2) {
            AuthTokenManager::set_user_frozen(user_id, true);
            AuthTokenManager::revoke_user_tokens(user_id);
        } else if (sc_req.status == 1) {
            AuthTokenManager::set_user_frozen(user_id, false);
        }

        int64_t now = current_time_ms();
        UserStatusChangeResponseData data{
            .user_id = user_id,
            .status = sc_req.status,
            .status_desc = (sc_req.status == 1 ? "NORMAL" : "FROZEN"),
            .operator_id = operator_id,
            .updated_at = now
        };
        return make_success_response(data);
    }

    static http::response<http::string_body> handle_adjust_user_wallet(
        int64_t user_id,
        int64_t operator_id,
        const http::request<http::string_body>& req
    ) {
        UserWalletAdjustRequest adj_req;
        auto err = glz::read_json(adj_req, req.body());
        if (err) return make_error_response(AppError::InvalidJsonPayload);

        int64_t amount_cents = (adj_req.amount_cents != 0) ? adj_req.amount_cents : yuan_to_cents(adj_req.amount);
        if (amount_cents == 0) {
            return make_error_response(AppError::InvalidAmount, "Adjustment amount cannot be zero");
        }

        std::string idem_key = std::format("ADJ_{}_{}_{}", user_id, operator_id, current_time_ms());
        auto res = DbRepository::instance().adjust_user_wallet(user_id, amount_cents, operator_id, idem_key, adj_req.remark);
        if (!res) return make_error_response(res.error());

        return make_success_response(*res);
    }

    // ==========================================
    // 5. 订单管理、用户历史订单查询与一键退款
    // ==========================================

    static http::response<http::string_body> handle_get_orders(
        int page,
        int page_size,
        int64_t station_id_filter,
        std::string_view status_filter,
        std::string_view start_date,
        std::string_view end_date
    ) {
        auto res = DbRepository::instance().get_orders_admin_paged(page, page_size, station_id_filter, status_filter, start_date, end_date);
        if (!res) return make_error_response(res.error());
        return make_success_response(*res);
    }

    static http::response<http::string_body> handle_get_user_historical_orders(
        int64_t query_user_id,
        std::string_view phone,
        int page,
        int page_size,
        std::string_view sort_order
    ) {
        int64_t target_uid = query_user_id;
        if (target_uid <= 0 && !phone.empty()) {
            auto u_res = DbRepository::instance().get_user_by_phone(phone);
            if (!u_res) return make_error_response(u_res.error());
            target_uid = u_res->user_id;
        }

        if (target_uid <= 0) {
            return make_error_response(AppError::InvalidParameters, "Must provide user_id or phone");
        }

        auto res = DbRepository::instance().get_admin_user_orders(target_uid, page, page_size, sort_order);
        if (!res) return make_error_response(res.error());

        return make_success_response(*res);
    }

    static http::response<http::string_body> handle_refund_order(
        std::string_view order_id,
        int64_t operator_id,
        const http::request<http::string_body>& req
    ) {
        AdminOrderRefundRequest rf_req;
        glz::read_json(rf_req, req.body());

        auto o_res = DbRepository::instance().get_order_by_id(order_id);
        if (!o_res) return make_error_response(o_res.error());

        int64_t refund_cents = (rf_req.refund_amount_cents > 0) ? rf_req.refund_amount_cents : 
                              (rf_req.refund_amount > 0.0 ? yuan_to_cents(rf_req.refund_amount) : o_res->total_fee_cents);

        std::string idem_key = std::format("REFUND_{}_{}", order_id, current_time_ms());
        std::string reason = rf_req.reason.empty() ? "管理员一键退款" : rf_req.reason;

        auto res = DbRepository::instance().refund_order_with_wallet(order_id, refund_cents, operator_id, idem_key, reason);
        if (!res) return make_error_response(res.error());

        return make_success_response(*res);
    }
};

} // namespace ev
