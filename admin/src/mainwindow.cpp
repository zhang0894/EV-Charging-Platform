#include "mainwindow.h"
#include "dashboardwidget.h"
#include "dashboardmodel.h"
#include "pilestatuswidget.h"
#include "usermanagementwidget.h"
#include "ui_mainwindow.h"

#include <QDateTime>

MainWindow::MainWindow(const QString &authToken, QWidget *parent)
    : QMainWindow(parent)
    , ui(new Ui::MainWindow)
    , m_menuGroup(new QButtonGroup(this))
{
    ui->setupUi(this);

    // 设置窗口图标与默认尺寸
    setWindowIcon(QIcon(QStringLiteral(":/img/app-logo.svg")));
    resize(1280, 800);

    // 菜单按钮互斥（同一时刻仅一个选中）
    m_menuGroup->setExclusive(true);

    setupMenu();

    // 默认选中"销售业绩"，展示 Dashboard
    m_menuGroup->button(0)->setChecked(true);
    ui->contentStack->setCurrentIndex(0);

    // 连接 Dashboard 的日志信号到主窗口日志区
    // 仅引入 Dashboard 相关代码，不引入其他模块
    DashboardWidget *dashboard = qobject_cast<DashboardWidget *>(
        ui->contentStack->widget(0));
    if (dashboard) {
        connect(dashboard, &DashboardWidget::logMessage,
                this, &MainWindow::appendLog);
    }

    // 将管理员 Token 传递给 DashboardModel（setAuthToken 内部会自动触发 fetchData）
    DashboardModel *model = findChild<DashboardModel *>();
    if (model) {
        model->setAuthToken(authToken);
    }

    // 电桩状态页（索引 1）：用 PileStatusWidget 替换占位页，
    // 菜单按钮 btnPileStatus(id=1) 与页面索引的映射关系保持不变。
    PileStatusWidget *pileStatusPage = new PileStatusWidget(this);
    QWidget *oldPileStatusPage = ui->contentStack->widget(1);
    ui->contentStack->removeWidget(oldPileStatusPage);
    ui->contentStack->insertWidget(1, pileStatusPage);
    delete oldPileStatusPage;

    // 将管理员 Token 传递给 PileStatusModel（首次拉取由页面 showEvent 触发）
    pileStatusPage->setAuthToken(authToken);

    // 用户管理页（索引 4）：用 UserManagementWidget 替换占位页，
    // 菜单按钮 btnUserManage(id=4) 与页面索引的映射关系保持不变。
    UserManagementWidget *userPage = new UserManagementWidget(this);
    QWidget *oldUserPage = ui->contentStack->widget(4);
    ui->contentStack->removeWidget(oldUserPage);
    ui->contentStack->insertWidget(4, userPage);
    delete oldUserPage;

    // 将管理员 Token 传递给 UserManagementModel（内部随即拉取第 1 页用户列表）
    userPage->setAuthToken(authToken);

    appendLog(tr("系统启动完成，欢迎使用充电桩运营管理平台"));
}

MainWindow::~MainWindow()
{
    delete ui;
}

void MainWindow::setupMenu()
{
    // 将5个侧边栏按钮加入互斥按钮组，id 对应 contentStack 页面索引
    m_menuGroup->addButton(ui->btnDashboard, 0);
    m_menuGroup->addButton(ui->btnPileStatus, 1);
    m_menuGroup->addButton(ui->btnPileManage, 2);
    m_menuGroup->addButton(ui->btnStationManage, 3);
    m_menuGroup->addButton(ui->btnUserManage, 4);

    connect(m_menuGroup, &QButtonGroup::idClicked,
            this, &MainWindow::onMenuClicked);
}

void MainWindow::onMenuClicked(int id)
{
    ui->contentStack->setCurrentIndex(id);

    // 记录页面切换日志
    static const QStringList names = {
        tr("销售业绩"), tr("电桩状态"), tr("充电桩管理"),
        tr("充电站管理"), tr("用户管理")
    };
    appendLog(tr("切换页面：%1").arg(names.value(id, tr("未知"))));
}

void MainWindow::appendLog(const QString &message)
{
    // 日志前缀时间戳，便于排查操作轨迹
    const QString stamp = QDateTime::currentDateTime()
                              .toString("yyyy-MM-dd hh:mm:ss");
    ui->logEdit->appendPlainText(QStringLiteral("[%1] %2").arg(stamp, message));
}
