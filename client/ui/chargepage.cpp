#include "ui/chargepage.h"

#include "core/session.h"
#include "ui/receipttext.h"

#include <QDateTime>
#include <QHBoxLayout>
#include <QLabel>
#include <QListWidget>
#include <QMessageBox>
#include <QPushButton>
#include <QStackedWidget>
#include <QTime>
#include <QTimer>
#include <QVBoxLayout>

using namespace ChargeService;

namespace {

QLabel *cap(const QString &t, QWidget *parent)
{
    auto *l = new QLabel(t, parent);
    l->setObjectName(QStringLiteral("Cap"));
    return l;
}

} // namespace

ChargePage::ChargePage(QWidget *parent)
    : QWidget(parent)
{
    auto *lay = new QVBoxLayout(this);
    lay->setContentsMargins(14, 14, 14, 14);
    m_views = new QStackedWidget(this);
    lay->addWidget(m_views);

    // ---------- 画面 0：选桩（B 的设计） ----------
    auto *pick = new QWidget(this);
    auto *pl = new QVBoxLayout(pick);
    pl->setSpacing(10);
    m_stationTitle = new QLabel(QStringLiteral("请选择充电站"), pick);
    m_stationTitle->setObjectName(QStringLiteral("H1"));
    pl->addWidget(m_stationTitle);
    m_stationMeta = new QLabel(pick);
    m_stationMeta->setObjectName(QStringLiteral("Cap"));
    m_stationMeta->setWordWrap(true);
    pl->addWidget(m_stationMeta);
    m_navigationBtn = new QPushButton(QStringLiteral("一键导航"), pick);
    m_navigationBtn->setObjectName(QStringLiteral("Ghost"));
    pl->addWidget(m_navigationBtn, 0, Qt::AlignLeft);
    pl->addWidget(cap(QStringLiteral("该站电桩状态"), pick));
    m_pileList = new QListWidget(pick);
    m_pileList->setObjectName(QStringLiteral("PileList"));
    m_pileList->setStyleSheet(QStringLiteral(
        "QListWidget#PileList::item:selected { border:2px solid #0E8A57; background:#F7FCF9; }"));
    pl->addWidget(m_pileList, 1);
    m_reserveBtn = new QPushButton(QStringLiteral("预约充电"), pick);
    pl->addWidget(m_reserveBtn);
    m_views->addWidget(pick);

    // ---------- 画面 1：结算小票（UC-U-09） ----------
    auto *settlePage = new QWidget(this);
    auto *sl = new QVBoxLayout(settlePage);
    sl->setSpacing(10);
    auto *st = new QLabel(QStringLiteral("订单结算"), settlePage);
    st->setObjectName(QStringLiteral("H1"));
    sl->addWidget(st);
    m_settleHint = new QLabel(QStringLiteral("请确认支付以完成订单"), settlePage);
    m_settleHint->setObjectName(QStringLiteral("Warn"));
    sl->addWidget(m_settleHint);
    auto *rbox = new QWidget(settlePage);
    rbox->setObjectName(QStringLiteral("Card"));
    auto *receiptLay = new QVBoxLayout(rbox);
    m_receiptText = new QLabel(rbox);
    m_receiptText->setTextFormat(Qt::RichText);
    m_receiptText->setWordWrap(true);
    receiptLay->addWidget(m_receiptText);
    sl->addWidget(rbox);
    sl->addStretch(1);
    m_payBtn = new QPushButton(QStringLiteral("确认支付"), settlePage);
    sl->addWidget(m_payBtn);
    m_views->addWidget(settlePage);

    // ---------- 画面 2：充电中 ----------
    auto *charging = new QWidget(this);
    auto *gl = new QVBoxLayout(charging);
    gl->setSpacing(10);
    auto *gt = new QLabel(QStringLiteral("充电中"), charging);
    gt->setObjectName(QStringLiteral("H1"));
    gl->addWidget(gt);
    m_chargeStation = cap(QString(), charging);
    gl->addWidget(m_chargeStation);

    auto *box = new QWidget(charging);
    box->setObjectName(QStringLiteral("Card"));
    auto *xl = new QVBoxLayout(box);
    xl->addWidget(cap(QStringLiteral("已充时长"), box), 0, Qt::AlignHCenter);
    m_duration = new QLabel(QStringLiteral("00:00:00"), box);
    m_duration->setObjectName(QStringLiteral("Big"));
    m_duration->setAlignment(Qt::AlignCenter);
    xl->addWidget(m_duration);
    m_kwh  = new QLabel(box);
    m_cost = new QLabel(box);
    m_capLabel = new QLabel(box);
    m_capLabel->setObjectName(QStringLiteral("Warn"));
    m_cost->setObjectName(QStringLiteral("Money"));
    m_kwh->setAlignment(Qt::AlignCenter);
    m_cost->setAlignment(Qt::AlignCenter);
    m_capLabel->setAlignment(Qt::AlignCenter);
    xl->addWidget(m_kwh);
    xl->addWidget(m_cost);
    xl->addWidget(m_capLabel);
    gl->addWidget(box);
    gl->addWidget(cap(QStringLiteral("费用由服务器实时计算"), charging),
                  0, Qt::AlignHCenter);
    gl->addStretch(1);
    auto *stopBtn = new QPushButton(QStringLiteral("结束充电"), charging);
    stopBtn->setObjectName(QStringLiteral("Danger"));
    gl->addWidget(stopBtn);
    m_views->addWidget(charging);

    // ---------- 画面 3：已预约（桩详情 + 倒计时） ----------
    auto *reserved = new QWidget(this);
    auto *rl = new QVBoxLayout(reserved);
    rl->setSpacing(10);
    auto *rt = new QLabel(QStringLiteral("预约成功"), reserved);
    rt->setObjectName(QStringLiteral("H1"));
    rl->addWidget(rt);
    rl->addWidget(cap(QStringLiteral("请在倒计时内到站；超时押金 ¥20 不退，主动取消收违约金 ¥5"),
                      reserved));
    auto *rcard = new QWidget(reserved);
    rcard->setObjectName(QStringLiteral("Card"));
    auto *rcl = new QVBoxLayout(rcard);
    m_reserveText = new QLabel(rcard);
    m_reserveText->setWordWrap(true);
    rcl->addWidget(m_reserveText);
    rcl->addWidget(cap(QStringLiteral("剩余到站时间"), rcard), 0, Qt::AlignHCenter);
    m_countdown = new QLabel(QStringLiteral("--:--"), rcard);
    m_countdown->setObjectName(QStringLiteral("Big"));
    m_countdown->setAlignment(Qt::AlignCenter);
    rcl->addWidget(m_countdown);
    rl->addWidget(rcard);
    rl->addStretch(1);
    auto *arriveBtn = new QPushButton(QStringLiteral("已到站，开始充电"), reserved);
    rl->addWidget(arriveBtn);
    auto *cancelBtn = new QPushButton(QStringLiteral("取消预约"), reserved);
    cancelBtn->setObjectName(QStringLiteral("Danger"));
    rl->addWidget(cancelBtn);
    m_views->addWidget(reserved);

    // 时长每秒本地走（平滑不跳），电量/费用每 2 秒问一次 server
    m_timer = new QTimer(this);
    m_timer->setInterval(1000);
    m_reserveTimer = new QTimer(this);
    m_reserveTimer->setInterval(1000);
    m_pickTimer = new QTimer(this);      // 桩状态是活的（模拟车流/别人预约），选桩页定时刷新
    m_pickTimer->setInterval(5000);

    connect(m_reserveBtn, &QPushButton::clicked, this, &ChargePage::onReserve);
    connect(arriveBtn, &QPushButton::clicked, this, &ChargePage::onArrive);
    connect(cancelBtn, &QPushButton::clicked, this, &ChargePage::onCancelReserve);
    connect(stopBtn, &QPushButton::clicked, this, &ChargePage::onStop);
    connect(m_payBtn, &QPushButton::clicked, this, &ChargePage::onPay);
    connect(m_timer, &QTimer::timeout, this, &ChargePage::onTick);
    connect(m_reserveTimer, &QTimer::timeout, this, &ChargePage::onReserveTick);
    connect(m_pickTimer, &QTimer::timeout, this, [this] {
        if (m_views->currentIndex() == 0 && m_stationId > 0)
            reloadPiles();
    });
    connect(m_navigationBtn, &QPushButton::clicked, this, [this] {
        for (const StationOpt &station : stationOptions()) {
            if (station.id == m_stationId) {
                emit navigationRequested(station.latitude, station.longitude,
                                         station.name);
                return;
            }
        }
    });
}

// 进入充电页：未完成订单 > 有效预约 > 选桩
void ChargePage::enter()
{
    ActiveOrder ao;
    if (findUnfinished(Session::i().userId(), &ao)) {
        if (ao.status == 0)
            showCharging(ao);      // 数据在 server，换机/重开也能接着看
        else
            showSettle(ao.id);
        return;
    }
    Reservation r;
    if (activeReservation(&r)) {   // 预约在 server，换机/重开都还在
        showReserved(r);
        return;
    }
    showPick();
}

void ChargePage::enterWithStation(int stationId)
{
    m_stationId = stationId;
    enter();
}

void ChargePage::showPick()
{
    m_timer->stop();
    m_reserveTimer->stop();
    m_pickTimer->start();
    if (m_stationId <= 0) {
        m_stationTitle->setText(QStringLiteral("请选择充电站"));
        m_stationMeta->setText(QStringLiteral("从「电站」页选择一个充电站进入"));
        m_pileList->clear();
        m_reserveBtn->setEnabled(false);
        m_views->setCurrentIndex(0);
        return;
    }
    for (const StationOpt &s : stationOptions())
        if (s.id == m_stationId) {
            m_stationTitle->setText(s.name);
            m_stationPrice = s.price;
            m_stationMeta->setText(QStringLiteral("%1\n电价：%2 元/度 · 距当前位置：%3 km")
                                       .arg(s.address)
                                       .arg(s.price, 0, 'f', 2)
                                       .arg(s.distanceKm, 0, 'f', 1));
            break;
        }
    reloadPiles();
    m_views->setCurrentIndex(0);
}

void ChargePage::reloadPiles()
{
    const QString keep = m_pileList->currentItem()
        ? m_pileList->currentItem()->data(Qt::UserRole).toString() : QString();
    m_pileList->clear();
    for (const PileOpt &p : freePiles(m_stationId)) {
        const QString color = p.statusCode == 1 ? QStringLiteral("#0E8A57")
                            : p.statusCode == 3 ? QStringLiteral("#E8871A")
                                                : QStringLiteral("#D93025");
        auto *item = new QListWidgetItem(m_pileList);
        auto *card = new QWidget(m_pileList);
        // QListWidget 会按 item 的 sizeHint 裁剪 itemWidget；原来的 104px
        // 在字体较大或电桩名称换行时会把第二行文字遮住。给卡片和 item
        // 都留出足够的垂直空间，并让布局自行计算内容高度。
        card->setMinimumHeight(116);
        auto *row = new QHBoxLayout(card);
        row->setContentsMargins(12, 12, 12, 12);
        auto *info = new QLabel(QStringLiteral("%1\n%2 kW  ·  %3 元/度")
                                    .arg(p.code)
                                    .arg(p.powerKw, 0, 'f', 1)
                                    .arg(m_stationPrice, 0, 'f', 2), card);
        info->setWordWrap(true);
        row->addWidget(info);
        row->addStretch();
        auto *status = new QLabel(p.statusText, card);
        status->setStyleSheet(QStringLiteral("color:%1; font-weight:700;").arg(color));
        row->addWidget(status);
        const int cardHeight = qMax(116, card->sizeHint().height());
        item->setSizeHint(QSize(0, cardHeight + 8));
        m_pileList->setItemWidget(item, card);
        item->setData(Qt::UserRole, p.id);
        item->setData(Qt::UserRole + 1, p.statusCode);
        if (!keep.isEmpty() && p.id == keep)
            m_pileList->setCurrentItem(item);
    }
    m_reserveBtn->setEnabled(m_pileList->count() > 0);
    if (m_pileList->count() == 0)
        new QListWidgetItem(QStringLiteral("该站暂无电桩数据"), m_pileList);
}

void ChargePage::showSettle(const QString &orderId)
{
    m_pickTimer->stop();
    m_timer->stop();
    m_reserveTimer->stop();
    m_settleOrderId = orderId;
    m_paid = false;
    Receipt r;
    if (!orderDetail(orderId, &r))
        return;
    m_receiptText->setText(receiptHtml(r));
    m_settleHint->setText(QStringLiteral("请确认支付以完成订单"));
    m_payBtn->setText(QStringLiteral("确认支付"));
    m_views->setCurrentIndex(1);
}

void ChargePage::onPay()
{
    if (m_paid) {                  // 已支付 → 按钮变「完成」回到选桩页
        showPick();
        return;
    }
    QString err;
    double deducted = 0, balance = 0;
    if (!settle(m_settleOrderId, &deducted, &balance, &err)) {
        QMessageBox::warning(this, QStringLiteral("结算失败"), err);
        return;
    }
    m_paid = true;
    m_settleHint->setText(QStringLiteral("支付成功！本单扣款 ￥%1，余额 ￥%2")
                              .arg(deducted, 0, 'f', 2).arg(balance, 0, 'f', 2));
    m_payBtn->setText(QStringLiteral("完成"));
}

// ================= 预约（UC-U-07 前半段，本版新增） =================

void ChargePage::onReserve()
{
    QListWidgetItem *item = m_pileList->currentItem();
    if (!item || item->data(Qt::UserRole + 1).toInt() != 1) {
        QMessageBox::warning(this, QStringLiteral("无法预约"),
                             QStringLiteral("请选择状态为空闲的充电桩"));
        return;
    }
    const QString pileId = item->data(Qt::UserRole).toString();
    PileOpt pile;
    for (const PileOpt &p : freePiles(m_stationId))
        if (p.id == pileId) { pile = p; break; }

    QString err;
    if (!reservePile(m_stationId, pile, m_stationTitle->text(), &err)) {
        QMessageBox::warning(this, QStringLiteral("无法预约"), err);
        reloadPiles();
        return;
    }
    Reservation r;
    const bool got = activeReservation(&r);
    const qint64 leftSec = got
        ? qMax<qint64>(0, (r.expiresAtMs - QDateTime::currentMSecsSinceEpoch()) / 1000)
        : 0;
    QMessageBox::information(this, QStringLiteral("预约成功"),
        QStringLiteral("预约成功！已支付押金 ¥%1（到站开始充电后全额退回）。\n"
                       "请在 %2 秒内到站；超时押金不退，主动取消收违约金 ¥%3。")
            .arg(reserveDeposit(), 0, 'f', 0)
            .arg(leftSec)
            .arg(cancelPenalty(), 0, 'f', 0));
    if (got)
        showReserved(r);
}

void ChargePage::showReserved(const Reservation &r)
{
    m_pickTimer->stop();
    m_timer->stop();
    m_reservation = r;
    if (m_reservation.powerKw <= 0)          // active-reservation 不带功率，去桩接口补
        for (const PileOpt &p : freePiles(r.stationId))
            if (p.id == r.pileId) { m_reservation.powerKw = p.powerKw; break; }
    const double balance = walletBalance();
    QString balText = balance >= 0
        ? QStringLiteral("当前余额：¥%1（押金已支付）").arg(balance, 0, 'f', 2)
        : QString();
    const QString typeLine = m_reservation.powerKw > 0
        ? QStringLiteral("%1 · %2 kW").arg(m_reservation.typeText)
              .arg(m_reservation.powerKw, 0, 'f', 1)
        : m_reservation.typeText;
    m_reserveText->setText(QStringLiteral("电站：%1\n电桩：%2\n类型：%3\n"
                                          "押金：¥%4（到站开充全额退回）\n%5")
                               .arg(r.stationName, r.pileCode, typeLine)
                               .arg(r.deposit, 0, 'f', 0)
                               .arg(balText));
    onReserveTick();
    m_views->setCurrentIndex(3);
    m_reserveTimer->start();
}

void ChargePage::onReserveTick()
{
    const qint64 leftMs = m_reservation.expiresAtMs
                          - QDateTime::currentMSecsSinceEpoch();
    if (leftMs <= 0) {             // 超时：server 端自动作废并没收押金
        m_reserveTimer->stop();
        if (qEnvironmentVariableIsEmpty("NCS_SHOT"))
            QMessageBox::information(this, QStringLiteral("预约超时"),
                QStringLiteral("超过到站时间，预约已自动取消，押金 ¥%1 不予退还。")
                    .arg(reserveDeposit(), 0, 'f', 0));
        showPick();
        return;
    }
    m_countdown->setText(QTime(0, 0).addMSecs(int(leftMs))
                             .toString(QStringLiteral("mm:ss")));
}

void ChargePage::onArrive()
{
    m_reserveTimer->stop();
    QString err;
    if (!startCharge(m_reservation.pileId, &err)) {
        QMessageBox::warning(this, QStringLiteral("无法开始充电"), err);
        Reservation r;                       // 预约保留，可重试或取消
        if (activeReservation(&r))
            showReserved(r);
        else
            showPick();
        return;
    }
    // server 在 start 时自动核销预约并全额退回押金，无需客户端清理
    ActiveOrder ao;
    if (findUnfinished(Session::i().userId(), &ao))
        showCharging(ao);
}

void ChargePage::onCancelReserve()
{
    if (QMessageBox::question(this, QStringLiteral("取消预约"),
                              QStringLiteral("取消预约将扣除违约金 ¥%1，退回 ¥%2。确定吗？")
                                  .arg(cancelPenalty(), 0, 'f', 0)
                                  .arg(reserveDeposit() - cancelPenalty(), 0, 'f', 0))
        != QMessageBox::Yes)
        return;
    m_reserveTimer->stop();
    double forfeit = 0, refund = 0;
    if (!cancelReservation(&forfeit, &refund)) {
        QMessageBox::warning(this, QStringLiteral("取消失败"),
                             QStringLiteral("取消预约失败，请重试"));
        m_reserveTimer->start();
        return;
    }
    QMessageBox::information(this, QStringLiteral("已取消"),
        QStringLiteral("预约已取消：扣除违约金 ¥%1，押金退回 ¥%2。")
            .arg(forfeit, 0, 'f', 0).arg(refund, 0, 'f', 0));
    showPick();
}

// ================= 充电（含花费上限自动结束） =================

void ChargePage::showCharging(const ActiveOrder &order)
{
    m_pickTimer->stop();
    m_active = order;
    m_chargeStation->setText(QStringLiteral("%1 · %2")
                                 .arg(order.stationName, order.pileCode));
    // 上限 = 钱包余额 - 1 元（= 剩余可用 + 押金抵扣，再留 1 元缓冲）
    // 缓冲是给 2 秒轮询间隔用的：最快的桩一个周期约多走 0.2 元，
    // 留 1 元保证自动结束后余额绝不为负、运营不亏钱；查不到余额就不设限
    const double balance = walletBalance();
    m_costCap = balance > 1 ? balance - 1 : 0;
    m_capLabel->setText(m_costCap > 0
        ? QStringLiteral("本次可用上限 ¥%1（已留 ¥1 缓冲），到达后自动结束")
              .arg(m_costCap, 0, 'f', 2)
        : QString());
    m_tick = 0;
    onTick();
    m_views->setCurrentIndex(2);
    m_timer->start();
}

void ChargePage::onTick()
{
    // 偶数拍问 server（它才是计费的唯一权威）；每一拍都本地刷新时长，秒表不跳
    if (m_tick++ % 2 == 0) {
        ActiveOrder ao;
        if (!findUnfinished(Session::i().userId(), &ao)) {
            m_timer->stop();       // 订单没了（可能在别处被结算）
            showPick();
            return;
        }
        if (ao.status != 0) {
            showSettle(ao.id);     // 在别处被停止 → 进结算页
            return;
        }
        m_active = ao;
        m_kwh->setText(QStringLiteral("累计电量 %1 度").arg(ao.kwh, 0, 'f', 2));
        m_cost->setText(QStringLiteral("当前费用 ￥%1").arg(ao.amount, 0, 'f', 2));

        // 花费到达上限 → 自动结束，绝不超过「剩余 + 押金」
        if (m_costCap > 0 && ao.amount >= m_costCap) {
            m_timer->stop();
            QString err;
            stopCharge(m_active.id, &err);
            if (qEnvironmentVariableIsEmpty("NCS_SHOT"))
                QMessageBox::information(this, QStringLiteral("已自动结束"),
                    QStringLiteral("费用已达可用上限 ¥%1（余额+押金），"
                                   "已自动结束充电。").arg(m_costCap, 0, 'f', 2));
            ActiveOrder done;
            if (findUnfinished(Session::i().userId(), &done))
                showSettle(done.id);
            else
                showPick();
            return;
        }
    }
    const QDateTime start = QDateTime::fromString(
        m_active.startTime, QStringLiteral("yyyy-MM-dd HH:mm:ss"));
    const qint64 secs = qMax<qint64>(0, start.secsTo(QDateTime::currentDateTime()));
    m_duration->setText(QTime(0, 0).addSecs(int(secs))
                            .toString(QStringLiteral("HH:mm:ss")));
}

void ChargePage::onStop()
{
    if (QMessageBox::question(this, QStringLiteral("结束充电"),
                              QStringLiteral("确定要结束本次充电吗？"))
        != QMessageBox::Yes)
        return;
    m_timer->stop();
    QString err;
    if (!stopCharge(m_active.id, &err)) {
        QMessageBox::warning(this, QStringLiteral("结束失败"), err);
        m_timer->start();
        return;
    }
    ActiveOrder ao;
    if (findUnfinished(Session::i().userId(), &ao))
        showSettle(ao.id);
    else
        showPick();
}
