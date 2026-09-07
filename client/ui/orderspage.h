#ifndef ORDERSPAGE_H
#define ORDERSPAGE_H

#include <QWidget>

class QListWidget;

// 我的订单（UC-U-10，云端版）—— 数据来自 GET /orders/my，点击查看小票
class OrdersPage : public QWidget
{
    Q_OBJECT
public:
    explicit OrdersPage(QWidget *parent = nullptr);

public slots:
    void refresh();

private:
    void showDetail(const QString &orderId);

    QListWidget *m_list;
};

#endif // ORDERSPAGE_H
