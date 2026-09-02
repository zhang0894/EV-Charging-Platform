#ifndef CHARGEPAGE_H
#define CHARGEPAGE_H

#include "core/chargeservice.h"

#include <QWidget>

class QComboBox;
class QLabel;
class QListWidget;
class QPushButton;
class QStackedWidget;

// 充电流程（A 负责）
// 今日进度：选桩页 + 进入前的未完成订单拦截（UC-U-06）
// 进入本页必须走 enter()：先查未完成订单，有就不允许开新单
class ChargePage : public QWidget
{
    Q_OBJECT
public:
    explicit ChargePage(QWidget *parent = nullptr);

public slots:
    void enter();                           // 底部导航切到「充电」时调用
    void enterWithStation(int stationId);   // B 的电站详情页「选桩充电」对接点

private slots:
    void reloadPiles();
    void onStart();

private:
    void showPick();
    void showBlocked(const ChargeService::ActiveOrder &order);

    QStackedWidget *m_views;

    // 画面 0：选桩
    QComboBox   *m_stationBox;
    QListWidget *m_pileList;
    QPushButton *m_startBtn;

    // 画面 1：有未完成订单时的拦截提示
    QLabel *m_blockedText;
};

#endif // CHARGEPAGE_H
