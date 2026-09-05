#ifndef PILEMANAGEMENTWIDGET_H
#define PILEMANAGEMENTWIDGET_H

#include <QWidget>
#include <QJsonArray>
#include <QPointer>
#include <QString>

class QComboBox;
class QLineEdit;
class QPushButton;
class QTableView;
class QLabel;
class PileManagementModel;

/**
 * @brief 充电桩管理页（PC 运营后台）
 *
 * 职责：充电桩列表展示 + 状态/类型筛选 + 远程重启。
 * 数据全部来自 PileManagementModel（唯一数据入口），UI 不硬编码业务数据。
 *
 * 布局：
 *   - 顶部工具栏：状态筛选下拉 / 类型筛选下拉 / 查询 / 刷新
 *     （注意：本页面没有"所属电站"筛选与站名搜索框）
 *   - 中间表格：  QTableView 绑定 Model 的 QStandardItemModel（8 列），
 *                 操作列动态安装"重启"按钮
 *   - 底部分页栏：上一页 / 页码信息 / 下一页
 *
 * 远程重启流程：
 *   确认弹窗 -> 按钮变为"重启中..."并禁用 -> POST restart
 *   -> 成功：提示"电桩 [pile_id] 重启成功"并刷新当前页（按钮随表格重建恢复）
 *   -> 失败：弹出服务端错误信息，并恢复按钮可点击状态
 */
class PileManagementWidget : public QWidget
{
    Q_OBJECT
public:
    explicit PileManagementWidget(QWidget *parent = nullptr);

    /**
     * @brief 设置管理员 Token 并发起首次列表拉取（由 MainWindow 调用）
     * 内部转发给 PileManagementModel::setAuthToken()，随后查询第 1 页。
     */
    void setAuthToken(const QString &token);

private slots:
    void onQueryClicked();             // "查询"按钮：重置第 1 页
    void onRefreshClicked();           // "刷新"按钮：刷新当前页
    void onPrevPage();                 // 上一页
    void onNextPage();                 // 下一页
    void onFilterChanged(int index);   // 状态/类型筛选变化 -> 重置第 1 页
    void onPilesReady(const QJsonArray &piles, int total, int page, int pageSize);
    void onRestartSuccess(const QString &msg);
    void onErrorOccurred(const QString &errorMsg);

private:
    void buildUi();                    // 构建界面骨架与样式（无 .ui 文件，纯代码布局）
    void applyFiltersAndFetch(int page); // 以当前筛选条件请求指定页
    void installActionButtons();       // 依据每行数据安装"重启"按钮
    void updatePager(int total, int page, int pageSize); // 刷新分页栏
    void confirmAndRestart(const QString &pileId, QPushButton *btn); // 确认并执行重启

    PileManagementModel *m_model;      // 数据源（唯一数据入口）

    // 顶部工具栏
    QComboBox   *m_statusCombo = nullptr; // 状态筛选（全部/空闲/充电中/故障/离线）
    QComboBox   *m_typeCombo = nullptr;   // 类型筛选（全部/快充/慢充）
    QPushButton *m_btnQuery = nullptr;
    QPushButton *m_btnRefresh = nullptr;
    // 中间表格
    QTableView  *m_tableView = nullptr;
    // 底部分页栏
    QPushButton *m_btnPrev = nullptr;
    QPushButton *m_btnNext = nullptr;
    QLabel      *m_pageLabel = nullptr;

    // 重启中的按钮（失败时需恢复其可用状态；刷新重建后自动失效）
    QPointer<QPushButton> m_pendingRestartBtn;

    // 分页状态
    int m_page = 1;        // 当前页码
    int m_pageSize = 10;   // 每页条数
    int m_total = 0;       // 总记录数
    int m_totalPages = 1;  // 总页数
};

#endif // PILEMANAGEMENTWIDGET_H
