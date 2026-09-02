#include "core/chargeservice.h"

#include <QSqlQuery>
#include <QVariant>

namespace ChargeService {

int timeScale()
{
    bool ok = false;
    const int v = qEnvironmentVariable("NCS_TIME_SCALE").toInt(&ok);
    return (ok && v > 0) ? v : 60;
}

double minStartBalance() { return 5.0; }   // 说明书 BR-04：最低起充金额

bool findUnfinished(int userId, ActiveOrder *out)
{
    // 数据库负责人 03_queries.sql A7：status 0/1 都算「未完成」
    QSqlQuery q;
    q.prepare(QStringLiteral(
        "SELECT id, pile_id, pile_code, station_name, start_time, unit_price,"
        "       status, duration_min, kwh, amount "
        "FROM v_order_detail WHERE user_id = ? AND status IN (0,1) "
        "ORDER BY start_time DESC LIMIT 1"));
    q.addBindValue(userId);
    if (!q.exec() || !q.next())
        return false;

    out->id          = q.value(0).toInt();
    out->pileId      = q.value(1).toInt();
    out->pileCode    = q.value(2).toString();
    out->stationName = q.value(3).toString();
    out->startTime   = q.value(4).toString();
    out->unitPrice   = q.value(5).toDouble();
    out->status      = q.value(6).toInt();
    out->durationMin = q.value(7).toInt();
    out->kwh         = q.value(8).toDouble();
    out->amount      = q.value(9).toDouble();

    QSqlQuery p;
    p.prepare(QStringLiteral("SELECT power_kw FROM piles WHERE id = ?"));
    p.addBindValue(out->pileId);
    if (p.exec() && p.next())
        out->powerKw = p.value(0).toDouble();
    return true;
}

QList<StationOpt> stationOptions()
{
    QList<StationOpt> list;
    QSqlQuery q(QStringLiteral(
        "SELECT id, name, price, pile_free FROM v_station_overview ORDER BY id"));
    while (q.next()) {
        StationOpt s;
        s.id        = q.value(0).toInt();
        s.name      = q.value(1).toString();
        s.price     = q.value(2).toDouble();
        s.freeCount = q.value(3).toInt();
        list << s;
    }
    return list;
}

QList<PileOpt> freePiles(int stationId)
{
    QList<PileOpt> list;
    QSqlQuery q;
    q.prepare(QStringLiteral(
        "SELECT id, code, type_text, power_kw FROM v_pile_detail "
        "WHERE station_id = ? AND status = 0 ORDER BY power_kw DESC"));
    q.addBindValue(stationId);
    q.exec();
    while (q.next()) {
        PileOpt p;
        p.id       = q.value(0).toInt();
        p.code     = q.value(1).toString();
        p.typeText = q.value(2).toString();
        p.powerKw  = q.value(3).toDouble();
        list << p;
    }
    return list;
}

int devDefaultUser()
{
    QSqlQuery q(QStringLiteral(
        "SELECT id FROM users WHERE status = 0 AND balance >= 20 "
        "AND id NOT IN (SELECT user_id FROM orders WHERE status IN (0,1)) "
        "ORDER BY id LIMIT 1"));
    if (q.next())
        return q.value(0).toInt();
    QSqlQuery fix(QStringLiteral("UPDATE users SET balance = 100 WHERE id = 1"));
    return 1;
}

} // namespace ChargeService
