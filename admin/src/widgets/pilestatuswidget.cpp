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

PileStatusWidget::PileStatusWidget(QWidget *parent)
    : QWidget(parent)
    , m_model(new PileStatusModel(this))
{
    buildUi();

    // Model 信号 -> 界面刷新 / 错误提示
    connect(m_model, &PileStatusModel::dataReady,
            this, &PileStatusWidget::onDataReady);
    connect(m_model, &PileStatusModel::errorOccurred,
            this, &PileStatusWidget::onErrorOccurred);

    // 右上角"刷新"按钮
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
    // 每次切换到本页自动刷新（Token 由 MainWindow 在构造时传入）
    m_model->fetchData();
}

// ------------- 界面构建（配色参考 DashboardWidget 深色科技风） -------------
void PileStatusWidget::buildUi()
{
    setObjectName(QStringLiteral("pileStatusPage"));

    // 卡片容器样式：背景 #0f1b2d / 边框 #1e2d45 / 圆角 8px
    setStyleSheet(QStringLiteral(
        "QFrame#cardPileTotal,QFrame#cardPileInUse,QFrame#cardPileIdle,"
        "QFrame#cardPileFault,QFrame#cardPileRate{"
        "background-color:#0f1b2d;border:1px solid #1e2d45;border-radius:8px;}"
        "QPushButton#btnPileRefresh{background-color:#1a2740;color:#b8c2d1;"
        "border:1px solid #2a3b55;border-radius:4px;padding:6px 18px;min-width:72px;}"
        "QPushButton#btnPileRefresh:hover{border:1px solid #00d4ff;color:#ffffff;}"));

    auto *rootLayout = new QVBoxLayout(this);
    rootLayout->setContentsMargins(20, 16, 20, 16);
    rootLayout->setSpacing(16);

    // ---------------- 顶部标题栏 + 刷新按钮 ----------------
    auto *topBar = new QWidget(this);
    auto *topLayout = new QHBoxLayout(topBar);
    topLayout->setContentsMargins(0, 0, 0, 0);

    auto *pageTitle = new QLabel(tr("电桩状态概览"), topBar);
    pageTitle->setStyleSheet(QStringLiteral(
        "color:#e6e9ef;font-size:16px;font-weight:600;background:transparent;"));

    m_btnRefresh = new QPushButton(tr("刷新"), topBar);
    m_btnRefresh->setObjectName(QStringLiteral("btnPileRefresh"));

    topLayout->addWidget(pageTitle);
    topLayout->addStretch(1);
    topLayout->addWidget(m_btnRefresh);
    rootLayout->addWidget(topBar);

    // ---------------- 5 个统计卡片（上 3 下 2 居中） ----------------
    // 采用 6 列网格（每列等宽 stretch=1）：
    //   第 0 行卡片各跨 2 列（0-1 / 2-3 / 4-5）
    //   第 1 行卡片各跨 2 列（1-2 / 3-4），左右各留 1 列实现居中
    auto *grid = new QGridLayout();
    grid->setSpacing(16);

    // 卡片 1：总桩数（白色数值，无副标题）
    QFrame *cardTotal = makeCard(tr("总桩数"), QStringLiteral("cardPileTotal"),
                                 &m_lblTotal, nullptr);
    // 卡片 2：在用桩（青色数值 + 占比副标题）
    QFrame *cardInUse = makeCard(tr("在用桩"), QStringLiteral("cardPileInUse"),
                                 &m_lblInUse, &m_lblInUseSub);
    // 卡片 3：闲置桩（绿色数值 + 占比副标题）
    QFrame *cardIdle = makeCard(tr("闲置桩"), QStringLiteral("cardPileIdle"),
                                &m_lblIdle, &m_lblIdleSub);
    // 卡片 4：故障桩（红色数值 + 占比副标题）
    QFrame *cardFault = makeCard(tr("故障桩"), QStringLiteral("cardPileFault"),
                                 &m_lblFault, &m_lblFaultSub);
    // 卡片 5：在线率（青色数值，副标题为 %）
    QFrame *cardRate = makeCard(tr("在线率"), QStringLiteral("cardPileRate"),
                                &m_lblRate, &m_lblRateSub);

    for (int c = 0; c < 6; ++c) {
        grid->setColumnStretch(c, 1);
    }
    grid->addWidget(cardTotal, 0, 0, 1, 2);
    grid->addWidget(cardInUse, 0, 2, 1, 2);
    grid->addWidget(cardIdle,  0, 4, 1, 2);
    grid->addWidget(cardFault, 1, 1, 1, 2);
    grid->addWidget(cardRate,  1, 3, 1, 2);
    rootLayout->addLayout(grid, 1);
    rootLayout->addStretch(0);
}

QFrame *PileStatusWidget::makeCard(const QString &title, const QString &objectName,
                                   QLabel **valueLabel, QLabel **subLabel)
{
    auto *card = new QFrame(this);
    card->setObjectName(objectName);
    card->setMinimumHeight(120);

    auto *lay = new QVBoxLayout(card);
    lay->setContentsMargins(16, 16, 16, 16); // 卡片内边距 16px
    lay->setSpacing(6);

    auto *lblTitle = new QLabel(title, card);
    lblTitle->setStyleSheet(QStringLiteral(
        "color:#8b9bb4;font-size:14px;font-weight:500;background:transparent;border:none;"));

    auto *lblValue = new QLabel(QStringLiteral("--"), card);
    lblValue->setStyleSheet(QStringLiteral(
        "color:#ffffff;font-size:30px;font-weight:700;background:transparent;border:none;"));

    lay->addWidget(lblTitle);
    lay->addWidget(lblValue);

    if (subLabel) {
        auto *lblSub = new QLabel(QStringLiteral(""), card);
        lblSub->setStyleSheet(QStringLiteral(
            "color:#5a6b85;font-size:12px;background:transparent;border:none;"));
        lay->addWidget(lblSub);
        *subLabel = lblSub;
    }

    *valueLabel = lblValue;
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

    // 卡片 1：总桩数（白色）
    m_lblTotal->setText(QString::number(
        d.value(QStringLiteral("total_piles")).toInt()));

    // 卡片 2：在用桩（青色）+ 占比
    const int inUse = d.value(QStringLiteral("in_use_count")).toInt();
    const double inUsePct = d.value(QStringLiteral("in_use_percentage")).toDouble();
    m_lblInUse->setText(QString::number(inUse));
    m_lblInUse->setStyleSheet(QStringLiteral(
        "color:#00d4ff;font-size:30px;font-weight:700;background:transparent;border:none;"));
    m_lblInUseSub->setText(tr("占比 %1%").arg(QString::number(inUsePct, 'f', 1)));

    // 卡片 3：闲置桩（绿色）+ 占比
    const int idle = d.value(QStringLiteral("idle_count")).toInt();
    const double idlePct = d.value(QStringLiteral("idle_percentage")).toDouble();
    m_lblIdle->setText(QString::number(idle));
    m_lblIdle->setStyleSheet(QStringLiteral(
        "color:#2ecc71;font-size:30px;font-weight:700;background:transparent;border:none;"));
    m_lblIdleSub->setText(tr("占比 %1%").arg(QString::number(idlePct, 'f', 1)));

    // 卡片 4：故障桩（红色）+ 占比
    const int fault = d.value(QStringLiteral("fault_count")).toInt();
    const double faultPct = d.value(QStringLiteral("fault_percentage")).toDouble();
    m_lblFault->setText(QString::number(fault));
    m_lblFault->setStyleSheet(QStringLiteral(
        "color:#ff5c5c;font-size:30px;font-weight:700;background:transparent;border:none;"));
    m_lblFaultSub->setText(tr("占比 %1%").arg(QString::number(faultPct, 'f', 1)));

    // 卡片 5：在线率（青色），副标题为 %
    const double onlineRate = d.value(QStringLiteral("online_rate")).toDouble();
    m_lblRate->setText(QString::number(onlineRate, 'f', 1));
    m_lblRate->setStyleSheet(QStringLiteral(
        "color:#00d4ff;font-size:30px;font-weight:700;background:transparent;border:none;"));
    m_lblRateSub->setText(QStringLiteral("%"));
}

// ------------- Model 回调：错误 -------------
void PileStatusWidget::onErrorOccurred(const QString &msg)
{
    QMessageBox::critical(this, tr("加载失败"), msg);
}
