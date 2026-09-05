#ifndef STATIONMANAGEMENTWIDGET_H
#define STATIONMANAGEMENTWIDGET_H

#include <QWidget>
#include <QJsonArray>
#include <QJsonObject>
#include <QPointer>
#include <QSet>
#include <QString>

class QDialog;
class QTabWidget;
class QTableWidget;
class QTableView;
class QLineEdit;
class QComboBox;
class QPushButton;
class QLabel;
class StationManagementModel;

/**
 * @brief 充电站管理页（PC 运营后台）
 *
 * 职责：充电站列表展示 + 站名搜索 + 状态筛选 + 新增模拟电站 +
 *       上下线操作 + 单站销售业绩详情。
 * 数据全部来自 StationManagementModel（唯一数据入口），UI 不硬编码业务数据。
 *
 * 布局：
 *   - 顶部工具栏：站名搜索框 / 状态筛选下拉 / 查询 / 刷新 / 新增电站
 *   - 中间表格：  QTableView 绑定 Model 的 QStandardItemModel，
 *                 操作列动态安装"详情"与"上线/下线"按钮
 *   - 底部分页栏：上一页 / 页码信息 / 下一页
 *
 * 弹窗：
 *   - 新增电站：QDialog 表单（站名/地址必填，纬度/经度可选），
 *               仅写入 Model 的本地模拟列表（ID 为负数，不写库）；
 *   - 销售详情：QDialog + QTabWidget（今日/近7天/近30天），每个 Tab 用
 *               QTableWidget 展示 日期|营收|充电量|订单数；模拟电站直接提示
 *               "该电站为本地模拟数据，暂无销售记录"。
 */
class StationManagementWidget : public QWidget
{
    Q_OBJECT
public:
    explicit StationManagementWidget(QWidget *parent = nullptr);

    /**
     * @brief 设置管理员 Token 并发起首次列表拉取（由 MainWindow 调用）
     * 内部转发给 StationManagementModel::setAuthToken()，随后查询第 1 页。
     */
    void setAuthToken(const QString &token);

private slots:
    void onQueryClicked();             // 站名搜索"查询"按钮
    void onRefreshClicked();           // 刷新当前页
    void onPrevPage();                 // 上一页
    void onNextPage();                 // 下一页
    void onStatusFilterChanged(int index); // 状态筛选变化 -> 重置第 1 页
    void onAddStationClicked();        // 新增模拟电站弹窗
    void onStationsReady(const QJsonArray &stations, int total, int page, int pageSize);
    void onSalesDataReady(const QJsonObject &data, int stationId);
    void onOperationSuccess(const QString &msg);
    void onErrorOccurred(const QString &errorMsg);

private:
    void buildUi();                    // 构建界面骨架与样式（无 .ui 文件，纯代码布局）
    void applyFiltersAndFetch(int page); // 以当前筛选条件请求指定页
    void installActionButtons();       // 依据每行状态安装"详情"/"上线/下线"按钮
    void updatePager(int total, int page, int pageSize); // 刷新分页栏
    void confirmAndSetStatus(int stationId, const QString &name, int targetStatus);
    void showAddStationDialog();       // 新增模拟电站对话框
    void showSalesDetail(int stationId, const QString &stationName); // 销售详情对话框
    void requestSalesRange(const QString &timeRange); // 请求指定时间维度销售数据
    void fillSalesTable(QTableWidget *table, const QJsonObject &data); // 填充销售表格

    StationManagementModel *m_model;   // 数据源（唯一数据入口）

    // 顶部工具栏
    QLineEdit   *m_searchEdit = nullptr;
    QComboBox   *m_statusCombo = nullptr;
    QPushButton *m_btnQuery = nullptr;
    QPushButton *m_btnRefresh = nullptr;
    QPushButton *m_btnAdd = nullptr;
    // 中间表格
    QTableView  *m_tableView = nullptr;
    // 底部分页栏
    QPushButton *m_btnPrev = nullptr;
    QPushButton *m_btnNext = nullptr;
    QLabel      *m_pageLabel = nullptr;

    // 销售详情弹窗相关（QDialog 为非模态 show，QPointer 防止关闭后悬挂）
    QPointer<QDialog> m_detailDialog;
    QTabWidget   *m_detailTabs = nullptr;
    QTableWidget *m_todayTable = nullptr;
    QTableWidget *m_weekTable = nullptr;
    QTableWidget *m_monthTable = nullptr;
    int m_detailStationId = 0;          // 当前详情弹窗对应的电站 ID
    QString m_pendingSalesRange;        // 在途销售请求对应的 time_range
    QSet<QString> m_salesLoaded;        // 当前电站已加载的 time_range 集合

    // 分页状态
    int m_page = 1;        // 当前页码
    int m_pageSize = 10;   // 每页条数
    int m_total = 0;       // 总记录数
    int m_totalPages = 1;  // 总页数
};

#endif // STATIONMANAGEMENTWIDGET_H
