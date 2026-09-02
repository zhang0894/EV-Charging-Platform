#pragma once

#include <cstdint>
#include <expected>
#include <string_view>

namespace ev {

enum class AppError : int32_t {
    Success = 0,

    // 10xxx: 用户与认证模块错误
    UserNotFound = 10001,
    UserAccountFrozen = 10002,
    InvalidPhoneFormat = 10003,
    InvalidCredentials = 10004,
    UserAlreadyExists = 10005,

    // 20xxx: 电站与充电桩错误
    StationNotFound = 20001,
    ChargingPileNotFound = 20002,
    PileBusyOrReserved = 20003,
    PileInFaultState = 20004,
    ActiveOrderExists = 20005,
    NoActiveOrderFound = 20006,
    OrderNotFound = 20007,
    OrderCannotBeStopped = 20008,
    OrderCannotBeSettled = 20009,

    // 30xxx: 资金与交易错误
    InsufficientBalance = 30001,
    DuplicateTransactionKey = 30002,
    InvalidAmount = 30003,
    OrderAlreadyRefunded = 30004,
    InvalidRefundAmount = 30005,

    // 40xxx: 鉴权与权限
    Unauthorized = 40001,
    TokenExpired = 40002,
    PermissionDenied = 40003,
    InvalidAuthHeader = 40004,

    // 50xxx: 服务端与系统错误
    DatabaseError = 50001,
    HardwareTimeout = 50002,
    InternalError = 50003,
    InvalidJsonPayload = 50004,
    RouteNotFound = 50005,
    InvalidParameters = 50006
};

inline std::string_view error_message(AppError err) noexcept {
    switch (err) {
        case AppError::Success: return "success";
        case AppError::UserNotFound: return "User not found";
        case AppError::UserAccountFrozen: return "User account frozen, operations restricted";
        case AppError::InvalidPhoneFormat: return "Invalid phone format: must be 11 digits";
        case AppError::InvalidCredentials: return "Invalid account or password";
        case AppError::UserAlreadyExists: return "User phone already registered";
        case AppError::StationNotFound: return "Charging station not found";
        case AppError::ChargingPileNotFound: return "Charging pile not found";
        case AppError::PileBusyOrReserved: return "Charging pile is busy or reserved";
        case AppError::PileInFaultState: return "Charging pile in fault/maintenance state";
        case AppError::ActiveOrderExists: return "Active charging order exists, please settle first";
        case AppError::NoActiveOrderFound: return "No active charging order found";
        case AppError::OrderNotFound: return "Charging order not found";
        case AppError::OrderCannotBeStopped: return "Order status cannot be stopped";
        case AppError::OrderCannotBeSettled: return "Order is not in unsettled status";
        case AppError::InsufficientBalance: return "Insufficient wallet balance, please recharge";
        case AppError::DuplicateTransactionKey: return "Duplicate transaction key (Idempotency conflict)";
        case AppError::InvalidAmount: return "Amount must be strictly positive";
        case AppError::OrderAlreadyRefunded: return "Order has already been refunded";
        case AppError::InvalidRefundAmount: return "Refund amount exceeds actual paid amount";
        case AppError::Unauthorized: return "Unauthorized: missing or invalid authentication token";
        case AppError::TokenExpired: return "Authentication token expired";
        case AppError::PermissionDenied: return "Permission denied: access forbidden";
        case AppError::InvalidAuthHeader: return "Invalid Authorization header format";
        case AppError::DatabaseError: return "Database query or transaction execution failed";
        case AppError::HardwareTimeout: return "Hardware communication timeout";
        case AppError::InternalError: return "Internal server error";
        case AppError::InvalidJsonPayload: return "Invalid JSON payload in request body";
        case AppError::RouteNotFound: return "Endpoint route not found";
        case AppError::InvalidParameters: return "Invalid request parameters";
    }
    return "Unknown application error";
}

inline unsigned int http_status_for_error(AppError err) noexcept {
    switch (err) {
        case AppError::Success: return 200;
        case AppError::InvalidPhoneFormat:
        case AppError::InvalidAmount:
        case AppError::InvalidJsonPayload:
        case AppError::InvalidParameters: return 400;
        case AppError::Unauthorized:
        case AppError::TokenExpired:
        case AppError::InvalidAuthHeader:
        case AppError::InvalidCredentials: return 401;
        case AppError::UserAccountFrozen:
        case AppError::PermissionDenied: return 403;
        case AppError::UserNotFound:
        case AppError::StationNotFound:
        case AppError::ChargingPileNotFound:
        case AppError::OrderNotFound:
        case AppError::NoActiveOrderFound:
        case AppError::RouteNotFound: return 404;
        case AppError::UserAlreadyExists:
        case AppError::PileBusyOrReserved:
        case AppError::ActiveOrderExists:
        case AppError::DuplicateTransactionKey:
        case AppError::OrderAlreadyRefunded: return 409;
        case AppError::PileInFaultState:
        case AppError::InsufficientBalance:
        case AppError::InvalidRefundAmount:
        case AppError::OrderCannotBeStopped:
        case AppError::OrderCannotBeSettled: return 422;
        case AppError::DatabaseError:
        case AppError::HardwareTimeout:
        case AppError::InternalError: return 500;
    }
    return 500;
}

template <typename T>
using Result = std::expected<T, AppError>;

} // namespace ev
