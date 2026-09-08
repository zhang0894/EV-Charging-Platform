#ifndef PILESTATUSWIDGET_H
#define PILESTATUSWIDGET_H

#include <QWidget>
#include <QString>
#include <QJsonObject>

class QShowEvent;
class QFrame;
class QLabel;
class QPushButton;
class QChartView;
class PileStatusModel;

/**
 * @brief 电桩状态概览页（PC 运营后台）
 *
 * 布局：标题栏含"电桩状态概览 · 总桩数 N"，下方左右分栏——
 *   左 50% 为 6 张统计卡片（2 行 × 3 列），右 50% 为双层圆环图。
 *
 * 6 张卡片（仅标题 + 大号数字，无副标题）：
 *   在线数  = in_use_count + idle_count          (#2b7bff 蓝)
 *   闲置数  = idle_count                         (#22c55e 绿)
 *   在用数  = in_use_count                       (#2b7bff 蓝)
 *   维护数  = fault_count + offline_count        (#6b7a8a 灰)
 *   故障数  = fault_count                         (#ef4444 红)
 *   离线数  = total - inUse - idle - fault       (#f59e0b 橙)
 *   约束：在线数 + 维护数 = 总桩数
 *
 * 双层圆环图（QPieSeries × 2）：
 *   外圈 4 扇区：在用 / 闲置 / 故障 / 离线（显示百分比标签）
 *   内圈 2 扇区：在线 / 维护（直径约为外圈 50%）
 *   图例在饼图下方
 *
 * 刷新逻辑：
 *   - showEvent 中调用 fetchData()，每次切到该页自动刷新；
 *   - 右上角"刷新"按钮点击后调用 fetchData()。
 */
class PileStatusWidget : public QWidget
{
    Q_OBJECT
public:
    explicit PileStatusWidget(QWidget *parent = nullptr);

    void setAuthToken(const QString &token);

protected:
    void showEvent(QShowEvent *event) override;

signals:
    /** 日志转发：Model 的查询/操作事件与失败信息（由 MainWindow 连接 appendLog） */
    void logMessage(const QString &message);

private slots:
    void onDataReady(const QJsonObject &data);
    void onErrorOccurred(const QString &msg);

private:
    void buildUi();
    void refreshCards();
    void updateChart(int inUse, int idle, int fault, int offline,
                     int online, int maintenance, int total);

    // 卡片构造辅助：title -> [QFrame, 数值 QLabel, 百分比 QLabel]
    // valueColor 用于给数字与百分比标签设置对应扇区颜色
    QFrame *makeCard(const QString &title, const QString &objectName,
                     QLabel **valueLabel, QLabel **pctLabel,
                     const QString &valueColor);

    PileStatusModel *m_model;
    QPushButton *m_btnRefresh = nullptr;
    QLabel *m_lblTotalInTitle = nullptr;   // 标题栏中的"总桩数 N"
    QChartView *m_chartView = nullptr;

    // 6 张卡片的数值标签 + 百分比标签
    QLabel *m_lblOnline      = nullptr;  QLabel *m_lblOnlinePct      = nullptr;
    QLabel *m_lblIdle        = nullptr;  QLabel *m_lblIdlePct        = nullptr;
    QLabel *m_lblInUse       = nullptr;  QLabel *m_lblInUsePct       = nullptr;
    QLabel *m_lblMaintenance = nullptr;  QLabel *m_lblMaintenancePct = nullptr;
    QLabel *m_lblFault       = nullptr;  QLabel *m_lblFaultPct       = nullptr;
    QLabel *m_lblOffline     = nullptr;  QLabel *m_lblOfflinePct     = nullptr;

    QJsonObject m_lastData;
};

#endif // PILESTATUSWIDGET_H
