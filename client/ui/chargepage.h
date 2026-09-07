#ifndef CHARGEPAGE_H
#define CHARGEPAGE_H

#include "core/chargeservice.h"

#include <QWidget>

class QLabel;
class QListWidget;
class QPushButton;
class QStackedWidget;
class QTimer;

// 充电流程（预约版）：预约(扣押金 20) → 倒计时内到站 → 充电 → 到上限自动结束
//   预约充电：校验余额>=20，扣押金，15 分钟内到站（NCS_RESERVE_MIN 可改，测试用 1）
//   到站后「一到站，开始充电」进入原来的充电画面
//   充电花费上限 = 开充时钱包余额（= 剩余可用 + 押金抵扣），到顶自动结束不透支
class ChargePage : public QWidget
{
    Q_OBJECT
public:
    explicit ChargePage(QWidget *parent = nullptr);

signals:
    void navigationRequested(double latitude, double longitude, const QString &stationName);

public slots:
    void enter();                           // 底部导航切到「充电」时调用
    void enterWithStation(int stationId);   // B 的电站详情页「选桩充电」对接点

private slots:
    void reloadPiles();
    void onReserve();
    void onArrive();
    void onCancelReserve();
    void onReserveTick();
    void onStop();
    void onPay();
    void onTick();

private:
    void showPick();
    void showSettle(const QString &orderId);
    void showCharging(const ChargeService::ActiveOrder &order);
    void showReserved(const ChargeService::Reservation &r);

    QStackedWidget *m_views;

    // 画面 0：选桩（B 的设计：站名 + 地址/价格 + 一键导航 + 桩卡片）
    QLabel      *m_stationTitle;
    QLabel      *m_stationMeta;
    QPushButton *m_navigationBtn;
    QListWidget *m_pileList;
    QPushButton *m_reserveBtn;
    QTimer      *m_pickTimer;   // 选桩页每 5 秒刷新桩状态（server 车流是动态的）
    int          m_stationId = 0;
    double       m_stationPrice = 0;

    // 画面 1：结算小票（UC-U-09）
    QLabel      *m_receiptText;
    QLabel      *m_settleHint;
    QPushButton *m_payBtn;
    QString      m_settleOrderId;
    bool         m_paid = false;

    // 画面 2：充电中
    QLabel       *m_chargeStation;
    QLabel       *m_duration;
    QLabel       *m_kwh;
    QLabel       *m_cost;
    QLabel       *m_capLabel;
    QTimer       *m_timer;
    int           m_tick = 0;
    double        m_costCap = 0;    // 本次充电花费上限（元），0 = 未知不限
    ChargeService::ActiveOrder m_active;

    // 画面 3：已预约（桩详情 + 倒计时）
    QLabel  *m_reserveText;
    QLabel  *m_countdown;
    QTimer  *m_reserveTimer;
    ChargeService::Reservation m_reservation;
};

#endif // CHARGEPAGE_H
