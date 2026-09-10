#include "ui/chargepage.h"

#include "core/session.h"
#include "core/userservice.h"
#include "ui/receipttext.h"
#include "ui/socgauge.h"
#include "ui/carart.h"

#include <QApplication>
#include <QClipboard>
#include <QDateTime>
#include <QDialog>
#include <QFrame>
#include <QFontMetrics>
#include <QHBoxLayout>
#include <QLabel>
#include <QListWidget>
#include <QMessageBox>
#include <QPointer>
#include <QPushButton>
#include <QStackedWidget>
#include <QThread>
#include <QTime>
#include <QtMath>
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

// server 的桩名是「站名-01号慢充桩」，站名已在页顶显示，卡片里只留后半段
QString shortPileName(const QString &pileName, const QString &stationName)
{
    if (!stationName.isEmpty() && pileName.startsWith(stationName)) {
        QString rest = pileName.mid(stationName.size());
        while (rest.startsWith(QLatin1Char('-')) || rest.startsWith(QLatin1Char(' ')))
            rest.remove(0, 1);
        if (!rest.isEmpty())
            return rest;
    }
    return pileName;
}

QString pileStatusColor(int statusCode)
{
    switch (statusCode) {
    case 1:  return QStringLiteral("#0E8A57");   // 空闲
    case 2:
    case 3:
    case 4:  return QStringLiteral("#E8871A");   // 准备/充电中/待拔枪
    case 8:  return QStringLiteral("#D93025");   // 已预约锁定
    default: return QStringLiteral("#D93025");   // 故障/离线
    }
}

// 选桩列表：卡片本身透明，选中时露出 item 的绿色边框；padding 交给卡片自己的 margin
const int  PileCardMarginH = 14;
const int  PileCardMarginV = 10;
const int  PileItemInset   = 8;    // ::item 的 margin(4px 2px)+边框，用来估算文字可用宽度

} // namespace

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
    m_stationTitle = new QLabel(QStringLiteral("请选择充电站"), pick);
    m_stationTitle->setObjectName(QStringLiteral("H1"));
    m_stationTitle->setWordWrap(true);       // 站名很长，不能被右边裁掉
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
    m_pileList->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_pileList->setStyleSheet(QStringLiteral(
        "QListWidget#PileList::item { padding:0; margin:4px 2px; }"
        "QListWidget#PileList::item:selected { border:2px solid #0E8A57; background:#F7FCF9; }"));
    pl->addWidget(m_pileList, 1);
    m_reserveBtn = new QPushButton(QStringLiteral("预约充电"), pick);
    pl->addWidget(m_reserveBtn);
    m_views->addWidget(pick);

    // ---------- 画面 1：结算小票 ----------
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

    // ---------- 画面 2：充电中（参考小程序：仪表盘 + 车 + 三格统计 + 胶囊按钮） ----------
    auto *charging = new QWidget(this);
    auto *gl = new QVBoxLayout(charging);
    gl->setContentsMargins(4, 0, 4, 0);
    gl->setSpacing(6);
    auto *orderRow = new QHBoxLayout;
    orderRow->addStretch();
    auto *orderCap = new QLabel(QStringLiteral("订单编号："), charging);
    orderCap->setObjectName(QStringLiteral("OrderNo"));
    orderRow->addWidget(orderCap);
    m_orderNo = new QLabel(charging);
    m_orderNo->setObjectName(QStringLiteral("OrderNo"));
    orderRow->addWidget(m_orderNo);
    auto *copyBtn = new QPushButton(QStringLiteral("复制"), charging);
    copyBtn->setObjectName(QStringLiteral("Small"));
    orderRow->addWidget(copyBtn);
    orderRow->addStretch();
    gl->addLayout(orderRow);
    m_gauge = new SocGauge(charging);
    gl->addWidget(m_gauge, 0, Qt::AlignHCenter);
    gl->addWidget(new CarArt(charging), 0, Qt::AlignHCenter);
    m_chargeStation = new QLabel(charging);
    m_chargeStation->setObjectName(QStringLiteral("CardTitle"));
    m_chargeStation->setAlignment(Qt::AlignCenter);
    m_chargeStation->setWordWrap(true);
    gl->addWidget(m_chargeStation);
    m_deviceNo = cap(QString(), charging);
    m_deviceNo->setAlignment(Qt::AlignCenter);
    gl->addWidget(m_deviceNo);

    auto *stats = new QHBoxLayout;
    stats->setContentsMargins(0, 10, 0, 6);
    auto addStat = [&](QLabel *&value, const QString &caption) {
        auto *col = new QVBoxLayout;
        col->setSpacing(2);
        value = new QLabel(QStringLiteral("--"), charging);
        value->setObjectName(QStringLiteral("Stat"));
        value->setAlignment(Qt::AlignCenter);
        auto *c = new QLabel(caption, charging);
        c->setObjectName(QStringLiteral("StatCap"));
        c->setAlignment(Qt::AlignCenter);
        col->addWidget(value);
        col->addWidget(c);
        stats->addLayout(col, 1);
    };
    auto addSep = [&] {
        auto *sep = new QFrame(charging);
        sep->setFrameShape(QFrame::VLine);
        sep->setStyleSheet(QStringLiteral("color:%1;").arg(Theme::Line));
        stats->addWidget(sep);
    };
    addStat(m_statCost, QStringLiteral("累计费用(元)"));
    addSep();
    addStat(m_statTime, QStringLiteral("累计充电时长"));
    addSep();
    addStat(m_statKwh, QStringLiteral("累计度数(度)"));
    gl->addLayout(stats);

    m_capLabel = new QLabel(charging);
    m_capLabel->setObjectName(QStringLiteral("Warn"));
    m_capLabel->setAlignment(Qt::AlignCenter);
    m_capLabel->setWordWrap(true);
    gl->addWidget(m_capLabel);
    gl->addStretch(1);
    auto *stopBtn = new QPushButton(QStringLiteral("停止充电"), charging);
    gl->addWidget(stopBtn);
    auto *detailBtn = new QPushButton(QStringLiteral("查看详情"), charging);
    detailBtn->setObjectName(QStringLiteral("Secondary"));
    gl->addWidget(detailBtn);
    m_views->addWidget(charging);

    connect(copyBtn, &QPushButton::clicked, this, [this] {
        QApplication::clipboard()->setText(m_orderNo->text());
    });
    connect(detailBtn, &QPushButton::clicked, this, &ChargePage::onShowDetail);

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
    if (UserService::accountFrozen())   // 被管理端冻结：主窗口会弹提示并退回登录页
        return;
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
    // stationOptions() 是页面缓存，翻页后未必包含当前电站。只有服务端
    // 明确给出 is_online=false 时才应禁用预约，缓存未命中不能误判为下线。
    m_stationOnline = true;
    for (const StationOpt &s : stationOptions())
        if (s.id == m_stationId) {
            m_stationTitle->setText(s.name);
            m_stationPrice = s.price;
            m_stationOnline = s.isOnline;
            m_stationMeta->setText(QStringLiteral("%1\n电价：%2 元/度 · 距当前位置：%3 km")
                                       .arg(s.address)
                                       .arg(s.price, 0, 'f', 2)
                                       .arg(s.distanceKm, 0, 'f', 1));
            break;
        }
    if (!m_stationOnline)
        m_stationMeta->setText(m_stationMeta->text() + QStringLiteral("\n该电站已下线，暂不可充电"));
    m_reserveBtn->setEnabled(m_stationOnline);
    reloadPiles();
    m_views->setCurrentIndex(0);
}

void ChargePage::reloadPiles()
{
    const int gen = ++m_pileGen;
    const int sid = m_stationId;
    QPointer<ChargePage> self(this);
    freePilesAsync(sid, [self, gen, sid](const QList<PileOpt> &piles, const QString &err) {
        if (!self || gen != self->m_pileGen || sid != self->m_stationId)
            return;                                   // 过期回包（已换站/已有更新的一次）
        if (!err.isEmpty()) {                         // 网络抖动：保留上次画面，不清空
            if (self->m_pileList->count() == 0)
                new QListWidgetItem(err, self->m_pileList);
            return;
        }
        self->applyPiles(piles);
    });
}

// 高度不用字体估算（stylesheet 的字号/行距和 QFontMetrics 常常对不上），
// 直接问布局：卡片在真实宽度下需要多高，再留 12px 余量，两行字绝不裁
int ChargePage::pileCardHeight(QWidget *card) const
{
    int width = m_pileList->viewport()->width() - PileItemInset * 2;
    if (width < 200)
        width = 420 - 28 - PileItemInset * 2 - 12;   // 列表还没显示出来时按手机宽度估
    const int h = card->hasHeightForWidth() ? card->heightForWidth(width)
                                            : card->sizeHint().height();
    return qMax(80, h + 12);
}

QWidget *ChargePage::makePileCard(const PileOpt &p)
{
    auto *card = new QWidget(m_pileList);
    card->setObjectName(QStringLiteral("PileCard"));
    card->setStyleSheet(QStringLiteral("QWidget#PileCard { background:transparent; }"));
    auto *row = new QHBoxLayout(card);
    row->setContentsMargins(PileCardMarginH, PileCardMarginV, PileCardMarginH, PileCardMarginV);
    row->setSpacing(10);
    auto *info = new QLabel(card);
    info->setObjectName(QStringLiteral("PileInfo"));
    info->setWordWrap(true);
    info->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    row->addWidget(info, 1);
    auto *status = new QLabel(card);
    status->setObjectName(QStringLiteral("PileStatus"));
    status->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    row->addWidget(status, 0);
    updatePileCard(card, p);
    return card;
}

void ChargePage::updatePileCard(QWidget *card, const PileOpt &p)
{
    auto *info   = card->findChild<QLabel *>(QStringLiteral("PileInfo"));
    auto *status = card->findChild<QLabel *>(QStringLiteral("PileStatus"));
    if (!info || !status)
        return;
    const QString text = QStringLiteral("%1\n%2 · %3 kW · %4 元/度")
                             .arg(shortPileName(p.code, m_stationTitle->text()), p.typeText)
                             .arg(p.powerKw, 0, 'f', 1)
                             .arg(m_stationPrice, 0, 'f', 2);
    if (info->text() != text)
        info->setText(text);
    if (status->text() != p.statusText)
        status->setText(p.statusText);
    status->setStyleSheet(QStringLiteral("color:%1; font-weight:700;")
                              .arg(pileStatusColor(p.statusCode)));
}

// 同一批桩 → 只改文字/颜色，不重建（不闪、选中不丢、滚动位置不动）
// 桩集合变了（换站/新桩）→ 才整体重建
void ChargePage::applyPiles(const QList<PileOpt> &piles)
{
    bool same = !piles.isEmpty() && m_pileList->count() == piles.size();
    for (int i = 0; same && i < piles.size(); ++i)
        same = m_pileList->item(i)->data(Qt::UserRole).toString() == piles[i].id;
    if (same) {
        for (int i = 0; i < piles.size(); ++i) {
            QListWidgetItem *item = m_pileList->item(i);
            item->setData(Qt::UserRole + 1, piles[i].statusCode);
            if (QWidget *card = m_pileList->itemWidget(item))
                updatePileCard(card, piles[i]);
        }
        return;
    }

    const QString keep = m_pileList->currentItem()
        ? m_pileList->currentItem()->data(Qt::UserRole).toString() : QString();
    m_pileList->setUpdatesEnabled(false);
    m_pileList->clear();
    for (const PileOpt &p : piles) {
        auto *item = new QListWidgetItem(m_pileList);
        QWidget *card = makePileCard(p);
        item->setSizeHint(QSize(0, pileCardHeight(card)));
        m_pileList->setItemWidget(item, card);
        item->setData(Qt::UserRole, p.id);
        item->setData(Qt::UserRole + 1, p.statusCode);
        if (!keep.isEmpty() && p.id == keep)
            m_pileList->setCurrentItem(item);
    }
    if (piles.isEmpty())
        new QListWidgetItem(QStringLiteral("该站暂无电桩数据"), m_pileList);
    // 截图自检：NCS_SHOT_SELECT=1 自动选中第一根桩（看选中态的绿框）
    if (!m_pileList->currentItem() && !piles.isEmpty()
        && !qEnvironmentVariableIsEmpty("NCS_SHOT_SELECT"))
        m_pileList->setCurrentRow(0);
    m_pileList->setUpdatesEnabled(true);
    m_reserveBtn->setEnabled(m_stationOnline && !piles.isEmpty());
}

void ChargePage::showSettle(const QString &orderId)
{
    m_pickTimer->stop();
    m_timer->stop();
    m_reserveTimer->stop();
    m_settleOrderId = orderId;
    m_paid = false;
    // 刚 stop 完 server 落库可能慢半拍，小票取不到就重试几次；
    // 实在取不到也要进结算页（只显示订单号），不能把用户留在充电页
    Receipt r;
    bool got = false;
    for (int i = 0; i < 4 && !got; ++i) {
        got = orderDetail(orderId, &r);
        if (!got) QThread::msleep(400);
    }
    if (!got) {
        r = Receipt();
        r.id = orderId;
        r.statusText = QStringLiteral("待结算");
    }
    m_receiptText->setText(receiptHtml(r));
    m_settleHint->setText(got ? QStringLiteral("请确认支付以完成订单")
                              : QStringLiteral("小票暂时取不到，可直接确认支付"));
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

// ================= 预约 =================

void ChargePage::onReserve()
{
    if (!m_stationOnline) {
        QMessageBox::information(this, QStringLiteral("电站已下线"),
                                 QStringLiteral("该电站已被弃用，暂不能进行充电操作。"));
        return;
    }
    if (UserService::accountFrozen())   // 冻结账号不能预约（server 目前不拦，客户端把关）
        return;
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
    if (UserService::accountFrozen())
        return;
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
    m_orderNo->setText(order.id);
    m_chargeStation->setText(QStringLiteral("⚡ %1 – %2")
                                 .arg(order.pileCode, order.stationName));
    m_deviceNo->setText(QStringLiteral("设备编号：%1").arg(order.pileCode));
    // 上限 = 钱包余额 - 1 元（= 剩余可用 + 押金抵扣，再留 1 元缓冲）
    // 缓冲是给 2 秒轮询间隔用的：最快的桩一个周期约多走 0.2 元，
    // 留 1 元保证自动结束后余额绝不为负、运营不亏钱；查不到余额就不设限
    const double balance = walletBalance();
    m_costCap = balance > 1 ? balance - 1 : 0;
    m_capLabel->setText(m_costCap > 0
        ? QStringLiteral("本次可用上限 ¥%1，到达后自动结束")
              .arg(m_costCap, 0, 'f', 2)
        : QString());
    m_tick = 0;
    m_pollBusy = false;
    ++m_orderGen;
    m_views->setCurrentIndex(2);
    m_timer->start();      // 先启动再刷新：重开 app 时若已过 30 秒，第一帧就能自动结束
    onTick();
}

void ChargePage::onTick()
{
    // 偶数拍问 server（它才是计费的唯一权威）；每一拍都本地刷新时长，秒表不跳
    // 请求是异步的：回包前界面照常响应，不会卡住秒表
    if (m_tick++ % 2 == 0 && !m_pollBusy) {
        m_pollBusy = true;
        const int gen = ++m_orderGen;
        QPointer<ChargePage> self(this);
        findUnfinishedAsync([self, gen](bool ok, bool found, const ActiveOrder &ao,
                                        const QString &) {
            if (!self)
                return;
            self->m_pollBusy = false;
            if (gen != self->m_orderGen || !self->m_timer->isActive()
                || self->m_views->currentIndex() != 2)
                return;                 // 已经离开充电页，回包作废
            if (!ok)
                return;                 // 网络抖动：保留上次数字，下一拍再试
            self->handlePolledOrder(found, ao);
        });
    }
    refreshChargingStats();
}

// 三格统计 + 仪表盘：时长本地按秒走，费用/电量/SOC 用 server 最近一次的值
void ChargePage::refreshChargingStats()
{
    const QDateTime start = QDateTime::fromString(
        m_active.startTime, QStringLiteral("yyyy-MM-dd HH:mm:ss"));
    const qint64 secs = qMax<qint64>(0, start.secsTo(QDateTime::currentDateTime()));
    m_statTime->setText(QTime(0, 0).addSecs(int(secs)).toString(QStringLiteral("HH:mm:ss")));
    m_statCost->setText(QString::number(m_active.amount, 'f', 2));
    m_statKwh->setText(QString::number(m_active.kwh, 'f', 2));
    const int soc = demoSoc(secs);
    m_gauge->setValue(soc, soc >= 100 ? QStringLiteral("已充满，正在结束充电…")
                                      : QStringLiteral("预计还需 %1 秒充满").arg(30 - secs));
    if (soc >= 100 && m_timer->isActive())
        finishWhenFull();
}

// 充满（100%）自动结束：通知 server 停止并直接进入结算页，不用手动点停止
void ChargePage::finishWhenFull()
{
    m_timer->stop();
    ++m_orderGen;                       // 作废还在路上的轮询回包
    QString err;
    if (!stopCharge(m_active.id, &err)) {
        if (qEnvironmentVariableIsEmpty("NCS_SHOT"))
            QMessageBox::warning(this, QStringLiteral("结束失败"), err);
        ActiveOrder ao;                       // 可能已在别处被停止/结算，按 server 状态走
        if (findUnfinished(Session::i().userId(), &ao))
            ao.status == 0 ? m_timer->start() : showSettle(ao.id);
        else
            showPick();
        return;
    }
    showSettle(m_active.id);                  // stop 成功即待结算，直接进结算页
}

// 演示用进度：不看 server 的 soc，每辆车固定从 20% 开始，开充 30 秒后到 100%
//（server 模拟的电池包太大，慢充要几分钟才涨 1%，课堂演示看不出变化）
int ChargePage::demoSoc(qint64 elapsedSec) const
{
    constexpr int startSoc = 20, fullSec = 30;
    if (elapsedSec >= fullSec)
        return 100;
    return startSoc + int((100 - startSoc) * elapsedSec / fullSec);
}

// 查看详情：充电中也能看当前订单的小票（费用为截至目前的累计）
void ChargePage::onShowDetail()
{
    Receipt r;
    if (!orderDetail(m_active.id, &r)) {
        r.id = m_active.id;
        r.stationName = m_active.stationName;
        r.pileCode = m_active.pileCode;
        r.startTime = m_active.startTime;
        r.kwh = m_active.kwh;
        r.totalFee = m_active.amount;
        r.statusText = QStringLiteral("充电中");
    }
    auto *dlg = new QDialog(this);
    dlg->setWindowTitle(QStringLiteral("订单详情"));
    dlg->setAttribute(Qt::WA_DeleteOnClose);
    dlg->setFixedWidth(340);
    auto *lay = new QVBoxLayout(dlg);
    auto *card = new QWidget(dlg);
    card->setObjectName(QStringLiteral("Card"));
    auto *cl = new QVBoxLayout(card);
    auto *text = new QLabel(receiptHtml(r, true), card);
    text->setTextFormat(Qt::RichText);
    text->setWordWrap(true);
    cl->addWidget(text);
    lay->addWidget(card);
    auto *close = new QPushButton(QStringLiteral("关闭"), dlg);
    connect(close, &QPushButton::clicked, dlg, &QDialog::accept);
    lay->addWidget(close);
    dlg->show();       // 不用 open()：macOS 的窗口模态会变成 sheet
}

void ChargePage::handlePolledOrder(bool found, const ActiveOrder &ao)
{
    if (!found) {
        m_timer->stop();           // 订单没了（可能在别处被结算）
        showPick();
        return;
    }
    if (ao.status != 0) {
        showSettle(ao.id);         // 在别处被停止 → 进结算页
        return;
    }
    m_active = ao;
    refreshChargingStats();

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
    }
}

void ChargePage::onStop()
{
    if (QMessageBox::question(this, QStringLiteral("停止充电"),
                              QStringLiteral("确定要停止本次充电吗？"))
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
