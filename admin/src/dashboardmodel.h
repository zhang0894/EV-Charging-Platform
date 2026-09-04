#ifndef DASHBOARDMODEL_H
#define DASHBOARDMODEL_H

#include <QStandardItemModel>
#include <QDate>
#include <QVector>
#include <QString>

QT_BEGIN_NAMESPACE
class QNetworkAccessManager;
class QNetworkReply;
class QNetworkRequest;
class QByteArray;
class QJsonObject;
QT_END_NAMESPACE

/**
 * @brief 销售业绩看板数据模型
 *
 * 继承自 QStandardItemModel，作为 DashboardWidget 的唯一数据源。
 * 所有界面展示数据（核心指标卡片、折线图）均从本 Model 读取，UI 层不硬编码数据。
 *
 * 字段命名与《端口设计文档.md》3.2 节保持一致：
 *   - today_revenue / month_revenue / total_revenue（元）
 *   - revenue_series / energy_kwh_series / order_count_series / dates
 *
 * 数据来源：
 *   - fetchData() 通过 QNetworkAccessManager 向后台发起 HTTP 请求：
 *       GET http://127.0.0.1:8080/api/v1/admin/dashboard/summary        （核心指标）
 *       GET http://127.0.0.1:8080/api/v1/admin/dashboard/revenue-trend  （营收趋势，days=7/30）
 *     收到 JSON 响应（统一信封 {code,msg,data,timestamp}）后解析并更新内部数据，
 *     然后通过 summaryChanged() / trendDataChanged() 信号通知 View 刷新；
 *     请求失败时发出 fetchFailed(reason) 信号，不清空已有数据。
 *   - 构造时先用 initMockData() 生成占位数据（服务器响应到达前 / 请求失败时展示），
 *     并自动调用 fetchData() 拉取真实数据。
 *
 * 模型内部维护两个数据集：近7日、近30日，各含 4 个维度
 *   (日期、营收、电量、订单数)。loadDataset() 将指定数据集填充到
 *   QStandardItemModel 的行列结构中，供折线图按标准 model API 读取。
 *
 * 说明：loadFromServer() 为保留的备用接口（仅打印日志），实际网络请求由 fetchData() 完成。
 */
class DashboardModel : public QStandardItemModel
{
    Q_OBJECT
public:
    /** 列索引：日期 / 营收 / 电量 / 订单数 */
    enum Column {
        DateCol = 0,
        RevenueCol = 1,
        EnergyCol = 2,
        OrderCountCol = 3,
        ColCount = 4
    };

    /** 时间范围枚举：近7日 / 近30日 */
    enum TimeRange {
        Last7Days = 7,
        Last30Days = 30
    };

    /**
     * @brief 运营核心指标汇总（对应 GET /admin/dashboard/summary 返回的 data）
     */
    struct Summary
    {
        double today_revenue = 0.0;          // 今日营收（元）
        qint64 today_revenue_cents = 0;      // 今日营收（分）
        double month_revenue = 0.0;          // 本月营收（元）
        qint64 month_revenue_cents = 0;      // 本月营收（分）
        double total_revenue = 0.0;          // 总营收（元）
        qint64 total_revenue_cents = 0;      // 总营收（分）
        double today_energy_kwh = 0.0;       // 今日电量（度）
        int today_order_count = 0;           // 今日订单数
        int total_user_count = 0;            // 平台总用户数
        int active_charging_sessions = 0;    // 当前进行中的充电会话数
    };

    /**
     * @brief 单日趋势数据点（日期、营收、电量、订单数 四维度）
     */
    struct TrendPoint
    {
        QString date;            // 日期 "yyyy-MM-dd"
        double revenue = 0.0;    // 当日营收（元）
        double energy_kwh = 0.0; // 当日电量（度）
        int order_count = 0;     // 当日订单数
    };

    explicit DashboardModel(QObject *parent = nullptr);

    /** 获取核心指标汇总（供卡片读取） */
    const Summary &summary() const;

    /** 获取当前展示的时间范围 */
    TimeRange currentRange() const;

    /**
     * @brief 将指定范围的数据集填充进 Model 的行列结构
     * @param range 近7日 / 近30日
     * 填充后 rowCount / data() 可被折线图按标准方式读取（日期列、营收列）。
     */
    void loadDataset(TimeRange range);

    /**
     * @brief 备用接口：从服务端拉取数据（保留）
     *
     * 实际网络请求由 fetchData() 执行；本方法仅作为备用入口保留，
     * 目前只打印 qDebug() 日志，不发起网络请求。
     */
    void loadFromServer();

public slots:
    /**
     * @brief 从后台服务器拉取看板真实数据（实际执行网络请求的入口）
     *
     * 并发请求 3.2 节两个接口：
     *   - GET /api/v1/admin/dashboard/summary
     *   - GET /api/v1/admin/dashboard/revenue-trend?days=7
     *   - GET /api/v1/admin/dashboard/revenue-trend?days=30
     * 响应到达后解析 JSON、更新 Model 数据，并发出数据变更信号通知 View 刷新。
     * 构造函数中会自动调用一次；View 也可在需要手动刷新时再次调用。
     */
    void fetchData();

    /**
     * @brief 设置管理员鉴权 Token（对应文档 1.3 节 Bearer Token）
     *
     * 设置后所有请求会自动携带 "Authorization: Bearer <token>" 头；
     * 未设置时不携带该头（便于本地联调）。
     */
    void setAuthToken(const QString &token);

signals:
    /** 核心指标汇总已更新，View 可据此刷新指标卡片 */
    void summaryChanged();

    /** 营收趋势数据已更新（当前展示的数据集行列已重建），View 可据此刷新折线图 */
    void trendDataChanged();

    /** 网络请求或响应解析失败（reason 为可展示/可记录的失败原因） */
    void fetchFailed(const QString &reason);

private:
    /** 初始化占位数据：汇总指标 + 7日 + 30日趋势（服务器数据到达前展示） */
    void initMockData();

    /** 懒初始化 QNetworkAccessManager（以 this 为 parent，随 Model 生命周期释放） */
    void ensureNetworkManager();

    /** 为请求填充公共头（Content-Type / Accept / Authorization） */
    void prepareRequest(QNetworkRequest *request) const;

    /** 发起核心指标汇总请求 */
    void requestSummary();

    /** 发起指定时间范围的营收趋势请求 */
    void requestTrend(TimeRange range);

    /** 处理 summary 响应：校验 -> 解析 -> 更新 m_summary -> 发信号 */
    void handleSummaryReply(QNetworkReply *reply);

    /** 处理 revenue-trend 响应：校验 -> 解析 -> 更新对应数据集 -> 重建行列 -> 发信号 */
    void handleTrendReply(QNetworkReply *reply, TimeRange range);

    /**
     * @brief 解析统一响应信封 {code,msg,data}
     * @return true 表示 code==0 且 data 已取出；false 表示失败（已 emit fetchFailed）
     */
    bool extractData(const QByteArray &body, const QString &apiTag, QJsonObject &outData);

    Summary m_summary;                       // 核心指标汇总
    QVector<TrendPoint> m_data7d;            // 近7日数据集
    QVector<TrendPoint> m_data30d;           // 近30日数据集
    TimeRange m_currentRange = Last7Days;    // 当前加载的时间范围

    QNetworkAccessManager *m_networkManager = nullptr; // HTTP 请求管理器（懒创建）
    QString m_authToken;                    // 管理员 Bearer Token（可为空）

    // 服务器地址：当前写死，后续再改为可配置
    const QString m_serverBase = QStringLiteral("http://62.234.84.145:8080");
};

#endif // DASHBOARDMODEL_H
