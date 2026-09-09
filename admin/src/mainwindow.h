#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>
#include <QButtonGroup>

QT_BEGIN_NAMESPACE
namespace Ui { class MainWindow; }
QT_END_NAMESPACE

/**
 * @brief 主窗口
 *
 * 职责单一：负责侧边栏菜单与内容区 QStackedWidget 的页面切换，
 * 以及底部日志输出。不包含任何业务模块逻辑。
 *
 * 仅引入与 Dashboard（销售业绩）相关的页面；其余4个菜单项
 * 暂用占位页，待后续模块实现时替换。
 */
class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    explicit MainWindow(const QString &authToken, const QString &username,
                        QWidget *parent = nullptr);
    ~MainWindow();

    /**
     * @brief 跳转至订单管理页并按用户筛选（跨页入口，供用户管理"查看订单"调用）
     * @param userId 用户ID（优先使用，>0 时有效）
     * @param phone  手机号（user_id 无效时的备选）
     *
     * 复用侧边栏菜单的切换路径：同步按钮选中态、切换 Stack 页面并记录日志，
     * 随后调用订单页 setFilterByUser() 自动填入条件并发起查询。
     */
    void showOrdersForUser(int userId, const QString &phone);

signals:
    /** 用户点击"退出登录"并确认后发出，由 main.cpp 接管回到登录流程 */
    void logoutRequested();

public slots:
    /** 追加一行操作日志到底部日志区 */
    void appendLog(const QString &message);

private slots:
    /** 侧边栏菜单切换：根据按钮索引切换 Stack 页面 */
    void onMenuClicked(int id);

private:
    void setupMenu();          // 初始化侧边栏按钮组
    void setupPlaceholders();  // 初始化占位页文案
    void applyTheme(bool dark); // 应用浅色/深色主题
    void toggleTheme();         // 切换白天/夜晚主题

    Ui::MainWindow *ui;
    QButtonGroup *m_menuGroup; // 侧边栏按钮互斥组
    bool m_isDark = false;     // 当前是否为深色主题
};

#endif // MAINWINDOW_H
