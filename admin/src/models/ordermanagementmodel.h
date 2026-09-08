#ifndef ORDERMANAGEMENTMODEL_H
#define ORDERMANAGEMENTMODEL_H

#include <QObject>
#include <QColor>
#include <QStandardItemModel>
#include <QString>
#include <QJsonArray>
#include <QJsonObject>

QT_BEGIN_NAMESPACE
class QNetworkAccessManager;
class QNetworkReply;
class QNetworkRequest;
QT_END_NAMESPACE

/**
 * @brief 订单管理数据模型（PC 运营后台 - 订单管理模块）
 *
 * 继承自 QObject，内部持有 QStandardItemModel 作为表格数据源（每行一个订单），
 * 通过 getModel() 提供给 Widget 绑定 QTableView。
 *
 * 接口对应《API 设计文档》3.6 节（本期实现 3.6.1 / 3.6.3）：
 *   - GET /api/v1/admin/orders                      分页检索全平台充电订单
 *       查询参数: page / page_size
 *                / station_id(可选) / order_status(可选: CHARGING, COMPLETED,
 *                  UNSETTLED, REFUNDED) / start_date(可选) / end_date(可选)
 *   - POST /api/v1/admin/orders/{order_id}/refund   管理员一键退款
 *       请求头: Idempotency-Key（每次请求生成唯一 UUID，防止重复退款）
 *       请求体: { refund_amount, refund_amount_cents, reason }
 *
 * 实测说明（2026-09-08，线上 62.234.84.145:8080）：
 *   - 接口可用，total 约 20 万级；
 *   - 列表行内含 user_id / user_phone 字段（此前缺失，已由服务端补齐）；
 *   - user_id / phone / status(兼容 order_status) / start_date / end_date
 *     筛选参数均已生效；
 *   - 按用户查询走同一接口（user_id/phone 参数），原独立接口
 *     /admin/orders/user 已下线（HTTP 404）。
 *
 * 统一响应信封：{ code, msg, data, timestamp }，code==0 表示成功。
 * 表格列顺序：订单ID | 用户ID | 用户手机号 | 充电站 | 充电桩 | 充电量(kWh)
 *             | 电费 | 服务费 | 超时费 | 总金额 | 状态 | 创建时间 | 结束时间 | 操作
 */
class OrderManagementModel : public QObject
{
    Q_OBJECT
public:
    /** 列索引（14 列） */
    enum Column {
        OrderIdCol = 0,
        UserIdCol,
        UserPhoneCol,
        StationCol,
        PileCol,
        EnergyCol,
        ElecFeeCol,
        ServFeeCol,
        OvertimeFeeCol,
        TotalFeeCol,
        StatusCol,
        StartTimeCol,
        EndTimeCol,
        ActionCol,
        ColCount = 14
    };

    /** 行自定义数据角色（Widget 构建退款按钮时读取） */
    enum DataRole {
        OrderIdRole  = Qt::UserRole + 1,  // 订单号（QString，如 ORD_xxx）
        StatusRole   = Qt::UserRole + 2,  // 订单状态原文（QString：CHARGING 等）
        TotalFeeRole = Qt::UserRole + 3   // 订单总金额（double，元，退款金额上限）
    };

    explicit OrderManagementModel(QObject *parent = nullptr);

    /** 获取表格数据源（供 Widget 绑定 QTableView） */
    QStandardItemModel *getModel();

    /**
     * @brief 设置管理员鉴权 Token（Bearer Token，由 MainWindow 传入）
     * 仅保存 Token，不自动发起请求；首次拉取由 Widget 触发。
     */
    void setAuthToken(const QString &token);

    /**
     * @brief 分页检索全平台充电订单（文档 3.6.1）
     * @param page 当前页码（从 1 开始）
     * @param pageSize 每页条数（默认 10）
     * @param stationId 站点筛选（-1=全部不携带，其它为具体站点 ID）
     * @param orderStatus 状态筛选（空串=全部不携带；CHARGING/UNSETTLED/COMPLETED/REFUNDED）
     * @param startDate 起始日期（空串=不携带，格式 yyyy-MM-dd）
     * @param endDate 结束日期（空串=不携带，格式 yyyy-MM-dd）
     */
    void fetchOrders(int page = 1, int pageSize = 10,
                     int stationId = -1,
                     const QString &orderStatus = QString(),
                     const QString &startDate = QString(),
                     const QString &endDate = QString());

    /**
     * @brief 按用户查询历史订单（新版文档 3.6.1 合并接口，第二期新增）
     * @param userId 用户ID（0=不携带）
     * @param phone 用户手机号（空串=不携带）
     *
     * GET /api/v1/admin/orders?user_id=&phone=&page=&page_size=
     * 2026-09-08 接口改版：原独立接口 /admin/orders/user 已下线（404），
     * user_id/phone 合并为列表接口的可选参数；两者同时提供时必须指向
     * 同一用户，矛盾时服务端返回 HTTP 400。
     * 响应结构与 3.6.1 相同（orders/total/page/page_size），
     * 复用同一 populateOrders 填表与 ordersReady 信号。
     */
    void fetchOrdersByUser(qint64 userId, const QString &phone,
                           int page = 1, int pageSize = 10);

    /**
     * @brief 查询订单详情（文档 3.6.4，第二期新增）
     * @param orderId 订单号
     *
     * GET /api/v1/admin/orders/{order_id}
     * 成功后发射 orderDetailReady(data)，data 为完整订单对象
     * （含 user_id / user_phone / 各项费用 / SOC 等）；
     * 已退款订单的退款明细块由服务端按需返回，缺失时 Widget 不展示该区块。
     */
    void fetchOrderDetail(const QString &orderId);

    /**
     * @brief 管理员对指定订单一键退款（文档 3.6.3）
     * @param orderId 订单号
     * @param refundAmount 退款金额（元；服务端校验不得超过订单实付金额）
     * @param reason 退款原因（写入退款审计流水）
     *
     * 请求自动携带 Idempotency-Key 头（RF-<UUID>），防止网络重试导致重复退款。
     */
    void refundOrder(const QString &orderId, double refundAmount,
                     const QString &reason);

    /** 订单状态 -> 中文文案（未知状态返回原文） */
    static QString orderStatusText(const QString &status);

    /** 订单状态 -> 展示颜色（充电中青 / 待结算橙 / 已完成绿 / 已退款灰） */
    static QColor orderStatusColor(const QString &status);

    /** 桩类型 -> 中文文案（FAST=快充 / SLOW=慢充，未知返回空串） */
    static QString pileTypeText(const QString &type);

signals:
    /** 订单列表已就绪并填充进 Model，Widget 据此更新分页栏与操作按钮 */
    void ordersReady(const QJsonArray &orders, int total, int page, int pageSize);

    /** 订单详情已就绪（第二期：按用户查询/订单详情弹窗），data 为完整订单对象 */
    void orderDetailReady(const QJsonObject &data);

    /** 退款成功（msg 为可直接展示的提示信息，含退款流水号与用户余额变化） */
    void refundSuccess(const QString &msg);

    /** 网络请求失败 / HTTP 异常状态 / 响应解析失败 / 业务错误码非 0 */
    void errorOccurred(const QString &errorMsg);

    /** 日志请求：查询/操作事件（由 Widget 转发至 MainWindow 日志区，不含 Token 等敏感信息） */
    void logRequested(const QString &message);

private:
    /** 将订单数组填充进表格 Model（清空旧行后逐行追加） */
    void populateOrders(const QJsonArray &orders);

    /** 处理列表响应（3.6.1 / 3.6.2 共用）：校验 -> 填表 -> 发 ordersReady */
    void handleOrdersReply(QNetworkReply *reply, const QString &apiTag);

    /** 处理详情响应（3.6.4）：校验 -> 发 orderDetailReady */
    void handleDetailReply(QNetworkReply *reply, const QString &orderId);

    /** 处理退款响应：校验 -> 发 refundSuccess */
    void handleRefundReply(QNetworkReply *reply, const QString &orderId);

    /**
     * @brief 解析统一响应信封 {code,msg,data}
     * @return true 表示 code==0 且 data 已取出；false 表示失败（已 emit errorOccurred）
     * 解析失败时在日志中打印原始响应，便于排查文档与实现不一致的情况。
     */
    bool extractData(const QByteArray &body, const QString &apiTag, QJsonObject &outData);

    QStandardItemModel *m_tableModel = nullptr;        // 表格数据源

    // 服务器地址：当前写死，后续再改为可配置
    const QString m_serverBase = QStringLiteral("http://62.234.84.145:8080");

    // 当前查询上下文（响应到达时用于兜底默认值）
    int m_page = 1;
    int m_pageSize = 10;
    int m_stationId = -1;
    QString m_orderStatus;
    QString m_startDate;
    QString m_endDate;
    // 当前请求是否携带 phone 参数（手机号为精确匹配：用户不存在时后端
    // 返回 HTTP 404 而非空列表，handleOrdersReply 据此做"未找到"特殊处理）
    bool m_lastQueryHasPhone = false;
};

#endif // ORDERMANAGEMENTMODEL_H
