#pragma once

#include "db_pool.hpp"
#include "../common/models.hpp"
#include "../common/error.hpp"
#include <string_view>
#include <vector>
#include <optional>

namespace ev {

class DbRepository {
public:
    static DbRepository& instance();

    // ==========================================
    // 1. 用户与认证
    // ==========================================
    Result<UserModel> get_user_by_id(int64_t user_id);
    Result<UserModel> get_user_by_phone(std::string_view phone);
    Result<UserModel> get_user_by_account(std::string_view account);
    Result<std::pair<UserModel, bool>> get_or_create_user_passwordless(std::string_view phone);
    Result<UserModel> create_user(std::string_view phone, std::string_view password, std::string_view nickname, std::string_view role = "user");
    Result<void> update_user_password(int64_t user_id, std::string_view new_password);
    Result<void> update_user_nickname(int64_t user_id, std::string_view nickname);
    Result<std::string> update_user_avatar(int64_t user_id, std::string_view avatar_url);
    Result<void> save_user_avatar(int64_t user_id, std::string_view content_type, std::string_view binary_data);
    Result<std::optional<AvatarModel>> get_user_avatar(int64_t user_id);
    Result<void> clear_user_avatars();
    Result<void> update_user_status(int64_t user_id, int status);
    Result<UserAdminListResponseData> get_users_admin_paged(int page, int page_size, std::string_view phone_filter = "", int status_filter = 0);

    // ==========================================
    // 2. 钱包与资金
    // ==========================================
    Result<UserWalletModel> get_wallet(int64_t user_id);
    Result<TransactionItemDTO> recharge_wallet(int64_t user_id, int64_t amount_cents, std::string_view idempotent_key, std::string_view remark);
    Result<TransactionListResponseData> get_transactions_paged(int64_t user_id, int page, int page_size, int flow_type = 0);
    Result<UserWalletAdjustResponseData> adjust_user_wallet(int64_t user_id, int64_t amount_cents, int64_t operator_id, std::string_view idempotent_key, std::string_view remark);

    // ==========================================
    // 3. 充电站管理与销售统计
    // ==========================================
    Result<std::vector<StationModel>> get_all_stations();
    Result<StationModel> get_station_by_id(int64_t station_id);
    Result<StationAdminListResponseData> get_stations_admin_paged(int page, int page_size, std::string_view name_filter = "", int status_filter = 0);
    Result<int64_t> create_station(const CreateStationRequest& req);
    Result<void> update_station(int64_t station_id, const UpdateStationRequest& req);
    Result<void> delete_station(int64_t station_id);
    Result<StationSalesStatsResponseData> get_station_sales_stats(int64_t station_id, std::string_view time_range);

    // ==========================================
    // 4. 充电桩管理
    // ==========================================
    Result<std::vector<PileModel>> get_all_piles();
    Result<std::vector<PileModel>> get_piles_by_station(int64_t station_id);
    Result<PileModel> get_pile_by_id(std::string_view pile_id);
    Result<PileAdminListResponseData> get_piles_admin_paged(int page, int page_size, int64_t station_id_filter = 0, std::string_view status_filter = "", std::string_view type_filter = "");
    Result<void> create_pile(const CreatePileRequest& req);
    Result<void> update_pile_status(std::string_view pile_id, std::string_view status);
    Result<void> update_pile_metrics(std::string_view pile_id, int64_t add_count, double add_hours);

    // ==========================================
    // 5. 充电业务流程与订单
    // ==========================================
    Result<std::optional<OrderModel>> get_active_order_by_user(int64_t user_id);
    Result<OrderModel> get_order_by_id(std::string_view order_id);
    Result<void> create_order(const OrderModel& order);
    Result<StopChargingResponseData> stop_order(
        std::string_view order_id,
        int64_t end_time,
        int end_soc,
        double energy_kwh,
        int64_t elec_fee_cents,
        int64_t serv_fee_cents,
        int overtime_mins,
        int64_t overtime_fee_cents,
        int64_t total_fee_cents,
        std::string_view stop_reason
    );
    Result<SettleOrderResponseData> settle_order_with_wallet(std::string_view order_id, std::string_view idempotent_key);
    Result<OrderListResponseData> get_user_orders_paged(int64_t user_id, int page, int page_size, std::string_view status_filter = "", std::string_view sort_order = "desc");
    Result<OrderListResponseData> get_orders_admin_paged(int page, int page_size, int64_t station_id_filter = 0, std::string_view status_filter = "", std::string_view start_date = "", std::string_view end_date = "");
    Result<AdminUserOrdersResponseData> get_admin_user_orders(int64_t user_id, int page, int page_size, std::string_view sort_order = "asc");
    Result<AdminOrderRefundResponseData> refund_order_with_wallet(std::string_view order_id, int64_t refund_amount_cents, int64_t operator_id, std::string_view idempotent_key, std::string_view reason);

    // ==========================================
    // 6. 运营大盘数据统计
    // ==========================================
    Result<AdminDashboardSummaryData> get_admin_dashboard_summary();
    Result<AdminRevenueTrendData> get_admin_revenue_trend(int days = 7);
    Result<AdminPileStatusOverviewData> get_admin_pile_status_overview();

    // ==========================================
    // 7. 充电桩预约
    // ==========================================
    Result<ReservePileResponseData> create_reservation(int64_t user_id, std::string_view pile_id);
    Result<std::optional<ReservationModel>> get_active_reservation_by_user(int64_t user_id);
    Result<std::optional<ReservationModel>> get_active_reservation_by_pile(std::string_view pile_id);
    Result<CancelReservationResponseData> cancel_reservation(int64_t user_id, std::string_view reservation_id);
    Result<void> fulfill_reservation(int64_t user_id, std::string_view pile_id);
    Result<std::vector<std::string>> timeout_expired_reservations();

private:
    DbRepository() = default;
};

} // namespace ev
