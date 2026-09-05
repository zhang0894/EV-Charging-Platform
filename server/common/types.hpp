#pragma once

#include <chrono>
#include <cstdint>
#include <string>
#include <string_view>

namespace ev {

// 充电桩运行状态
enum class PileStatus : uint8_t {
    IDLE = 1,         // 空闲可用
    PREPARING = 2,    // 准备中 / 正在插枪
    CHARGING = 3,     // 充电中
    FINISHING = 4,    // 充电完成 / 待结算拔枪
    FAULT = 5,        // 设备故障
    MAINTENANCE = 6,  // 维护锁定中
    OFFLINE = 7,      // 设备离线
    RESERVED = 8      // 已预约锁定
};

inline std::string_view to_string(PileStatus status) {
    switch (status) {
        case PileStatus::IDLE: return "IDLE";
        case PileStatus::PREPARING: return "PREPARING";
        case PileStatus::CHARGING: return "CHARGING";
        case PileStatus::FINISHING: return "FINISHING";
        case PileStatus::FAULT: return "FAULT";
        case PileStatus::MAINTENANCE: return "MAINTENANCE";
        case PileStatus::OFFLINE: return "OFFLINE";
        case PileStatus::RESERVED: return "RESERVED";
    }
    return "UNKNOWN";
}

// 充电桩类型
enum class PileType : uint8_t {
    FAST = 1,  // 直流快充
    SLOW = 2   // 交流慢充
};

inline std::string_view to_string(PileType type) {
    switch (type) {
        case PileType::FAST: return "FAST";
        case PileType::SLOW: return "SLOW";
    }
    return "UNKNOWN";
}

// 充电订单状态
enum class OrderStatus : uint8_t {
    CHARGING = 1,   // 进行中
    UNSETTLED = 2,  // 充电已停止，待结算扣费
    COMPLETED = 3,  // 已完成已扣费
    REFUNDED = 4,   // 已退款
    CANCELLED = 5   // 已取消
};

inline std::string_view to_string(OrderStatus status) {
    switch (status) {
        case OrderStatus::CHARGING: return "CHARGING";
        case OrderStatus::UNSETTLED: return "UNSETTLED";
        case OrderStatus::COMPLETED: return "COMPLETED";
        case OrderStatus::REFUNDED: return "REFUNDED";
        case OrderStatus::CANCELLED: return "CANCELLED";
    }
    return "UNKNOWN";
}

// 资金流水类型
enum class FlowType : uint8_t {
    RECHARGE = 1,            // 钱包充值
    CHARGE_DEDUCTION = 2,    // 充电扣费
    REFUND = 3,              // 充电退补 / 退款
    ADMIN_ADJUST = 4,        // 管理员调账补偿
    RESERVATION_DEPOSIT = 5, // 预约押金扣除
    RESERVATION_REFUND = 6   // 预约押金退还
};

inline std::string_view to_string(FlowType type) {
    switch (type) {
        case FlowType::RECHARGE: return "RECHARGE";
        case FlowType::CHARGE_DEDUCTION: return "CHARGE_DEDUCTION";
        case FlowType::REFUND: return "REFUND";
        case FlowType::ADMIN_ADJUST: return "ADMIN_ADJUST";
        case FlowType::RESERVATION_DEPOSIT: return "RESERVATION_DEPOSIT";
        case FlowType::RESERVATION_REFUND: return "RESERVATION_REFUND";
    }
    return "UNKNOWN";
}

// 预约状态
enum class ReservationStatus : uint8_t {
    ACTIVE = 1,    // 预约生效中
    FULFILLED = 2, // 已履约到场开枪
    CANCELLED = 3, // 用户主动取消
    TIMEOUT = 4    // 超时自动释放
};

inline std::string_view to_string(ReservationStatus status) {
    switch (status) {
        case ReservationStatus::ACTIVE: return "ACTIVE";
        case ReservationStatus::FULFILLED: return "FULFILLED";
        case ReservationStatus::CANCELLED: return "CANCELLED";
        case ReservationStatus::TIMEOUT: return "TIMEOUT";
    }
    return "UNKNOWN";
}

// 用户角色权限
enum class UserRole : uint8_t {
    USER = 1,
    ADMIN = 2
};

inline std::string_view to_string(UserRole role) {
    switch (role) {
        case UserRole::USER: return "user";
        case UserRole::ADMIN: return "admin";
    }
    return "user";
}

// 充电启动策略
enum class StrategyType : uint8_t {
    FULL = 1,    // 充满自停
    MONEY = 2,   // 按指定金额充电
    ENERGY = 3,  // 按指定度数充电
    TIME = 4     // 按指定分钟数充电
};

// 工具函数：获取当前毫秒级 Unix 时间戳
inline int64_t current_time_ms() noexcept {
    return std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()
    ).count();
}

// 金额单位转换：分 -> 元
inline double cents_to_yuan(int64_t cents) noexcept {
    return static_cast<double>(cents) / 100.0;
}

// 金额单位转换：元 -> 分 (四舍五入)
inline int64_t yuan_to_cents(double yuan) noexcept {
    return static_cast<int64_t>(yuan * 100.0 + (yuan >= 0.0 ? 0.5 : -0.5));
}

} // namespace ev
