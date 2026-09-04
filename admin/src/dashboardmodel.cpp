#include "dashboardmodel.h"

#include <QDebug>
#include <QLocale>

#include <QNetworkAccessManager>
#include <QNetworkRequest>
#include <QNetworkReply>
#include <QUrl>
#include <QUrlQuery>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>

DashboardModel::DashboardModel(QObject *parent)
    : QStandardItemModel(parent)
{
    // 设置表头（日期、营收、电量、订单数）
    setColumnCount(ColCount);
    setHorizontalHeaderLabels({ tr("日期"), tr("营收"), tr("电量"), tr("订单数") });

    // 先用占位数据填充（服务器响应到达前 / 请求失败时界面仍有数据可展示）
    initMockData();

    // 默认加载近7日数据集，填充到 Model 行列
    loadDataset(Last7Days);

    // 注意：网络请求在 setAuthToken() 设置合法 Token 后自动触发，
    //       构造阶段不发起请求（此时尚无管理员 Token）。
}

const DashboardModel::Summary &DashboardModel::summary() const
{
    return m_summary;
}

DashboardModel::TimeRange DashboardModel::currentRange() const
{
    return m_currentRange;
}

void DashboardModel::loadDataset(TimeRange range)
{
    m_currentRange = range;

    // 清空旧行，准备填充新数据集
    removeRows(0, rowCount());

    const QVector<TrendPoint> &data = (range == Last7Days) ? m_data7d : m_data30d;

    for (const TrendPoint &p : data) {
        QList<QStandardItem *> row;
        // 日期列（显示文本 = 原始日期；UserRole 同时存原始日期字符串）
        QStandardItem *dateItem = new QStandardItem(p.date);
        dateItem->setTextAlignment(Qt::AlignCenter);
        dateItem->setData(p.date, Qt::UserRole);
        // 营收列：显示千分位格式化文本；UserRole 存原始 double 便于图表读取
        QStandardItem *revItem = new QStandardItem(
            QLocale(QLocale::Chinese).toString(p.revenue, 'f', 2));
        revItem->setTextAlignment(Qt::AlignRight | Qt::AlignVCenter);
        revItem->setData(p.revenue, Qt::UserRole);
        // 电量列（度，保留2位）
        QStandardItem *engItem = new QStandardItem(
            QLocale(QLocale::Chinese).toString(p.energy_kwh, 'f', 2));
        engItem->setTextAlignment(Qt::AlignRight | Qt::AlignVCenter);
        engItem->setData(p.energy_kwh, Qt::UserRole);
        // 订单数列
        QStandardItem *ordItem = new QStandardItem(QString::number(p.order_count));
        ordItem->setTextAlignment(Qt::AlignRight | Qt::AlignVCenter);
        ordItem->setData(p.order_count, Qt::UserRole);

        row << dateItem << revItem << engItem << ordItem;
        appendRow(row);
    }
}

void DashboardModel::loadFromServer()
{
    // 【备用接口】保留：实际网络请求由 fetchData() 完成，本方法仅打印日志。
    qDebug() << "[DashboardModel] loadFromServer() called - 备用接口，"
                "实际网络请求请调用 fetchData()（GET /api/v1/admin/dashboard/summary 与 "
                "/api/v1/admin/dashboard/revenue-trend）。";
}

// ============================================================================
// 以下为服务器数据拉取实现（《端口设计文档.md》3.2 节）
//   GET /api/v1/admin/dashboard/summary        运营核心指标看板
//   GET /api/v1/admin/dashboard/revenue-trend  销售业绩与营收趋势（days=7/30）
// 统一响应信封：{ "code": 0, "msg": "success", "data": {...}, "timestamp": ... }
// ============================================================================

void DashboardModel::fetchData()
{
    ensureNetworkManager();

    qDebug().noquote() << "[DashboardModel] fetchData() - 开始从服务器拉取看板数据:"
                       << m_serverBase;

    // 并发发起 3 个 GET 请求，各自独立处理响应、独立发信号
    requestSummary();
    requestTrend(Last7Days);
    requestTrend(Last30Days);
}

void DashboardModel::setAuthToken(const QString &token)
{
    m_authToken = token.trimmed();
    // Token 设置后自动发起数据拉取（登录成功 → setAuthToken → fetchData）
    if (!m_authToken.isEmpty()) {
        fetchData();
    }
}

void DashboardModel::ensureNetworkManager()
{
    if (!m_networkManager) {
        // 以 this 为 parent，Model 析构时自动释放
        m_networkManager = new QNetworkAccessManager(this);
    }
}

void DashboardModel::prepareRequest(QNetworkRequest *request) const
{
    request->setHeader(QNetworkRequest::ContentTypeHeader,
                       QStringLiteral("application/json"));
    request->setRawHeader("Accept", "application/json");
    // 文档 1.3 节：受保护接口需携带 Bearer Token；未设置 Token 时不带头（本地联调用）
    if (!m_authToken.isEmpty()) {
        request->setRawHeader("Authorization",
                              (QStringLiteral("Bearer ") + m_authToken).toUtf8());
    }
}

void DashboardModel::requestSummary()
{
    const QUrl url(m_serverBase + QStringLiteral("/api/v1/admin/dashboard/summary"));
    QNetworkRequest request(url);
    prepareRequest(&request);

    QNetworkReply *reply = m_networkManager->get(request);
    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        handleSummaryReply(reply);
    });
}

void DashboardModel::requestTrend(TimeRange range)
{
    QUrl url(m_serverBase + QStringLiteral("/api/v1/admin/dashboard/revenue-trend"));
    QUrlQuery query;
    query.addQueryItem(QStringLiteral("days"), QString::number(static_cast<int>(range)));
    url.setQuery(query);

    QNetworkRequest request(url);
    prepareRequest(&request);

    QNetworkReply *reply = m_networkManager->get(request);
    connect(reply, &QNetworkReply::finished, this, [this, reply, range]() {
        handleTrendReply(reply, range);
    });
}

void DashboardModel::handleSummaryReply(QNetworkReply *reply)
{
    // 先取走全部所需信息，再 deleteLater() 释放 reply
    const QNetworkReply::NetworkError netError = reply->error();
    const QString netErrorString = reply->errorString();
    const int httpStatus =
        reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    const QByteArray body = reply->readAll();
    reply->deleteLater();

    const QString apiTag = QStringLiteral("GET /api/v1/admin/dashboard/summary");

    if (netError != QNetworkReply::NoError) {
        const QString reason =
            QStringLiteral("%1 网络请求失败 (HTTP %2): %3")
                .arg(apiTag).arg(httpStatus).arg(netErrorString);
        qWarning() << "[DashboardModel]" << reason;
        emit fetchFailed(reason);
        return;
    }

    QJsonObject data;
    if (!extractData(body, apiTag, data)) {
        return; // extractData() 内部已 emit fetchFailed
    }

    // 字段与文档 3.2.1 节 data 结构一一对应；金额分用 toVariant().toLongLong() 精确解析
    Summary s;
    s.today_revenue            = data.value("today_revenue").toDouble();
    s.today_revenue_cents      = static_cast<qint64>(
        data.value("today_revenue_cents").toVariant().toLongLong());
    s.month_revenue            = data.value("month_revenue").toDouble();
    s.month_revenue_cents      = static_cast<qint64>(
        data.value("month_revenue_cents").toVariant().toLongLong());
    s.total_revenue            = data.value("total_revenue").toDouble();
    s.total_revenue_cents      = static_cast<qint64>(
        data.value("total_revenue_cents").toVariant().toLongLong());
    s.today_energy_kwh         = data.value("today_energy_kwh").toDouble();
    s.today_order_count        = data.value("today_order_count").toInt();
    s.total_user_count         = data.value("total_user_count").toInt();
    s.active_charging_sessions = data.value("active_charging_sessions").toInt();

    m_summary = s;

    qDebug().noquote() << "[DashboardModel] 核心指标更新成功 - 今日营收:"
                       << s.today_revenue << "元, 今日订单:"
                       << s.today_order_count << "单, 总用户:"
                       << s.total_user_count << "人";
    emit summaryChanged();
}

void DashboardModel::handleTrendReply(QNetworkReply *reply, TimeRange range)
{
    const QNetworkReply::NetworkError netError = reply->error();
    const QString netErrorString = reply->errorString();
    const int httpStatus =
        reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    const QByteArray body = reply->readAll();
    reply->deleteLater();

    const QString apiTag =
        QStringLiteral("GET /api/v1/admin/dashboard/revenue-trend?days=%1")
            .arg(static_cast<int>(range));

    if (netError != QNetworkReply::NoError) {
        const QString reason =
            QStringLiteral("%1 网络请求失败 (HTTP %2): %3")
                .arg(apiTag).arg(httpStatus).arg(netErrorString);
        qWarning() << "[DashboardModel]" << reason;
        emit fetchFailed(reason);
        return;
    }

    QJsonObject data;
    if (!extractData(body, apiTag, data)) {
        return;
    }

    // 文档 3.2.2 节 data：dates / revenue_series / energy_kwh_series / order_count_series
    const QJsonArray dates        = data.value("dates").toArray();
    const QJsonArray revenueArr   = data.value("revenue_series").toArray();
    const QJsonArray energyArr    = data.value("energy_kwh_series").toArray();
    const QJsonArray orderCountArr = data.value("order_count_series").toArray();

    // 四条序列按最短长度对齐，防止越界
    const qsizetype pointCount =
        qMin(qMin(dates.size(), revenueArr.size()),
             qMin(energyArr.size(), orderCountArr.size()));
    if (pointCount <= 0) {
        const QString reason =
            QStringLiteral("%1 返回数据为空或各序列长度不一致").arg(apiTag);
        qWarning() << "[DashboardModel]" << reason;
        emit fetchFailed(reason);
        return;
    }

    QVector<TrendPoint> points;
    points.reserve(static_cast<int>(pointCount));
    for (qsizetype i = 0; i < pointCount; ++i) {
        TrendPoint p;
        p.date        = dates.at(i).toString();
        p.revenue     = revenueArr.at(i).toDouble();
        p.energy_kwh  = energyArr.at(i).toDouble();
        p.order_count = orderCountArr.at(i).toInt();
        points.append(p);
    }

    if (range == Last7Days) {
        m_data7d = points;
    } else {
        m_data30d = points;
    }

    // 若到达的正是当前展示的数据集，立即重建 Model 行列，保证 View 读到最新数据
    if (m_currentRange == range) {
        loadDataset(range);
    }

    qDebug().noquote() << "[DashboardModel] 营收趋势更新成功 -" << apiTag
                       << "," << pointCount << "个数据点";
    emit trendDataChanged();
}

bool DashboardModel::extractData(const QByteArray &body, const QString &apiTag,
                                 QJsonObject &outData)
{
    QJsonParseError parseError;
    const QJsonDocument doc = QJsonDocument::fromJson(body, &parseError);
    if (parseError.error != QJsonParseError::NoError || !doc.isObject()) {
        const QString reason =
            QStringLiteral("%1 响应 JSON 解析失败: %2")
                .arg(apiTag, parseError.errorString());
        qWarning() << "[DashboardModel]" << reason;
        emit fetchFailed(reason);
        return false;
    }

    const QJsonObject root = doc.object();
    const int code = root.value("code").toInt(-1);
    if (code != 0) {
        const QString reason =
            QStringLiteral("%1 业务错误 code=%2, msg=%3")
                .arg(apiTag)
                .arg(code)
                .arg(root.value("msg").toString(QStringLiteral("unknown")));
        qWarning() << "[DashboardModel]" << reason;
        emit fetchFailed(reason);
        return false;
    }

    outData = root.value("data").toObject();
    return true;
}

void DashboardModel::initMockData()
{
    // ---------------- 1. 核心指标汇总（参考 3.2 节 summary 返回结构） ----------------
    m_summary.today_revenue        = 12850.60;   // 今日营收（元）
    m_summary.today_revenue_cents  = 1285060;    // 今日营收（分）
    m_summary.month_revenue        = 348920.00;  // 本月营收（元）
    m_summary.month_revenue_cents  = 34892000;   // 本月营收（分）
    m_summary.total_revenue        = 2189400.50; // 总营收（元）
    m_summary.total_revenue_cents  = 218940050;  // 总营收（分）
    m_summary.today_energy_kwh     = 8750.40;   // 今日电量（度）
    m_summary.today_order_count     = 312;        // 今日订单数
    m_summary.total_user_count      = 5280;       // 平台总用户数
    m_summary.active_charging_sessions = 48;      // 进行中充电会话

    // ---------------- 2. 近7日数据集（参考 3.2 节 revenue-trend LAST_7_DAYS） ----
    // dates / revenue_series / energy_kwh_series / order_count_series 与文档示例一致
    const QDate today = QDate::currentDate();
    const QStringList dates7 = {
        today.addDays(-6).toString("yyyy-MM-dd"),
        today.addDays(-5).toString("yyyy-MM-dd"),
        today.addDays(-4).toString("yyyy-MM-dd"),
        today.addDays(-3).toString("yyyy-MM-dd"),
        today.addDays(-2).toString("yyyy-MM-dd"),
        today.addDays(-1).toString("yyyy-MM-dd"),
        today.toString("yyyy-MM-dd")
    };
    const QVector<double> rev7    = {10240.5, 11500.0, 14200.8, 15800.0, 13400.2, 12900.0, 12850.6};
    const QVector<double> eng7    = {6900.0, 7800.2, 9500.4, 10600.0, 9100.5, 8700.0, 8750.4};
    const QVector<int>    ord7    = {250, 278, 340, 380, 315, 305, 312};

    m_data7d.clear();
    m_data7d.reserve(7);
    for (int i = 0; i < 7; ++i) {
        TrendPoint p;
        p.date        = dates7.at(i);
        p.revenue     = rev7.at(i);
        p.energy_kwh  = eng7.at(i);
        p.order_count = ord7.at(i);
        m_data7d.append(p);
    }

    // ---------------- 3. 近30日数据集 ----------------
    // 前23天为合理波动的假数据；后7天复用7日数据集，保证尾部与7日一致。
    m_data30d.clear();
    m_data30d.reserve(30);

    // 前23天营收（元），围绕 10000~14500 区间波动
    const QVector<double> revEarly = {
        10800.0, 11200.0,  9800.0, 12500.0, 13000.0, 11900.0, 11000.0,
        13400.0, 14000.0, 12800.0, 11500.0, 10900.0, 12200.0, 13600.0,
        12890.0, 11700.0, 12400.0, 13100.0, 11950.0, 12700.0, 13300.0,
        12100.0, 11500.0
    };

    // 由营收按近似比率派生电量(≈0.675)与订单数(≈0.024)，构成四维度数据
    for (int i = 0; i < 23; ++i) {
        TrendPoint p;
        const double r = revEarly.at(i);
        p.date        = today.addDays(-(29 - i)).toString("yyyy-MM-dd");
        p.revenue     = r;
        p.energy_kwh  = r * 0.675;
        p.order_count = qRound(r * 0.024);
        m_data30d.append(p);
    }
    // 尾部7天直接复用7日数据，保证 7日/30日 在重叠区间数据一致
    for (const TrendPoint &p : m_data7d) {
        m_data30d.append(p);
    }
}
