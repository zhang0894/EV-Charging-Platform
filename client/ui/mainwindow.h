#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QWidget>

class ChargePage;
class QButtonGroup;
class QStackedWidget;

// 主框架 —— 420×760 手机竖屏 + 底部导航
// 页面编号（接页面时不要改动顺序）：
//   0=电站列表(B)  1=充电(A)  2=订单(A)  3=我的(B)
class MainWindow : public QWidget
{
    Q_OBJECT
public:
    explicit MainWindow(QWidget *parent = nullptr);

    enum Page { PageStations = 0, PageCharge, PageOrders, PageMine };

public slots:
    void showPage(int page);
    void openChargeForStation(int stationId);   // B 的「选桩充电」接这里

private:
    QStackedWidget *m_pages;
    QButtonGroup   *m_navGroup;
    ChargePage     *m_chargePage;
};

#endif // MAINWINDOW_H