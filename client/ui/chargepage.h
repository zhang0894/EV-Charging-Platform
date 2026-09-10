#ifndef CHARGEPAGE_H
#define CHARGEPAGE_H

#include "core/chargeservice.h"

#include <QWidget>

class QLabel;
class QListWidget;
class QPushButton;
class QStackedWidget;
class QTimer;
class SocGauge;

// 充电流程（预约版）：预约(扣押金 20) → 倒计时内到站 → 充电 → 到上限自动结束
//   预约充电：server 扣押金 20，120 秒内到站
//   到站后「已到站，开始充电」进入充电画面
//   充电花费上限 = 开充时钱包余额 − 1，到顶自动结束不透支
//   选桩页每 5 秒、充电页每 2 秒向 server 拉状态，全部异步且原地更新（不闪）
class ChargePage : public QWidget
{
    Q_OBJECT
public:
    explicit ChargePage(QWidget *parent = nullptr);

signals:
    void navigationRequested(double latitude, double longitude, const QString &stationName);

public slots:
    void enter();                           // 底部导航切到「充电」时调用
    void enterWithStation(int stationId);   // 电站详情页「选桩充电」对接点

private slots:
    void reloadPiles();
    void onReserve();
    void onArrive();
    void onCancelReserve();
    void onReserveTick();
    void onStop();
    void onPay();
    void onTick();
    void onShowDetail();                    // 充电中「查看详情」

private:
    void showPick();
    void applyPiles(const QList<ChargeService::PileOpt> &piles);   // 原地更新，桩集合变了才重建
    QWidget *makePileCard(const ChargeService::PileOpt &p);
    void updatePileCard(QWidget *card, const ChargeService::PileOpt &p);
    int  pileCardHeight(QWidget *card) const;
    void handlePolledOrder(bool found, const ChargeService::ActiveOrder &ao);
    void refreshChargingStats();            // 把 m_active 刷到仪表盘和三个统计格
    int  demoSoc(qint64 elapsedSec) const;                 // 演示用：20%→100% 固定 30 秒
    void finishWhenFull();                                  // 100% 自动停止并进结算页
    void showSettle(const QString &orderId);
    void showCharging(const ChargeService::ActiveOrder &order);
    void showReserved(const ChargeService::Reservation &r);

    QStackedWidget *m_views;

    // 画面 0：选桩
    QLabel      *m_stationTitle;
    QLabel      *m_stationMeta;
    QPushButton *m_navigationBtn;
    QListWidget *m_pileList;
    QPushButton *m_reserveBtn;
    QTimer      *m_pickTimer;   // 选桩页每 5 秒刷新桩状态（server 车流是动态的）
    int          m_stationId = 0;
    double       m_stationPrice = 0;
    bool         m_stationOnline = true;   // 管理端下线的电站不能预约
    int          m_pileGen = 0;   // 异步回包的“代号”，站换了/更新的回包一律丢弃

    // 画面 1：结算小票
    QLabel      *m_receiptText;
    QLabel      *m_settleHint;
    QPushButton *m_payBtn;
    QString      m_settleOrderId;
    bool         m_paid = false;

    // 画面 2：充电中（仪表盘 + 车 + 三个统计格）
    QLabel       *m_orderNo;
    SocGauge     *m_gauge;
    QLabel       *m_chargeStation;   // ⚡ 01号慢充桩 – 站名
    QLabel       *m_deviceNo;        // 设备编号：P05485_01
    QLabel       *m_statCost;
    QLabel       *m_statTime;
    QLabel       *m_statKwh;
    QLabel       *m_capLabel;
    QTimer       *m_timer;
    int           m_tick = 0;
    int           m_orderGen = 0;
    bool          m_pollBusy = false;   // 上一次轮询还没回来就不再叠加请求
    double        m_costCap = 0;    // 本次充电花费上限（元），0 = 未知不限
    ChargeService::ActiveOrder m_active;

    // 画面 3：已预约（桩详情 + 倒计时）
    QLabel  *m_reserveText;
    QLabel  *m_countdown;
    QTimer  *m_reserveTimer;
    ChargeService::Reservation m_reservation;
};

#endif // CHARGEPAGE_H
