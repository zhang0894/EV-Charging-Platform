#include "dashboardwidget.h"
#include "dashboardmodel.h"
#include "ui_dashboardwidget.h"

#include <QDateTime>
#include <QDate>
#include <QLocale>

DashboardWidget::DashboardWidget(QWidget *parent)
    : QWidget(parent)
    , ui(new Ui::DashboardWidget)
    , m_model(new DashboardModel(this))
    , m_chart(new QChart)
    , m_series(new QLineSeries)
    , m_axisX(new QDateTimeAxis)
    , m_axisY(new QValueAxis)
{
    ui->setupUi(this);

    // 服务器数据到达后由 Model 发信号驱动 UI 刷新：
    //   summaryChanged    -> 刷新三个核心指标卡片
    //   trendDataChanged  -> 刷新营收趋势折线图
    // （Model 构造时已自动 fetchData() 发起 HTTP 请求，此处只需订阅信号）
    connect(m_model, &DashboardModel::summaryChanged, this, &DashboardWidget::refreshCards);
    connect(m_model, &DashboardModel::trendDataChanged, this, &DashboardWidget::refreshChart);

    // 三个卡片等宽：Qt6 的 .ui 不支持布局的 stretch 字符串属性，
    // 改用 setStretchFactor 强制等比拉伸（QLayout 标准接口，无绝对坐标）。
    ui->cardLayout->setStretchFactor(ui->cardToday, 1);
    ui->cardLayout->setStretchFactor(ui->cardMonth, 1);
    ui->cardLayout->setStretchFactor(ui->cardTotal, 1);

    // 图表骨架初始化；模型默认加载近7日占位数据，服务器响应到达后自动替换
    initChart();

    // “近7日 / 近30日”按钮：autoExclusive 已在 .ui 中开启，这里直接连点击
    connect(ui->btn7Days,  &QPushButton::clicked, this, &DashboardWidget::onBtn7Days);
    connect(ui->btn30Days, &QPushButton::clicked, this, &DashboardWidget::onBtn30Days);

    // 首次渲染：卡片 + 折线图
    refreshCards();
    refreshChart();
}

DashboardWidget::~DashboardWidget()
{
    delete ui;
}

// ------------- 图表骨架初始化 -------------
void DashboardWidget::initChart()
{
    // 折线样式：青色高光
    QPen pen(QColor(0x00, 0xd4, 0xff));
    pen.setWidth(2);
    m_series->setPen(pen);
    m_series->setPointsVisible(true);

    // 图表标题与背景，融入深色科技感主题
    m_chart->setTitle(QStringLiteral("营收趋势"));
    m_chart->setTitleBrush(QBrush(QColor(0xe6, 0xe9, 0xef)));
    m_chart->setBackgroundBrush(QBrush(QColor(0x0f, 0x1b, 0x2d)));
    m_chart->setBackgroundPen(QPen(QColor(0x1e, 0x2d, 0x45)));
    m_chart->legend()->hide();
    // 统一用 setMargins 控制图表内边距（避免访问 QGraphicsLayout 不完整类型）。
    m_chart->setMargins(QMargins(2, 2, 2, 2));

    // X 轴：日期轴
    m_axisX->setTitleText(QStringLiteral("日期"));
    m_axisX->setFormat(QStringLiteral("MM-dd"));
    m_axisX->setLabelsColor(QColor(0x8b, 0x9b, 0xb4));
    m_axisX->setGridLineColor(QColor(0x1e, 0x2d, 0x45));
    m_axisX->setLinePenColor(QColor(0x2a, 0x3b, 0x55));
    m_axisX->setTitleBrush(QBrush(QColor(0x8b, 0x9b, 0xb4)));

    // Y 轴：营收轴
    m_axisY->setTitleText(QStringLiteral("营收 (元)"));
    m_axisY->setLabelsColor(QColor(0x8b, 0x9b, 0xb4));
    m_axisY->setGridLineColor(QColor(0x1e, 0x2d, 0x45));
    m_axisY->setLinePenColor(QColor(0x2a, 0x3b, 0x55));
    m_axisY->setLabelFormat("%.0f");
    m_axisY->setTitleBrush(QBrush(QColor(0x8b, 0x9b, 0xb4)));

    // 组装
    m_chart->addSeries(m_series);
    m_chart->addAxis(m_axisX, Qt::AlignBottom);
    m_chart->addAxis(m_axisY, Qt::AlignLeft);
    m_series->attachAxis(m_axisX);
    m_series->attachAxis(m_axisY);

    ui->chartView->setChart(m_chart);
    ui->chartView->setRenderHint(QPainter::Antialiasing);
    ui->chartView->setBackgroundBrush(QBrush(QColor(0x0f, 0x1b, 0x2d)));
}

// ------------- 从 Model 读取汇总刷新卡片 -------------
void DashboardWidget::refreshCards()
{
    // 核心指标卡片数据全部来自 Model 的 summary（对应 3.2 节 summary 返回字段）
    const DashboardModel::Summary &s = m_model->summary();
    QLocale cn(QLocale::Chinese, QLocale::China);   // 千分位格式化

    ui->labelValueToday->setText(QStringLiteral("¥%1")
        .arg(cn.toString(s.today_revenue, 'f', 2)));
    ui->labelValueMonth->setText(QStringLiteral("¥%1")
        .arg(cn.toString(s.month_revenue, 'f', 2)));
    ui->labelValueTotal->setText(QStringLiteral("¥%1")
        .arg(cn.toString(s.total_revenue, 'f', 2)));
}

// ------------- 从 Model 读取日期+营收刷新折线图 -------------
void DashboardWidget::refreshChart()
{
    const int n = m_model->rowCount();
    if (n == 0) {
        m_series->clear();
        return;
    }

    QList<QPointF> points;
    points.reserve(n);
    QDateTime firstDt, lastDt;
    double maxRev = 0.0;

    // 逐行从 Model 读取：日期列 + 营收列（UserRole 存原始数值）
    for (int i = 0; i < n; ++i) {
        const QModelIndex dateIdx = m_model->index(i, DashboardModel::DateCol);
        const QModelIndex revIdx   = m_model->index(i, DashboardModel::RevenueCol);

        const QString dateStr = dateIdx.data(Qt::UserRole).toString();
        const double  revenue = revIdx.data(Qt::UserRole).toDouble();

        // Qt 6 移除了 QDateTime(const QDate&) 构造，改用 QDate::startOfDay() 得到当天零点时刻
        QDate d = QDate::fromString(dateStr, Qt::ISODate);
        if (!d.isValid()) d = QDate::currentDate();
        const QDateTime dt = d.startOfDay();
        if (i == 0) firstDt = dt;
        lastDt = dt;

        points << QPointF(dt.toMSecsSinceEpoch(), revenue);
        if (revenue > maxRev) maxRev = revenue;
    }

    m_series->replace(points);
    m_axisX->setRange(firstDt, lastDt);
    m_axisY->setRange(0, maxRev * 1.15);
    m_axisY->applyNiceNumbers();
}

// ------------- 按钮槽：切换数据集 -------------
void DashboardWidget::onBtn7Days()
{
    m_model->loadDataset(DashboardModel::Last7Days);
    refreshChart();
    emit logMessage(QStringLiteral("切换营收趋势：近7日"));
}

void DashboardWidget::onBtn30Days()
{
    m_model->loadDataset(DashboardModel::Last30Days);
    refreshChart();
    emit logMessage(QStringLiteral("切换营收趋势：近30日"));
}
