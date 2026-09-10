#include "core/chargeservice.h"

#include "core/apiclient.h"
#include "core/session.h"


#include <QDateTime>
#include <QJsonArray>
#include <QJsonObject>
#include <QRegularExpression>
#include <QtMath>
#include <QUuid>

namespace ChargeService {
namespace {
// 模拟定位：NCS_LAT/NCS_LNG 可覆盖（server 种子坐标会变），默认北京
double g_lat = [] { bool ok; const double v = qEnvironmentVariable("NCS_LAT").toDouble(&ok); return ok ? v : 40.0; }();
double g_lng = [] { bool ok; const double v = qEnvironmentVariable("NCS_LNG").toDouble(&ok); return ok ? v : 116.35; }();
QList<StationOpt> g_stations;

double distanceInKm(double fromLat, double fromLng, double toLat, double toLng)
{
    constexpr double EarthRadiusKm = 6371.0;
    const double lat1 = qDegreesToRadians(fromLat), lat2 = qDegreesToRadians(toLat);
    const double dLat = lat2 - lat1, dLng = qDegreesToRadians(toLng - fromLng);
    const double a = qSin(dLat / 2) * qSin(dLat / 2)
                   + qCos(lat1) * qCos(lat2) * qSin(dLng / 2) * qSin(dLng / 2);
    return EarthRadiusKm * 2 * qAtan2(qSqrt(a), qSqrt(1 - a));
}

// 同步 / 异步两条路共用的解析，字段名只在这里出现一次
StationOpt parseStation(const QJsonObject &s, double fromLat, double fromLng)
{
    StationOpt o;
    o.id         = s.value(QStringLiteral("station_id")).toInt();
    o.name       = s.value(QStringLiteral("station_name")).toString();
    o.price      = s.value(QStringLiteral("price_per_kwh")).toDouble();
    o.freeCount  = s.value(QStringLiteral("idle_piles")).toInt();
    o.totalCount = s.value(QStringLiteral("total_piles")).toInt();
    o.address    = s.value(QStringLiteral("address")).toString();
    o.latitude   = s.value(QStringLiteral("latitude")).toDouble();
    o.longitude  = s.value(QStringLiteral("longitude")).toDouble();
    o.distanceKm = s.value(QStringLiteral("distance_km")).toDouble();
    // 电站是否在线（管理端可下线）：server 正常返回 bool，兼容字符串/数字形式
    const QJsonValue online = s.value(QStringLiteral("is_online"));
    if (online.isBool()) {
        o.isOnline = online.toBool();
    } else if (online.isString()) {
        const QString value = online.toString().trimmed().toLower();
        o.isOnline = value != QStringLiteral("false") && value != QStringLiteral("0")
                  && value != QStringLiteral("offline") && value != QStringLiteral("suspended")
                  && value != QStringLiteral("暂停营业") && value != QStringLiteral("已下线");
    } else if (online.isDouble()) {
        o.isOnline = !qFuzzyIsNull(online.toDouble());
    } else if (s.contains(QStringLiteral("station_status"))) {
        const QJsonValue status = s.value(QStringLiteral("station_status"));
        if (status.isBool()) o.isOnline = status.toBool();
        else if (status.isString()) {
            const QString value = status.toString().trimmed().toLower();
            o.isOnline = value == QStringLiteral("online") || value == QStringLiteral("active")
                      || value == QStringLiteral("运营中") || value == QStringLiteral("营业中")
                      || value == QStringLiteral("1");
        } else if (status.isDouble()) o.isOnline = !qFuzzyIsNull(status.toDouble());
    }
    if (o.latitude != 0 && o.longitude != 0)
        o.distanceKm = distanceInKm(fromLat, fromLng, o.latitude, o.longitude);
    return o;
}

// server 的 8569 站里混进了 4 条地址在外地、坐标却落在北京市中心的脏数据
// （id 1712 山东诸城 / 3065 上海松江 / 3410 天津武清 / 3418 长沙天心区）。
// 它们正好压在 IP 定位的默认点（天安门）上，谁用 IP 定位谁就先看到它们，
// 已反馈后端；修好之前客户端先按地址关键字剔除
bool looksOutsideBeijing(const StationOpt &s)
{
    static const QRegularExpression pat(QStringLiteral(
        "天津市|河北省|廊坊市|保定市|石家庄|张家口市|承德市|唐山市|山东省|山西省|"
        "上海市|长沙市|天心区|武清区|香河县|三河市|燕郊"));
    return pat.match(s.address).hasMatch() || pat.match(s.name).hasMatch();
}

PileOpt parsePile(const QJsonObject &p)
{
    PileOpt o;
    o.id         = p.value(QStringLiteral("pile_id")).toString();
    o.code       = p.value(QStringLiteral("pile_name")).toString();
    o.typeText   = p.value(QStringLiteral("type")).toString() == QStringLiteral("FAST")
                       ? QStringLiteral("快充") : QStringLiteral("慢充");
    o.powerKw    = p.value(QStringLiteral("power_kw")).toDouble();
    o.statusCode = p.value(QStringLiteral("status_code")).toInt();
    o.statusText = p.value(QStringLiteral("status_desc")).toString();
    o.totalChargeCount = p.value(QStringLiteral("total_charge_count")).toInt();
    return o;
}

// active-order 回包 → ActiveOrder；没有未完成订单返回 false
bool parseActiveOrder(const QJsonObject &data, ActiveOrder *out)
{
    if (!data.value(QStringLiteral("has_active_order")).toBool())
        return false;
    const QJsonObject o = data.value(QStringLiteral("active_order")).toObject();
    out->id          = o.value(QStringLiteral("order_id")).toString();
    out->stationName = o.value(QStringLiteral("station_name")).toString();
    out->pileCode    = o.value(QStringLiteral("pile_id")).toString();
    out->startTime   = QDateTime::fromMSecsSinceEpoch(
                           qint64(o.value(QStringLiteral("start_time")).toDouble()))
                           .toString(QStringLiteral("yyyy-MM-dd HH:mm:ss"));
    out->status      = o.value(QStringLiteral("order_status")).toString()
                               == QStringLiteral("CHARGING") ? 0 : 1;
    out->kwh         = o.value(QStringLiteral("charged_energy_kwh")).toDouble();
    out->amount      = o.value(QStringLiteral("current_cost")).toDouble();
    out->soc         = o.value(QStringLiteral("soc")).toInt();
    return true;
}
}

bool loginAs(const QString &phone, QString *err)
{
    const QJsonObject body{{QStringLiteral("phone"), phone},
                           {QStringLiteral("auth_type"), QStringLiteral("passwordless")}};
    QJsonObject data;
    if (!Api::post(QStringLiteral("/api/v1/auth/login"), body, &data, err))
        return false;
    Session::i().setUserId(data.value(QStringLiteral("user_id")).toInt());
    Session::i().setToken(data.value(QStringLiteral("access_token")).toString());
    Session::i().setRefreshToken(data.value(QStringLiteral("refresh_token")).toString());
    return true;
}

bool devLogin(QString *err)
{
    QString phone = qEnvironmentVariable("NCS_PHONE");
    if (phone.isEmpty())
        phone = QStringLiteral("13800000001");   // 默认用有余额的种子用户（新号余额 0 会被 20 元门槛拦住）
    return loginAs(phone, err);
}

bool findUnfinished(int userId, ActiveOrder *out)
{
    Q_UNUSED(userId);          // 云端靠 token 识别用户，参数只为兼容界面签名
    QJsonObject data;
    if (!Api::get(QStringLiteral("/api/v1/charging/active-order"), &data))
        return false;
    return parseActiveOrder(data, out);
}

// 充电页轮询用：不阻塞界面，秒表和按钮照常响应
void findUnfinishedAsync(std::function<void(bool ok, bool found, const ActiveOrder &,
                                            const QString &err)> callback)
{
    Api::getAsync(QStringLiteral("/api/v1/charging/active-order"),
        [callback](bool ok, const QJsonObject &data, const QString &err) {
            ActiveOrder ao;
            const bool found = ok && parseActiveOrder(data, &ao);
            callback(ok, found, ao, err);
        });
}

QList<StationOpt> stationOptions()
{
    if (!g_stations.isEmpty())
        return g_stations;
    // 用当前模拟定位坐标
    QList<StationOpt> list;
    QJsonObject data;
    // stations/inquire 带坐标 = 按距离排序（旧的 stations/nearby 已删除）
    if (!Api::get(QStringLiteral("/api/v1/stations/inquire"
                                 "?latitude=%1&longitude=%2&page=1&page_size=20")
                     .arg(g_lat, 0, 'f', 6).arg(g_lng, 0, 'f', 6), &data))
        return list;
    for (const auto &v : data.value(QStringLiteral("stations")).toArray()) {
        const StationOpt st = parseStation(v.toObject(), g_lat, g_lng);
        if (!looksOutsideBeijing(st))
            list << st;
    }
    g_stations = list;
    return list;
}

void stationOptionsAsync(std::function<void(const QList<StationOpt> &, const QString &)> callback)
{
    stationOptionsAtAsync(g_lat, g_lng, callback);
}
void stationOptionsAtAsync(double latitude, double longitude, std::function<void(const QList<StationOpt> &, const QString &)> callback,
                           int page, int pageSize)
{
    const QString path=QStringLiteral("/api/v1/stations/inquire?latitude=%1&longitude=%2&page=%3&page_size=%4")
                           .arg(latitude,0,'f',6).arg(longitude,0,'f',6).arg(page).arg(pageSize);
    Api::getAsync(path,
        [callback, latitude, longitude](bool ok, const QJsonObject &data, const QString &error) {
            QList<StationOpt> list;
            if (ok) for (const auto &v : data.value(QStringLiteral("stations")).toArray()) {
                const StationOpt st = parseStation(v.toObject(), latitude, longitude);
                if (!looksOutsideBeijing(st))
                    list << st;
            }
            if (ok) g_stations = list;
            callback(list, error);
        });
}
void setUserLocation(double latitude,double longitude){if(qAbs(latitude)<=90&&qAbs(longitude)<=180){g_lat=latitude;g_lng=longitude;}}
void userLocation(double &latitude,double &longitude){latitude=g_lat;longitude=g_lng;}

QList<PileOpt> freePiles(int stationId)
{
    // 统一桩接口 /piles?station_id=（stations/{id} 不再带桩列表）
    // 字段：power_kw、type=FAST/SLOW、status_code 1~8（8 = 已预约锁定）
    QList<PileOpt> list;
    QJsonObject data;
    if (!Api::get(QStringLiteral("/api/v1/piles?station_id=%1&page=1&page_size=30")
                      .arg(stationId), &data))
        return list;
    for (const auto &v : data.value(QStringLiteral("piles")).toArray())
        list << parsePile(v.toObject());
    return list;
}

// 选桩页 5 秒一刷用：异步拉，界面不卡
void freePilesAsync(int stationId,
                    std::function<void(const QList<PileOpt> &, const QString &)> callback)
{
    Api::getAsync(QStringLiteral("/api/v1/piles?station_id=%1&page=1&page_size=30")
                      .arg(stationId),
        [callback](bool ok, const QJsonObject &data, const QString &err) {
            QList<PileOpt> list;
            if (ok) for (const auto &v : data.value(QStringLiteral("piles")).toArray())
                list << parsePile(v.toObject());
            callback(list, ok ? QString() : err);
        });
}

// ================= 预约充电 =================
// server 规则：押金 20 元真扣；到站开充全额退回；主动取消扣 5 退 15；
// 超时 120 秒由 server 每秒清扫（押金不退），桩对所有账号显示 已预约锁定(8)

double reserveDeposit() { return 20.0; }   // 与 server 一致，仅用于界面文案
double cancelPenalty()  { return 5.0; }

double walletBalance(QString *err)
{
    QJsonObject data;
    if (!Api::get(QStringLiteral("/api/v1/wallet/balance"), &data, err))
        return -1;
    return data.contains(QStringLiteral("balance_cents"))
               ? data.value(QStringLiteral("balance_cents")).toDouble() / 100.0
               : data.value(QStringLiteral("balance")).toDouble();
}

bool reservePile(int stationId, const PileOpt &pile, const QString &stationName,
                 QString *err)
{
    Q_UNUSED(stationId);
    Q_UNUSED(stationName);      // server 自己知道桩属于哪个站
    const QJsonObject body{{QStringLiteral("pile_id"), pile.id}};
    return Api::post(QStringLiteral("/api/v1/charging/reserve"), body, nullptr, err);
}

bool activeReservation(Reservation *out)
{
    QJsonObject data;
    if (!Api::get(QStringLiteral("/api/v1/charging/active-reservation"), &data))
        return false;
    if (!data.value(QStringLiteral("has_active_reservation")).toBool())
        return false;
    const QJsonObject r = data.value(QStringLiteral("active_reservation")).toObject();
    out->pileId      = r.value(QStringLiteral("pile_id")).toString();
    out->pileCode    = r.value(QStringLiteral("pile_name")).toString();
    out->stationName = r.value(QStringLiteral("station_name")).toString();
    out->stationId   = r.value(QStringLiteral("station_id")).toInt();
    out->typeText    = r.value(QStringLiteral("pile_type")).toString()
                               == QStringLiteral("FAST")
                           ? QStringLiteral("快充") : QStringLiteral("慢充");
    out->deposit     = r.value(QStringLiteral("deposit")).toDouble();
    out->expiresAtMs = qint64(r.value(QStringLiteral("expire_at")).toDouble());
    out->powerKw     = 0;   // 该接口不带功率，界面需要时自己去 /piles 查
    return true;
}

bool cancelReservation(double *forfeit, double *refund)
{
    QJsonObject data;
    if (!Api::post(QStringLiteral("/api/v1/charging/cancel-reservation"),
                   QJsonObject{}, &data, nullptr))
        return false;
    if (forfeit) *forfeit = data.value(QStringLiteral("penalty_fee")).toDouble();
    if (refund)  *refund  = data.value(QStringLiteral("refund_amount")).toDouble();
    return true;
}

bool startCharge(const QString &pileId, QString *err, QString *orderId)
{
    const QJsonObject body{{QStringLiteral("pile_id"), pileId},
                           {QStringLiteral("strategy_type"), QStringLiteral("FULL")},
                           {QStringLiteral("strategy_value"), 0},
                           {QStringLiteral("pre_freeze_amount"), 20.0}};
    QJsonObject data;
    if (!Api::post(QStringLiteral("/api/v1/charging/start"), body, &data, err,
                   QStringLiteral("CHG-") +
                       QUuid::createUuid().toString(QUuid::WithoutBraces)))
        return false;
    if (orderId) *orderId = data.value(QStringLiteral("order_id")).toString();
    return true;
}

bool stopCharge(const QString &orderId, QString *err)
{
    const QJsonObject body{{QStringLiteral("order_id"), orderId},
                           {QStringLiteral("stop_reason"),
                            QStringLiteral("USER_MANUAL_STOP")}};
    return Api::post(QStringLiteral("/api/v1/charging/stop"), body, nullptr, err);
}

// ================= 结算与订单历史 =================
namespace {

QString msToTimeText(qint64 ms)
{
    if (ms <= 0)
        return QString();
    return QDateTime::fromMSecsSinceEpoch(ms)
        .toString(QStringLiteral("yyyy-MM-dd HH:mm:ss"));
}

// order_status → (中文, 数字状态)：和本地版的 0/1/2 语义保持一致
QString statusToText(const QString &st, int *num)
{
    if (st == QStringLiteral("CHARGING"))  { if (num) *num = 0; return QStringLiteral("充电中"); }
    if (st == QStringLiteral("UNSETTLED")) { if (num) *num = 1; return QStringLiteral("待结算"); }
    if (st == QStringLiteral("COMPLETED")) { if (num) *num = 2; return QStringLiteral("已结算"); }
    if (st == QStringLiteral("REFUNDED"))  { if (num) *num = 2; return QStringLiteral("已退款"); }
    if (num) *num = 2;
    return st;
}

// /orders/my 行 → Receipt
bool receiptFromRow(const QJsonObject &o, Receipt *out)
{
    out->id             = o.value(QStringLiteral("order_id")).toString();
    out->stationName    = o.value(QStringLiteral("station_name")).toString();
    out->pileCode       = o.value(QStringLiteral("pile_id")).toString();
    const qint64 startMs = qint64(o.value(QStringLiteral("start_time")).toDouble());
    const qint64 endMs   = qint64(o.value(QStringLiteral("end_time")).toDouble());
    out->startTime      = msToTimeText(startMs);
    out->endTime        = msToTimeText(endMs);
    // 行数据只有整分钟，短订单会显示 0 分钟 → 用时间戳自己算到秒
    out->durationSec    = endMs > startMs ? int((endMs - startMs) / 1000)
                        : o.value(QStringLiteral("duration_minutes")).toInt() * 60;
    out->kwh            = o.value(QStringLiteral("charged_energy_kwh")).toDouble();
    out->electricityFee = o.value(QStringLiteral("electricity_fee")).toDouble();
    out->serviceFee     = o.value(QStringLiteral("service_fee")).toDouble();
    out->overtimeFee    = o.value(QStringLiteral("overtime_fee")).toDouble();
    out->totalFee       = o.value(QStringLiteral("total_fee")).toDouble();
    out->statusText     = statusToText(
        o.value(QStringLiteral("order_status")).toString(), nullptr);
    return true;
}

} // namespace

bool settle(const QString &orderId, double *deducted, double *newBalance, QString *err)
{
    const QJsonObject body{{QStringLiteral("order_id"), orderId}};
    QJsonObject data;
    if (!Api::post(QStringLiteral("/api/v1/charging/settle"), body, &data, err,
                   QStringLiteral("SETTLE-") + orderId))   // 幂等键=订单号，重复点不会扣两次
        return false;
    if (deducted)   *deducted   = data.value(QStringLiteral("wallet_deducted")).toDouble();
    if (newBalance) *newBalance = data.value(QStringLiteral("new_balance")).toDouble();
    return true;
}

bool orderDetail(const QString &orderId, Receipt *out)
{
    QJsonObject o;
    // 详情接口在 /charging/orders/{id}；拿不到就去 /orders/my 里找同号订单兜底
    if (!Api::get(QStringLiteral("/api/v1/charging/orders/%1").arg(orderId), &o)) {
        QJsonObject data;
        if (!Api::get(QStringLiteral("/api/v1/orders/my?page=1&page_size=50"), &data))
            return false;
        for (const auto &v : data.value(QStringLiteral("orders")).toArray())
            if (v.toObject().value(QStringLiteral("order_id")).toString() == orderId)
                return receiptFromRow(v.toObject(), out);
        return false;
    }
    out->id             = o.value(QStringLiteral("order_id")).toString();
    out->stationName    = o.value(QStringLiteral("station_name")).toString();
    out->pileCode       = o.value(QStringLiteral("pile_id")).toString();
    out->startTime      = msToTimeText(qint64(o.value(QStringLiteral("start_time")).toDouble()));
    out->endTime        = msToTimeText(qint64(o.value(QStringLiteral("end_time")).toDouble()));
    out->durationSec    = o.value(QStringLiteral("duration_seconds")).toInt();
    out->kwh            = o.value(QStringLiteral("charged_energy_kwh")).toDouble();
    out->electricityFee = o.value(QStringLiteral("electricity_fee")).toDouble();
    out->serviceFee     = o.value(QStringLiteral("service_fee")).toDouble();
    out->overtimeFee    = o.value(QStringLiteral("overtime_fee")).toDouble();
    out->totalFee       = o.value(QStringLiteral("total_amount")).toDouble();
    out->statusText     = statusToText(
        o.value(QStringLiteral("order_status")).toString(), nullptr);
    return true;
}

QList<OrderRow> orderHistory(int limit)
{
    QList<OrderRow> list;
    QJsonObject data;
    if (!Api::get(QStringLiteral("/api/v1/orders/my?page=1&page_size=%1").arg(limit),
                  &data))
        return list;
    for (const auto &v : data.value(QStringLiteral("orders")).toArray()) {
        const QJsonObject o = v.toObject();
        OrderRow r;
        r.id          = o.value(QStringLiteral("order_id")).toString();
        r.stationName = o.value(QStringLiteral("station_name")).toString();
        r.pileCode    = o.value(QStringLiteral("pile_id")).toString();
        r.typeText    = o.value(QStringLiteral("pile_type")).toString()
                                == QStringLiteral("FAST")
                            ? QStringLiteral("快充") : QStringLiteral("慢充");
        const qint64 startMs = qint64(o.value(QStringLiteral("start_time")).toDouble());
        const qint64 endMs   = qint64(o.value(QStringLiteral("end_time")).toDouble());
        r.startTime   = msToTimeText(startMs);
        r.durationSec = endMs > startMs ? int((endMs - startMs) / 1000)
                      : o.value(QStringLiteral("duration_minutes")).toInt() * 60;
        r.kwh         = o.value(QStringLiteral("charged_energy_kwh")).toDouble();
        r.amount      = o.value(QStringLiteral("total_fee")).toDouble();
        r.statusText  = statusToText(
            o.value(QStringLiteral("order_status")).toString(), &r.status);
        list << r;
    }
    return list;
}

} // namespace ChargeService
