#include "pilemanagementmodel.h"
#include "tokenmanager.h"

#include <QDebug>

#include <QNetworkRequest>
#include <QNetworkReply>
#include <QUrl>
#include <QUrlQuery>
#include <QJsonDocument>
#include <QJsonObject>
#include <QColor>

#include <QApplication>
#include <QFontMetrics>
#include <QTableView>
#include <QWidget>

namespace {
// 长文本单元格处理：按表格列宽以右侧省略号（Qt::ElideRight）截断显示，
// 完整文本通过 setToolTip() 悬停展示；文本短于可用宽度时原样显示、不设 ToolTip。
// scope 为 Model 的父对象（管理页 Widget），viewName 为目标 QTableView 的 objectName；
// 若视图尚未显示（数据先于页面打开到达），退化为“超过 30 个字符才截断”的规则。
// 注：本 Model 在充电站详情弹窗中也有实例（父对象为 StationManagementWidget，
// 其下不存在 "pileTable" 视图），此时自动走 30 字符降级规则，行为安全。
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
    if (status == QStringLiteral("IDLE"))     return QColor(0x16, 0xa3, 0x4a); // 绿
    if (status == QStringLiteral("CHARGING")) return QColor(0x2b, 0x7b, 0xff); // 蓝
    if (status == QStringLiteral("FAULT"))    return QColor(0xdc, 0x26, 0x26); // 红
    if (status == QStringLiteral("OFFLINE"))  return QColor(0x8a, 0x9a, 0xa8); // 灰
    return QColor(0x1a, 0x23, 0x32); // 默认主文字色
}

QString PileManagementModel::pileTypeText(const QString &type)
{
    if (type == QStringLiteral("FAST")) return QStringLiteral("快充");
    if (type == QStringLiteral("SLOW")) return QStringLiteral("慢充");
    return type;
}

QColor PileManagementModel::pileTypeColor(const QString &type)
{
    if (type == QStringLiteral("FAST")) return QColor(0x2b, 0x7b, 0xff); // 蓝
    if (type == QStringLiteral("SLOW")) return QColor(0x8a, 0x9a, 0xa8); // 灰
    return QColor(0x1a, 0x23, 0x32);
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
    // Token 由 TokenManager 单例统一管理（登录后由 main.cpp 设置），
    // Model 不再保存 Token；首次拉取由 Widget 触发。
    Q_UNUSED(token);
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

    // 新接口：GET /api/v1/piles（旧 /api/v1/admin/piles 已移除，返回 404）
    QUrl url(m_serverBase + QStringLiteral("/api/v1/piles"));
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

    QStringList conds;
    if (stationId > 0)   conds << tr("电站ID %1").arg(stationId);
    if (!m_statusFilter.isEmpty()) conds << pileStatusText(m_statusFilter);
    if (!m_typeFilter.isEmpty())   conds << pileTypeText(m_typeFilter);
    emit logRequested(conds.isEmpty()
        ? tr("充电桩列表查询")
        : tr("充电桩列表查询：%1").arg(conds.join(QStringLiteral("，"))));

    QNetworkRequest request(url);
    // prepareRequest 由 TokenManager::get 内部统一处理
    TokenManager::instance()->get(request, [this](QNetworkReply *reply) {
        handlePilesReply(reply);
    });
}

// ============================================================================
// 远程重启（文档 3.4 节）
//   POST /api/v1/admin/piles/{pile_id}/restart   body: {"reason": "管理员远程重启"}
// ============================================================================

void PileManagementModel::restartPile(const QString &pileId)
{
    const QUrl url(m_serverBase
                   + QStringLiteral("/api/v1/admin/piles/%1/restart").arg(pileId));
    QNetworkRequest request(url);
    // prepareRequest 由 TokenManager::post 内部统一处理

    const QByteArray body = QJsonDocument(
        QJsonObject{{QStringLiteral("reason"), QStringLiteral("管理员远程重启")}})
        .toJson(QJsonDocument::Compact);

    qDebug().noquote() << "[PileManagementModel] restartPile() -" << url.toString();

    // 日志：远程重启操作（已发起）
    emit logRequested(tr("充电桩重启：电桩 %1（已发起）").arg(pileId));

    TokenManager::instance()->post(request, body,
        [this, pileId](QNetworkReply *reply) {
            handleRestartReply(reply, pileId);
        });
}

// ============================================================================
// 内部辅助
// ============================================================================

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
        // 所属电站列（超长省略号截断，悬停显示电站全称）
        QStandardItem *stationItem = new QStandardItem();
        stationItem->setTextAlignment(Qt::AlignLeft | Qt::AlignVCenter);
        applyElidedCellText(stationItem, stationName, parent(), "pileTable",
                            static_cast<int>(StationNameCol));
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

    const QString apiTag = QStringLiteral("GET /api/v1/piles");

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
