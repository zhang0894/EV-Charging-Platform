#include "pilestatuswidget.h"
#include "pilestatusmodel.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGridLayout>
#include <QFrame>
#include <QLabel>
#include <QPushButton>
#include <QMessageBox>
#include <QShowEvent>
#include <QChartView>
#include <QChart>
#include <QPieSeries>
#include <QPieSlice>
#include <QFont>
#include <QMargins>

PileStatusWidget::PileStatusWidget(QWidget *parent)
    : QWidget(parent)
    , m_model(new PileStatusModel(this))
{
    buildUi();

    connect(m_model, &PileStatusModel::dataReady,
            this, &PileStatusWidget::onDataReady);
    connect(m_model, &PileStatusModel::errorOccurred,
            this, &PileStatusWidget::onErrorOccurred);

    // 日志转发：查询/操作事件 + 失败信息统一上抛主窗口日志区
    connect(m_model, &PileStatusModel::logRequested,
            this, &PileStatusWidget::logMessage);
    connect(m_model, &PileStatusModel::errorOccurred, this,
            [this](const QString &msg) {
                emit logMessage(tr("失败：%1").arg(msg));
            });

    connect(m_btnRefresh, &QPushButton::clicked, this, [this]() {
        m_model->fetchData();
    });
}

void PileStatusWidget::setAuthToken(const QString &token)
{
    m_model->setAuthToken(token);
}

void PileStatusWidget::showEvent(QShowEvent *event)
{
    QWidget::showEvent(event);
    m_model->fetchData();
}

// ------------- 界面构建 -------------
void PileStatusWidget::buildUi()
{
    setObjectName(QStringLiteral("pileStatusPage"));

    setStyleSheet(QStringLiteral(
        "QFrame#cardPileOnline,QFrame#cardPileIdle,QFrame#cardPileInUse,"
        "QFrame#cardPileMaintenance,QFrame#cardPileFault,QFrame#cardPileOffline{"
        "background-color:#ffffff;border:1px solid #e8ecf0;border-radius:8px;}"
        "QPushButton#btnPileRefresh{background-color:#ffffff;color:#1a2332;"
        "border:1px solid #d9dee5;border-radius:6px;padding:6px 18px;min-width:72px;}"
        "QPushButton#btnPileRefresh:hover{border:1px solid #2b7bff;color:#2b7bff;}"));

    auto *rootLayout = new QVBoxLayout(this);
    rootLayout->setContentsMargins(20, 16, 20, 16);
    rootLayout->setSpacing(16);

    // ---------------- 顶部标题栏（含总桩数） + 刷新按钮 ----------------
    auto *topBar = new QWidget(this);
    auto *topLayout = new QHBoxLayout(topBar);
    topLayout->setContentsMargins(0, 0, 0, 0);

    // 标题 + 总桩数（格式："电桩状态概览 · 总桩数 N"）
    auto *pageTitle = new QLabel(tr("电桩状态概览"), topBar);
    pageTitle->setStyleSheet(QStringLiteral(
        "color:#1a2332;font-size:16px;font-weight:600;background:transparent;"));

    m_lblTotalInTitle = new QLabel(QStringLiteral("· 总桩数 --"), topBar);
    m_lblTotalInTitle->setStyleSheet(QStringLiteral(
        "color:#4a5a6e;font-size:14px;font-weight:500;background:transparent;"));

    m_btnRefresh = new QPushButton(tr("刷新"), topBar);
    m_btnRefresh->setObjectName(QStringLiteral("btnPileRefresh"));

    topLayout->addWidget(pageTitle);
    topLayout->addWidget(m_lblTotalInTitle);
    topLayout->addStretch(1);
    topLayout->addWidget(m_btnRefresh);
    rootLayout->addWidget(topBar);

    // ---------------- 左右分栏：左 50% 卡片 / 右 50% 饼图 ----------------
    auto *mainLayout = new QHBoxLayout();
    mainLayout->setSpacing(16);

    // ---- 左侧：6 张卡片 2×3 紧凑排列 ----
    auto *leftWidget = new QWidget(this);
    auto *grid = new QGridLayout(leftWidget);
    grid->setContentsMargins(0, 0, 0, 0);
    grid->setSpacing(10);

    // 卡片颜色与饼图对应扇区颜色一致
    // 在线=青 #00d4ff / 闲置=绿 #22c55e / 在用=蓝 #2b7bff
    // 维护=橙 #f59e0b / 故障=红 #ef4444 / 离线=黄 #eab308
    const QString kCyan    = QStringLiteral("color:#00d4ff;font-size:24px;font-weight:700;background:transparent;border:none;");
    const QString kGreen   = QStringLiteral("color:#22c55e;font-size:24px;font-weight:700;background:transparent;border:none;");
    const QString kBlue    = QStringLiteral("color:#2b7bff;font-size:24px;font-weight:700;background:transparent;border:none;");
    const QString kOrange  = QStringLiteral("color:#f59e0b;font-size:24px;font-weight:700;background:transparent;border:none;");
    const QString kRed     = QStringLiteral("color:#ef4444;font-size:24px;font-weight:700;background:transparent;border:none;");
    const QString kYellow  = QStringLiteral("color:#eab308;font-size:24px;font-weight:700;background:transparent;border:none;");

    // 第 0 行：在线数 / 闲置数 / 在用数
    // 第 1 行：维护数 / 故障数 / 离线数
    QFrame *cardOnline       = makeCard(tr("在线数"), QStringLiteral("cardPileOnline"),      &m_lblOnline,      &m_lblOnlinePct,      kCyan);
    QFrame *cardIdle         = makeCard(tr("闲置数"), QStringLiteral("cardPileIdle"),        &m_lblIdle,        &m_lblIdlePct,        kGreen);
    QFrame *cardInUse        = makeCard(tr("在用数"), QStringLiteral("cardPileInUse"),       &m_lblInUse,       &m_lblInUsePct,       kBlue);
    QFrame *cardMaintenance  = makeCard(tr("维护数"), QStringLiteral("cardPileMaintenance"), &m_lblMaintenance, &m_lblMaintenancePct, kOrange);
    QFrame *cardFault        = makeCard(tr("故障数"), QStringLiteral("cardPileFault"),       &m_lblFault,       &m_lblFaultPct,       kRed);
    QFrame *cardOffline      = makeCard(tr("离线数"), QStringLiteral("cardPileOffline"),     &m_lblOffline,     &m_lblOfflinePct,     kYellow);

    for (int c = 0; c < 3; ++c) {
        grid->setColumnStretch(c, 1);
    }
    grid->addWidget(cardOnline,      0, 0);
    grid->addWidget(cardIdle,        0, 1);
    grid->addWidget(cardInUse,       0, 2);
    grid->addWidget(cardMaintenance, 1, 0);
    grid->addWidget(cardFault,       1, 1);
    grid->addWidget(cardOffline,     1, 2);

    mainLayout->addWidget(leftWidget, 1);

    // ---- 右侧：双层圆环图 ----
    m_chartView = new QChartView(this);
    m_chartView->setRenderHint(QPainter::Antialiasing);
    mainLayout->addWidget(m_chartView, 1);

    rootLayout->addLayout(mainLayout, 1);
}

QFrame *PileStatusWidget::makeCard(const QString &title, const QString &objectName,
                                   QLabel **valueLabel, QLabel **pctLabel,
                                   const QString &valueColor)
{
    auto *card = new QFrame(this);
    card->setObjectName(objectName);
    card->setMinimumHeight(80);

    auto *lay = new QVBoxLayout(card);
    lay->setContentsMargins(10, 10, 10, 10);  // 内边距 10
    lay->setSpacing(4);

    auto *lblTitle = new QLabel(title, card);
    lblTitle->setObjectName(QStringLiteral("pileCardTitle"));
    lblTitle->setStyleSheet(QStringLiteral(
        "color:#4a5a6e;font-size:14px;font-weight:500;background:transparent;border:none;"));

    auto *lblValue = new QLabel(QStringLiteral("--"), card);
    lblValue->setStyleSheet(valueColor);

    // 百分比标签：颜色与数量一致，字号 14px
    auto *lblPct = new QLabel(QStringLiteral("--"), card);
    // 把 valueColor 中的 24px 替换为 14px 作为百分比样式
    lblPct->setStyleSheet(QString(valueColor)
        .replace(QStringLiteral("24px"), QStringLiteral("14px"))
        .replace(QStringLiteral("font-weight:700"), QStringLiteral("font-weight:500")));

    lay->addWidget(lblTitle);
    lay->addWidget(lblValue);
    lay->addWidget(lblPct);
    lay->addStretch(1);

    *valueLabel = lblValue;
    *pctLabel = lblPct;
    return card;
}

// ------------- Model 回调：数据就绪 -------------
void PileStatusWidget::onDataReady(const QJsonObject &data)
{
    m_lastData = data;
    refreshCards();
}

void PileStatusWidget::refreshCards()
{
    if (m_lastData.isEmpty()) {
        return;
    }

    const QJsonObject &d = m_lastData;

    const int total  = d.value(QStringLiteral("total_piles")).toInt();
    const int inUse  = d.value(QStringLiteral("in_use_count")).toInt();
    const int idle   = d.value(QStringLiteral("idle_count")).toInt();
    const int fault  = d.value(QStringLiteral("fault_count")).toInt();
    const int offline = total - inUse - idle - fault;
    const int online = inUse + idle;
    const int maintenance = fault + offline;

    // 标题栏：总桩数（带千分位分隔符）
    m_lblTotalInTitle->setText(QStringLiteral("· 总桩数 %1").arg(
        QLocale().toString(total)));

    // 6 张卡片数值
    m_lblOnline->setText(QString::number(online));
    m_lblIdle->setText(QString::number(idle));
    m_lblInUse->setText(QString::number(inUse));
    m_lblMaintenance->setText(QString::number(maintenance));
    m_lblFault->setText(QString::number(fault));
    m_lblOffline->setText(QString::number(offline));

    // 百分比 = 该分类数量 / 总桩数 × 100%（总桩数为 0 时显示 0.0%）
    const auto pctStr = [total](int v) -> QString {
        const double p = total > 0 ? static_cast<double>(v) / total * 100.0 : 0.0;
        return QString::number(p, 'f', 1) + QStringLiteral("%");
    };
    m_lblOnlinePct->setText(pctStr(online));
    m_lblIdlePct->setText(pctStr(idle));
    m_lblInUsePct->setText(pctStr(inUse));
    m_lblMaintenancePct->setText(pctStr(maintenance));
    m_lblFaultPct->setText(pctStr(fault));
    m_lblOfflinePct->setText(pctStr(offline));

    updateChart(inUse, idle, fault, offline, online, maintenance, total);
}

// ------------- 双层圆环图更新 -------------
void PileStatusWidget::updateChart(int inUse, int idle, int fault, int offline,
                                   int online, int maintenance, int total)
{
    Q_UNUSED(total);

    auto *chart = new QChart();

    // 扇区标签颜色：深色模式用浅色字
    const QColor labelColor = m_dark ? QColor(0xe8, 0xec, 0xf0) : QColor(0x1a, 0x23, 0x32);

    // ===== 外圈：4 扇区（在用 / 闲置 / 故障 / 离线） =====
    auto *outerSeries = new QPieSeries();
    outerSeries->setPieSize(0.9);     // 外圈占 chart 区域 90%
    outerSeries->setHoleSize(0.5);    // 外圈内孔 50% -> 环宽 40%

    struct SliceDef { int value; const char *name; const char *color; };
    // 外圈 4 扇区：在用(蓝) / 闲置(绿) / 故障(红) / 离线(黄)
    const SliceDef outerDefs[] = {
        { inUse,   "在用", "#2b7bff" },
        { idle,    "闲置", "#22c55e" },
        { fault,   "故障", "#ef4444" },
        { offline, "离线", "#eab308" }
    };
    for (const SliceDef &def : outerDefs) {
        if (def.value <= 0) {
            continue;
        }
        QPieSlice *slice = outerSeries->append(tr(def.name), def.value);
        slice->setColor(QColor(def.color));
        slice->setLabelColor(labelColor);
        slice->setLabelVisible(true);
        slice->setLabelPosition(QPieSlice::LabelInsideHorizontal);
    }
    chart->addSeries(outerSeries);

    // ===== 内圈：2 扇区（在线 / 维护），直径约为外圈 50% =====
    auto *innerSeries = new QPieSeries();
    innerSeries->setPieSize(0.45);    // 内圈占 chart 区域 45%（≈ 外圈 50%）
    innerSeries->setHoleSize(0.0);   // 内圈实心

    // 内圈 2 扇区：在线(青) / 维护(橙)
    const SliceDef innerDefs[] = {
        { online,      "在线", "#00d4ff" },
        { maintenance, "维护", "#f59e0b" }
    };
    for (const SliceDef &def : innerDefs) {
        if (def.value <= 0) {
            continue;
        }
        QPieSlice *slice = innerSeries->append(tr(def.name), def.value);
        slice->setColor(QColor(def.color));
        slice->setLabelColor(labelColor);
        slice->setLabelVisible(true);
        slice->setLabelPosition(QPieSlice::LabelInsideHorizontal);
    }
    chart->addSeries(innerSeries);

    // ===== 图表全局样式 =====
    chart->setTitle(tr("电桩状态分布"));
    chart->setTitleFont(QFont(QStringLiteral("Microsoft YaHei"), 12, QFont::Bold));
    chart->setTitleBrush(QBrush(labelColor));
    chart->legend()->setAlignment(Qt::AlignBottom);
    chart->legend()->setFont(QFont(QStringLiteral("Microsoft YaHei"), 9));
    chart->legend()->setLabelColor(m_dark ? QColor(0xa0, 0xa8, 0xb8) : QColor(0x4a, 0x5a, 0x6e));
    chart->setAnimationOptions(QChart::SeriesAnimations);
    chart->setBackgroundVisible(false);
    chart->setMargins(QMargins(0, 0, 0, 0));

    // 旧 chart 由 QChartView::setChart 自动释放
    m_chartView->setChart(chart);
}

// ------------- Model 回调：错误 -------------
void PileStatusWidget::onErrorOccurred(const QString &msg)
{
    QMessageBox::critical(this, tr("加载失败"), msg);
}

// ------------- 白天/夜晚主题切换 -------------
void PileStatusWidget::applyTheme(bool dark)
{
    m_dark = dark;

    if (!dark) {
        setStyleSheet(QStringLiteral(
            "QFrame#cardPileOnline,QFrame#cardPileIdle,QFrame#cardPileInUse,"
            "QFrame#cardPileMaintenance,QFrame#cardPileFault,QFrame#cardPileOffline{"
            "background-color:#ffffff;border:1px solid #e8ecf0;border-radius:8px;}"
            "QPushButton#btnPileRefresh{background-color:#ffffff;color:#1a2332;"
            "border:1px solid #d9dee5;border-radius:6px;padding:6px 18px;min-width:72px;}"
            "QPushButton#btnPileRefresh:hover{border:1px solid #2b7bff;color:#2b7bff;}"));
    } else {
        setStyleSheet(QStringLiteral(
            "QFrame#cardPileOnline,QFrame#cardPileIdle,QFrame#cardPileInUse,"
            "QFrame#cardPileMaintenance,QFrame#cardPileFault,QFrame#cardPileOffline{"
            "background-color:#252a33;border:1px solid #3a4050;border-radius:8px;}"
            "QPushButton#btnPileRefresh{background-color:#2a3040;color:#e8ecf0;"
            "border:1px solid #3a4050;border-radius:6px;padding:6px 18px;min-width:72px;}"
            "QPushButton#btnPileRefresh:hover{border:1px solid #2b7bff;color:#4d9bff;}"));
    }
    // 标题与总桩数标签文字颜色随主题切换
    if (auto *title = findChild<QLabel *>()) {
        // 第一个 QLabel 即标题（buildUi 中第一个创建的）
        title->setStyleSheet(dark
            ? QStringLiteral("color:#e8ecf0;font-size:16px;font-weight:600;background:transparent;")
            : QStringLiteral("color:#1a2332;font-size:16px;font-weight:600;background:transparent;"));
    }
    if (m_lblTotalInTitle) {
        m_lblTotalInTitle->setStyleSheet(dark
            ? QStringLiteral("color:#a0a8b8;font-size:14px;font-weight:500;background:transparent;")
            : QStringLiteral("color:#4a5a6e;font-size:14px;font-weight:500;background:transparent;"));
    }

    // 卡片标题（在线数/闲置数等）颜色随主题切换
    const QString cardTitleQss = dark
        ? QStringLiteral("color:#c8d0dc;font-size:14px;font-weight:500;background:transparent;border:none;")
        : QStringLiteral("color:#4a5a6e;font-size:14px;font-weight:500;background:transparent;border:none;");
    for (QLabel *lbl : findChildren<QLabel *>(QStringLiteral("pileCardTitle"))) {
        lbl->setStyleSheet(cardTitleQss);
    }

    // 饼图标题与图例颜色
    if (m_chartView && m_chartView->chart()) {
        QChart *chart = m_chartView->chart();
        chart->setTitleBrush(QBrush(dark ? QColor(0xe8, 0xec, 0xf0) : QColor(0x1a, 0x23, 0x32)));
        if (chart->legend()) {
            chart->legend()->setLabelColor(dark ? QColor(0xa0, 0xa8, 0xb8) : QColor(0x4a, 0x5a, 0x6e));
        }
    }

    // 重绘饼图（扇区标签颜色随主题切换），若已有数据则刷新
    if (!m_lastData.isEmpty()) {
        refreshCards();
    }
}
