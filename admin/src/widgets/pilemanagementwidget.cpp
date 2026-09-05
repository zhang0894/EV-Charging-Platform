#include "pilemanagementwidget.h"
#include "pilemanagementmodel.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QFrame>
#include <QComboBox>
#include <QPushButton>
#include <QTableView>
#include <QHeaderView>
#include <QLabel>
#include <QMessageBox>

// 表格内"重启"按钮样式（橙红），与深色科技感主题一致
static const QString kRestartBtnStyle = QStringLiteral(
    "QPushButton{background-color:#2a2013;color:#ff9f43;"
    "border:1px solid #5c4520;border-radius:4px;padding:3px 12px;min-width:52px;}"
    "QPushButton:hover:enabled{background-color:#5c4520;color:#ffffff;border-color:#ff9f43;}"
    "QPushButton:disabled{color:#5a6b85;border-color:#1e2d45;background-color:#162238;}");

PileManagementWidget::PileManagementWidget(QWidget *parent)
    : QWidget(parent)
    , m_model(new PileManagementModel(this))
{
    buildUi();

    // Model 信号 -> 界面刷新 / 错误提示
    connect(m_model, &PileManagementModel::pilesReady,
            this, &PileManagementWidget::onPilesReady);
    connect(m_model, &PileManagementModel::restartSuccess,
            this, &PileManagementWidget::onRestartSuccess);
    connect(m_model, &PileManagementModel::errorOccurred,
            this, &PileManagementWidget::onErrorOccurred);

    // 工具栏交互
    connect(m_btnQuery, &QPushButton::clicked, this, &PileManagementWidget::onQueryClicked);
    connect(m_btnRefresh, &QPushButton::clicked, this, &PileManagementWidget::onRefreshClicked);
    connect(m_statusCombo, &QComboBox::currentIndexChanged,
            this, &PileManagementWidget::onFilterChanged);
    connect(m_typeCombo, &QComboBox::currentIndexChanged,
            this, &PileManagementWidget::onFilterChanged);

    // 分页交互
    connect(m_btnPrev, &QPushButton::clicked, this, &PileManagementWidget::onPrevPage);
    connect(m_btnNext, &QPushButton::clicked, this, &PileManagementWidget::onNextPage);
}

void PileManagementWidget::setAuthToken(const QString &token)
{
    m_model->setAuthToken(token);
    // 登录成功后首次拉取第 1 页（默认条件：全部状态 / 全部类型）
    applyFiltersAndFetch(1);
}

// ------------- 界面构建（配色参考既有模块深色科技风） -------------
void PileManagementWidget::buildUi()
{
    setObjectName(QStringLiteral("pileManagementPage"));

    setStyleSheet(QStringLiteral(
        /* 顶部工具栏容器 */
        "QFrame#pileToolbar{background-color:#0f1b2d;border:1px solid #1e2d45;border-radius:8px;}"
        /* 筛选下拉框 */
        "QComboBox#pileStatusCombo,QComboBox#pileTypeCombo{background-color:#0a1424;color:#e6e9ef;"
        "border:1px solid #2a3b55;border-radius:4px;padding:6px 12px;}"
        "QComboBox#pileStatusCombo:hover,QComboBox#pileTypeCombo:hover{border:1px solid #00d4ff;}"
        "QComboBox#pileStatusCombo QAbstractItemView,QComboBox#pileTypeCombo QAbstractItemView"
        "{background-color:#0f1b2d;color:#e6e9ef;selection-background-color:#1e3a5f;selection-color:#ffffff;}"
        /* 查询/刷新按钮（与既有模块同风格） */
        "QPushButton#btnPileQuery,QPushButton#btnPileRefresh{background-color:#1a2740;"
        "color:#b8c2d1;border:1px solid #2a3b55;border-radius:4px;padding:6px 18px;min-width:72px;}"
        "QPushButton#btnPileQuery:hover,QPushButton#btnPileRefresh:hover{border:1px solid #00d4ff;color:#ffffff;}"
        /* 表格 */
        "QTableView#pileTable{background-color:#0f1b2d;alternate-background-color:#12203a;"
        "color:#e6e9ef;gridline-color:#1e2d45;border:1px solid #1e2d45;border-radius:8px;"
        "selection-background-color:#1e3a5f;selection-color:#ffffff;}"
        "QTableView#pileTable QHeaderView::section{background-color:#162238;color:#8b9bb4;"
        "border:none;border-bottom:1px solid #2a3b55;padding:8px;font-weight:600;}"
        "QTableView#pileTable QTableCornerButton::section{background-color:#162238;border:none;}"
        /* 分页按钮 */
        "QPushButton#btnPilePrev,QPushButton#btnPileNext{background-color:#1a2740;"
        "color:#b8c2d1;border:1px solid #2a3b55;border-radius:4px;padding:5px 16px;}"
        "QPushButton#btnPilePrev:hover:enabled,QPushButton#btnPileNext:hover:enabled{border:1px solid #00d4ff;color:#ffffff;}"
        "QPushButton#btnPilePrev:disabled,QPushButton#btnPileNext:disabled{color:#5a6b85;border-color:#1e2d45;}"
        /* 页码信息 */
        "QLabel#pilePageLabel{color:#8b9bb4;font-size:13px;}"));

    auto *rootLayout = new QVBoxLayout(this);
    rootLayout->setContentsMargins(20, 16, 20, 12);
    rootLayout->setSpacing(12);

    // ---------------- 顶部工具栏 ----------------
    auto *toolbar = new QFrame(this);
    toolbar->setObjectName(QStringLiteral("pileToolbar"));
    auto *toolLayout = new QHBoxLayout(toolbar);
    toolLayout->setContentsMargins(12, 10, 12, 10);
    toolLayout->setSpacing(10);

    // 状态筛选：data 依序为 ""(全部,不传参) / IDLE / CHARGING / FAULT / OFFLINE
    m_statusCombo = new QComboBox(toolbar);
    m_statusCombo->setObjectName(QStringLiteral("pileStatusCombo"));
    m_statusCombo->addItem(QStringLiteral("全部"), QString());
    m_statusCombo->addItem(QStringLiteral("空闲"), QStringLiteral("IDLE"));
    m_statusCombo->addItem(QStringLiteral("充电中"), QStringLiteral("CHARGING"));
    m_statusCombo->addItem(QStringLiteral("故障"), QStringLiteral("FAULT"));
    m_statusCombo->addItem(QStringLiteral("离线"), QStringLiteral("OFFLINE"));

    // 类型筛选：data 依序为 ""(全部,不传参) / FAST / SLOW
    m_typeCombo = new QComboBox(toolbar);
    m_typeCombo->setObjectName(QStringLiteral("pileTypeCombo"));
    m_typeCombo->addItem(QStringLiteral("全部"), QString());
    m_typeCombo->addItem(QStringLiteral("快充"), QStringLiteral("FAST"));
    m_typeCombo->addItem(QStringLiteral("慢充"), QStringLiteral("SLOW"));

    m_btnQuery = new QPushButton(QStringLiteral("查询"), toolbar);
    m_btnQuery->setObjectName(QStringLiteral("btnPileQuery"));
    m_btnRefresh = new QPushButton(QStringLiteral("刷新"), toolbar);
    m_btnRefresh->setObjectName(QStringLiteral("btnPileRefresh"));

    toolLayout->addWidget(m_statusCombo);
    toolLayout->addWidget(m_typeCombo);
    toolLayout->addWidget(m_btnQuery);
    toolLayout->addStretch(1);
    toolLayout->addWidget(m_btnRefresh);
    rootLayout->addWidget(toolbar);

    // ---------------- 中间表格 ----------------
    m_tableView = new QTableView(this);
    m_tableView->setObjectName(QStringLiteral("pileTable"));
    m_tableView->setModel(m_model->getModel());
    m_tableView->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_tableView->setSelectionMode(QAbstractItemView::SingleSelection);
    m_tableView->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_tableView->setAlternatingRowColors(true);
    m_tableView->setWordWrap(false);
    m_tableView->verticalHeader()->setVisible(false);
    m_tableView->verticalHeader()->setDefaultSectionSize(44);
    m_tableView->horizontalHeader()->setHighlightSections(false);
    m_tableView->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    // 操作列（重启按钮）固定宽度，其余列均分拉伸
    m_tableView->horizontalHeader()->setSectionResizeMode(
        PileManagementModel::ActionCol, QHeaderView::Fixed);
    m_tableView->setColumnWidth(PileManagementModel::ActionCol, 110);
    rootLayout->addWidget(m_tableView, 1);

    // ---------------- 底部分页栏 ----------------
    auto *pagerBar = new QWidget(this);
    auto *pagerLayout = new QHBoxLayout(pagerBar);
    pagerLayout->setContentsMargins(0, 0, 0, 0);
    pagerLayout->setSpacing(12);

    m_btnPrev = new QPushButton(QStringLiteral("上一页"), pagerBar);
    m_btnPrev->setObjectName(QStringLiteral("btnPilePrev"));
    m_pageLabel = new QLabel(QStringLiteral("暂无数据"), pagerBar);
    m_pageLabel->setObjectName(QStringLiteral("pilePageLabel"));
    m_pageLabel->setAlignment(Qt::AlignCenter);
    m_btnNext = new QPushButton(QStringLiteral("下一页"), pagerBar);
    m_btnNext->setObjectName(QStringLiteral("btnPileNext"));

    pagerLayout->addStretch(1);
    pagerLayout->addWidget(m_btnPrev);
    pagerLayout->addWidget(m_pageLabel);
    pagerLayout->addWidget(m_btnNext);
    pagerLayout->addStretch(1);
    rootLayout->addWidget(pagerBar);

    m_btnPrev->setEnabled(false);
    m_btnNext->setEnabled(false);
}

// ------------- 以当前筛选条件请求指定页 -------------
void PileManagementWidget::applyFiltersAndFetch(int page)
{
    const QString statusFilter = m_statusCombo ? m_statusCombo->currentData().toString() : QString();
    const QString typeFilter = m_typeCombo ? m_typeCombo->currentData().toString() : QString();
    m_model->fetchPiles(page, m_pageSize, -1, statusFilter, typeFilter);
}

// ------------- 每行操作列安装"重启"按钮 -------------
void PileManagementWidget::installActionButtons()
{
    QStandardItemModel *tm = m_model->getModel();
    for (int r = 0; r < tm->rowCount(); ++r) {
        const QModelIndex idx = tm->index(r, PileManagementModel::ActionCol);
        const QString pileId = idx.data(PileManagementModel::PileIdRole).toString();

        QPushButton *btnRestart = new QPushButton(tr("重启"), m_tableView);
        btnRestart->setCursor(Qt::PointingHandCursor);
        btnRestart->setStyleSheet(kRestartBtnStyle);
        // 按值捕获桩编号，避免行号随刷新变化带来的错位
        connect(btnRestart, &QPushButton::clicked, this,
                [this, pileId, btnRestart]() { confirmAndRestart(pileId, btnRestart); });

        // 模型行被移除/重建时，视图会自动删除旧按钮，无需手动管理
        m_tableView->setIndexWidget(idx, btnRestart);
    }
}

// ------------- 分页栏刷新 -------------
void PileManagementWidget::updatePager(int total, int page, int pageSize)
{
    m_total = total;
    m_page = qMax(1, page);
    if (pageSize > 0) {
        m_pageSize = pageSize;
    }
    m_totalPages = (m_pageSize > 0) ? (total + m_pageSize - 1) / m_pageSize : 1;
    if (m_totalPages < 1) {
        m_totalPages = 1;
    }

    if (total > 0) {
        m_pageLabel->setText(QStringLiteral("第 %1 / %2 页 · 共 %3 条")
                                 .arg(m_page).arg(m_totalPages).arg(total));
    } else {
        m_pageLabel->setText(QStringLiteral("暂无数据"));
    }
    m_btnPrev->setEnabled(m_page > 1);
    m_btnNext->setEnabled(m_page < m_totalPages);
}

// ------------- Model 回调：列表就绪 -------------
void PileManagementWidget::onPilesReady(const QJsonArray &piles, int total,
                                        int page, int pageSize)
{
    Q_UNUSED(piles);
    // 若当前页超出总页数（如筛选后数据变少），自动回退重新拉取
    int totalPages = (pageSize > 0) ? (total + pageSize - 1) / pageSize : 1;
    if (totalPages < 1) totalPages = 1;
    if (total == 0 && m_page > 1) {
        applyFiltersAndFetch(1);
        return;
    }
    if (m_page > totalPages) {
        applyFiltersAndFetch(totalPages);
        return;
    }
    updatePager(total, page, pageSize);
    installActionButtons();
}

// ------------- Model 回调：重启成功 -------------
void PileManagementWidget::onRestartSuccess(const QString &msg)
{
    QMessageBox::information(this, tr("操作成功"), msg);
    m_pendingRestartBtn.clear();
    // 重启成功后电桩状态变为 IDLE，刷新当前页同步状态列（按钮随表格重建恢复）
    applyFiltersAndFetch(m_page);
}

// ------------- Model 回调：错误 -------------
void PileManagementWidget::onErrorOccurred(const QString &errorMsg)
{
    QMessageBox::critical(this, tr("操作失败"), errorMsg);
    // 重启失败时恢复按钮为可点击状态（列表加载失败时按钮本就可用，恢复无副作用）
    if (m_pendingRestartBtn) {
        m_pendingRestartBtn->setText(tr("重启"));
        m_pendingRestartBtn->setEnabled(true);
        m_pendingRestartBtn.clear();
    }
}

// ------------- 工具栏槽 -------------
void PileManagementWidget::onQueryClicked()
{
    // 查询重置为第 1 页
    applyFiltersAndFetch(1);
}

void PileManagementWidget::onRefreshClicked()
{
    applyFiltersAndFetch(m_page);
}

void PileManagementWidget::onFilterChanged(int index)
{
    Q_UNUSED(index);
    // 状态/类型筛选变化：重置为第 1 页并自动查询
    applyFiltersAndFetch(1);
}

// ------------- 分页槽 -------------
void PileManagementWidget::onPrevPage()
{
    if (m_page > 1) {
        applyFiltersAndFetch(m_page - 1);
    }
}

void PileManagementWidget::onNextPage()
{
    if (m_page < m_totalPages) {
        applyFiltersAndFetch(m_page + 1);
    }
}

// ------------- 远程重启：确认与执行 -------------
void PileManagementWidget::confirmAndRestart(const QString &pileId, QPushButton *btn)
{
    const QMessageBox::StandardButton ret = QMessageBox::question(
        this, tr("远程重启"),
        tr("确认对电桩 [%1] 执行远程重启吗？").arg(pileId),
        QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
    if (ret != QMessageBox::Yes) {
        return;
    }

    // 进入"重启中..."状态并禁用点击，直至成功刷新或失败恢复
    m_pendingRestartBtn = btn;
    btn->setText(tr("重启中..."));
    btn->setEnabled(false);

    m_model->restartPile(pileId);
}
