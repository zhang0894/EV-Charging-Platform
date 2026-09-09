#include "ui/mainwindow.h"

#include "ui/chargepage.h"
#include "ui/orderspage.h"
#include "ui/stationlistpage.h"
#include "ui/profilepage.h"
#include "ui/navigationpage.h"
#include "core/apiclient.h"
#include "core/session.h"

#include <QButtonGroup>
#include <QHBoxLayout>
#include <QMessageBox>
#include <QPushButton>
#include <QStackedWidget>
#include <QTimer>
#include <QVBoxLayout>

MainWindow::MainWindow(QWidget *parent)
    : QWidget(parent)
{
    setObjectName(QStringLiteral("MainWindow"));   // 主题里给它铺渐变底色
    setWindowTitle(QStringLiteral("充电用户端"));
    setFixedSize(420, 760);

    auto *lay = new QVBoxLayout(this);
    lay->setContentsMargins(0, 0, 0, 0);
    lay->setSpacing(0);

    m_pages = new QStackedWidget(this);
    auto *stations = new StationListPage(this);
    m_pages->addWidget(stations);
    m_chargePage = new ChargePage(this);
    m_pages->addWidget(m_chargePage);
    m_ordersPage = new OrdersPage(this);
    m_pages->addWidget(m_ordersPage);
    m_profilePage = new ProfilePage(this);
    m_pages->addWidget(m_profilePage);
    auto *navigation = new NavigationPage(this);
    m_pages->addWidget(navigation);
    lay->addWidget(m_pages, 1);

    auto *nav = new QWidget(this);
    nav->setObjectName(QStringLiteral("Nav"));
    auto *nl = new QHBoxLayout(nav);
    nl->setContentsMargins(0, 0, 0, 0);
    nl->setSpacing(0);
    m_navGroup = new QButtonGroup(this);
    m_navGroup->setExclusive(true);
    const QStringList names = {QStringLiteral("电站"), QStringLiteral("充电"),
                               QStringLiteral("订单"), QStringLiteral("我的")};
    for (int i = 0; i < names.size(); ++i) {
        auto *btn = new QPushButton(names[i], nav);
        btn->setObjectName(QStringLiteral("NavBtn"));
        btn->setCheckable(true);
        m_navGroup->addButton(btn, i);
        nl->addWidget(btn, 1);
    }
    lay->addWidget(nav);
    connect(stations, &StationListPage::stationSelected, this, &MainWindow::openChargeForStation);
    connect(m_profilePage, &ProfilePage::logoutRequested, this, &MainWindow::logoutRequested);
    connect(m_chargePage, &ChargePage::navigationRequested, this, [this, navigation](double lat, double lng, const QString &name) {
        navigation->setDestination(lat, lng, name);
        m_pages->setCurrentWidget(navigation);
    });
    connect(navigation,&NavigationPage::backRequested,this,[this]{showPage(PageStations);});
    // 管理端冻结账户：server 对登录/充值/预约/结算都会回 10002，
    // 个人资料里 status=2；两条路都汇到这一个信号
    connect(Api::events(), &Api::Events::accountFrozen, this, &MainWindow::onAccountFrozen);

#if QT_VERSION >= QT_VERSION_CHECK(5, 15, 0)
    connect(m_navGroup, &QButtonGroup::idClicked, this, &MainWindow::showPage);
#else
    connect(m_navGroup, qOverload<int>(&QButtonGroup::buttonClicked),
            this, &MainWindow::showPage);
#endif

    m_navGroup->button(PageStations)->setChecked(true);
    m_pages->setCurrentIndex(PageStations);
}

void MainWindow::showPage(int page)
{
    m_navGroup->button(page)->setChecked(true);
    m_pages->setCurrentIndex(page);
    if (page == PageCharge)
        m_chargePage->enter();     // 每次进入都要做「未完成订单」检查
    else if (page == PageOrders)
        m_ordersPage->refresh();
    else if (page == PageMine)
        m_profilePage->reload();
}

void MainWindow::openChargeForStation(int stationId)
{
    m_navGroup->button(PageCharge)->setChecked(true);
    m_pages->setCurrentIndex(PageCharge);
    m_chargePage->enterWithStation(stationId);
}

void MainWindow::onAccountFrozen()
{
    if (m_frozenShown)
        return;
    m_frozenShown = true;
    // 用非模态弹窗：截图自检和定时器不会被 exec() 卡住
    auto *box = new QMessageBox(QMessageBox::Warning, QStringLiteral("账户已冻结"),
                                QStringLiteral("账户已冻结，请联系管理员"),
                                QMessageBox::Ok, this);
    box->setAttribute(Qt::WA_DeleteOnClose);
    connect(box, &QMessageBox::finished, this, [this] {
        Session::i().setToken({});
        Session::i().setRefreshToken({});
        Session::i().setUserId(0);
        emit logoutRequested();      // 回登录页；再登录会被 server 以 10002 拒绝
    });
    box->show();
    // 截图自检：NCS_SHOT=a.png 时把这个提示框另存为 a.png.frozen.png
    if (!qEnvironmentVariableIsEmpty("NCS_SHOT")) {
        const QString file = qEnvironmentVariable("NCS_SHOT") + QStringLiteral(".frozen.png");
        QTimer::singleShot(600, box, [box, file] { box->grab().save(file); });
    }
}
