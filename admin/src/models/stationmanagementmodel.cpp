#include "stationmanagementmodel.h"

#include "tokenmanager.h"

#include <QDebug>
#include <QDateTime>

#include <QNetworkRequest>
#include <QNetworkReply>
#include <QUrl>
#include <QUrlQuery>
#include <QJsonDocument>
#include <QJsonObject>

#include <QApplication>
#include <QFontMetrics>
#include <QTableView>
#include <QWidget>

namespace {
// 长文本单元格处理：按表格列宽以右侧省略号（Qt::ElideRight）截断显示，
// 完整文本通过 setToolTip() 悬停展示；文本短于可用宽度时原样显示、不设 ToolTip。
// scope 为 Model 的父对象（管理页 Widget），viewName 为目标 QTableView 的 objectName；
// 若视图尚未显示（数据先于页面打开到达），退化为“超过 30 个字符才截断”的规则。
void applyElidedCellText(QStandardItem *item, const QString &fullText,
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
} // namespace

StationManagementModel::StationManagementModel(QObject *parent)
    : QObject(parent)
{
    m_tableModel = new QStandardItemModel(this);
    m_tableModel->setColumnCount(ColCount);
    m_tableModel->setHorizontalHeaderLabels({
        tr("站ID"), tr("站名"), tr("地址"), tr("经纬度"),
        tr("总桩数"), tr("可用率"), tr("状态"), tr("操作")
    });

    // 注意：网络请求在 Widget 调用 fetchStations() 后才发起（构造阶段尚无 Token）。
}

QStandardItemModel *StationManagementModel::getModel()
{
    return m_tableModel;
}

void StationManagementModel::setAuthToken(const QString &token)
{
    // Token 由 TokenManager 单例统一管理（登录后由 main.cpp 设置），
    // Model 不再保存 Token；首次拉取由 Widget 触发。
    Q_UNUSED(token);
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

    QUrl url(m_serverBase + QStringLiteral("/api/v1/stations/inquire"));
    QUrlQuery query;
    query.addQueryItem(QStringLiteral("page"), QString::number(m_page));
    query.addQueryItem(QStringLiteral("page_size"), QString::number(m_pageSize));
    if (!m_nameFilter.isEmpty()) {
        query.addQueryItem(QStringLiteral("name"), m_nameFilter);
    }
    // 状态筛选：后端新接口已支持 status 参数（1=营业中/ONLINE，2=暂停营业/OFFLINE）；
    // "全部"(m_statusFilter < 1) 时不携带该参数。
    if (m_statusFilter >= 1) {
        query.addQueryItem(QStringLiteral("status"), QString::number(m_statusFilter));
    }
    url.setQuery(query);

    qDebug().noquote() << "[StationManagementModel] fetchStations() -"
                       << url.toString();

    QStringList conds;
    if (!m_nameFilter.isEmpty())   conds << tr("站名包含“%1”").arg(m_nameFilter);
    if (m_statusFilter >= 1)      conds << (m_statusFilter == 1 ? tr("正常运营") : tr("暂停营业"));
    emit logRequested(conds.isEmpty()
        ? tr("充电站列表查询")
        : tr("充电站列表查询：%1").arg(conds.join(QStringLiteral("，"))));

    QNetworkRequest request(url);
    // prepareRequest 由 TokenManager 内部完成（注入 Authorization 头）；
    // 401/40001/40002 自动刷新 Token 并重试，回调最终收到重试后的 reply。
    TokenManager::instance()->get(request, [this](QNetworkReply *reply) {
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
    // 注：total_piles 保留用户在新增对话框中填写的电桩数量（不填默认为 0），
    // 不可在此清零；模拟电站无真实电桩在线，故在线/闲置数固定为 0。
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
    // 日志：上下线远程操作（真实电站与模拟电站统一在此记录，已发起）
    emit logRequested(tr("充电站%1操作：站ID %2（已发起）")
                          .arg(newStatus == 1 ? tr("上线") : tr("下线"))
                          .arg(stationId));

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
    const QString action = (newStatus == 1)
        ? QStringLiteral("online") : QStringLiteral("offline");
    const QUrl url(m_serverBase
                   + QStringLiteral("/api/v1/admin/stations/%1/%2").arg(stationId).arg(action));
    QNetworkRequest request(url);

    TokenManager::instance()->post(request, QByteArray("{}"),
        [this, stationId, newStatus](QNetworkReply *reply) {
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

    QUrl url(m_serverBase
             + QStringLiteral("/api/v1/admin/stations/%1/sales").arg(stationId));
    QUrlQuery query;
    query.addQueryItem(QStringLiteral("time_range"), timeRange);
    url.setQuery(query);

    qDebug().noquote() << "[StationManagementModel] fetchStationSales() -"
                       << url.toString();

    // 日志：充电站详情弹窗销售数据查询（time_range: today/7d/30d）
    emit logRequested(tr("充电站销售数据查询：站ID %1，范围 %2")
                          .arg(stationId).arg(timeRange));

    QNetworkRequest request(url);
    TokenManager::instance()->get(request, [this, stationId](QNetworkReply *reply) {
        handleSalesReply(reply, stationId);
    });
}

// ============================================================================
// 内部辅助
// ============================================================================

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
        const double longitude = s.value(QStringLiteral("longitude")).toDouble();
        const double latitude = s.value(QStringLiteral("latitude")).toDouble();
        const int totalPiles = s.value(QStringLiteral("total_piles")).toInt();
        const int idlePiles = s.value(QStringLiteral("idle_piles")).toInt();
        // 新接口无 online_rate 字段：可用率 = idle_piles / total_piles * 100；
        // 模拟电站 idle_piles=0（无论用户填多少电桩数量），可用率显示 0.0%。
        double availRate = 0.0;
        if (totalPiles > 0) {
            availRate = idlePiles * 100.0 / totalPiles;
        } else if (s.contains(QStringLiteral("online_rate"))) {
            // 兼容旧字段（模拟电站 JSON 中仍为 0.0）
            availRate = s.value(QStringLiteral("online_rate")).toDouble();
        }
        // 状态字段：统一使用 is_online（bool），兼容旧字段 status / station_status
        int status = 1;
        if (s.contains(QStringLiteral("is_online"))) {
            status = s.value(QStringLiteral("is_online")).toBool(true) ? 1 : 2;
        } else if (s.contains(QStringLiteral("station_status"))) {
            status = s.value(QStringLiteral("station_status")).toInt(1);
        } else if (s.contains(QStringLiteral("status"))) {
            status = s.value(QStringLiteral("status")).toInt(1);
        }

        // 站ID 列
        QStandardItem *idItem = new QStandardItem(QString::number(stationId));
        idItem->setTextAlignment(Qt::AlignCenter);
        // 站名列（超长省略号截断，悬停显示电站全称）
        QStandardItem *nameItem = new QStandardItem();
        nameItem->setTextAlignment(Qt::AlignLeft | Qt::AlignVCenter);
        applyElidedCellText(nameItem, name, parent(), "stationTable",
                            static_cast<int>(NameCol));
        // 地址列（超长省略号截断，悬停显示完整地址）
        QStandardItem *addrItem = new QStandardItem();
        addrItem->setTextAlignment(Qt::AlignLeft | Qt::AlignVCenter);
        applyElidedCellText(addrItem, address, parent(), "stationTable",
                            static_cast<int>(AddressCol));
        // 经纬度列：格式 "经度, 纬度"（各保留 6 位小数）；
        // 模拟电站新增时未填写经纬度（值为 0）则显示 "-"
        QStandardItem *lngLatItem = new QStandardItem();
        lngLatItem->setTextAlignment(Qt::AlignCenter);
        if (longitude != 0.0 || latitude != 0.0) {
            lngLatItem->setText(QStringLiteral("%1, %2")
                                    .arg(QString::number(longitude, 'f', 6))
                                    .arg(QString::number(latitude, 'f', 6)));
        } else {
            lngLatItem->setText(QStringLiteral("-"));
        }
        // 总桩数列
        QStandardItem *pilesItem = new QStandardItem(QString::number(totalPiles));
        pilesItem->setTextAlignment(Qt::AlignCenter);
        // 可用率列（idle_piles/total_piles，保留一位小数）
        QStandardItem *rateItem = new QStandardItem(
            QStringLiteral("%1%").arg(QString::number(availRate, 'f', 1)));
        rateItem->setTextAlignment(Qt::AlignCenter);
        // 状态列：1=正常运营/营业中（绿） 2=暂停营业/下线（橙），原始值存 StatusRole
        const bool offline = (status == 2);
        QStandardItem *stItem = new QStandardItem(
            offline ? tr("暂停营业") : tr("正常运营"));
        stItem->setTextAlignment(Qt::AlignCenter);
        stItem->setForeground(offline ? QColor(0xd9, 0x77, 0x06)
                                      : QColor(0x16, 0xa3, 0x4a));
        stItem->setData(status, StatusRole);
        // 操作列占位：按钮由 Widget 依据本行的角色数据动态安装
        QStandardItem *actItem = new QStandardItem(QString());
        actItem->setData(stationId, StationIdRole);
        actItem->setData(status, StatusRole);
        actItem->setData(name, NameRole);
        actItem->setTextAlignment(Qt::AlignCenter);

        m_tableModel->appendRow({idItem, nameItem, addrItem, lngLatItem,
                                 pilesItem, rateItem, stItem, actItem});
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

    const QString apiTag = QStringLiteral("GET /api/v1/stations/inquire");

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
        QStringLiteral("GET /api/v1/admin/stations/%1/sales").arg(stationId);

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
