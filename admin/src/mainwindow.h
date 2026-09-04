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
    explicit MainWindow(const QString &authToken, QWidget *parent = nullptr);
    ~MainWindow();

public slots:
    /** 追加一行操作日志到底部日志区 */
    void appendLog(const QString &message);

private slots:
    /** 侧边栏菜单切换：根据按钮索引切换 Stack 页面 */
    void onMenuClicked(int id);

private:
    void setupMenu();          // 初始化侧边栏按钮组
    void setupPlaceholders();  // 初始化占位页文案

    Ui::MainWindow *ui;
    QButtonGroup *m_menuGroup; // 侧边栏按钮互斥组
};

#endif // MAINWINDOW_H
