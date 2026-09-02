#pragma once

#include "../common/types.hpp"
#include "../common/error.hpp"
#include "../common/models.hpp"
#include "../common/response.hpp"
#include "../db/db_repository.hpp"
#include "../memory/state_pool.hpp"
#include "../websocket/ws_manager.hpp"
#include <glaze/glaze.hpp>

namespace ev {

class ChargingController {
public:
    static http::response<http::string_body> handle_check_active_order(int64_t user_id) {
        auto res = DbRepository::instance().get_active_order_by_user(user_id);
        if (!res) {
            return make_error_response(res.error());
        }

        ActiveOrderCheckResponseData data;
        if (res->has_value()) {
            const auto& ord = res->value();
            auto st_res = DbRepository::instance().get_station_by_id(ord.station_id);
            std::string st_name = st_res ? st_res->station_name : "";

            auto pile_st = ChargingStatePool::instance().get_pile_state(ord.pile_id);
            int soc = pile_st ? pile_st->current_soc : ord.start_soc;
            double energy = pile_st ? pile_st->charged_energy_kwh : ord.charged_energy_kwh;
            double cost = pile_st ? cents_to_yuan(pile_st->total_fee_cents) : cents_to_yuan(ord.total_fee_cents);

            data.has_active_order = true;
            data.active_order = ActiveOrderDTO{
                .order_id = ord.order_id,
                .station_id = ord.station_id,
                .station_name = st_name,
                .pile_id = ord.pile_id,
                .order_status = ord.order_status,
                .start_time = ord.start_time,
                .charged_energy_kwh = energy,
                .current_cost = cost,
                .soc = soc
            };
        } else {
            data.has_active_order = false;
        }

        return make_success_response(data);
    }

    static http::response<http::string_body> handle_start_charging(
        int64_t user_id,
        const http::request<http::string_body>& req
    ) {
        StartChargingRequest start_req;
        auto err = glz::read_json(start_req, req.body());
        if (err || start_req.pile_id.empty()) {
            return make_error_response(AppError::InvalidJsonPayload, "Missing pile_id");
        }

        // 1. 检查是否有未结订单
        auto act_res = DbRepository::instance().get_active_order_by_user(user_id);
        if (act_res && act_res->has_value()) {
            return make_error_response(AppError::ActiveOrderExists, "Please settle existing active order first");
        }

        // 2. 检查钱包余额是否满足最低起充门槛 (20元)
        auto w_res = DbRepository::instance().get_wallet(user_id);
        if (!w_res || w_res->balance_cents < 2000) {
            return make_error_response(AppError::InsufficientBalance, "Wallet balance must be at least 20.00 RMB to start charging");
        }

        // 3. 校验充电桩存在与空闲状态
        auto p_res = DbRepository::instance().get_pile_by_id(start_req.pile_id);
        if (!p_res) {
            return make_error_response(p_res.error());
        }

        auto p_pool = ChargingStatePool::instance().get_pile_state(start_req.pile_id);
        if (!p_pool || p_pool->status != "IDLE") {
            return make_error_response(AppError::PileBusyOrReserved, "Charging pile is not in IDLE state");
        }

        auto st_res = DbRepository::instance().get_station_by_id(p_res->station_id);
        if (!st_res) {
            return make_error_response(st_res.error());
        }

        int64_t now = current_time_ms();
        std::string order_id = std::format("ORD_{}_{}", now, user_id);

        // 4. 更新内存状态池为 CHARGING
        bool pool_ok = ChargingStatePool::instance().start_charging(
            start_req.pile_id,
            order_id,
            user_id,
            20,
            st_res->price_per_kwh,
            st_res->service_fee_per_kwh,
            st_res->overtime_fee_per_15min,
            st_res->overtime_grace_minutes
        );

        if (!pool_ok) {
            return make_error_response(AppError::PileBusyOrReserved);
        }

        // 5. 写入数据库持久化
        OrderModel order{
            .order_id = order_id,
            .user_id = user_id,
            .station_id = st_res->station_id,
            .pile_id = start_req.pile_id,
            .strategy_type = start_req.strategy_type,
            .strategy_value = start_req.strategy_value,
            .order_status = "CHARGING",
            .start_time = now,
            .start_soc = 20,
            .end_soc = 20,
            .electricity_price = st_res->price_per_kwh,
            .service_price = st_res->service_fee_per_kwh,
            .overtime_grace_minutes = st_res->overtime_grace_minutes,
            .overtime_rate_per_15min = st_res->overtime_fee_per_15min,
            .created_at = now,
            .updated_at = now
        };

        auto create_res = DbRepository::instance().create_order(order);
        if (!create_res) {
            ChargingStatePool::instance().stop_charging(start_req.pile_id);
            return make_error_response(create_res.error());
        }

        // 广播桩状态变动通知
        WsManager::instance().broadcast_pile_status(PileStatusChangedBroadcastFrame{
            .event = "PILE_STATUS_CHANGED",
            .station_id = st_res->station_id,
            .pile_id = start_req.pile_id,
            .old_status = "IDLE",
            .new_status = "CHARGING",
            .new_status_code = 3,
            .timestamp = now
        });

        StartChargingResponseData data{
            .order_id = order_id,
            .pile_id = start_req.pile_id,
            .station_id = st_res->station_id,
            .station_name = st_res->station_name,
            .order_status = "CHARGING",
            .start_time = now,
            .initial_soc = 20,
            .unit_price = st_res->price_per_kwh,
            .service_price = st_res->service_fee_per_kwh,
            .overtime_fee_per_15min = st_res->overtime_fee_per_15min,
            .ws_telemetry_url = std::format("ws://localhost:8080/ws/v1/charging/{}", order_id)
        };

        return make_success_response(data);
    }

    static http::response<http::string_body> handle_stop_charging(
        int64_t user_id,
        const http::request<http::string_body>& req
    ) {
        StopChargingRequest stop_req;
        auto err = glz::read_json(stop_req, req.body());
        if (err || stop_req.order_id.empty()) {
            return make_error_response(AppError::InvalidJsonPayload, "Missing order_id");
        }

        auto o_res = DbRepository::instance().get_order_by_id(stop_req.order_id);
        if (!o_res) return make_error_response(o_res.error());

        if (o_res->user_id != user_id && user_id != 0) {
            return make_error_response(AppError::PermissionDenied);
        }

        if (o_res->order_status != "CHARGING") {
            return make_error_response(AppError::OrderCannotBeStopped, "Order is not in CHARGING state");
        }

        // 从内存状态池中获取最新遥测累加指标
        auto pool_st = ChargingStatePool::instance().get_pile_state(o_res->pile_id);
        int64_t now = current_time_ms();

        double energy = pool_st ? pool_st->charged_energy_kwh : 0.0;
        int end_soc = pool_st ? pool_st->current_soc : 100;
        int64_t elec_cents = pool_st ? pool_st->electricity_fee_cents : yuan_to_cents(energy * o_res->electricity_price);
        int64_t serv_cents = pool_st ? pool_st->service_fee_cents : yuan_to_cents(energy * o_res->service_price);
        int overtime_mins = pool_st ? pool_st->overtime_duration_minutes : 0;
        int64_t overtime_cents = pool_st ? pool_st->overtime_fee_cents : 0;
        int64_t total_cents = elec_cents + serv_cents + overtime_cents;

        // 释放状态池中的电桩为 IDLE
        ChargingStatePool::instance().stop_charging(o_res->pile_id);

        auto stop_res = DbRepository::instance().stop_order(
            stop_req.order_id,
            now,
            end_soc,
            energy,
            elec_cents,
            serv_cents,
            overtime_mins,
            overtime_cents,
            total_cents,
            stop_req.stop_reason
        );

        if (!stop_res) {
            return make_error_response(stop_res.error());
        }

        // 广播结束帧
        WsManager::instance().broadcast_charging_finished(ChargingFinishedFrame{
            .event = "CHARGING_FINISHED",
            .order_id = stop_req.order_id,
            .pile_id = o_res->pile_id,
            .finish_reason = stop_req.stop_reason,
            .total_energy_kwh = energy,
            .electricity_fee = cents_to_yuan(elec_cents),
            .service_fee = cents_to_yuan(serv_cents),
            .overtime_fee = cents_to_yuan(overtime_cents),
            .total_amount = cents_to_yuan(total_cents),
            .timestamp = now
        });

        return make_success_response(*stop_res);
    }

    static http::response<http::string_body> handle_settle_order(
        int64_t user_id,
        const http::request<http::string_body>& req
    ) {
        SettleOrderRequest settle_req;
        auto err = glz::read_json(settle_req, req.body());
        if (err || settle_req.order_id.empty()) {
            return make_error_response(AppError::InvalidJsonPayload, "Missing order_id");
        }

        auto o_res = DbRepository::instance().get_order_by_id(settle_req.order_id);
        if (!o_res) return make_error_response(o_res.error());

        if (o_res->user_id != user_id && user_id != 0) {
            return make_error_response(AppError::PermissionDenied);
        }

        std::string idem_key = std::format("SETTLE_{}", settle_req.order_id);
        auto res = DbRepository::instance().settle_order_with_wallet(settle_req.order_id, idem_key);
        if (!res) {
            return make_error_response(res.error());
        }

        return make_success_response(*res);
    }

    static http::response<http::string_body> handle_get_my_orders(
        int64_t user_id,
        int page,
        int page_size,
        std::string_view status_filter,
        std::string_view sort_order
    ) {
        auto res = DbRepository::instance().get_user_orders_paged(user_id, page, page_size, status_filter, sort_order);
        if (!res) {
            return make_error_response(res.error());
        }
        return make_success_response(*res);
    }

    static http::response<http::string_body> handle_get_order_detail(
        int64_t user_id,
        std::string_view order_id
    ) {
        auto o_res = DbRepository::instance().get_order_by_id(order_id);
        if (!o_res) return make_error_response(o_res.error());

        if (o_res->user_id != user_id && user_id != 0) {
            return make_error_response(AppError::PermissionDenied);
        }

        auto st_res = DbRepository::instance().get_station_by_id(o_res->station_id);
        auto p_res = DbRepository::instance().get_pile_by_id(o_res->pile_id);
        auto u_res = DbRepository::instance().get_user_by_id(o_res->user_id);

        int64_t duration = (o_res->end_time > o_res->start_time) ? (o_res->end_time - o_res->start_time) / 1000 : 0;

        OrderDetailResponseData data{
            .order_id = o_res->order_id,
            .user_id = o_res->user_id,
            .user_phone = u_res ? u_res->phone : "",
            .station_id = o_res->station_id,
            .station_name = st_res ? st_res->station_name : "",
            .pile_id = o_res->pile_id,
            .pile_type = p_res ? p_res->type : "FAST",
            .order_status = o_res->order_status,
            .start_time = o_res->start_time,
            .end_time = o_res->end_time,
            .duration_seconds = duration,
            .start_soc = o_res->start_soc,
            .end_soc = o_res->end_soc,
            .charged_energy_kwh = o_res->charged_energy_kwh,
            .electricity_price = o_res->electricity_price,
            .electricity_fee = cents_to_yuan(o_res->electricity_fee_cents),
            .service_price = o_res->service_price,
            .service_fee = cents_to_yuan(o_res->service_fee_cents),
            .overtime_grace_minutes = o_res->overtime_grace_minutes,
            .overtime_duration_minutes = o_res->overtime_duration_minutes,
            .overtime_rate_per_15min = o_res->overtime_rate_per_15min,
            .overtime_fee = cents_to_yuan(o_res->overtime_fee_cents),
            .total_amount = cents_to_yuan(o_res->total_fee_cents),
            .total_amount_cents = o_res->total_fee_cents,
            .stop_reason = o_res->stop_reason,
            .settled_at = o_res->settled_at
        };

        if (o_res->order_status == "REFUNDED" && !o_res->refund_transaction_id.empty()) {
            data.refund_info = RefundDetailDTO{
                .refund_transaction_id = o_res->refund_transaction_id,
                .refund_amount = cents_to_yuan(o_res->total_fee_cents),
                .operator_id = o_res->operator_id,
                .reason = o_res->refund_reason,
                .refunded_at = o_res->refunded_at
            };
        }

        return make_success_response(data);
    }
};

} // namespace ev
