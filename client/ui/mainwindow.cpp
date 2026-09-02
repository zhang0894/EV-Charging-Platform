#include "ui/mainwindow.h"

#include "ui/chargepage.h"
#include "ui/placeholderpage.h"

#include <QButtonGroup>
#include <QHBoxLayout>
#include <QPushButton>
#include <QStackedWidget>
#include <QTimer>
#include <QVBoxLayout>

MainWindow::MainWindow(QWidget *parent)
    : QWidget(parent)
{
    setWindowTitle(QStringLiteral("充电用户端"));
    setFixedSize(420, 760);

    auto *lay = new QVBoxLayout(this);
    lay->setContentsMargins(0, 0, 0, 0);
    lay->setSpacing(0);

    m_pages = new QStackedWidget(this);
    m_pages->addWidget(new PlaceholderPage(
        QStringLiteral("电站列表 / 电站详情\n（B 负责：UC-U-02 ~ 04）"), this));
    m_pages->addWidget(new PlaceholderPage(
        QStringLiteral("充电流程\n（A 负责：UC-U-06 ~ 09）"), this));
    m_pages->addWidget(new PlaceholderPage(
        QStringLiteral("我的订单\n（A 负责：UC-U-10）"), this));
    m_pages->addWidget(new PlaceholderPage(
        QStringLiteral("个人中心\n（B 负责：UC-U-01 / 05）"), this));
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

#if QT_VERSION >= QT_VERSION_CHECK(5, 15, 0)
    connect(m_navGroup, &QButtonGroup::idClicked, this, &MainWindow::showPage);
#else
    connect(m_navGroup, qOverload<int>(&QButtonGroup::buttonClicked),
            this, &MainWindow::showPage);
#endif

    // 默认停在充电页；enter() 可能弹提示框，
    // 不能在构造函数里（窗口还没 show）就阻塞，延迟到事件循环再进
    m_navGroup->button(PageCharge)->setChecked(true);
    m_pages->setCurrentIndex(PageCharge);
    QTimer::singleShot(0, this, [this] { showPage(PageCharge); });
}

void MainWindow::showPage(int page)
{
    m_navGroup->button(page)->setChecked(true);
    m_pages->setCurrentIndex(page);
    if (page == PageCharge)
        m_chargePage->enter();     // 每次进入都要做「未完成订单」检查
}

void MainWindow::openChargeForStation(int stationId)
{
    m_navGroup->button(PageCharge)->setChecked(true);
    m_pages->setCurrentIndex(PageCharge);
    m_chargePage->enterWithStation(stationId);
}