#ifndef RECEIPTTEXT_H
#define RECEIPTTEXT_H

#include "core/chargeservice.h"

#include <QString>

// 充电时长的文字：不足 1 分钟显示秒，整分显示分钟，其余显示 X 分 Y 秒
inline QString durationText(int secs)
{
    if (secs < 60)
        return QStringLiteral("%1 秒").arg(secs);
    if (secs % 60 == 0)
        return QStringLiteral("%1 分钟").arg(secs / 60);
    return QStringLiteral("%1 分 %2 秒").arg(secs / 60).arg(secs % 60);
}

// 订单小票的 HTML —— 结算页(ChargePage)和订单历史详情(OrdersPage)共用
// 费用按 server 的三段式展示：电费 + 服务费 + 超时占位费
inline QString receiptHtml(const ChargeService::Receipt &r, bool showStatus = false)
{
    const QString dash = QStringLiteral("—");
    QString html = QStringLiteral(
        "<table cellspacing='6'>"
        "<tr><td>订单号</td><td><b>%1</b></td></tr>"
        "<tr><td>电站</td><td>%2</td></tr>"
        "<tr><td>电桩</td><td>%3</td></tr>"
        "<tr><td>开始时间</td><td>%4</td></tr>"
        "<tr><td>结束时间</td><td>%5</td></tr>"
        "<tr><td>充电时长</td><td>%6</td></tr>"
        "<tr><td>充电量</td><td>%7 度</td></tr>"
        "<tr><td>电费</td><td>￥%8</td></tr>"
        "<tr><td>服务费</td><td>￥%9</td></tr>")
        .arg(r.id, r.stationName, r.pileCode, r.startTime,
             r.endTime.isEmpty() ? dash : r.endTime)
        .arg(durationText(r.durationSec)).arg(r.kwh, 0, 'f', 2)
        .arg(r.electricityFee, 0, 'f', 2).arg(r.serviceFee, 0, 'f', 2);
    if (r.overtimeFee > 0)
        html += QStringLiteral("<tr><td>超时占位费</td><td>￥%1</td></tr>")
                    .arg(r.overtimeFee, 0, 'f', 2);
    html += QStringLiteral(
        "<tr><td>合计</td><td><b style='color:#0E8A57'>￥%1</b></td></tr>")
        .arg(r.totalFee, 0, 'f', 2);
    if (showStatus)
        html += QStringLiteral("<tr><td>状态</td><td>%1</td></tr>").arg(r.statusText);
    html += QStringLiteral("</table>");
    return html;
}

#endif // RECEIPTTEXT_H
