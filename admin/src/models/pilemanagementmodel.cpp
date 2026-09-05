#include "pilemanagementmodel.h"

#include <QDebug>

#include <QNetworkAccessManager>
#include <QNetworkRequest>
#include <QNetworkReply>
#include <QUrl>
#include <QUrlQuery>
#include <QJsonDocument>
#include <QJsonObject>
#include <QColor>

// ============================================================================
// 状态/类型字典（供 PileManagementWidget 与充电站详情弹窗共享）
// ============================================================================

QString PileManagementModel::pileStatusText(const QString &status)
{
    if (status == QStringLiteral("IDLE"))     return QStringLiteral("空闲");
    if (status == QStringLiteral("CHARGING")) return QStringLiteral("充电中");
    if (status == QStringLiteral("FAULT"))    return QStringLiteral("故障");
    if (status == QStringLiteral("OFFLINE"))  return QStringLiteral("离线");
    return status;
}

QColor PileManagementModel::pileStatusColor(const QString &status)
{
    if (status == QStringLiteral("IDLE"))     return QColor(0x2e, 0xcc, 0x71); // 绿
    if (status == QStringLiteral("CHARGING")) return QColor(0x00, 0xd4, 0xff); // 青
    if (status == QStringLiteral("FAULT"))    return QColor(0xff, 0x5c, 0x5c); // 红
    if (status == QStringLiteral("OFFLINE"))  return QColor(0x8b, 0x9b, 0xb4); // 灰
    return QColor(0xe6, 0xe9, 0xef); // 默认白
}

QString PileManagementModel::pileTypeText(const QString &type)
{
    if (type == QStringLiteral("FAST")) return QStringLiteral("快充");
    if (type == QStringLiteral("SLOW")) return QStringLiteral("慢充");
    return type;
}

QColor PileManagementModel::pileTypeColor(const QString &type)
{
    if (type == QStringLiteral("FAST")) return QColor(0x00, 0xd4, 0xff); // 青
    if (type == QStringLiteral("SLOW")) return QColor(0x8b, 0x9b, 0xb4); // 灰
    return QColor(0xe6, 0xe9, 0xef);
}

// ============================================================================
// 构造 / 基础
// ============================================================================

PileManagementModel::PileManagementModel(QObject *parent)
    : QObject(parent)
{
    m_tableModel = new QStandardItemModel(this);
    m_tableModel->setColumnCount(ColCount);
    m_tableModel->setHorizontalHeaderLabels({
        tr("桩编号"), tr("所属电站"), tr("类型"), tr("功率(kW)"),
        tr("状态"), tr("累计充电次数"), tr("累计充电时长"), tr("操作")
    });

    // 注意：网络请求在 Widget 调用 fetchPiles() 后才发起（构造阶段尚无 Token）。
}

QStandardItemModel *PileManagementModel::getModel()
{
    return m_tableModel;
}

void PileManagementModel::setAuthToken(const QString &token)
{
    m_authToken = token.trimmed();
}

// ============================================================================
// 分页查询充电桩列表（文档 3.4 节）
//   GET /api/v1/admin/piles?page=&page_size=&station_id=&status=&type=
// ============================================================================

void PileManagementModel::fetchPiles(int page, int pageSize, int stationId,
                                     const QString &statusFilter,
                                     const QString &typeFilter)
{
    m_page = qMax(1, page);
    m_pageSize = qMax(1, pageSize);
    m_statusFilter = statusFilter.trimmed().toUpper();
    m_typeFilter = typeFilter.trimmed().toUpper();

    ensureNetworkManager();

    QUrl url(m_serverBase + QStringLiteral("/api/v1/admin/piles"));
    QUrlQuery query;
    query.addQueryItem(QStringLiteral("page"), QString::number(m_page));
    query.addQueryItem(QStringLiteral("page_size"), QString::number(m_pageSize));
    if (stationId > 0) {
        query.addQueryItem(QStringLiteral("station_id"), QString::number(stationId));
    }
    if (!m_statusFilter.isEmpty()) {
        query.addQueryItem(QStringLiteral("status"), m_statusFilter);
    }
    if (!m_typeFilter.isEmpty()) {
        query.addQueryItem(QStringLiteral("type"), m_typeFilter);
    }
    url.setQuery(query);

    qDebug().noquote() << "[PileManagementModel] fetchPiles() -" << url.toString();

    QNetworkRequest request(url);
    prepareRequest(&request);

    QNetworkReply *reply = m_networkManager->get(request);
    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        handlePilesReply(reply);
    });
}

// ============================================================================
// 远程重启（文档 3.4 节）
//   POST /api/v1/admin/piles/{pile_id}/restart   body: {"reason": "管理员远程重启"}
// ============================================================================

void PileManagementModel::restartPile(const QString &pileId)
{
    ensureNetworkManager();

    const QUrl url(m_serverBase
                   + QStringLiteral("/api/v1/admin/piles/%1/restart").arg(pileId));
    QNetworkRequest request(url);
    prepareRequest(&request);

    const QByteArray body = QJsonDocument(
        QJsonObject{{QStringLiteral("reason"), QStringLiteral("管理员远程重启")}})
        .toJson(QJsonDocument::Compact);

    qDebug().noquote() << "[PileManagementModel] restartPile() -" << url.toString();

    QNetworkReply *reply = m_networkManager->post(request, body);
    connect(reply, &QNetworkReply::finished, this, [this, reply, pileId]() {
        handleRestartReply(reply, pileId);
    });
}

// ============================================================================
// 内部辅助
// ============================================================================

void PileManagementModel::ensureNetworkManager()
{
    if (!m_networkManager) {
        m_networkManager = new QNetworkAccessManager(this);
    }
}

void PileManagementModel::prepareRequest(QNetworkRequest *request) const
{
    request->setHeader(QNetworkRequest::ContentTypeHeader,
                       QStringLiteral("application/json"));
    request->setRawHeader("Accept", "application/json");
    // 受保护接口需携带 Bearer Token；未设置 Token 时不带头（本地联调用）
    if (!m_authToken.isEmpty()) {
        request->setRawHeader("Authorization",
                              (QStringLiteral("Bearer ") + m_authToken).toUtf8());
    }
}

void PileManagementModel::populatePiles(const QJsonArray &piles)
{
    m_tableModel->removeRows(0, m_tableModel->rowCount());

    for (qsizetype i = 0; i < piles.size(); ++i) {
        const QJsonObject p = piles.at(i).toObject();

        const QString pileId = p.value(QStringLiteral("pile_id")).toString();
        const QString stationName = p.value(QStringLiteral("station_name")).toString();
        const QString type = p.value(QStringLiteral("type")).toString();
        const double powerKw = p.value(QStringLiteral("power_kw")).toDouble();
        const QString status = p.value(QStringLiteral("current_status")).toString();
        const int chargeCount = p.value(QStringLiteral("total_charge_count")).toInt();
        const double chargeHours = p.value(QStringLiteral("total_charge_hours")).toDouble();

        // 桩编号列
        QStandardItem *idItem = new QStandardItem(pileId);
        idItem->setTextAlignment(Qt::AlignCenter);
        // 所属电站列
        QStandardItem *stationItem = new QStandardItem(stationName);
        stationItem->setTextAlignment(Qt::AlignLeft | Qt::AlignVCenter);
        // 类型列（快充青 / 慢充灰）
        QStandardItem *typeItem = new QStandardItem(pileTypeText(type));
        typeItem->setTextAlignment(Qt::AlignCenter);
        typeItem->setForeground(pileTypeColor(type));
        // 功率列（保留 1 位小数）
        QStandardItem *powerItem = new QStandardItem(QString::number(powerKw, 'f', 1));
        powerItem->setTextAlignment(Qt::AlignCenter);
        // 状态列（带颜色，原始值存角色）
        QStandardItem *stItem = new QStandardItem(pileStatusText(status));
        stItem->setTextAlignment(Qt::AlignCenter);
        stItem->setForeground(pileStatusColor(status));
        stItem->setData(status, CurrentStatusRole);
        // 累计充电次数列
        QStandardItem *countItem = new QStandardItem(QString::number(chargeCount));
        countItem->setTextAlignment(Qt::AlignCenter);
        // 累计充电时长列（如 1240.5h）
        QStandardItem *hoursItem = new QStandardItem(
            QStringLiteral("%1h").arg(QString::number(chargeHours, 'f', 1)));
        hoursItem->setTextAlignment(Qt::AlignCenter);
        // 操作列占位：按钮由 Widget 依据本行的角色数据动态安装
        QStandardItem *actItem = new QStandardItem(QString());
        actItem->setData(pileId, PileIdRole);
        actItem->setData(status, CurrentStatusRole);
        actItem->setTextAlignment(Qt::AlignCenter);

        m_tableModel->appendRow({idItem, stationItem, typeItem, powerItem,
                                 stItem, countItem, hoursItem, actItem});
    }
}

void PileManagementModel::handlePilesReply(QNetworkReply *reply)
{
    // 先取走全部所需信息，再 deleteLater() 释放 reply
    const QNetworkReply::NetworkError netError = reply->error();
    const QString netErrorString = reply->errorString();
    const int httpStatus =
        reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    const QByteArray body = reply->readAll();
    reply->deleteLater();

    const QString apiTag = QStringLiteral("GET /api/v1/admin/piles");

    if (netError != QNetworkReply::NoError) {
        const QString msg = QStringLiteral("%1 网络请求失败 (HTTP %2): %3")
                                .arg(apiTag).arg(httpStatus).arg(netErrorString);
        qWarning() << "[PileManagementModel]" << msg;
        emit errorOccurred(msg);
        return;
    }
    if (httpStatus < 200 || httpStatus >= 300) {
        const QString msg = QStringLiteral("%1 服务器返回异常状态 HTTP %2")
                                .arg(apiTag).arg(httpStatus);
        qWarning().noquote() << "[PileManagementModel]" << msg
                             << "原始响应:" << QString::fromUtf8(body);
        emit errorOccurred(msg);
        return;
    }

    QJsonObject data;
    if (!extractData(body, apiTag, data)) {
        return; // extractData() 内部已 emit errorOccurred
    }
    // 与文档结构不一致时打印原始响应（piles 字段缺失视为异常）
    if (!data.contains(QStringLiteral("piles"))) {
        qWarning().noquote()
            << "[PileManagementModel]" << apiTag
            << "响应缺少 piles 字段, 原始响应:" << QString::fromUtf8(body);
    }

    const int total = data.value(QStringLiteral("total")).toInt(0);
    const int page = data.value(QStringLiteral("page")).toInt(m_page);
    const int pageSize = data.value(QStringLiteral("page_size")).toInt(m_pageSize);
    const QJsonArray piles = data.value(QStringLiteral("piles")).toArray();

    populatePiles(piles);

    qDebug().noquote() << "[PileManagementModel] 充电桩列表更新成功 - 本页:"
                       << piles.size() << "条, 总数:" << total;
    emit pilesReady(piles, total, page, pageSize);
}

void PileManagementModel::handleRestartReply(QNetworkReply *reply, const QString &pileId)
{
    const QNetworkReply::NetworkError netError = reply->error();
    const QString netErrorString = reply->errorString();
    const int httpStatus =
        reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    const QByteArray body = reply->readAll();
    reply->deleteLater();

    const QString apiTag =
        QStringLiteral("POST /api/v1/admin/piles/%1/restart").arg(pileId);

    if (netError != QNetworkReply::NoError) {
        const QString msg = QStringLiteral("%1 网络请求失败 (HTTP %2): %3")
                                .arg(apiTag).arg(httpStatus).arg(netErrorString);
        qWarning() << "[PileManagementModel]" << msg;
        emit errorOccurred(msg);
        return;
    }
    if (httpStatus < 200 || httpStatus >= 300) {
        const QString msg = QStringLiteral("%1 服务器返回异常状态 HTTP %2")
                                .arg(apiTag).arg(httpStatus);
        qWarning().noquote() << "[PileManagementModel]" << msg
                             << "原始响应:" << QString::fromUtf8(body);
        emit errorOccurred(msg);
        return;
    }

    QJsonObject data;
    if (!extractData(body, apiTag, data)) {
        return;
    }

    // 与文档结构不一致时打印原始响应（execution_status 字段缺失视为异常）
    const QString execStatus = data.value(QStringLiteral("execution_status")).toString();
    if (!data.contains(QStringLiteral("execution_status"))) {
        qWarning().noquote()
            << "[PileManagementModel]" << apiTag
            << "响应缺少 execution_status 字段, 原始响应:" << QString::fromUtf8(body);
    }

    if (!execStatus.isEmpty() && execStatus != QStringLiteral("SUCCESS")) {
        const QString msg = QStringLiteral("电桩 %1 重启失败: execution_status=%2")
                                .arg(pileId, execStatus);
        qWarning() << "[PileManagementModel]" << msg;
        emit errorOccurred(msg);
        return;
    }

    const QString msg = QStringLiteral("电桩 %1 重启成功").arg(pileId);
    qDebug().noquote() << "[PileManagementModel]" << msg;
    emit restartSuccess(msg);
}

bool PileManagementModel::extractData(const QByteArray &body,
                                      const QString &apiTag, QJsonObject &outData)
{
    QJsonParseError parseError;
    const QJsonDocument doc = QJsonDocument::fromJson(body, &parseError);
    if (parseError.error != QJsonParseError::NoError || !doc.isObject()) {
        const QString reason = QStringLiteral("%1 响应 JSON 解析失败: %2")
                                   .arg(apiTag, parseError.errorString());
        qWarning().noquote() << "[PileManagementModel]" << reason
                             << "原始响应:" << QString::fromUtf8(body);
        emit errorOccurred(reason);
        return false;
    }

    const QJsonObject root = doc.object();
    const int code = root.value(QStringLiteral("code")).toInt(-1);
    if (code != 0) {
        const QString reason = QStringLiteral("%1 业务错误 code=%2, msg=%3")
                .arg(apiTag)
                .arg(code)
                .arg(root.value(QStringLiteral("msg"))
                         .toString(QStringLiteral("unknown")));
        qWarning() << "[PileManagementModel]" << reason;
        emit errorOccurred(reason);
        return false;
    }

    outData = root.value(QStringLiteral("data")).toObject();
    return true;
}
