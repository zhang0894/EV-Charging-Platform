#ifndef PILEMANAGEMENTMODEL_H
#define PILEMANAGEMENTMODEL_H

#include <QObject>
#include <QStandardItemModel>
#include <QString>
#include <QColor>
#include <QJsonArray>
#include <QJsonObject>

QT_BEGIN_NAMESPACE
class QNetworkAccessManager;
class QNetworkReply;
class QNetworkRequest;
QT_END_NAMESPACE

/**
 * @brief 充电桩管理数据模型（PC 运营后台 - 充电桩管理模块）
 *
 * 继承自 QObject，内部持有 QStandardItemModel 作为表格数据源（每行一个充电桩），
 * 通过 getModel() 提供给 Widget 绑定 QTableView。
 *
 * 接口对应《端口设计文档》3.4 节：
 *   - GET /api/v1/admin/piles                      分页查询充电桩列表
 *       查询参数: page / page_size / station_id(可选) / status(可选) / type(可选)
 *       status: IDLE / CHARGING / FAULT / OFFLINE（全部时不传）
 *       type:   FAST / SLOW（全部时不传）
 *   - POST /api/v1/admin/piles/{pile_id}/restart   远程重启（body: {"reason": ...}）
 *
 * 注意：pile_id 为字符串（如 "P10101"），拼接进重启 URL 时无需编码处理。
 *
 * 统一响应信封：{ code, msg, data, timestamp }，code==0 表示成功。
 * 表格列顺序：桩编号 | 所属电站 | 类型 | 功率(kW) | 状态 | 累计充电次数 | 累计充电时长 | 操作(占位)
 *
 * 该 Model 同时被"充电站管理详情弹窗"复用（StationManagementWidget 持有独立实例，
 * 传 station_id 参数拉取单站电桩列表），状态/类型的中文与配色字典以静态方法提供，
 * 供两个 Widget 共享，避免重复定义。
 */
class PileManagementModel : public QObject
{
    Q_OBJECT
public:
    /** 列索引：桩编号 / 所属电站 / 类型 / 功率 / 状态 / 累计次数 / 累计时长 / 操作 */
    enum Column {
        PileIdCol = 0,
        StationNameCol,
        TypeCol,
        PowerCol,
        StatusCol,
        ChargeCountCol,
        ChargeHoursCol,
        ActionCol,
        ColCount = 8
    };

    /** 行自定义数据角色（Widget 构建操作按钮时读取） */
    enum DataRole {
        PileIdRole        = Qt::UserRole + 1, // 桩编号（QString，如 "P10101"）
        CurrentStatusRole = Qt::UserRole + 2  // 当前状态（QString: IDLE/CHARGING/FAULT/OFFLINE）
    };

    // ---------------- 状态/类型字典（供各 Widget 共享） ----------------
    /** 状态中文：IDLE->空闲, CHARGING->充电中, FAULT->故障, OFFLINE->离线 */
    static QString pileStatusText(const QString &status);
    /** 状态配色：空闲=绿 #2ecc71, 充电中=青 #00d4ff, 故障=红 #ff5c5c, 离线=灰 #8b9bb4 */
    static QColor pileStatusColor(const QString &status);
    /** 类型中文：FAST->快充, SLOW->慢充 */
    static QString pileTypeText(const QString &type);
    /** 类型配色：快充=青 #00d4ff, 慢充=灰 #8b9bb4 */
    static QColor pileTypeColor(const QString &type);

    explicit PileManagementModel(QObject *parent = nullptr);

    /** 获取表格数据源（供 Widget 绑定 QTableView） */
    QStandardItemModel *getModel();

    /**
     * @brief 设置管理员鉴权 Token（Bearer Token，由 MainWindow 传入）
     * 仅保存 Token，不自动发起请求；首次拉取由 Widget 触发。
     */
    void setAuthToken(const QString &token);

    /**
     * @brief 分页查询充电桩列表
     * @param page 当前页码（从 1 开始）
     * @param pageSize 每页条数（默认 10）
     * @param stationId 所属电站 ID（>0 时携带 station_id 参数，供详情弹窗复用；-1 表示不筛选）
     * @param statusFilter 状态筛选（""=全部不携带, IDLE/CHARGING/FAULT/OFFLINE）
     * @param typeFilter 类型筛选（""=全部不携带, FAST/SLOW）
     */
    void fetchPiles(int page = 1, int pageSize = 10, int stationId = -1,
                    const QString &statusFilter = QString(),
                    const QString &typeFilter = QString());

    /**
     * @brief 远程重启充电桩
     * @param pileId 桩编号（如 "P10101"，直接拼入 URL）
     * 请求体固定为 {"reason": "管理员远程重启"}；成功后电桩状态将变为 IDLE。
     */
    void restartPile(const QString &pileId);

signals:
    /** 充电桩列表已就绪并填充进 Model，Widget 据此更新分页栏与操作按钮 */
    void pilesReady(const QJsonArray &piles, int total, int page, int pageSize);

    /** 重启成功（msg 为可展示的提示信息，如"电桩 P10101 重启成功"） */
    void restartSuccess(const QString &msg);

    /** 网络请求失败 / HTTP 异常状态 / 响应解析失败 / 业务错误码非 0 */
    void errorOccurred(const QString &errorMsg);

private:
    /** 懒初始化 QNetworkAccessManager（以 this 为 parent，随 Model 释放） */
    void ensureNetworkManager();

    /** 为请求填充公共头（Content-Type / Accept / Authorization） */
    void prepareRequest(QNetworkRequest *request) const;

    /** 将充电桩数组填充进表格 Model（清空旧行后逐行追加） */
    void populatePiles(const QJsonArray &piles);

    /** 处理列表响应：校验 -> 填表 -> 发 pilesReady */
    void handlePilesReply(QNetworkReply *reply);

    /** 处理重启响应：校验 -> 发 restartSuccess / errorOccurred */
    void handleRestartReply(QNetworkReply *reply, const QString &pileId);

    /**
     * @brief 解析统一响应信封 {code,msg,data}
     * @return true 表示 code==0 且 data 已取出；false 表示失败（已 emit errorOccurred）
     * 解析失败时在日志中打印原始响应，便于排查文档与实现不一致的情况。
     */
    bool extractData(const QByteArray &body, const QString &apiTag, QJsonObject &outData);

    QStandardItemModel *m_tableModel = nullptr;        // 表格数据源
    QNetworkAccessManager *m_networkManager = nullptr; // HTTP 请求管理器（懒创建）
    QString m_authToken;                               // 管理员 Bearer Token

    // 服务器地址：当前写死，后续再改为可配置
    const QString m_serverBase = QStringLiteral("http://62.234.84.145:8080");

    // 当前查询上下文（响应到达时用于兜底默认值；station_id 为一次性参数不在此保存）
    int m_page = 1;
    int m_pageSize = 10;
    QString m_statusFilter;
    QString m_typeFilter;
};

#endif // PILEMANAGEMENTMODEL_H
