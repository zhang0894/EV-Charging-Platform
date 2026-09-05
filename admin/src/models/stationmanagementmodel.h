#ifndef STATIONMANAGEMENTMODEL_H
#define STATIONMANAGEMENTMODEL_H

#include <QObject>
#include <QStandardItemModel>
#include <QString>
#include <QList>
#include <QJsonArray>
#include <QJsonObject>

QT_BEGIN_NAMESPACE
class QNetworkAccessManager;
class QNetworkReply;
class QNetworkRequest;
QT_END_NAMESPACE

/**
 * @brief 充电站管理数据模型（PC 运营后台 - 充电站管理模块）
 *
 * 继承自 QObject，内部持有 QStandardItemModel 作为表格数据源（每行一个充电站），
 * 通过 getModel() 提供给 Widget 绑定 QTableView。
 *
 * 接口对应《端口设计文档》3.3 节：
 *   - GET /api/v1/admin/stations                        分页查询充电站列表
 *       查询参数: page / page_size / name(可选) / status(可选, 1=正常运营 2=维护中)
 *   - GET /api/v1/admin/stations/{id}/sales-stats       单站销售业绩
 *       查询参数: time_range(today / 7d / 30d)
 *   - POST /api/v1/admin/stations/{id}/online           充电站上线
 *   - POST /api/v1/admin/stations/{id}/offline          充电站下线
 *
 * 模拟数据方案（真实充电站来自高德地图 API，不写库）：
 *   - m_mockStations 保存本次会话中用户新增的模拟电站，ID 为负数(-1,-2,...)；
 *   - 每次查询列表时，模拟数据转为与 API 相同结构的 JSON 拼在真实数据之前，
 *     total = 真实总数 + 模拟数量；
 *   - 模拟电站的上下线只改内存状态；查看详情由 Widget 直接提示无销售记录；
 *   - 程序退出时 m_mockStations 自然销毁，不写数据库。
 *
 * 统一响应信封：{ code, msg, data, timestamp }，code==0 表示成功。
 * 表格列顺序：站ID | 站名 | 地址 | 总桩数 | 在线率 | 状态 | 操作(占位)
 */
class StationManagementModel : public QObject
{
    Q_OBJECT
public:
    /** 列索引：站ID / 站名 / 地址 / 总桩数 / 在线率 / 状态 / 操作 */
    enum Column {
        StationIdCol = 0,
        NameCol,
        AddressCol,
        TotalPilesCol,
        OnlineRateCol,
        StatusCol,
        ActionCol,
        ColCount = 7
    };

    /** 行自定义数据角色（Widget 构建操作按钮时读取） */
    enum DataRole {
        StationIdRole = Qt::UserRole + 1,  // 充电站 ID（int，模拟数据为负数）
        StatusRole    = Qt::UserRole + 2,  // 状态（int, 1=正常运营 2=维护中）
        NameRole      = Qt::UserRole + 3   // 站名（QString）
    };

    /** 充电站信息（与 API 返回字段对齐） */
    struct StationInfo
    {
        int station_id = 0;
        QString station_name;
        QString address;
        double latitude = 0.0;
        double longitude = 0.0;
        int total_piles = 0;
        int online_piles = 0;
        int idle_piles = 0;
        double online_rate = 0.0;
        int status = 1;                    // 1=正常运营, 2=维护中
        double price_per_kwh = 0.0;
        double service_fee_per_kwh = 0.0;
        double overtime_fee_per_15min = 0.0;
        qint64 created_at = 0;             // 毫秒级 Unix 时间戳
    };

    explicit StationManagementModel(QObject *parent = nullptr);

    /** 获取表格数据源（供 Widget 绑定 QTableView） */
    QStandardItemModel *getModel();

    /**
     * @brief 设置管理员鉴权 Token（Bearer Token，由 MainWindow 传入）
     * 仅保存 Token，不自动发起请求；首次拉取由 Widget 触发。
     */
    void setAuthToken(const QString &token);

    /**
     * @brief 分页查询充电站列表（真实数据 + 模拟数据合并）
     * @param page 当前页码（从 1 开始）
     * @param pageSize 每页条数（默认 10）
     * @param nameFilter 站名模糊搜索（空串则不携带该参数，仅作用于真实数据）
     * @param statusFilter 状态筛选（-1=全部不携带, 1=正常运营, 2=维护中）
     */
    void fetchStations(int page = 1, int pageSize = 10,
                       const QString &nameFilter = QString(),
                       int statusFilter = -1);

    /** 新增模拟电站（ID 由 Model 分配负数，加入 m_mockStations） */
    void addMockStation(const StationInfo &info);

    /**
     * @brief 充电站上线/下线（真实和模拟统一处理）
     * @param stationId 充电站 ID（模拟数据为负数，只改内存状态）
     * @param newStatus 目标状态（1=上线/正常运营, 2=下线/维护中）
     */
    void setStationStatus(int stationId, int newStatus);

    /**
     * @brief 查询单站销售业绩
     * @param stationId 充电站 ID（须为真实电站）
     * @param timeRange today / 7d / 30d
     */
    void fetchStationSales(int stationId, const QString &timeRange);

signals:
    /** 充电站列表已就绪并填充进 Model，Widget 据此更新分页栏与操作按钮 */
    void stationsReady(const QJsonArray &stations, int total, int page, int pageSize);

    /** 单站销售业绩已就绪（data 为响应信封中的 data 对象） */
    void salesDataReady(const QJsonObject &data, int stationId);

    /** 上线/下线操作成功（msg 为可展示的提示信息） */
    void operationSuccess(const QString &msg);

    /** 网络请求失败 / HTTP 异常状态 / 响应解析失败 / 业务错误码非 0 */
    void errorOccurred(const QString &errorMsg);

private:
    /** 懒初始化 QNetworkAccessManager（以 this 为 parent，随 Model 释放） */
    void ensureNetworkManager();

    /** 为请求填充公共头（Content-Type / Accept / Authorization） */
    void prepareRequest(QNetworkRequest *request) const;

    /** StationInfo -> 与 API 相同结构的 JSON（用于合并模拟数据） */
    QJsonObject stationInfoToJson(const StationInfo &info) const;

    /** 将充电站数组填充进表格 Model（清空旧行后逐行追加） */
    void populateStations(const QJsonArray &stations);

    /** 处理列表响应：校验 -> 合并模拟数据 -> 填表 -> 发 stationsReady */
    void handleStationsReply(QNetworkReply *reply);

    /** 处理销售业绩响应：校验 -> 发 salesDataReady */
    void handleSalesReply(QNetworkReply *reply, int stationId);

    /** 处理上线/下线响应：校验 -> 发 operationSuccess */
    void handleStatusReply(QNetworkReply *reply, int stationId, int newStatus);

    /**
     * @brief 解析统一响应信封 {code,msg,data}
     * @return true 表示 code==0 且 data 已取出；false 表示失败（已 emit errorOccurred）
     * 解析失败时在日志中打印原始响应，便于排查文档与实现不一致的情况。
     */
    bool extractData(const QByteArray &body, const QString &apiTag, QJsonObject &outData);

    // ---------------- 模拟数据（仅内存，程序退出即销毁） ----------------
    QList<StationInfo> m_mockStations;  // 本次会话中新增的模拟电站
    int m_nextMockId = -1;              // 模拟电站 ID 分配器（-1, -2, -3 ...）

    QStandardItemModel *m_tableModel = nullptr; // 表格数据源
    QNetworkAccessManager *m_networkManager = nullptr; // HTTP 请求管理器（懒创建）
    QString m_authToken;                // 管理员 Bearer Token

    // 服务器地址：当前写死，后续再改为可配置
    const QString m_serverBase = QStringLiteral("http://62.234.84.145:8080");

    // 当前查询上下文（响应到达时用于兜底默认值）
    int m_page = 1;
    int m_pageSize = 10;
    QString m_nameFilter;
    int m_statusFilter = -1;
};

#endif // STATIONMANAGEMENTMODEL_H
