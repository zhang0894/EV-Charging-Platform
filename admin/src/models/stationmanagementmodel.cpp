#include "stationmanagementmodel.h"

#include <QDebug>
#include <QDateTime>

#include <QNetworkAccessManager>
#include <QNetworkRequest>
#include <QNetworkReply>
#include <QUrl>
#include <QUrlQuery>
#include <QJsonDocument>
#include <QJsonObject>

StationManagementModel::StationManagementModel(QObject *parent)
    : QObject(parent)
{
    m_tableModel = new QStandardItemModel(this);
    m_tableModel->setColumnCount(ColCount);
    m_tableModel->setHorizontalHeaderLabels({
        tr("站ID"), tr("站名"), tr("地址"),
        tr("总桩数"), tr("在线率"), tr("状态"), tr("操作")
    });

    // 注意：网络请求在 Widget 调用 fetchStations() 后才发起（构造阶段尚无 Token）。
}

QStandardItemModel *StationManagementModel::getModel()
{
    return m_tableModel;
}

void StationManagementModel::setAuthToken(const QString &token)
{
    m_authToken = token.trimmed();
}

// ============================================================================
// 分页查询充电站列表（文档 3.3 节）
//   GET /api/v1/admin/stations?page=&page_size=&name=&status=
// ============================================================================

void StationManagementModel::fetchStations(int page, int pageSize,
                                           const QString &nameFilter,
                                           int statusFilter)
{
    m_page = qMax(1, page);
    m_pageSize = qMax(1, pageSize);
    m_nameFilter = nameFilter.trimmed();
    m_statusFilter = statusFilter;

    ensureNetworkManager();

    QUrl url(m_serverBase + QStringLiteral("/api/v1/admin/stations"));
    QUrlQuery query;
    query.addQueryItem(QStringLiteral("page"), QString::number(m_page));
    query.addQueryItem(QStringLiteral("page_size"), QString::number(m_pageSize));
    if (!m_nameFilter.isEmpty()) {
        query.addQueryItem(QStringLiteral("name"), m_nameFilter);
    }
    if (m_statusFilter >= 1) {
        query.addQueryItem(QStringLiteral("status"), QString::number(m_statusFilter));
    }
    url.setQuery(query);

    qDebug().noquote() << "[StationManagementModel] fetchStations() -"
                       << url.toString();

    QNetworkRequest request(url);
    prepareRequest(&request);

    QNetworkReply *reply = m_networkManager->get(request);
    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        handleStationsReply(reply);
    });
}

// ============================================================================
// 新增模拟电站（仅本地内存）
// ============================================================================

void StationManagementModel::addMockStation(const StationInfo &info)
{
    StationInfo s = info;
    s.station_id = m_nextMockId--;   // 模拟电站 ID 为负数：-1, -2, -3 ...
    s.total_piles = 0;
    s.online_piles = 0;
    s.idle_piles = 0;
    s.online_rate = 0.0;
    s.status = 1;
    s.price_per_kwh = 0.0;
    s.service_fee_per_kwh = 0.0;
    s.overtime_fee_per_15min = 0.0;
    s.created_at = QDateTime::currentMSecsSinceEpoch();

    m_mockStations.append(s);
    qDebug().noquote() << "[StationManagementModel] 新增模拟电站 - ID:"
                       << s.station_id << "站名:" << s.station_name;
}

// ============================================================================
// 充电站上线/下线（文档 3.3 节；真实和模拟统一处理）
//   POST /api/v1/admin/stations/{station_id}/online
//   POST /api/v1/admin/stations/{station_id}/offline
// ============================================================================

void StationManagementModel::setStationStatus(int stationId, int newStatus)
{
    // 模拟电站：只改内存状态，不调用真实 API
    if (stationId < 0) {
        for (StationInfo &s : m_mockStations) {
            if (s.station_id == stationId) {
                s.status = newStatus;
                const QString actionText = (newStatus == 1) ? tr("上线") : tr("下线");
                const QString msg = QStringLiteral("模拟电站 %1（ID: %2）%3成功（仅本地内存，程序退出后自动清除）")
                                        .arg(s.station_name).arg(stationId).arg(actionText);
                qDebug().noquote() << "[StationManagementModel]" << msg;
                emit operationSuccess(msg);
                return;
            }
        }
        const QString msg = QStringLiteral("未找到 ID 为 %1 的模拟电站").arg(stationId);
        qWarning() << "[StationManagementModel]" << msg;
        emit errorOccurred(msg);
        return;
    }

    // 真实电站：POST /online 或 /offline
    ensureNetworkManager();

    const QString action = (newStatus == 1)
        ? QStringLiteral("online") : QStringLiteral("offline");
    const QUrl url(m_serverBase
                   + QStringLiteral("/api/v1/admin/stations/%1/%2").arg(stationId).arg(action));
    QNetworkRequest request(url);
    prepareRequest(&request);

    QNetworkReply *reply = m_networkManager->post(request, QByteArray("{}"));
    connect(reply, &QNetworkReply::finished, this, [this, reply, stationId, newStatus]() {
        handleStatusReply(reply, stationId, newStatus);
    });
}

// ============================================================================
// 单站销售业绩（文档 3.3 节）
//   GET /api/v1/admin/stations/{station_id}/sales-stats?time_range=today|7d|30d
// ============================================================================

void StationManagementModel::fetchStationSales(int stationId, const QString &timeRange)
{
    // 模拟电站无销售数据（Widget 侧已拦截，此处兜底）
    if (stationId < 0) {
        emit errorOccurred(tr("该电站为本地模拟数据，暂无销售记录"));
        return;
    }

    ensureNetworkManager();

    QUrl url(m_serverBase
             + QStringLiteral("/api/v1/admin/stations/%1/sales-stats").arg(stationId));
    QUrlQuery query;
    query.addQueryItem(QStringLiteral("time_range"), timeRange);
    url.setQuery(query);

    qDebug().noquote() << "[StationManagementModel] fetchStationSales() -"
                       << url.toString();

    QNetworkRequest request(url);
    prepareRequest(&request);

    QNetworkReply *reply = m_networkManager->get(request);
    connect(reply, &QNetworkReply::finished, this, [this, reply, stationId]() {
        handleSalesReply(reply, stationId);
    });
}

// ============================================================================
// 内部辅助
// ============================================================================

void StationManagementModel::ensureNetworkManager()
{
    if (!m_networkManager) {
        m_networkManager = new QNetworkAccessManager(this);
    }
}

void StationManagementModel::prepareRequest(QNetworkRequest *request) const
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

QJsonObject StationManagementModel::stationInfoToJson(const StationInfo &info) const
{
    QJsonObject o;
    o.insert(QStringLiteral("station_id"), info.station_id);
    o.insert(QStringLiteral("station_name"), info.station_name);
    o.insert(QStringLiteral("address"), info.address);
    o.insert(QStringLiteral("latitude"), info.latitude);
    o.insert(QStringLiteral("longitude"), info.longitude);
    o.insert(QStringLiteral("total_piles"), info.total_piles);
    o.insert(QStringLiteral("online_piles"), info.online_piles);
    o.insert(QStringLiteral("idle_piles"), info.idle_piles);
    o.insert(QStringLiteral("online_rate"), info.online_rate);
    o.insert(QStringLiteral("status"), info.status);
    o.insert(QStringLiteral("price_per_kwh"), info.price_per_kwh);
    o.insert(QStringLiteral("service_fee_per_kwh"), info.service_fee_per_kwh);
    o.insert(QStringLiteral("overtime_fee_per_15min"), info.overtime_fee_per_15min);
    o.insert(QStringLiteral("created_at"),
             static_cast<double>(info.created_at));
    return o;
}

void StationManagementModel::populateStations(const QJsonArray &stations)
{
    m_tableModel->removeRows(0, m_tableModel->rowCount());

    for (qsizetype i = 0; i < stations.size(); ++i) {
        const QJsonObject s = stations.at(i).toObject();

        const int stationId = s.value(QStringLiteral("station_id")).toInt();
        const QString name = s.value(QStringLiteral("station_name")).toString();
        const QString address = s.value(QStringLiteral("address")).toString();
        const int totalPiles = s.value(QStringLiteral("total_piles")).toInt();
        const double onlineRate = s.value(QStringLiteral("online_rate")).toDouble();
        const int status = s.value(QStringLiteral("status")).toInt();

        // 站ID 列
        QStandardItem *idItem = new QStandardItem(QString::number(stationId));
        idItem->setTextAlignment(Qt::AlignCenter);
        // 站名列
        QStandardItem *nameItem = new QStandardItem(name);
        nameItem->setTextAlignment(Qt::AlignLeft | Qt::AlignVCenter);
        // 地址列
        QStandardItem *addrItem = new QStandardItem(address);
        addrItem->setTextAlignment(Qt::AlignLeft | Qt::AlignVCenter);
        // 总桩数列
        QStandardItem *pilesItem = new QStandardItem(QString::number(totalPiles));
        pilesItem->setTextAlignment(Qt::AlignCenter);
        // 在线率列（保留一位小数）
        QStandardItem *rateItem = new QStandardItem(
            QStringLiteral("%1%").arg(QString::number(onlineRate, 'f', 1)));
        rateItem->setTextAlignment(Qt::AlignCenter);
        // 状态列：1=正常运营（绿） 2=维护中（橙），原始值存 StatusRole
        const bool offline = (status == 2);
        QStandardItem *stItem = new QStandardItem(
            offline ? tr("维护中") : tr("正常运营"));
        stItem->setTextAlignment(Qt::AlignCenter);
        stItem->setForeground(offline ? QColor(0xff, 0x9f, 0x43)
                                      : QColor(0x2e, 0xcc, 0x71));
        stItem->setData(status, StatusRole);
        // 操作列占位：按钮由 Widget 依据本行的角色数据动态安装
        QStandardItem *actItem = new QStandardItem(QString());
        actItem->setData(stationId, StationIdRole);
        actItem->setData(status, StatusRole);
        actItem->setData(name, NameRole);
        actItem->setTextAlignment(Qt::AlignCenter);

        m_tableModel->appendRow({idItem, nameItem, addrItem, pilesItem,
                                 rateItem, stItem, actItem});
    }
}

void StationManagementModel::handleStationsReply(QNetworkReply *reply)
{
    // 先取走全部所需信息，再 deleteLater() 释放 reply
    const QNetworkReply::NetworkError netError = reply->error();
    const QString netErrorString = reply->errorString();
    const int httpStatus =
        reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    const QByteArray body = reply->readAll();
    reply->deleteLater();

    const QString apiTag = QStringLiteral("GET /api/v1/admin/stations");

    if (netError != QNetworkReply::NoError) {
        const QString msg = QStringLiteral("%1 网络请求失败 (HTTP %2): %3")
                                .arg(apiTag).arg(httpStatus).arg(netErrorString);
        qWarning() << "[StationManagementModel]" << msg;
        emit errorOccurred(msg);
        return;
    }
    if (httpStatus < 200 || httpStatus >= 300) {
        const QString msg = QStringLiteral("%1 服务器返回异常状态 HTTP %2")
                                .arg(apiTag).arg(httpStatus);
        qWarning().noquote() << "[StationManagementModel]" << msg
                             << "原始响应:" << QString::fromUtf8(body);
        emit errorOccurred(msg);
        return;
    }

    QJsonObject data;
    if (!extractData(body, apiTag, data)) {
        return; // extractData() 内部已 emit errorOccurred
    }
    // 与文档结构不一致时打印原始响应（stations 字段缺失视为异常）
    if (!data.contains(QStringLiteral("stations"))) {
        qWarning().noquote()
            << "[StationManagementModel]" << apiTag
            << "响应缺少 stations 字段, 原始响应:" << QString::fromUtf8(body);
    }

    const int total = data.value(QStringLiteral("total")).toInt(0);
    const int page = data.value(QStringLiteral("page")).toInt(m_page);
    const int pageSize = data.value(QStringLiteral("page_size")).toInt(m_pageSize);
    const QJsonArray realStations = data.value(QStringLiteral("stations")).toArray();

    // 合并：模拟数据排在真实数据之前，total = 真实总数 + 模拟数量
    QJsonArray merged;
    for (const StationInfo &s : m_mockStations) {
        merged.append(stationInfoToJson(s));
    }
    for (qsizetype i = 0; i < realStations.size(); ++i) {
        merged.append(realStations.at(i));
    }

    populateStations(merged);

    qDebug().noquote() << "[StationManagementModel] 充电站列表更新成功 - 模拟:"
                       << m_mockStations.size() << "条, 本页真实:"
                       << realStations.size() << "条, 总数:" << (total + m_mockStations.size());
    emit stationsReady(merged, total + m_mockStations.size(), page, pageSize);
}

void StationManagementModel::handleSalesReply(QNetworkReply *reply, int stationId)
{
    const QNetworkReply::NetworkError netError = reply->error();
    const QString netErrorString = reply->errorString();
    const int httpStatus =
        reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    const QByteArray body = reply->readAll();
    reply->deleteLater();

    const QString apiTag =
        QStringLiteral("GET /api/v1/admin/stations/%1/sales-stats").arg(stationId);

    if (netError != QNetworkReply::NoError) {
        const QString msg = QStringLiteral("%1 网络请求失败 (HTTP %2): %3")
                                .arg(apiTag).arg(httpStatus).arg(netErrorString);
        qWarning() << "[StationManagementModel]" << msg;
        emit errorOccurred(msg);
        return;
    }
    if (httpStatus < 200 || httpStatus >= 300) {
        const QString msg = QStringLiteral("%1 服务器返回异常状态 HTTP %2")
                                .arg(apiTag).arg(httpStatus);
        qWarning().noquote() << "[StationManagementModel]" << msg
                             << "原始响应:" << QString::fromUtf8(body);
        emit errorOccurred(msg);
        return;
    }

    QJsonObject data;
    if (!extractData(body, apiTag, data)) {
        return;
    }
    // 与文档结构不一致时打印原始响应（timeline 字段缺失视为异常）
    if (!data.contains(QStringLiteral("timeline"))) {
        qWarning().noquote()
            << "[StationManagementModel]" << apiTag
            << "响应缺少 timeline 字段, 原始响应:" << QString::fromUtf8(body);
    }

    qDebug().noquote() << "[StationManagementModel] 销售业绩更新成功 - 电站:"
                       << stationId;
    emit salesDataReady(data, stationId);
}

void StationManagementModel::handleStatusReply(QNetworkReply *reply,
                                               int stationId, int newStatus)
{
    const QNetworkReply::NetworkError netError = reply->error();
    const QString netErrorString = reply->errorString();
    const int httpStatus =
        reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    const QByteArray body = reply->readAll();
    reply->deleteLater();

    const QString actionText = (newStatus == 1) ? tr("上线") : tr("下线");
    const QString apiTag = QStringLiteral("POST /api/v1/admin/stations/%1/%2")
                               .arg(stationId)
                               .arg(newStatus == 1 ? QStringLiteral("online")
                                                   : QStringLiteral("offline"));

    if (netError != QNetworkReply::NoError) {
        const QString msg = QStringLiteral("%1 网络请求失败 (HTTP %2): %3")
                                .arg(apiTag).arg(httpStatus).arg(netErrorString);
        qWarning() << "[StationManagementModel]" << msg;
        emit errorOccurred(msg);
        return;
    }
    if (httpStatus < 200 || httpStatus >= 300) {
        const QString msg = QStringLiteral("%1 服务器返回异常状态 HTTP %2")
                                .arg(apiTag).arg(httpStatus);
        qWarning().noquote() << "[StationManagementModel]" << msg
                             << "原始响应:" << QString::fromUtf8(body);
        emit errorOccurred(msg);
        return;
    }

    QJsonObject data;
    if (!extractData(body, apiTag, data)) {
        return;
    }

    QString msg = QStringLiteral("充电站 %1%2成功").arg(stationId).arg(actionText);
    // 下线响应可能携带 terminated_orders（被终止的进行中订单数）
    const int terminated = data.value(QStringLiteral("terminated_orders")).toInt(0);
    if (newStatus == 2 && terminated > 0) {
        msg += QStringLiteral("（终止进行中订单 %1 笔）").arg(terminated);
    }
    qDebug().noquote() << "[StationManagementModel]" << msg;
    emit operationSuccess(msg);
}

bool StationManagementModel::extractData(const QByteArray &body,
                                         const QString &apiTag, QJsonObject &outData)
{
    QJsonParseError parseError;
    const QJsonDocument doc = QJsonDocument::fromJson(body, &parseError);
    if (parseError.error != QJsonParseError::NoError || !doc.isObject()) {
        const QString reason = QStringLiteral("%1 响应 JSON 解析失败: %2")
                                   .arg(apiTag, parseError.errorString());
        qWarning().noquote() << "[StationManagementModel]" << reason
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
        qWarning() << "[StationManagementModel]" << reason;
        emit errorOccurred(reason);
        return false;
    }

    outData = root.value(QStringLiteral("data")).toObject();
    return true;
}
