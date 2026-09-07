#ifndef CHARGESERVICE_H
#define CHARGESERVICE_H

#include <QList>
#include <QString>
#include <functional>

// 充电业务逻辑（云端版）—— 数据全部来自 server API（见 端口设计文档 2.x）
// UI 层只认识这里的函数和结构体，完全不知道数据是从网络来的
// 结构体字段和本地 SQLite 版保持一致，界面代码一行不用改
namespace ChargeService {

struct StationOpt {
    int     id = 0;
    QString name;
    double  price = 0;       // 元/度（price_per_kwh，服务费另计）
    int     freeCount = 0;   // idle_piles
    int     totalCount = 0;
    QString address;
    double latitude = 0, longitude = 0, distanceKm = 0;
};

struct PileOpt {             // 某站的一个空闲桩
    QString id;              // 云端桩号是字符串，如 P00101
    QString code;            // pile_name，如 01号直流快充桩
    QString typeText;        // 快充/慢充（由 type=FAST/SLOW 翻译）
    double  powerKw = 0;     // power_kw
    int     statusCode = 0;
    QString statusText;
    int     totalChargeCount = 0;
};

struct ActiveOrder {         // 当前用户的未完成订单（active-order 接口）
    QString id;              // 云端订单号是字符串，如 ORD_20260902_1001
    QString pileCode;        // pile_id
    QString stationName;
    QString startTime;       // 已从毫秒时间戳转成 "yyyy-MM-dd HH:mm:ss"
    int     status = 0;      // 0=充电中 1=待结算（由 order_status 映射）
    double  kwh = 0;         // charged_energy_kwh
    double  amount = 0;      // current_cost（server 实时算好，含服务费）
    int     soc = 0;         // 电池电量 %（server 模拟，本版界面不展示）
};

struct Receipt {             // 订单小票（费用按 server 的三段式）
    QString id;
    QString stationName;
    QString pileCode;
    QString startTime;
    QString endTime;
    int     durationSec = 0;     // 充电秒数（短订单也能显示 xx 秒）
    double  kwh = 0;
    double  electricityFee = 0;  // 电费
    double  serviceFee = 0;      // 服务费
    double  overtimeFee = 0;     // 超时占位费
    double  totalFee = 0;
    QString statusText;
};

struct OrderRow {            // 「我的订单」一行（GET /orders/my）
    QString id;
    QString stationName;
    QString pileCode;
    QString typeText;
    QString startTime;
    int     durationSec = 0;
    double  kwh = 0;
    double  amount = 0;      // total_fee
    QString statusText;
    int     status = 2;      // 0=充电中 1=待结算 2=已结算
};

// 免密登录拿 token（正式登录页是 B 的，这里先直接用手机号登，NCS_PHONE 可换号）
bool devLogin(QString *err);

bool               findUnfinished(int userId, ActiveOrder *out);   // userId 仅为兼容界面签名
QList<StationOpt>  stationOptions();
void stationOptionsAsync(std::function<void(const QList<StationOpt> &, const QString &)> callback);
void stationOptionsAtAsync(double latitude, double longitude, std::function<void(const QList<StationOpt> &, const QString &)> callback);
void setUserLocation(double latitude, double longitude);
void userLocation(double &latitude, double &longitude);
QList<PileOpt>     freePiles(int stationId);

// —— 预约充电（9/6 起走 server 真接口） ——————————————————
// server 规则：押金 20 元预约时真扣；到站开充全额退回；主动取消扣 5 退 15；
// 超时 120 秒 server 自动作废（押金不退）；被预约的桩全网显示 已预约锁定(8)
struct Reservation {
    QString pileId;          // 云端桩号，如 P00101
    QString pileCode;        // pile_name
    QString stationName;
    int     stationId = 0;
    double  powerKw = 0;     // active-reservation 不带功率，需要时从 /piles 补
    QString typeText;
    double  deposit = 0;     // 押金（元）
    qint64  expiresAtMs = 0; // 过期时刻（epoch 毫秒，server 下发）
};

double reserveDeposit();     // 20 元（界面文案用，钱由 server 扣）
double cancelPenalty();      // 取消违约金 5 元（界面文案用）
double walletBalance(QString *err = nullptr);   // 钱包余额（元），失败返回 -1

bool reservePile(int stationId, const PileOpt &pile, const QString &stationName,
                 QString *err);                 // POST /charging/reserve
bool activeReservation(Reservation *out);       // GET  /charging/active-reservation
bool cancelReservation(double *forfeit = nullptr, double *refund = nullptr);
                                                // POST /charging/cancel-reservation

// 充电花费上限 = 钱包余额 - 1 元（留 1 元缓冲吸收 2 秒轮询间隔）
// 充电中每次从 server 拉到 current_cost >= 上限就自动结束充电，余额绝不为负
bool reservePile(int stationId, const PileOpt &pile, const QString &stationName,
                 QString *err);
bool activeReservation(Reservation *out);   // 有未过期的预约吗
void clearReservation();                    // 到站开充 / 取消 / 超时后调用

// 开始充电（UC-U-07）：POST /charging/start，预冻结 20 元
// 余额不足/桩被占等错误信息由 server 返回（已翻中文），直接写进 *err
bool startCharge(const QString &pileId, QString *err, QString *orderId = nullptr);
// 结束充电（UC-U-08 收尾）：POST /charging/stop → 订单变待结算
bool stopCharge(const QString &orderId, QString *err);

// 结算（UC-U-09）：POST /charging/settle → 扣款；返回扣了多少、剩多少
bool settle(const QString &orderId, double *deducted, double *newBalance, QString *err);
// 订单小票（UC-U-09/10）：详情接口拿不到时自动去 /orders/my 里找（server 部署问题的兜底）
bool orderDetail(const QString &orderId, Receipt *out);
QList<OrderRow> orderHistory(int limit = 50);

} // namespace ChargeService

#endif // CHARGESERVICE_H
