#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QWidget>

class ChargePage;
class OrdersPage;
class ProfilePage;
class QButtonGroup;
class QStackedWidget;

// 主框架 —— 420×760 手机竖屏 + 底部导航
// 页面编号（接页面时不要改动顺序）：
//   0=电站列表  1=充电  2=订单  3=我的
class MainWindow : public QWidget
{
    Q_OBJECT
public:
    explicit MainWindow(QWidget *parent = nullptr);

    enum Page { PageStations = 0, PageCharge, PageOrders, PageMine };

signals:
    void logoutRequested();

public slots:
    void showPage(int page);
    void openChargeForStation(int stationId);   // 电站详情页「选桩充电」接这里

private slots:
    void onAccountFrozen();     // 任一接口返回 10002 → 提示并退回登录页

private:
    bool            m_frozenShown = false;
    QStackedWidget *m_pages;
    QButtonGroup   *m_navGroup;
    ChargePage     *m_chargePage;
    OrdersPage     *m_ordersPage;
    ProfilePage    *m_profilePage;
};

#endif // MAINWINDOW_H
