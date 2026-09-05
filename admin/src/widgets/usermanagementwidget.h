#ifndef USERMANAGEMENTWIDGET_H
#define USERMANAGEMENTWIDGET_H

#include <QWidget>
#include <QJsonArray>
#include <QString>

class QLineEdit;
class QComboBox;
class QPushButton;
class QTableView;
class QLabel;
class UserManagementModel;

/**
 * @brief 用户管理页（PC 运营后台）
 *
 * 职责：用户列表展示 + 手机号搜索 + 状态筛选 + 冻结/解冻操作。
 * 数据全部来自 UserManagementModel（唯一数据入口），UI 不硬编码业务数据。
 *
 * 布局：
 *   - 顶部工具栏：手机号搜索框 / 状态筛选下拉框 / 查询 / 刷新
 *   - 中间表格：  QTableView 绑定 Model 的 QStandardItemModel，
 *                 每行最后一列动态安装"冻结"或"解冻"按钮
 *   - 底部分页栏：上一页 / 页码信息 / 下一页
 *
 * 交互逻辑：
 *   - 点击操作按钮 -> 确认对话框 -> setUserStatus() -> 成功后刷新当前页；
 *   - 搜索 / 状态筛选变化 -> 重置为第 1 页重新查询；
 *   - 上一页/下一页 -> 以新页码重新调用 fetchUsers()。
 */
class UserManagementWidget : public QWidget
{
    Q_OBJECT
public:
    explicit UserManagementWidget(QWidget *parent = nullptr);

    /**
     * @brief 设置管理员 Token 并发起首次用户列表拉取（由 MainWindow 调用）
     * 内部转发给 UserManagementModel::setAuthToken()，随后查询第 1 页。
     */
    void setAuthToken(const QString &token);

private slots:
    void onQueryClicked();            // 搜索框"查询"按钮
    void onRefreshClicked();          // 刷新当前页
    void onPrevPage();                // 上一页
    void onNextPage();                // 下一页
    void onStatusFilterChanged(int index); // 状态筛选变化 -> 重置第 1 页
    void onUsersReady(const QJsonArray &users, int total, int page, int pageSize);
    void onOperationSuccess(const QString &msg);
    void onErrorOccurred(const QString &errorMsg);

private:
    void buildUi();                   // 构建界面骨架与样式（无 .ui 文件，纯代码布局）
    void applyFiltersAndFetch(int page); // 以当前筛选条件请求指定页
    void installActionButtons();      // 依据每行状态安装"冻结"/"解冻"按钮
    void updatePager(int total, int page, int pageSize); // 刷新分页栏
    void confirmAndSetStatus(int userId, const QString &phone, int targetStatus);

    UserManagementModel *m_model;     // 数据源（唯一数据入口）

    // 顶部工具栏
    QLineEdit  *m_searchEdit = nullptr;
    QComboBox  *m_statusCombo = nullptr;
    QPushButton *m_btnQuery = nullptr;
    QPushButton *m_btnRefresh = nullptr;
    // 中间表格
    QTableView *m_tableView = nullptr;
    // 底部分页栏
    QPushButton *m_btnPrev = nullptr;
    QPushButton *m_btnNext = nullptr;
    QLabel      *m_pageLabel = nullptr;

    // 分页状态
    int m_page = 1;        // 当前页码
    int m_pageSize = 10;   // 每页条数
    int m_total = 0;       // 总记录数
    int m_totalPages = 1;  // 总页数
};

#endif // USERMANAGEMENTWIDGET_H
