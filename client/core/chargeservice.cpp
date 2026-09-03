#include "core/chargeservice.h"

#include "core/apiclient.h"
#include "core/session.h"


#include <QDateTime>
#include <QJsonArray>
#include <QJsonObject>

namespace ChargeService {

bool devLogin(QString *err)
{
    QString phone = qEnvironmentVariable("NCS_PHONE");
    if (phone.isEmpty())
        phone = QStringLiteral("13866667777");

    const QJsonObject body{{QStringLiteral("phone"), phone},
                           {QStringLiteral("auth_type"), QStringLiteral("passwordless")}};
    QJsonObject data;
    if (!Api::post(QStringLiteral("/api/v1/auth/login"), body, &data, err))
        return false;
    Session::i().setUserId(data.value(QStringLiteral("user_id")).toInt());
    Session::i().setToken(data.value(QStringLiteral("access_token")).toString());
    return true;
}

bool findUnfinished(int userId, ActiveOrder *out)
{
    Q_UNUSED(userId);          // 云端靠 token 识别用户，参数只为兼容界面签名
    QJsonObject data;
    if (!Api::get(QStringLiteral("/api/v1/charging/active-order"), &data))
        return false;
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
    return true;
}

QList<StationOpt> stationOptions()
{
    // 软件模拟定位：server 种子数据在上海，先固定用上海市中心坐标
    // 等 B 的定位下拉框接进来，把坐标换成用户选的区域即可
    QList<StationOpt> list;
    QJsonObject data;
    if (!Api::get(QStringLiteral("/api/v1/stations/nearby"
                                 "?latitude=31.2304&longitude=121.4737"
                                 "&radius_km=50&limit=20"), &data))
        return list;
    const QJsonArray arr = data.value(QStringLiteral("stations")).toArray();
    for (const auto &v : arr) {
        const QJsonObject s = v.toObject();
        StationOpt o;
        o.id        = s.value(QStringLiteral("station_id")).toInt();
        o.name      = s.value(QStringLiteral("station_name")).toString();
        o.price     = s.value(QStringLiteral("price_per_kwh")).toDouble();
        o.freeCount = s.value(QStringLiteral("idle_piles")).toInt();
        list << o;
    }
    return list;
}

QList<PileOpt> freePiles(int stationId)
{
    QList<PileOpt> list;
    QJsonObject data;
    if (!Api::get(QStringLiteral("/api/v1/stations/%1").arg(stationId), &data))
        return list;
    const QJsonArray arr = data.value(QStringLiteral("piles")).toArray();
    for (const auto &v : arr) {
        const QJsonObject p = v.toObject();
        if (p.value(QStringLiteral("status_code")).toInt() != 1)   // 1 = 空闲可用
            continue;
        PileOpt o;
        o.id       = p.value(QStringLiteral("pile_id")).toString();
        o.code     = p.value(QStringLiteral("pile_name")).toString();
        o.typeText = p.value(QStringLiteral("type_desc")).toString();
        o.powerKw  = p.value(QStringLiteral("max_power_kw")).toDouble();
        list << o;
    }
    return list;
}

} // namespace ChargeService
