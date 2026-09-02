#include "ui/chargepage.h"

#include "core/session.h"

#include <QComboBox>
#include <QLabel>
#include <QListWidget>
#include <QMessageBox>
#include <QPushButton>
#include <QStackedWidget>
#include <QVBoxLayout>

using namespace ChargeService;

ChargePage::ChargePage(QWidget *parent)
    : QWidget(parent)
{
    auto *lay = new QVBoxLayout(this);
    lay->setContentsMargins(14, 14, 14, 14);
    m_views = new QStackedWidget(this);
    lay->addWidget(m_views);

    // ---------- 画面 0：选桩 ----------
    auto *pick = new QWidget(this);
    auto *pl = new QVBoxLayout(pick);
    pl->setSpacing(10);
    auto *title = new QLabel(QStringLiteral("发起充电"), pick);
    title->setObjectName(QStringLiteral("H1"));
    pl->addWidget(title);
    auto *cap1 = new QLabel(
        QStringLiteral("选择电站（B 的电站列表接入后由「选桩充电」跳转进来）"), pick);
    cap1->setObjectName(QStringLiteral("Cap"));
    pl->addWidget(cap1);
    m_stationBox = new QComboBox(pick);
    pl->addWidget(m_stationBox);
    auto *cap2 = new QLabel(QStringLiteral("空闲电桩"), pick);
    cap2->setObjectName(QStringLiteral("Cap"));
    pl->addWidget(cap2);
    m_pileList = new QListWidget(pick);
    pl->addWidget(m_pileList, 1);
    m_startBtn = new QPushButton(QStringLiteral("开始充电"), pick);
    pl->addWidget(m_startBtn);
    m_views->addWidget(pick);

    // ---------- 画面 1：未完成订单拦截 ----------
    auto *blocked = new QWidget(this);
    auto *bl = new QVBoxLayout(blocked);
    bl->setSpacing(10);
    auto *bt = new QLabel(QStringLiteral("未完成订单"), blocked);
    bt->setObjectName(QStringLiteral("H1"));
    bl->addWidget(bt);
    auto *warn = new QLabel(QStringLiteral("您有未完成的充电订单，请先结算"), blocked);
    warn->setObjectName(QStringLiteral("Warn"));
    bl->addWidget(warn);
    auto *card = new QWidget(blocked);
    card->setObjectName(QStringLiteral("Card"));
    auto *cl = new QVBoxLayout(card);
    m_blockedText = new QLabel(card);
    m_blockedText->setWordWrap(true);
    cl->addWidget(m_blockedText);
    bl->addWidget(card);
    bl->addStretch(1);
    m_views->addWidget(blocked);

    connect(m_stationBox, qOverload<int>(&QComboBox::currentIndexChanged),
            this, &ChargePage::reloadPiles);
    connect(m_startBtn, &QPushButton::clicked, this, &ChargePage::onStart);
}

// 说明书 UC-U-06：进入充电页先查未完成订单，有就强制拦截
void ChargePage::enter()
{
    ActiveOrder ao;
    if (findUnfinished(Session::i().userId(), &ao)) {
        showBlocked(ao);
        return;
    }
    showPick();
}

void ChargePage::enterWithStation(int stationId)
{
    enter();
    if (m_views->currentIndex() != 0)
        return;                    // 有未完成订单时不允许开新单
    const int idx = m_stationBox->findData(stationId);
    if (idx >= 0)
        m_stationBox->setCurrentIndex(idx);
}

void ChargePage::showPick()
{
    const int keep = m_stationBox->currentData().toInt();
    m_stationBox->blockSignals(true);
    m_stationBox->clear();
    for (const StationOpt &s : stationOptions())
        m_stationBox->addItem(QStringLiteral("%1　%2 元/度　空闲 %3")
                                  .arg(s.name).arg(s.price, 0, 'f', 2).arg(s.freeCount),
                              s.id);
    const int idx = m_stationBox->findData(keep);
    m_stationBox->setCurrentIndex(idx >= 0 ? idx : 0);
    m_stationBox->blockSignals(false);
    reloadPiles();
    m_views->setCurrentIndex(0);
}

void ChargePage::reloadPiles()
{
    m_pileList->clear();
    const int stationId = m_stationBox->currentData().toInt();
    for (const PileOpt &p : freePiles(stationId)) {
        auto *item = new QListWidgetItem(
            QStringLiteral("%1　%2　%3 kW").arg(p.code, p.typeText).arg(p.powerKw, 0, 'f', 1),
            m_pileList);
        item->setData(Qt::UserRole, p.id);
    }
    m_startBtn->setEnabled(m_pileList->count() > 0);
    if (m_pileList->count() == 0)
        new QListWidgetItem(QStringLiteral("该站暂无空闲电桩"), m_pileList);
    else
        m_pileList->setCurrentRow(0);
}

void ChargePage::showBlocked(const ActiveOrder &order)
{
    const QString statusText = order.status == 0 ? QStringLiteral("充电中")
                                                 : QStringLiteral("待结算");
    m_blockedText->setText(QStringLiteral("订单号：%1\n电站：%2\n电桩：%3\n"
                                          "开始时间：%4\n状态：%5")
                               .arg(order.id)
                               .arg(order.stationName, order.pileCode,
                                    order.startTime, statusText));
    m_views->setCurrentIndex(1);
}

void ChargePage::onStart()
{
    // TODO：UC-U-07 在这里接 ChargeService::startCharge
    QMessageBox::information(this, QStringLiteral("提示"),
                             QStringLiteral("开始充电功能开发中"));
}
