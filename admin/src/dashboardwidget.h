#ifndef DASHBOARDWIDGET_H
#define DASHBOARDWIDGET_H

#include <QWidget>
// Qt 6 中 QtCharts 类位于全局命名空间，无需 QT_CHARTS_USE_NAMESPACE 宏（该宏已被移除）。
// 直接包含具体头文件即可使用 QChart / QLineSeries / QDateTimeAxis / QValueAxis。
#include <QtCharts/QChart>
#include <QtCharts/QLineSeries>
#include <QtCharts/QDateTimeAxis>
#include <QtCharts/QValueAxis>

class DashboardModel;
namespace Ui { class DashboardWidget; }

/**
 * @brief 销售业绩页（Dashboard）
 *
 * 职责：展示销售业绩核心指标卡片 + 营收趋势折线图。
 * 所有展示数据均从 DashboardModel 读取（卡片读 summary，折线图读日期/营收列），
 * UI 层不硬编码任何业务数据。
 *
 * 提供“近7日 / 近30日”切换按钮，点击后调用 Model 加载对应数据集并动态刷新图表。
 */
class DashboardWidget : public QWidget
{
    Q_OBJECT

public:
    explicit DashboardWidget(QWidget *parent = nullptr);
    ~DashboardWidget();

signals:
    /** 操作日志信号，由 MainWindow 日志区接收 */
    void logMessage(const QString &msg);

private slots:
    void onBtn7Days();   // 切换近7日数据
    void onBtn30Days();  // 切换近30日数据

private:
    void initChart();     // 构建折线图骨架（坐标轴/系列/样式）
    void refreshCards();  // 从 Model summary 刷新三个核心指标卡片
    void refreshChart();  // 从 Model 日期列+营收列刷新折线图

    Ui::DashboardWidget *ui;
    DashboardModel *m_model;     // 数据源（唯一数据入口）

    // 折线图组件
    QChart        *m_chart;
    QLineSeries   *m_series;    // 营收折线
    QDateTimeAxis *m_axisX;     // 日期轴
    QValueAxis    *m_axisY;     // 营收轴
};

#endif // DASHBOARDWIDGET_H
