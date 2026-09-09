#include "dashboardwidget.h"
#include "dashboardmodel.h"
#include "ui_dashboardwidget.h"

#include <QDateTime>
#include <QDate>
#include <QLocale>
#include <QPixmap>
#include <QVariantAnimation>
#include <QEasingCurve>
#include <QToolTip>
#include <limits>

DashboardWidget::DashboardWidget(QWidget *parent)
    : QWidget(parent)
    , ui(new Ui::DashboardWidget)
    , m_model(new DashboardModel(this))
    , m_chart(new QChart)
    , m_series(new QSplineSeries)
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

    // 初始化卡片视觉样式（图标 / 渐变背景 / 字体 / 圆形图标底）
    setupCards(false);

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
    // 折线样式：主题蓝
    QPen pen(QColor(0x2b, 0x7b, 0xff));
    pen.setWidth(2);
    m_series->setPen(pen);
    m_series->setPointsVisible(true);
    m_series->setMarkerSize(5);

    // 图表标题与背景，融入浅色专业风主题
    m_chart->setTitle(QStringLiteral("营收趋势"));
    m_chart->setTitleBrush(QBrush(QColor(0x1a, 0x23, 0x32)));
    m_chart->setBackgroundBrush(QBrush(QColor(0xff, 0xff, 0xff)));
    m_chart->setBackgroundPen(QPen(QColor(0xe8, 0xec, 0xf0)));
    m_chart->legend()->hide();
    // 统一用 setMargins 控制图表内边距（避免访问 QGraphicsLayout 不完整类型）。
    m_chart->setMargins(QMargins(2, 2, 2, 2));

    // X 轴：日期轴
    m_axisX->setTitleText(QStringLiteral("日期"));
    m_axisX->setFormat(QStringLiteral("MM-dd"));
    m_axisX->setLabelsColor(QColor(0x4a, 0x5a, 0x6e));
    m_axisX->setGridLineColor(QColor(0xf3, 0xf5, 0xf8));   // 网格线变淡
    m_axisX->setLinePenColor(QColor(0xd9, 0xde, 0xe5));
    m_axisX->setTitleBrush(QBrush(QColor(0x4a, 0x5a, 0x6e)));

    // Y 轴：营收轴（带单位"元"）
    m_axisY->setTitleText(QStringLiteral("营收（元）"));
    m_axisY->setLabelsColor(QColor(0x4a, 0x5a, 0x6e));
    m_axisY->setGridLineColor(QColor(0xf3, 0xf5, 0xf8));   // 网格线变淡
    m_axisY->setLinePenColor(QColor(0xd9, 0xde, 0xe5));
    m_axisY->setLabelFormat("%.0f");
    m_axisY->setTitleBrush(QBrush(QColor(0x4a, 0x5a, 0x6e)));

    // 组装
    m_chart->addSeries(m_series);
    m_chart->addAxis(m_axisX, Qt::AlignBottom);
    m_chart->addAxis(m_axisY, Qt::AlignLeft);
    m_series->attachAxis(m_axisX);
    m_series->attachAxis(m_axisY);

    ui->chartView->setChart(m_chart);
    ui->chartView->setRenderHint(QPainter::Antialiasing);
    ui->chartView->setBackgroundBrush(QBrush(QColor(0xff, 0xff, 0xff)));

    // 数据点 hover 提示框：显示日期 + 营收
    connect(m_series, &QSplineSeries::hovered, this,
            [this](const QPointF &point, bool state) {
        if (state) {
            const QDateTime dt = QDateTime::fromMSecsSinceEpoch(
                static_cast<qint64>(point.x()));
            QLocale cn(QLocale::Chinese, QLocale::China);
            const QString tip = QStringLiteral("%1  营收：¥%2")
                .arg(dt.toString(QStringLiteral("MM-dd")))
                .arg(cn.toString(point.y(), 'f', 2));
            QToolTip::showText(QCursor::pos(), tip, ui->chartView);
        } else {
            QToolTip::hideText();
        }
    });
}

// ------------- 卡片视觉样式初始化 -------------
void DashboardWidget::setupCards(bool dark)
{
    // 三张卡片：蓝 / 绿 / 紫 浅色渐变背景 + 圆角 + 柔和边框
    if (!dark) {
        ui->cardToday->setStyleSheet(QStringLiteral(
            "#cardToday { background-color: qlineargradient(x1:0,y1:0,x2:1,y2:1,"
            "stop:0 #e6f0ff, stop:1 #f0f7ff); border-radius: 14px; border: 1px solid #d6e4ff; }"));
        ui->cardMonth->setStyleSheet(QStringLiteral(
            "#cardMonth { background-color: qlineargradient(x1:0,y1:0,x2:1,y2:1,"
            "stop:0 #e6f9f0, stop:1 #f0fbf5); border-radius: 14px; border: 1px solid #b7eb8f; }"));
        ui->cardTotal->setStyleSheet(QStringLiteral(
            "#cardTotal { background-color: qlineargradient(x1:0,y1:0,x2:1,y2:1,"
            "stop:0 #f0e6ff, stop:1 #f7f0ff); border-radius: 14px; border: 1px solid #d3adf7; }"));
    } else {
        // 深色模式：降低渐变亮度，保持色系区分
        ui->cardToday->setStyleSheet(QStringLiteral(
            "#cardToday { background-color: qlineargradient(x1:0,y1:0,x2:1,y2:1,"
            "stop:0 #1a2d4a, stop:1 #1e3355); border-radius: 14px; border: 1px solid #2b4a7a; }"));
        ui->cardMonth->setStyleSheet(QStringLiteral(
            "#cardMonth { background-color: qlineargradient(x1:0,y1:0,x2:1,y2:1,"
            "stop:0 #1a3328, stop:1 #1e3a2e); border-radius: 14px; border: 1px solid #2d5a3a; }"));
        ui->cardTotal->setStyleSheet(QStringLiteral(
            "#cardTotal { background-color: qlineargradient(x1:0,y1:0,x2:1,y2:1,"
            "stop:0 #2a1f4a, stop:1 #2e2455); border-radius: 14px; border: 1px solid #4a3a7a; }"));
    }

    // 圆形图标底：蓝 / 绿 / 紫
    ui->iconToday->setStyleSheet(QStringLiteral(
        "background-color: #1677FF; border-radius: 18px;"));
    ui->iconMonth->setStyleSheet(QStringLiteral(
        "background-color: #52c41a; border-radius: 18px;"));
    ui->iconTotal->setStyleSheet(QStringLiteral(
        "background-color: #722ed1; border-radius: 18px;"));

    // 图标（从 qrc 加载 SVG）
    ui->iconToday->setPixmap(QPixmap(QStringLiteral(":/img/icon-money.svg")));
    ui->iconMonth->setPixmap(QPixmap(QStringLiteral(":/img/icon-chart.svg")));
    ui->iconTotal->setPixmap(QPixmap(QStringLiteral(":/img/icon-bank.svg")));

    // 标题、数值、副标题颜色随主题切换（深色模式用浅色字）
    const QString titleColor = dark ? QStringLiteral("#c8d0dc") : QStringLiteral("#4a5a6e");
    const QString valueColor = dark ? QStringLiteral("#ffffff") : QStringLiteral("#1a2332");
    const QString subColor   = dark ? QStringLiteral("#8a92a0") : QStringLiteral("#8a9aa8");

    // 标题：15px 中等字重，次级色
    const QString titleQss = QStringLiteral("color: %1; font-size: 15px; font-weight: 500; background: transparent;").arg(titleColor);
    ui->labelTitleToday->setStyleSheet(titleQss);
    ui->labelTitleMonth->setStyleSheet(titleQss);
    ui->labelTitleTotal->setStyleSheet(titleQss);

    // 数值：大号粗体
    const QString valueQss = QStringLiteral("color: %1; font-size: 28px; font-weight: 700; background: transparent;").arg(valueColor);
    ui->labelValueToday->setStyleSheet(valueQss);
    ui->labelValueMonth->setStyleSheet(valueQss);
    ui->labelValueTotal->setStyleSheet(valueQss);

    // 副标题：12px 浅灰
    const QString subQss = QStringLiteral("color: %1; font-size: 12px; background: transparent;").arg(subColor);
    ui->labelSubToday->setStyleSheet(subQss);
    ui->labelSubMonth->setStyleSheet(subQss);
    ui->labelSubTotal->setStyleSheet(subQss);

    // 隐藏趋势比较行（不展示环比）
    ui->labelTrendToday->hide();
    ui->labelTrendMonth->hide();
    ui->labelTrendTotal->hide();
}

// ------------- 从 Model 读取汇总刷新卡片 -------------
void DashboardWidget::refreshCards()
{
    // 核心指标卡片数据全部来自 Model 的 summary（对应 3.2 节 summary 返回字段）
    const DashboardModel::Summary &s = m_model->summary();

    // 数字从 0 滚动到目标值（带 ¥ 前缀，2 位小数）
    animateValue(ui->labelValueToday, s.today_revenue);
    animateValue(ui->labelValueMonth, s.month_revenue);
    animateValue(ui->labelValueTotal, s.total_revenue);
}

// ------------- 数字滚动动画 -------------
void DashboardWidget::animateValue(QLabel *label, double target,
                                   const QString &prefix, int decimals)
{
    auto *anim = new QVariantAnimation(this);
    anim->setDuration(1200);
    anim->setStartValue(0.0);
    anim->setEndValue(target);
    anim->setEasingCurve(QEasingCurve::OutCubic);
    QLocale cn(QLocale::Chinese, QLocale::China);
    connect(anim, &QVariantAnimation::valueChanged, label,
            [label, prefix, decimals, cn](const QVariant &val) {
        label->setText(prefix + cn.toString(val.toDouble(), 'f', decimals));
    });
    connect(anim, &QVariantAnimation::finished, anim, &QObject::deleteLater);
    anim->start();
}

// ------------- 趋势文本与配色 -------------
void DashboardWidget::setTrend(QLabel *label, const QString &prefix, double percent)
{
    const bool up = percent >= 0.0;
    const QString arrow = up ? QStringLiteral("↑") : QStringLiteral("↓");
    const QString color = up ? QStringLiteral("#52c41a") : QStringLiteral("#ef4444");
    label->setText(QStringLiteral("%1 %2%3%")
                       .arg(prefix, arrow, QString::number(qAbs(percent), 'f', 1)));
    label->setStyleSheet(QStringLiteral(
        "color: %1; font-size: 12px; font-weight: 600; background: transparent;").arg(color));
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
    double minRev = std::numeric_limits<double>::max();

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
        if (revenue < minRev) minRev = revenue;
    }

    m_series->replace(points);
    m_axisX->setRange(firstDt, lastDt);
    // 纵轴自适应：上界 maxRev*1.15 留出顶部空间，下界 minRev*0.85 放大数据变化趋势
    m_axisY->setRange(minRev * 0.85, maxRev * 1.15) ;
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

// ------------- 主题切换 -------------
void DashboardWidget::applyTheme(bool dark)
{
    // 重新应用卡片渐变背景与文字颜色
    setupCards(dark);

    // 图表：背景、边框、标题、坐标轴、网格线配色随主题切换
    if (m_chart) {
        if (!dark) {
            m_chart->setBackgroundBrush(QBrush(QColor(0xff, 0xff, 0xff)));
            m_chart->setBackgroundPen(QPen(QColor(0xe8, 0xec, 0xf0)));
            m_chart->setTitleBrush(QBrush(QColor(0x1a, 0x23, 0x32)));
            // 折线：蓝色，数据点蓝色
            m_series->setPen(QPen(QColor(0x2b, 0x7b, 0xff), 2));
            m_series->setColor(QColor(0x2b, 0x7b, 0xff));
        } else {
            // 深色：暗色底 + 白色边框 + 亮色标题 + 白色折线
            m_chart->setBackgroundBrush(QBrush(QColor(0x25, 0x2a, 0x33)));
            m_chart->setBackgroundPen(QPen(QColor(0x5a, 0x62, 0x70)));
            m_chart->setTitleBrush(QBrush(QColor(0xe8, 0xec, 0xf0)));
            // 折线：亮白色，数据点白色
            m_series->setPen(QPen(QColor(0x69, 0xb1, 0xff), 2));
            m_series->setColor(QColor(0x69, 0xb1, 0xff));
        }
        m_chart->update();
    }

    // 坐标轴文字与网格线配色
    if (m_axisX) {
        const QColor textColor = dark ? QColor(0xa0, 0xa8, 0xb8) : QColor(0x4a, 0x5a, 0x6e);
        const QColor axisColor = dark ? QColor(0x5a, 0x62, 0x70) : QColor(0xd9, 0xde, 0xe5);
        const QColor gridColor = dark ? QColor(0x3a, 0x40, 0x50) : QColor(0xf3, 0xf5, 0xf8);
        m_axisX->setLabelsColor(textColor);
        m_axisY->setLabelsColor(textColor);
        m_axisX->setTitleBrush(QBrush(textColor));
        m_axisY->setTitleBrush(QBrush(textColor));
        m_axisX->setLinePenColor(axisColor);
        m_axisY->setLinePenColor(axisColor);
        m_axisX->setGridLineColor(gridColor);
        m_axisY->setGridLineColor(gridColor);
    }

    // 图表外层 QChartView 与 chartFrame 背景（去掉白边）
    const QString frameBg = dark ? QStringLiteral("#252a33") : QStringLiteral("#ffffff");
    if (ui->chartFrame) {
        ui->chartFrame->setStyleSheet(QStringLiteral(
            "QFrame#chartFrame{background-color:%1;border:1px solid %2;border-radius:8px;}")
            .arg(frameBg, dark ? QStringLiteral("#3a4050") : QStringLiteral("#e8ecf0")));
    }
    if (ui->chartView) {
        ui->chartView->setStyleSheet(QStringLiteral(
            "background-color:%1;border:none;border-radius:8px;").arg(frameBg));
        ui->chartView->setBackgroundBrush(QBrush(dark ? QColor(0x25, 0x2a, 0x33)
                                                       : QColor(0xff, 0xff, 0xff)));
    }
}
