#ifndef ORDERMANAGEMENTWIDGET_H
#define ORDERMANAGEMENTWIDGET_H

#include <QWidget>
#include <QJsonArray>
#include <QJsonObject>
#include <QString>

class QTableView;
class QComboBox;
class QLineEdit;
class QDateEdit;
class QPushButton;
class QLabel;
class QDialog;
class OrderManagementModel;

/**
 * @brief 订单管理页（PC 运营后台）
 *
 * 职责：全平台订单分页检索 + 状态/站点/日期筛选 + 管理员一键退款。
 * 数据全部来自 OrderManagementModel（唯一数据入口），UI 不硬编码业务数据。
 *
 * 布局：
 *   - 顶部工具栏：状态筛选下拉 / 站ID输入框 / 起止日期 / 查询 / 刷新
 *   - 中间表格：  QTableView 绑定 Model 的 QStandardItemModel（14 列），
 *                 操作列对"已完成"订单动态安装"退款"按钮
 *   - 底部分页栏：上一页 / 页码信息 / 下一页
 *
 * 退款流程：点击"退款" -> 弹出对话框输入退款金额（默认且上限为订单总额）
 *           与退款原因 -> 确认后调用 Model::refundOrder() ->
 *           成功后提示退款流水信息并刷新当前页。
 *
 * 本期为第一阶段实现（列表检索 + 一键退款）；
 * 用户订单查询（3.6.2）与订单详情（3.6.4）为后续迭代。
 */
class OrderManagementWidget : public QWidget
{
    Q_OBJECT
public:
    explicit OrderManagementWidget(QWidget *parent = nullptr);

    /**
     * @brief 设置管理员 Token 并发起首次列表拉取（由 MainWindow 调用）
     * 内部转发给 OrderManagementModel::setAuthToken()，随后查询第 1 页。
     */
    void setAuthToken(const QString &token);

    /**
     * @brief 外部跳转入口：按用户筛选订单（跨页联动，供 MainWindow 调用）
     * @param userId 用户ID（优先使用，>0 时有效）
     * @param phone  手机号（user_id 无效时的备选）
     *
     * 填入工具栏对应输入框后复用 onUserQueryClicked() 的校验与查询逻辑，
     * 自动切换为"按用户查询"模式并重置到第 1 页。
     */
    void setFilterByUser(qint64 userId, const QString &phone);

private slots:
    void onQueryClicked();          // "查询"按钮：全局列表（状态/站点/日期筛选，重置第 1 页）
    void onUserQueryClicked();      // "查用户订单"按钮：按用户ID/手机号查询（第二期）
    void onRefreshClicked();        // 刷新当前页（按当前查询模式）
    void onStatusFilterChanged(int index); // 状态筛选变化 -> 重置第 1 页
    void onPrevPage();              // 上一页（按当前查询模式）
    void onNextPage();              // 下一页（按当前查询模式）
    void onOrdersReady(const QJsonArray &orders, int total, int page, int pageSize);
    void onOrderDetailReady(const QJsonObject &data); // 详情就绪 -> 弹窗（第二期）
    void onRefundSuccess(const QString &msg);
    void onErrorOccurred(const QString &errorMsg);

private:
    /** 当前查询模式（第二期）：全局列表 / 按用户查询，决定翻页与刷新的走向 */
    enum class QueryMode { Global, User };

    void buildUi();                    // 构建界面骨架与样式（无 .ui 文件，纯代码布局）
    void applyFiltersAndFetch(int page); // 全局列表：以当前筛选条件请求指定页
    void applyUserQueryAndFetch(int page); // 按用户查询：以当前用户ID/手机号请求指定页
    void dispatchCurrentQuery(int page);   // 按当前查询模式分发翻页/刷新请求
    void installActionButtons();       // 安装"详情"按钮 + "已完成"订单的"退款"按钮
    void updatePager(int total, int page, int pageSize); // 刷新分页栏
    void showRefundDialog(const QString &orderId, double totalFee); // 退款弹窗
    void showOrderDetail(const QString &orderId); // 发起详情请求（第二期）
    QDialog *buildDetailDialog(const QJsonObject &data); // 组装详情弹窗（第二期）

    OrderManagementModel *m_model;     // 数据源（唯一数据入口）

    // 顶部工具栏
    QComboBox   *m_statusCombo = nullptr;  // 状态筛选（itemData: ""=全部 / CHARGING 等）
    QLineEdit   *m_stationEdit = nullptr;  // 站ID筛选（数字，空=全部）
    QDateEdit   *m_startDateEdit = nullptr; // 起始日期（最小值显示"不限"）
    QDateEdit   *m_endDateEdit = nullptr;   // 结束日期（最小值显示"不限"）
    QPushButton *m_btnQuery = nullptr;
    QPushButton *m_btnRefresh = nullptr;
    QLineEdit   *m_userIdEdit = nullptr;   // 用户ID筛选（整数，空=不按用户查询）
    QLineEdit   *m_phoneEdit = nullptr;    // 手机号筛选（11位数字，空=不按手机查询）
    QPushButton *m_btnUserQuery = nullptr; // "查用户订单"按钮
    // 中间表格
    QTableView  *m_tableView = nullptr;
    // 底部分页栏
    QPushButton *m_btnPrev = nullptr;
    QPushButton *m_btnNext = nullptr;
    QLabel      *m_pageLabel = nullptr;

    // 分页状态
    int m_page = 1;        // 当前页码
    int m_pageSize = 10;   // 每页条数
    int m_total = 0;       // 总记录数
    int m_totalPages = 1;  // 总页数
    // 查询模式状态（第二期）：翻页/刷新按最近一次"查询"动作的模式与参数走向
    QueryMode m_queryMode = QueryMode::Global;
    qint64 m_curUserId = 0;        // 用户查询模式下的用户ID（0=未指定）
    QString m_curPhone;            // 用户查询模式下的手机号（空=未指定）
    bool m_detailBusy = false;     // 详情请求进行中（防止重复弹窗）
};

#endif // ORDERMANAGEMENTWIDGET_H
