#include "ordermanagementmodel.h"

#include "tokenmanager.h"

#include <QNetworkReply>
#include <QNetworkRequest>
#include <QUrlQuery>
#include <QJsonDocument>
#include <QDateTime>
#include <QUuid>
#include <QDebug>
#include <QCoreApplication>
#include <QWidget>
#include <QTableView>
#include <QApplication>
#include <QFontMetrics>

// ============================================================================
// 单元格文本省略：按目标 QTableView 当前列宽计算省略号截断（超长悬停显示全文）。
// 与 StationManagementModel / PileManagementModel 中的同名辅助保持一致。
// scope 为 Model 的父对象（管理页 Widget），viewName 为目标 QTableView 的 objectName；
// 若视图尚未显示（数据先于页面打开到达），退化为"超过 30 个字符才截断"的规则。
// ============================================================================
static void applyElidedCellText(QStandardItem *item, const QString &fullText,
                                QObject *scope, const char *viewName, int column)
{
    if (item == nullptr) return;

    const QTableView *view = nullptr;
    if (const auto *scopeWidget = qobject_cast<const QWidget *>(scope)) {
        view = scopeWidget->findChild<const QTableView *>(QLatin1StringView(viewName));
    }

    const QFontMetrics fm(view ? view->font() : QApplication::font());
    int available = 0;
    if (view != nullptr && view->isVisible() && view->columnWidth(column) > 0) {
        available = view->columnWidth(column) - 16; // 减去单元格左右内边距
    } else {
        // 视图未显示或列宽无效：按 30 个字符的宽度估算
        available = fm.horizontalAdvance(fullText.left(30));
    }

    if (available > 0 && fm.horizontalAdvance(fullText) <= available) {
        item->setText(fullText); // 宽度足够：原样显示，不设 ToolTip
        return;
    }
    item->setText(fm.elidedText(fullText, Qt::ElideRight, qMax(available, 1)));
    item->setToolTip(fullText); // 悬停显示未截断的完整内容
}

// ============================================================================
// 状态字典（文档 3.6.1 order_status: CHARGING / UNSETTLED / COMPLETED / REFUNDED）
// 颜色映射：充电中 #00d4ff / 待结算 #f59e0b / 已完成 #22c55e / 已退款 #8a9aa8
// ============================================================================

QString OrderManagementModel::orderStatusText(const QString &status)
{
    if (status == QLatin1String("CHARGING"))  return OrderManagementModel::tr("充电中");
    if (status == QLatin1String("UNSETTLED")) return OrderManagementModel::tr("待结算");
    if (status == QLatin1String("COMPLETED")) return OrderManagementModel::tr("已完成");
    if (status == QLatin1String("REFUNDED"))  return OrderManagementModel::tr("已退款");
    return status; // 未知状态：原文展示，便于发现后端新增状态
}

QColor OrderManagementModel::orderStatusColor(const QString &status)
{
    if (status == QLatin1String("CHARGING"))  return QColor(0x00, 0xd4, 0xff);
    if (status == QLatin1String("UNSETTLED")) return QColor(0xf5, 0x9e, 0x0b);
    if (status == QLatin1String("COMPLETED")) return QColor(0x22, 0xc5, 0x5e);
    if (status == QLatin1String("REFUNDED"))  return QColor(0x8a, 0x9a, 0xa8);
    return QColor(0x4a, 0x5a, 0x6e); // 未知状态用正文灰色
}

QString OrderManagementModel::pileTypeText(const QString &type)
{
    if (type == QLatin1String("FAST")) return OrderManagementModel::tr("快充");
    if (type == QLatin1String("SLOW")) return OrderManagementModel::tr("慢充");
    return QString(); // 未知类型返回空串，由调用方决定回退展示
}

// ============================================================================
// 构造 / 基础
// ============================================================================

OrderManagementModel::OrderManagementModel(QObject *parent)
    : QObject(parent)
    , m_tableModel(new QStandardItemModel(0, ColCount, this))
{
    m_tableModel->setHorizontalHeaderLabels({
        tr("订单ID"), tr("用户ID"), tr("用户手机号"), tr("充电站"), tr("充电桩"),
        tr("充电量(kWh)"), tr("电费"), tr("服务费"), tr("超时费"), tr("总金额"),
        tr("状态"), tr("创建时间"), tr("结束时间"), tr("操作")
    });
    // 注意：网络请求在 Widget 调用 fetchOrders() 后才发起（构造阶段尚无 Token）。
}

QStandardItemModel *OrderManagementModel::getModel()
{
    return m_tableModel;
}

void OrderManagementModel::setAuthToken(const QString &token)
{
    // Token 由 TokenManager 单例统一管理（登录后由 main.cpp 设置），
    // Model 不再保存 Token；首次拉取由 Widget 触发。
    Q_UNUSED(token);
}

// ============================================================================
// 分页检索全平台充电订单（文档 3.6.1）
//   GET /api/v1/admin/orders?page=&page_size=&station_id=&order_status=&start_date=&end_date=
// ============================================================================

void OrderManagementModel::fetchOrders(int page, int pageSize, int stationId,
                                       const QString &orderStatus,
                                       const QString &startDate,
                                       const QString &endDate)
{
    m_page = qMax(1, page);
    m_pageSize = qMax(1, pageSize);
    m_stationId = stationId;
    m_orderStatus = orderStatus.trimmed();
    m_startDate = startDate.trimmed();
    m_endDate = endDate.trimmed();
    m_lastQueryHasPhone = false; // 全局列表查询不带 phone 参数

    QUrl url(m_serverBase + QStringLiteral("/api/v1/admin/orders"));
    QUrlQuery query;
    query.addQueryItem(QStringLiteral("page"), QString::number(m_page));
    query.addQueryItem(QStringLiteral("page_size"), QString::number(m_pageSize));
    if (m_stationId > 0) {
        query.addQueryItem(QStringLiteral("station_id"), QString::number(m_stationId));
    }
    if (!m_orderStatus.isEmpty()) {
        query.addQueryItem(QStringLiteral("order_status"), m_orderStatus);
    }
    if (!m_startDate.isEmpty()) {
        query.addQueryItem(QStringLiteral("start_date"), m_startDate);
    }
    if (!m_endDate.isEmpty()) {
        query.addQueryItem(QStringLiteral("end_date"), m_endDate);
    }
    url.setQuery(query);

    qDebug().noquote() << "[OrderManagementModel] fetchOrders() -"
                       << url.toString();

    QNetworkRequest request(url);
    // prepareRequest 由 TokenManager 内部完成（注入 Authorization 头）；
    // 401/40001/40002 自动刷新 Token 并重试，回调最终收到重试后的 reply。
    TokenManager::instance()->get(request, [this](QNetworkReply *reply) {
        handleOrdersReply(reply, QStringLiteral("GET /api/v1/admin/orders"));
    });
}

// ============================================================================
// 按用户查询历史订单（新版文档 3.6.1 合并接口，第二期新增）
//   GET /api/v1/admin/orders?user_id=&phone=&page=&page_size=
//   2026-09-08 接口改版：原独立接口 /admin/orders/user 已下线（404），
//   user_id/phone 合并为列表接口的可选参数；两者同时提供时必须指向同一
//   用户，矛盾时服务端返回 HTTP 400（由错误弹窗透出）。
// ============================================================================

void OrderManagementModel::fetchOrdersByUser(qint64 userId, const QString &phone,
                                             int page, int pageSize)
{
    m_page = qMax(1, page);
    m_pageSize = qMax(1, pageSize);

    QUrl url(m_serverBase + QStringLiteral("/api/v1/admin/orders"));
    QUrlQuery query;
    if (userId > 0) {
        query.addQueryItem(QStringLiteral("user_id"), QString::number(userId));
    }
    const QString ph = phone.trimmed();
    if (!ph.isEmpty()) {
        query.addQueryItem(QStringLiteral("phone"), ph);
    }
    query.addQueryItem(QStringLiteral("page"), QString::number(m_page));
    query.addQueryItem(QStringLiteral("page_size"), QString::number(m_pageSize));
    url.setQuery(query);

    // 手机号为精确匹配：用户不存在时后端返回 HTTP 404（而非空列表），
    // handleOrdersReply 据此标记做"未找到该用户的订单"特殊处理
    m_lastQueryHasPhone = !ph.isEmpty();

    qDebug().noquote() << "[OrderManagementModel] fetchOrdersByUser() -"
                       << url.toString();

    QNetworkRequest request(url);
    TokenManager::instance()->get(request, [this](QNetworkReply *reply) {
        handleOrdersReply(reply, QStringLiteral("GET /api/v1/admin/orders?user"));
    });
}

// ============================================================================
// 订单详情（文档 3.6.4，第二期新增）
//   GET /api/v1/admin/orders/{order_id}
//   实测响应字段：order_id / user_id / user_phone / station_id / station_name /
//   pile_id / pile_type / order_status / start_time / end_time / duration_seconds /
//   start_soc / end_soc / charged_energy_kwh / electricity_price / electricity_fee /
//   service_price / service_fee / overtime_* / total_amount（注意与列表的 total_fee 不同）/
//   stop_reason / settled_at；退款明细块当前服务端按需返回，可能缺失。
// ============================================================================

void OrderManagementModel::fetchOrderDetail(const QString &orderId)
{
    QUrl url(m_serverBase
             + QStringLiteral("/api/v1/admin/orders/%1").arg(orderId));
    QNetworkRequest request(url);

    qDebug().noquote() << "[OrderManagementModel] fetchOrderDetail() -"
                       << url.toString();

    TokenManager::instance()->get(request, [this, orderId](QNetworkReply *reply) {
        handleDetailReply(reply, orderId);
    });
}

void OrderManagementModel::handleDetailReply(QNetworkReply *reply,
                                             const QString &orderId)
{
    const QNetworkReply::NetworkError netError = reply->error();
    const QString netErrorString = reply->errorString();
    const int httpStatus =
        reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    const QByteArray body = reply->readAll();
    reply->deleteLater();

    const QString apiTag = QStringLiteral("GET /api/v1/admin/orders/%1").arg(orderId);

    if (netError != QNetworkReply::NoError) {
        const QString msg = QStringLiteral("%1 网络请求失败 (HTTP %2): %3")
                                .arg(apiTag).arg(httpStatus).arg(netErrorString);
        qWarning() << "[OrderManagementModel]" << msg;
        emit errorOccurred(msg);
        return;
    }
    if (httpStatus < 200 || httpStatus >= 300) {
        const QString msg = QStringLiteral("%1 HTTP 异常状态 %2: %3")
                                .arg(apiTag).arg(httpStatus).arg(netErrorString);
        qWarning() << "[OrderManagementModel]" << msg
                   << "原始响应:" << QString::fromUtf8(body.left(1024));
        emit errorOccurred(msg);
        return;
    }

    QJsonObject data;
    if (!extractData(body, apiTag, data)) {
        return;
    }

    // 一致性兜底：详情对象缺失 order_id 时用请求参数补齐，便于弹窗展示
    if (data.value(QStringLiteral("order_id")).toString().isEmpty()) {
        data.insert(QStringLiteral("order_id"), orderId);
    }
    qDebug().noquote() << "[OrderManagementModel] 订单详情获取成功 -" << orderId;
    emit orderDetailReady(data);
}

// ============================================================================
// 管理员一键退款（文档 3.6.3）
//   POST /api/v1/admin/orders/{order_id}/refund
//   请求头: Idempotency-Key（文档 2.x：退款必须携带，防重复提交）
//   请求体: { refund_amount, refund_amount_cents, reason }
// ============================================================================

void OrderManagementModel::refundOrder(const QString &orderId, double refundAmount,
                                       const QString &reason)
{
    QUrl url(m_serverBase
             + QStringLiteral("/api/v1/admin/orders/%1/refund").arg(orderId));
    QNetworkRequest request(url);
    // 幂等键：每次请求生成唯一 ID，网络层重试时服务端可据此去重（文档要求）。
    // 必须在调用 TokenManager::post 之前设置：TokenManager 内部 prepareRequest
    // 仅补充 Content-Type/Accept/Authorization，会保留此处的自定义头。
    request.setRawHeader("Idempotency-Key",
                         (QStringLiteral("RF-")
                          + QUuid::createUuid().toString(QUuid::WithoutBraces))
                             .toUtf8());

    QJsonObject body;
    body.insert(QStringLiteral("refund_amount"), refundAmount);
    body.insert(QStringLiteral("refund_amount_cents"),
                static_cast<qint64>(qRound64(refundAmount * 100.0)));
    body.insert(QStringLiteral("reason"), reason);
    const QByteArray payload = QJsonDocument(body).toJson(QJsonDocument::Compact);

    qDebug().noquote() << "[OrderManagementModel] refundOrder() -"
                       << url.toString() << "payload:" << payload;

    TokenManager::instance()->post(request, payload, [this, orderId](QNetworkReply *reply) {
        handleRefundReply(reply, orderId);
    });
}

// ============================================================================
// 内部辅助
// ============================================================================

void OrderManagementModel::populateOrders(const QJsonArray &orders)
{
    m_tableModel->removeRows(0, m_tableModel->rowCount());

    for (qsizetype i = 0; i < orders.size(); ++i) {
        const QJsonObject o = orders.at(i).toObject();

        const QString orderId = o.value(QStringLiteral("order_id")).toString();
        // 实测线上响应无 user_id / user_phone 字段（文档 3.6.1 有），缺失时显示 "-"
        const QJsonValue uidVal = o.value(QStringLiteral("user_id"));
        const QString userId = uidVal.isUndefined() || uidVal.isNull()
            ? QStringLiteral("-") : QString::number(uidVal.toVariant().toLongLong());
        const QString userPhone = o.value(QStringLiteral("user_phone")).toString();
        const QString stationName = o.value(QStringLiteral("station_name")).toString();
        const QString pileId = o.value(QStringLiteral("pile_id")).toString();
        const double energy = o.value(QStringLiteral("charged_energy_kwh")).toDouble();
        const double elecFee = o.value(QStringLiteral("electricity_fee")).toDouble();
        const double servFee = o.value(QStringLiteral("service_fee")).toDouble();
        const double overtimeFee = o.value(QStringLiteral("overtime_fee")).toDouble();
        const double totalFee = o.value(QStringLiteral("total_fee")).toDouble();
        const QString status = o.value(QStringLiteral("order_status")).toString();
        const qint64 startTimeMs = static_cast<qint64>(
            o.value(QStringLiteral("start_time")).toDouble(0));
        const qint64 endTimeMs = static_cast<qint64>(
            o.value(QStringLiteral("end_time")).toDouble(0));

        const QString fmtMs = QStringLiteral("yyyy-MM-dd hh:mm:ss");
        const QString startTimeText =
            startTimeMs > 0 ? QDateTime::fromMSecsSinceEpoch(startTimeMs).toString(fmtMs)
                            : QStringLiteral("-");
        const QString endTimeText =
            endTimeMs > 0 ? QDateTime::fromMSecsSinceEpoch(endTimeMs).toString(fmtMs)
                          : QStringLiteral("-");

        // 订单ID 列（超长省略号截断，悬停显示完整订单号）
        QStandardItem *idItem = new QStandardItem();
        idItem->setTextAlignment(Qt::AlignLeft | Qt::AlignVCenter);
        applyElidedCellText(idItem, orderId, parent(), "orderTable",
                            static_cast<int>(OrderIdCol));
        // 用户ID 列
        QStandardItem *uidItem = new QStandardItem(userId);
        uidItem->setTextAlignment(Qt::AlignCenter);
        // 用户手机号列
        QStandardItem *phoneItem = new QStandardItem(
            userPhone.isEmpty() ? QStringLiteral("-") : userPhone);
        phoneItem->setTextAlignment(Qt::AlignCenter);
        // 充电站列（超长省略号截断，悬停显示全称）
        QStandardItem *stationItem = new QStandardItem();
        stationItem->setTextAlignment(Qt::AlignLeft | Qt::AlignVCenter);
        applyElidedCellText(stationItem, stationName, parent(), "orderTable",
                            static_cast<int>(StationCol));
        // 充电桩列
        QStandardItem *pileItem = new QStandardItem(pileId);
        pileItem->setTextAlignment(Qt::AlignCenter);
        // 充电量列（保留两位小数）
        QStandardItem *energyItem = new QStandardItem(
            QString::number(energy, 'f', 2));
        energyItem->setTextAlignment(Qt::AlignCenter);
        // 电费 / 服务费 / 超时费 / 总金额列（均为两位小数金额）
        auto makeFeeItem = [](double v) {
            QStandardItem *it = new QStandardItem(QString::number(v, 'f', 2));
            it->setTextAlignment(Qt::AlignCenter);
            return it;
        };
        QStandardItem *totalItem = makeFeeItem(totalFee);
        QFont totalFont = totalItem->font();
        totalFont.setBold(true);
        totalItem->setFont(totalFont);
        // 状态列：中文文案 + 状态色，原始状态存 StatusRole
        QStandardItem *stItem = new QStandardItem(orderStatusText(status));
        stItem->setForeground(orderStatusColor(status));
        stItem->setTextAlignment(Qt::AlignCenter);
        stItem->setData(status, StatusRole);
        // 创建时间 / 结束时间列
        QStandardItem *startItem = new QStandardItem(startTimeText);
        startItem->setTextAlignment(Qt::AlignCenter);
        QStandardItem *endItem = new QStandardItem(endTimeText);
        endItem->setTextAlignment(Qt::AlignCenter);
        // 操作列占位：退款按钮由 Widget 依据本行的角色数据动态安装
        QStandardItem *actItem = new QStandardItem(QString());
        actItem->setData(orderId, OrderIdRole);
        actItem->setData(status, StatusRole);
        actItem->setData(totalFee, TotalFeeRole);
        actItem->setTextAlignment(Qt::AlignCenter);

        m_tableModel->appendRow({idItem, uidItem, phoneItem, stationItem, pileItem,
                                 energyItem, makeFeeItem(elecFee), makeFeeItem(servFee),
                                 makeFeeItem(overtimeFee), totalItem, stItem,
                                 startItem, endItem, actItem});
    }
}

bool OrderManagementModel::extractData(const QByteArray &body,
                                       const QString &apiTag, QJsonObject &outData)
{
    QJsonParseError parseErr{};
    const QJsonDocument doc = QJsonDocument::fromJson(body, &parseErr);
    if (parseErr.error != QJsonParseError::NoError || !doc.isObject()) {
        const QString msg = QStringLiteral("%1 响应解析失败: %2")
                                .arg(apiTag, parseErr.errorString());
        qWarning() << "[OrderManagementModel]" << msg
                   << "原始响应:" << QString::fromUtf8(body.left(1024));
        emit errorOccurred(msg);
        return false;
    }

    const QJsonObject root = doc.object();
    if (root.value(QStringLiteral("code")).toInt(-1) != 0) {
        // 业务错误（如 30004 订单已退款 / 30005 退款金额非法）：直接透出服务端 msg
        const QString code = QString::number(
            root.value(QStringLiteral("code")).toInt());
        const QString bizMsg = root.value(QStringLiteral("msg")).toString();
        const QString msg = QStringLiteral("%1 业务错误(code=%2): %3")
                                .arg(apiTag, code,
                                     bizMsg.isEmpty() ? QStringLiteral("未知错误") : bizMsg);
        qWarning() << "[OrderManagementModel]" << msg
                   << "原始响应:" << QString::fromUtf8(body.left(1024));
        emit errorOccurred(msg);
        return false;
    }

    outData = root.value(QStringLiteral("data")).toObject();
    return true;
}

void OrderManagementModel::handleOrdersReply(QNetworkReply *reply,
                                             const QString &apiTag)
{
    // 先取走全部所需信息，再 deleteLater() 释放 reply
    const QNetworkReply::NetworkError netError = reply->error();
    const QString netErrorString = reply->errorString();
    const int httpStatus =
        reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    const QByteArray body = reply->readAll();
    reply->deleteLater();

    // 手机号精确查询未命中：后端对不存在的手机号返回 HTTP 404（此时
    // netError 同时为 ContentNotFoundError，须在通用网络错误检查之前拦截）。
    // 不弹错误提示，置空列表，由 UI 显示"未找到该用户的订单"。
    if (httpStatus == 404 && m_lastQueryHasPhone) {
        qDebug().noquote() << "[OrderManagementModel]" << apiTag
                           << "手机号未命中(404) - 置空列表";
        populateOrders(QJsonArray());
        emit ordersReady(QJsonArray(), 0, m_page, m_pageSize);
        return;
    }

    if (netError != QNetworkReply::NoError) {
        const QString msg = QStringLiteral("%1 网络请求失败 (HTTP %2): %3")
                                .arg(apiTag).arg(httpStatus).arg(netErrorString);
        qWarning() << "[OrderManagementModel]" << msg;
        emit errorOccurred(msg);
        return;
    }
    // 双重校验：业务信封 code==0 之外，HTTP 状态码也必须是 2xx
    if (httpStatus < 200 || httpStatus >= 300) {
        const QString msg = QStringLiteral("%1 HTTP 异常状态 %2: %3")
                                .arg(apiTag).arg(httpStatus).arg(netErrorString);
        qWarning() << "[OrderManagementModel]" << msg
                   << "原始响应:" << QString::fromUtf8(body.left(1024));
        emit errorOccurred(msg);
        return;
    }

    QJsonObject data;
    if (!extractData(body, apiTag, data)) {
        return;
    }

    const QJsonArray orders = data.value(QStringLiteral("orders")).toArray();
    const int total = static_cast<int>(data.value(QStringLiteral("total")).toInt());
    const int page = data.value(QStringLiteral("page")).toInt(m_page);
    const int pageSize = data.value(QStringLiteral("page_size")).toInt(m_pageSize);

    populateOrders(orders);
    qDebug().noquote() << "[OrderManagementModel] 订单列表更新成功 - 本页:"
                       << orders.size() << "条, 总数:" << total;
    emit ordersReady(orders, total, page, pageSize);
}

void OrderManagementModel::handleRefundReply(QNetworkReply *reply,
                                             const QString &orderId)
{
    const QNetworkReply::NetworkError netError = reply->error();
    const QString netErrorString = reply->errorString();
    const int httpStatus =
        reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    const QByteArray body = reply->readAll();
    reply->deleteLater();

    const QString apiTag = QStringLiteral("POST /api/v1/admin/orders/%1/refund").arg(orderId);

    if (netError != QNetworkReply::NoError) {
        const QString msg = QStringLiteral("%1 网络请求失败 (HTTP %2): %3")
                                .arg(apiTag).arg(httpStatus).arg(netErrorString);
        qWarning() << "[OrderManagementModel]" << msg;
        emit errorOccurred(msg);
        return;
    }
    if (httpStatus < 200 || httpStatus >= 300) {
        const QString msg = QStringLiteral("%1 HTTP 异常状态 %2: %3")
                                .arg(apiTag).arg(httpStatus).arg(netErrorString);
        qWarning() << "[OrderManagementModel]" << msg
                   << "原始响应:" << QString::fromUtf8(body.left(1024));
        emit errorOccurred(msg);
        return;
    }

    QJsonObject data;
    if (!extractData(body, apiTag, data)) {
        return;
    }

    // 文档 3.6.3 成功响应：refund_transaction_id / user_balance_before / user_balance_after ...
    const double refundAmount = data.value(QStringLiteral("refund_amount")).toDouble();
    const double balanceBefore = data.value(QStringLiteral("user_balance_before")).toDouble();
    const double balanceAfter = data.value(QStringLiteral("user_balance_after")).toDouble();
    const QString txId = data.value(QStringLiteral("refund_transaction_id")).toString();

    const QString msg = QStringLiteral("订单 %1 退款成功\n退款金额: %2 元\n退款流水号: %3\n用户余额: %4 元 -> %5 元")
                            .arg(orderId,
                                 QString::number(refundAmount, 'f', 2),
                                 txId.isEmpty() ? QStringLiteral("-") : txId,
                                 QString::number(balanceBefore, 'f', 2),
                                 QString::number(balanceAfter, 'f', 2));
    qDebug().noquote() << "[OrderManagementModel]" << msg;
    emit refundSuccess(msg);
}
