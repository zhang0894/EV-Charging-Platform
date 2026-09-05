#pragma once

#include "types.hpp"
#include <cstdint>
#include <string>
#include <vector>
#include <optional>

namespace ev {

// ==========================================
// 核心持久化领域模型
// ==========================================

struct UserModel {
    int64_t user_id{0};
    std::string phone;
    std::string password_hash;
    std::string nickname;
    std::string avatar_url;
    std::string role{"user"}; // "user" 或 "admin"
    int status{1};            // 1: 正常, 2: 冻结
    int64_t created_at{0};
    int64_t updated_at{0};
};

struct UserWalletModel {
    int64_t user_id{0};
    int64_t balance_cents{0};
    int64_t frozen_cents{0};
    int status{1}; // 1: 正常, 2: 冻结
    int64_t updated_at{0};
};

struct WalletTransactionModel {
    std::string id;
    int64_t user_id{0};
    int flow_type{1}; // 1: 充值, 2: 充电扣费, 3: 充电退补, 4: 管理员调账
    int64_t amount_cents{0};
    int64_t balance_before_cents{0};
    int64_t balance_after_cents{0};
    std::string related_order_id;
    int64_t operator_id{0};
    std::string remark;
    std::string idempotent_key;
    int64_t created_at{0};
};

struct StationModel {
    int64_t station_id{0};
    std::string station_name;
    std::string address;
    double latitude{0.0};
    double longitude{0.0};
    std::string contact_phone;
    std::string operating_hours{"00:00 - 24:00"};
    double price_per_kwh{1.45};
    double service_fee_per_kwh{0.35};
    double overtime_fee_per_15min{5.00};
    int overtime_grace_minutes{15};
    int status{1}; // 1: 运营中, 2: 维护
    int64_t created_at{0};
    int64_t updated_at{0};
};

struct PileModel {
    std::string pile_id;
    int64_t station_id{0};
    std::string pile_name;
    std::string type{"FAST"}; // FAST, SLOW
    std::string gun_type{"国标2015"};
    double max_power_kw{120.0};
    std::string voltage_range{"200V-750V"};
    std::string status{"IDLE"}; // IDLE, CHARGING, FAULT, MAINTENANCE, OFFLINE
    int64_t total_charge_count{0};
    double total_charge_hours{0.0};
    int64_t last_heartbeat_at{0};
    int64_t created_at{0};
    int64_t updated_at{0};
};

struct OrderModel {
    std::string order_id;
    int64_t user_id{0};
    int64_t station_id{0};
    std::string pile_id;
    std::string strategy_type{"FULL"};
    double strategy_value{0.0};
    std::string order_status{"CHARGING"}; // CHARGING, UNSETTLED, COMPLETED, REFUNDED, CANCELLED
    int64_t start_time{0};
    int64_t end_time{0};
    int start_soc{0};
    int end_soc{0};
    double charged_energy_kwh{0.0};
    double electricity_price{1.45};
    int64_t electricity_fee_cents{0};
    double service_price{0.35};
    int64_t service_fee_cents{0};
    int overtime_grace_minutes{15};
    int overtime_duration_minutes{0};
    double overtime_rate_per_15min{5.00};
    int64_t overtime_fee_cents{0};
    int64_t total_fee_cents{0};
    std::string stop_reason;
    int64_t settled_at{0};
    std::string refund_transaction_id;
    int64_t operator_id{0};
    std::string refund_reason;
    int64_t refunded_at{0};
    int64_t created_at{0};
    int64_t updated_at{0};
};

// ==========================================
// 接口请求 / 响应 DTO
// ==========================================

// 认证与用户 DTO
struct LoginRequest {
    std::string phone;
    std::string auth_type{"passwordless"};
    std::string password;
    std::string account; // 管理员登录兼容
};

struct RegisterRequest {
    std::string phone;
    std::string password;
    std::string nickname;
};

struct PasswordLoginRequest {
    std::string phone;
    std::string password;
};

struct ChangePasswordRequest {
    std::string old_password;
    std::string new_password;
    std::string phone; // 兼容免鉴权带手机号
};

struct RefreshTokenRequest {
    std::string refresh_token;
};

struct UpdateProfileRequest {
    std::string nickname;
    std::string avatar_url;
};

struct UploadAvatarRequest {
    std::string image_base64;
    std::string file_type{"png"};
};

struct AuthResponseData {
    int64_t user_id{0};
    int64_t admin_id{0};
    std::string username;
    std::string phone;
    std::string nickname;
    std::string avatar_url;
    double balance{0.0};
    int64_t balance_cents{0};
    bool is_new_user{false};
    std::string access_token;
    std::string refresh_token;
    std::string role{"user"};
    int expires_in{7200};
};

struct UserProfileResponseData {
    int64_t user_id{0};
    std::string phone;
    std::string nickname;
    std::string avatar_url;
    double balance{0.0};
    int64_t balance_cents{0};
    double frozen_amount{0.0};
    int64_t frozen_cents{0};
    int status{1};
    std::string status_desc{"NORMAL"};
    bool has_active_order{false};
    int64_t created_at{0};
};

// 钱包 DTO
struct RechargeRequest {
    double amount{0.0};
    int64_t amount_cents{0};
    std::string payment_method{"MOCK_PAY"};
    std::string remark{"客户端钱包充值"};
};

struct WalletBalanceResponseData {
    int64_t user_id{0};
    double balance{0.0};
    int64_t balance_cents{0};
    double frozen_amount{0.0};
    int64_t frozen_cents{0};
    double available_amount{0.0};
    std::string currency{"CNY"};
};

struct TransactionItemDTO {
    std::string transaction_id;
    int flow_type{1};
    std::string flow_type_desc;
    double amount{0.0};
    int64_t amount_cents{0};
    double balance_before{0.0};
    double balance_after{0.0};
    std::string related_order_id;
    std::string remark;
    int64_t created_at{0};
};

struct TransactionListResponseData {
    int64_t total{0};
    int page{1};
    int page_size{20};
    std::vector<TransactionItemDTO> records;
};

// 充电站与枪位 DTO
struct StationNearbyCardDTO {
    int64_t station_id{0};
    int64_t id{0}; // 紧凑整数唯一ID别名
    std::string station_name;
    std::string district; // 北京市16个行政区真实名称 (如: "朝阳区")
    uint8_t district_code{0}; // 0 ~ 15 编号
    std::string address;
    double latitude{0.0};
    double longitude{0.0};
    double distance_km{0.0};
    double price_per_kwh{1.45};
    double service_fee_per_kwh{0.35};
    double overtime_fee_per_15min{5.00};
    int total_piles{0};
    int pile_count{0}; // 充电桩总数别名
    int idle_piles{0};
    int available_count{0}; // 可用充电桩数量别名
    int fast_piles_idle{0};
    int slow_piles_idle{0};
    bool has_fast_pile{false}; // 是否拥有直流快充桩
    int station_status{1}; // 1: 运营中/在线, 2: 维护/已下线
    bool is_online{true};  // 在线状态标记
};

struct StationNearbyListResponseData {
    int64_t total{0};
    std::vector<StationNearbyCardDTO> stations;
};

struct StationDistrictListResponseData {
    int64_t total{0};
    int page{1};
    int page_size{20};
    std::string district;
    int district_code{0};
    std::vector<StationNearbyCardDTO> stations;
};

struct StationOnlineStatusResponseData {
    int64_t station_id{0};
    int status{1}; // 1: 在线, 2: 下线
    bool is_online{true};
    int terminated_orders{0};
    std::string message;
};

struct PileDetailDTO {
    std::string pile_id;
    std::string pile_name;
    std::string type{"FAST"};
    std::string type_desc{"直流快充"};
    std::string gun_type{"国标2015"};
    double max_power_kw{120.0};
    std::string voltage_range{"200V-750V"};
    std::string status{"IDLE"};
    int status_code{1};
    std::string status_desc{"空闲中"};
};

struct StationDetailResponseData {
    int64_t station_id{0};
    std::string station_name;
    std::string address;
    double latitude{0.0};
    double longitude{0.0};
    std::string contact_phone;
    std::string operating_hours{"00:00 - 24:00"};
    double price_per_kwh{1.45};
    double service_fee_per_kwh{0.35};
    double overtime_fee_per_15min{5.00};
    int overtime_grace_minutes{15};
    int total_piles{0};
    int idle_piles{0};
    std::vector<PileDetailDTO> piles;
};

// 充电核心流程 DTO
struct ActiveOrderDTO {
    std::string order_id;
    int64_t station_id{0};
    std::string station_name;
    std::string pile_id;
    std::string order_status{"CHARGING"};
    int64_t start_time{0};
    double charged_energy_kwh{0.0};
    double current_cost{0.0};
    int soc{0};
};

struct ActiveOrderCheckResponseData {
    bool has_active_order{false};
    std::optional<ActiveOrderDTO> active_order;
};

struct StartChargingRequest {
    std::string pile_id;
    std::string strategy_type{"FULL"};
    double strategy_value{0.0};
    double pre_freeze_amount{50.0};
    int64_t station_id{0};
};

struct StartChargingResponseData {
    std::string order_id;
    std::string pile_id;
    int64_t station_id{0};
    std::string station_name;
    std::string order_status{"CHARGING"};
    int64_t start_time{0};
    int initial_soc{20};
    double unit_price{1.45};
    double service_price{0.35};
    double overtime_fee_per_15min{5.00};
    std::string ws_telemetry_url;
};

struct StopChargingRequest {
    std::string order_id;
    std::string stop_reason{"USER_MANUAL_STOP"};
};

struct StopChargingResponseData {
    std::string order_id;
    std::string order_status{"UNSETTLED"};
    int64_t end_time{0};
    int64_t duration_seconds{0};
    double charged_energy_kwh{0.0};
    int final_soc{100};
    double electricity_fee{0.0};
    double service_fee{0.0};
    int overtime_minutes{0};
    double overtime_fee{0.0};
    double total_amount{0.0};
    int64_t total_amount_cents{0};
    bool need_settle{true};
};

struct SettleOrderRequest {
    std::string order_id;
    std::string idempotent_key;
};

struct SettleOrderResponseData {
    std::string order_id;
    std::string order_status{"COMPLETED"};
    double electricity_fee{0.0};
    double service_fee{0.0};
    double overtime_fee{0.0};
    double total_fee{0.0};
    int64_t total_fee_cents{0};
    double wallet_deducted{0.0};
    double new_balance{0.0};
    int64_t new_balance_cents{0};
    int64_t settled_at{0};
};

struct OrderItemDTO {
    std::string order_id;
    int64_t station_id{0};
    std::string station_name;
    std::string pile_id;
    std::string pile_type{"FAST"};
    std::string order_status{"COMPLETED"};
    int64_t start_time{0};
    int64_t end_time{0};
    int duration_minutes{0};
    double charged_energy_kwh{0.0};
    double electricity_fee{0.0};
    double service_fee{0.0};
    int overtime_minutes{0};
    double overtime_fee{0.0};
    double total_fee{0.0};
    int64_t total_fee_cents{0};
    int64_t settled_at{0};
};

struct OrderListResponseData {
    int64_t total{0};
    int page{1};
    int page_size{10};
    std::string sort_order{"desc"};
    std::vector<OrderItemDTO> orders;
};

struct RefundDetailDTO {
    std::string refund_transaction_id;
    double refund_amount{0.0};
    int64_t operator_id{0};
    std::string reason;
    int64_t refunded_at{0};
};

struct OrderDetailResponseData {
    std::string order_id;
    int64_t user_id{0};
    std::string user_phone;
    int64_t station_id{0};
    std::string station_name;
    std::string pile_id;
    std::string pile_type{"FAST"};
    std::string order_status{"COMPLETED"};
    int64_t start_time{0};
    int64_t end_time{0};
    int64_t duration_seconds{0};
    int start_soc{0};
    int end_soc{0};
    double charged_energy_kwh{0.0};
    double electricity_price{1.45};
    double electricity_fee{0.0};
    double service_price{0.35};
    double service_fee{0.0};
    int overtime_grace_minutes{15};
    int overtime_duration_minutes{0};
    double overtime_rate_per_15min{5.00};
    double overtime_fee{0.0};
    double total_amount{0.0};
    int64_t total_amount_cents{0};
    std::string stop_reason;
    int64_t settled_at{0};
    std::optional<RefundDetailDTO> refund_info;
};

// ==========================================
// 管理端专属 DTO
// ==========================================

struct AdminDashboardSummaryData {
    double today_revenue{0.0};
    int64_t today_revenue_cents{0};
    double month_revenue{0.0};
    int64_t month_revenue_cents{0};
    double total_revenue{0.0};
    int64_t total_revenue_cents{0};
    double today_energy_kwh{0.0};
    int64_t today_order_count{0};
    int64_t total_user_count{0};
    int64_t active_charging_sessions{0};
};

struct AdminRevenueTrendData {
    std::string time_range{"LAST_7_DAYS"};
    std::vector<std::string> dates;
    std::vector<double> revenue_series;
    std::vector<double> energy_kwh_series;
    std::vector<int64_t> order_count_series;
};

struct AdminPileStatusOverviewData {
    int total_piles{0};
    int in_use_count{0};
    double in_use_percentage{0.0};
    int idle_count{0};
    double idle_percentage{0.0};
    int fault_count{0};
    double fault_percentage{0.0};
    double online_rate{0.0};
};

// 单站销售统计 DTO
struct StationSalesSummaryDTO {
    double total_revenue{0.0};
    int64_t total_revenue_cents{0};
    double electricity_fee_total{0.0};
    double service_fee_total{0.0};
    double overtime_fee_total{0.0};
    double total_energy_kwh{0.0};
    int64_t total_order_count{0};
    double average_order_amount{0.0};
};

struct StationSalesTimelineDTO {
    std::vector<std::string> time_slots;
    std::vector<double> revenue_series;
    std::vector<double> energy_series;
    std::vector<int64_t> order_series;
};

struct StationSalesStatsResponseData {
    int64_t station_id{0};
    std::string station_name;
    std::string time_range{"today"};
    StationSalesSummaryDTO summary;
    StationSalesTimelineDTO timeline;
};

struct StationAdminItemDTO {
    int64_t station_id{0};
    std::string station_name;
    std::string address;
    double latitude{0.0};
    double longitude{0.0};
    int total_piles{0};
    int online_piles{0};
    int idle_piles{0};
    double online_rate{0.0};
    double price_per_kwh{1.45};
    double service_fee_per_kwh{0.35};
    double overtime_fee_per_15min{5.00};
    int status{1};
    int64_t created_at{0};
};

struct StationAdminListResponseData {
    int64_t total{0};
    int page{1};
    int page_size{10};
    std::vector<StationAdminItemDTO> stations;
};

struct CreateStationRequest {
    std::string station_name;
    std::string address;
    double latitude{0.0};
    double longitude{0.0};
    double price_per_kwh{1.45};
    double service_fee_per_kwh{0.35};
    double overtime_fee_per_15min{5.00};
    std::string contact_phone;
};

struct UpdateStationRequest {
    std::string station_name;
    double price_per_kwh{1.45};
    double service_fee_per_kwh{0.35};
    double overtime_fee_per_15min{5.00};
    int status{1};
};

struct PileAdminItemDTO {
    std::string pile_id;
    int64_t station_id{0};
    std::string station_name;
    std::string pile_name;
    std::string type{"FAST"};
    double power_kw{120.0};
    std::string current_status{"IDLE"};
    int current_status_code{1};
    int64_t total_charge_count{0};
    double total_charge_hours{0.0};
    int64_t last_heartbeat_at{0};
};

struct PileAdminListResponseData {
    int64_t total{0};
    int page{1};
    int page_size{10};
    std::vector<PileAdminItemDTO> piles;
};

struct CreatePileRequest {
    std::string pile_id;
    int64_t station_id{0};
    std::string pile_name;
    std::string type{"FAST"};
    double power_kw{120.0};
    double max_power_kw{120.0};
    std::string gun_type{"国标2015"};
    std::string voltage_range{"200V-750V"};
};

struct PileRestartRequest {
    std::string reason{"管理员远程重启"};
};

struct PileRestartResponseData {
    std::string pile_id;
    std::string command{"REBOOT"};
    std::string execution_status{"SUCCESS"};
    std::string new_status{"IDLE"};
    std::string message;
};

struct PileStatusChangeRequest {
    std::string target_status{"MAINTENANCE"};
    std::string reason{"桩位定期维护"};
};

struct PileStatusChangeResponseData {
    std::string pile_id;
    std::string previous_status;
    std::string current_status;
};

struct UserAdminItemDTO {
    int64_t user_id{0};
    std::string phone;
    std::string nickname;
    double balance{0.0};
    int64_t balance_cents{0};
    int status{1};
    std::string status_desc{"NORMAL"};
    int64_t created_at{0};
};

struct UserAdminListResponseData {
    int64_t total{0};
    int page{1};
    int page_size{10};
    std::vector<UserAdminItemDTO> users;
};

struct UserStatusChangeRequest {
    int status{1}; // 1: 正常, 2: 冻结
    std::string reason{"管理员风控处置"};
};

struct UserStatusChangeResponseData {
    int64_t user_id{0};
    int status{1};
    std::string status_desc{"NORMAL"};
    int64_t operator_id{0};
    int64_t updated_at{0};
};

struct UserWalletAdjustRequest {
    double amount{0.0};
    int64_t amount_cents{0};
    std::string remark{"管理员调账补偿"};
};

struct UserWalletAdjustResponseData {
    std::string transaction_id;
    int64_t user_id{0};
    double adjust_amount{0.0};
    double balance_before{0.0};
    double balance_after{0.0};
    int64_t operator_id{0};
    int64_t created_at{0};
};

struct AdminOrderRefundRequest {
    double refund_amount{0.0};
    int64_t refund_amount_cents{0};
    std::string reason{"管理员执行退款"};
};

struct AdminOrderRefundResponseData {
    std::string refund_transaction_id;
    std::string order_id;
    int64_t user_id{0};
    double refund_amount{0.0};
    int64_t refund_amount_cents{0};
    double user_balance_before{0.0};
    double user_balance_after{0.0};
    std::string order_status{"REFUNDED"};
    int64_t operator_id{0};
    int64_t refunded_at{0};
};

struct AdminUserOrdersResponseData {
    int64_t user_id{0};
    std::string phone;
    std::string nickname;
    int64_t total{0};
    int page{1};
    int page_size{10};
    std::string sort_order{"asc"};
    std::vector<OrderItemDTO> orders;
};

// ==========================================
// WebSocket 实时帧 DTO
// ==========================================

struct TelemetryUpdateData {
    int64_t timestamp{0};
    double voltage_v{0.0};
    double current_a{0.0};
    double power_kw{0.0};
    int current_soc{0};
    double charged_energy_kwh{0.0};
    double charging_fee{0.0};
    double service_fee{0.0};
    int overtime_duration_minutes{0};
    int overtime_grace_minutes{15};
    double overtime_fee{0.0};
    double current_total_fee{0.0};
    double temperature_celsius{0.0};
    int64_t elapsed_seconds{0};
    bool is_full{false};
    int64_t full_timestamp{0};
    std::string warning_message;
};

struct TelemetryFrame {
    std::string event{"TELEMETRY_UPDATE"};
    std::string order_id;
    std::string pile_id;
    TelemetryUpdateData data;
};

struct ChargingFinishedFrame {
    std::string event{"CHARGING_FINISHED"};
    std::string order_id;
    std::string pile_id;
    std::string finish_reason{"USER_UNPLUGGED"};
    double total_energy_kwh{0.0};
    double electricity_fee{0.0};
    double service_fee{0.0};
    double overtime_fee{0.0};
    double total_amount{0.0};
    int64_t timestamp{0};
};

struct StationNavPileStatusDTO {
    std::string pile_id;
    std::string type{"FAST"};
    std::string status{"IDLE"};
    double power_kw{120.0};
    int current_soc{0};
    int est_remaining_mins{0};
};

struct StationSnapshotData {
    int total_piles{0};
    int idle_piles{0};
    int fast_idle_piles{0};
    int slow_idle_piles{0};
    int busy_piles{0};
    int fault_piles{0};
    int queueing_cars{0};
    int estimated_wait_minutes{0};
    std::vector<StationNavPileStatusDTO> piles;
};

struct StationSnapshotFrame {
    std::string event{"STATION_SNAPSHOT"};
    int64_t station_id{0};
    std::string station_name;
    StationSnapshotData data;
    int64_t timestamp{0};
};

struct StationDynamicUpdateData {
    std::string pile_id;
    std::string change_type; // PILE_OCCUPIED, PILE_RELEASED, PILE_FAULT
    std::string old_status;
    std::string new_status;
    int idle_piles_remaining{0};
    int queueing_cars{0};
    std::string alert_level{"NORMAL"};
    std::string message;
};

struct StationDynamicUpdateFrame {
    std::string event{"STATION_DYNAMIC_UPDATE"};
    int64_t station_id{0};
    StationDynamicUpdateData data;
    int64_t timestamp{0};
};

struct PileStatusChangedBroadcastFrame {
    std::string event{"PILE_STATUS_CHANGED"};
    int64_t station_id{0};
    std::string pile_id;
    std::string old_status;
    std::string new_status;
    int new_status_code{1};
    int64_t timestamp{0};
};

struct DeviceFaultAlarmBroadcastFrame {
    std::string event{"DEVICE_FAULT_ALARM"};
    int64_t station_id{0};
    std::string pile_id;
    std::string fault_code;
    std::string fault_message;
    int64_t timestamp{0};
};

} // namespace ev
