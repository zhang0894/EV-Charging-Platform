#include "stationmanagementwidget.h"
#include "stationmanagementmodel.h"
#include "pilemanagementmodel.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QFormLayout>
#include <QFrame>
#include <QLineEdit>
#include <QComboBox>
#include <QPushButton>
#include <QTableView>
#include <QHeaderView>
#include <QLabel>
#include <QMessageBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QTabWidget>
#include <QTableWidget>
#include <QJsonDocument>

// 表格内操作按钮样式（详情=青 / 上线=绿 / 下线=橙红），与深色科技感主题一致
static const QString kDetailBtnStyle = QStringLiteral(
    "QPushButton{background-color:#0e2536;color:#00d4ff;"
    "border:1px solid #1f4a63;border-radius:4px;padding:3px 10px;min-width:44px;}"
    "QPushButton:hover{background-color:#1f4a63;color:#ffffff;border-color:#00d4ff;}");

static const QString kOnlineBtnStyle = QStringLiteral(
    "QPushButton{background-color:#12291f;color:#2ecc71;"
    "border:1px solid #1f5c40;border-radius:4px;padding:3px 10px;min-width:44px;}"
    "QPushButton:hover{background-color:#1f5c40;color:#ffffff;border-color:#2ecc71;}");

static const QString kOfflineBtnStyle = QStringLiteral(
    "QPushButton{background-color:#2a2013;color:#ff9f43;"
    "border:1px solid #5c4520;border-radius:4px;padding:3px 10px;min-width:44px;}"
    "QPushButton:hover{background-color:#5c4520;color:#ffffff;border-color:#ff9f43;}");

StationManagementWidget::StationManagementWidget(QWidget *parent)
    : QWidget(parent)
    , m_model(new StationManagementModel(this))
    , m_pileListModel(new PileManagementModel(this))
{
    buildUi();

    // Model 信号 -> 界面刷新 / 错误提示
    connect(m_model, &StationManagementModel::stationsReady,
            this, &StationManagementWidget::onStationsReady);
    connect(m_model, &StationManagementModel::salesDataReady,
            this, &StationManagementWidget::onSalesDataReady);
    connect(m_model, &StationManagementModel::operationSuccess,
            this, &StationManagementWidget::onOperationSuccess);
    connect(m_model, &StationManagementModel::errorOccurred,
            this, &StationManagementWidget::onErrorOccurred);

    // 详情弹窗"电桩列表"Tab 的数据源（复用 PileManagementModel，传 station_id 拉取）
    connect(m_pileListModel, &PileManagementModel::pilesReady,
            this, &StationManagementWidget::onStationPilesReady);
    connect(m_pileListModel, &PileManagementModel::errorOccurred,
            this, &StationManagementWidget::onErrorOccurred);

    // 工具栏交互
    connect(m_btnQuery, &QPushButton::clicked, this, &StationManagementWidget::onQueryClicked);
    connect(m_btnRefresh, &QPushButton::clicked, this, &StationManagementWidget::onRefreshClicked);
    connect(m_btnAdd, &QPushButton::clicked, this, &StationManagementWidget::onAddStationClicked);
    connect(m_searchEdit, &QLineEdit::returnPressed, this, &StationManagementWidget::onQueryClicked);
    connect(m_statusCombo, &QComboBox::currentIndexChanged,
            this, &StationManagementWidget::onStatusFilterChanged);

    // 分页交互
    connect(m_btnPrev, &QPushButton::clicked, this, &StationManagementWidget::onPrevPage);
    connect(m_btnNext, &QPushButton::clicked, this, &StationManagementWidget::onNextPage);
}

void StationManagementWidget::setAuthToken(const QString &token)
{
    m_model->setAuthToken(token);
    // 详情弹窗"电桩列表"Tab 复用 PileManagementModel，同样需要 Token
    m_pileListModel->setAuthToken(token);
    // 登录成功后首次拉取第 1 页（默认条件：无站名筛选 / 全部状态）
    applyFiltersAndFetch(1);
}

// ------------- 界面构建（配色参考 UserManagementWidget 深色科技风） -------------
void StationManagementWidget::buildUi()
{
    setObjectName(QStringLiteral("stationManagementPage"));

    setStyleSheet(QStringLiteral(
        /* 顶部工具栏容器 */
        "QFrame#stationToolbar{background-color:#0f1b2d;border:1px solid #1e2d45;border-radius:8px;}"
        /* 搜索框 */
        "QLineEdit#stationSearchEdit{background-color:#0a1424;color:#e6e9ef;"
        "border:1px solid #2a3b55;border-radius:4px;padding:6px 10px;}"
        "QLineEdit#stationSearchEdit:focus{border:1px solid #00d4ff;}"
        /* 状态筛选下拉框 */
        "QComboBox#stationStatusCombo{background-color:#0a1424;color:#e6e9ef;"
        "border:1px solid #2a3b55;border-radius:4px;padding:6px 12px;}"
        "QComboBox#stationStatusCombo:hover{border:1px solid #00d4ff;}"
        "QComboBox#stationStatusCombo QAbstractItemView{background-color:#0f1b2d;"
        "color:#e6e9ef;selection-background-color:#1e3a5f;selection-color:#ffffff;}"
        /* 查询/刷新按钮（与用户管理页同风格） */
        "QPushButton#btnStationQuery,QPushButton#btnStationRefresh{background-color:#1a2740;"
        "color:#b8c2d1;border:1px solid #2a3b55;border-radius:4px;padding:6px 18px;min-width:72px;}"
        "QPushButton#btnStationQuery:hover,QPushButton#btnStationRefresh:hover{border:1px solid #00d4ff;color:#ffffff;}"
        /* 新增电站按钮：青色高亮主操作 */
        "QPushButton#btnStationAdd{background-color:#0e3a4d;color:#00d4ff;"
        "border:1px solid #1f6a8c;border-radius:4px;padding:6px 18px;min-width:88px;}"
        "QPushButton#btnStationAdd:hover{background-color:#1f6a8c;color:#ffffff;border-color:#00d4ff;}"
        /* 表格 */
        "QTableView#stationTable{background-color:#0f1b2d;alternate-background-color:#12203a;"
        "color:#e6e9ef;gridline-color:#1e2d45;border:1px solid #1e2d45;border-radius:8px;"
        "selection-background-color:#1e3a5f;selection-color:#ffffff;}"
        "QTableView#stationTable QHeaderView::section{background-color:#162238;color:#8b9bb4;"
        "border:none;border-bottom:1px solid #2a3b55;padding:8px;font-weight:600;}"
        "QTableView#stationTable QTableCornerButton::section{background-color:#162238;border:none;}"
        /* 分页按钮 */
        "QPushButton#btnStationPrev,QPushButton#btnStationNext{background-color:#1a2740;"
        "color:#b8c2d1;border:1px solid #2a3b55;border-radius:4px;padding:5px 16px;}"
        "QPushButton#btnStationPrev:hover:enabled,QPushButton#btnStationNext:hover:enabled{border:1px solid #00d4ff;color:#ffffff;}"
        "QPushButton#btnStationPrev:disabled,QPushButton#btnStationNext:disabled{color:#5a6b85;border-color:#1e2d45;}"
        /* 页码信息 */
        "QLabel#stationPageLabel{color:#8b9bb4;font-size:13px;}"));

    auto *rootLayout = new QVBoxLayout(this);
    rootLayout->setContentsMargins(20, 16, 20, 12);
    rootLayout->setSpacing(12);

    // ---------------- 顶部工具栏 ----------------
    auto *toolbar = new QFrame(this);
    toolbar->setObjectName(QStringLiteral("stationToolbar"));
    auto *toolLayout = new QHBoxLayout(toolbar);
    toolLayout->setContentsMargins(12, 10, 12, 10);
    toolLayout->setSpacing(10);

    m_searchEdit = new QLineEdit(toolbar);
    m_searchEdit->setObjectName(QStringLiteral("stationSearchEdit"));
    m_searchEdit->setPlaceholderText(QStringLiteral("按站名搜索"));
    m_searchEdit->setClearButtonEnabled(true);
    m_searchEdit->setFixedWidth(220);

    m_statusCombo = new QComboBox(toolbar);
    m_statusCombo->setObjectName(QStringLiteral("stationStatusCombo"));
    // itemData: -1=全部(不携带 status 参数), 1=正常运营, 2=维护中
    m_statusCombo->addItem(QStringLiteral("全部"), -1);
    m_statusCombo->addItem(QStringLiteral("正常运营"), 1);
    m_statusCombo->addItem(QStringLiteral("维护中"), 2);

    m_btnQuery = new QPushButton(QStringLiteral("查询"), toolbar);
    m_btnQuery->setObjectName(QStringLiteral("btnStationQuery"));
    m_btnRefresh = new QPushButton(QStringLiteral("刷新"), toolbar);
    m_btnRefresh->setObjectName(QStringLiteral("btnStationRefresh"));
    m_btnAdd = new QPushButton(QStringLiteral("新增电站"), toolbar);
    m_btnAdd->setObjectName(QStringLiteral("btnStationAdd"));

    toolLayout->addWidget(m_searchEdit);
    toolLayout->addWidget(m_statusCombo);
    toolLayout->addWidget(m_btnQuery);
    toolLayout->addWidget(m_btnRefresh);
    toolLayout->addStretch(1);
    toolLayout->addWidget(m_btnAdd);
    rootLayout->addWidget(toolbar);

    // ---------------- 中间表格 ----------------
    m_tableView = new QTableView(this);
    m_tableView->setObjectName(QStringLiteral("stationTable"));
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
    // 操作列（详情 + 上线/下线 两个按钮）固定宽度，其余列均分拉伸
    m_tableView->horizontalHeader()->setSectionResizeMode(
        StationManagementModel::ActionCol, QHeaderView::Fixed);
    m_tableView->setColumnWidth(StationManagementModel::ActionCol, 170);
    rootLayout->addWidget(m_tableView, 1);

    // ---------------- 底部分页栏 ----------------
    auto *pagerBar = new QWidget(this);
    auto *pagerLayout = new QHBoxLayout(pagerBar);
    pagerLayout->setContentsMargins(0, 0, 0, 0);
    pagerLayout->setSpacing(12);

    m_btnPrev = new QPushButton(QStringLiteral("上一页"), pagerBar);
    m_btnPrev->setObjectName(QStringLiteral("btnStationPrev"));
    m_pageLabel = new QLabel(QStringLiteral("暂无数据"), pagerBar);
    m_pageLabel->setObjectName(QStringLiteral("stationPageLabel"));
    m_pageLabel->setAlignment(Qt::AlignCenter);
    m_btnNext = new QPushButton(QStringLiteral("下一页"), pagerBar);
    m_btnNext->setObjectName(QStringLiteral("btnStationNext"));

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
void StationManagementWidget::applyFiltersAndFetch(int page)
{
    const QString name = m_searchEdit ? m_searchEdit->text().trimmed() : QString();
    const int statusFilter = m_statusCombo
        ? m_statusCombo->currentData().toInt() : -1;
    m_model->fetchStations(page, m_pageSize, name, statusFilter);
}

// ------------- 每行操作列安装"详情"+"上线/下线"按钮 -------------
void StationManagementWidget::installActionButtons()
{
    QStandardItemModel *tm = m_model->getModel();
    for (int r = 0; r < tm->rowCount(); ++r) {
        const QModelIndex idx = tm->index(r, StationManagementModel::ActionCol);
        const int stationId = idx.data(StationManagementModel::StationIdRole).toInt();
        const int status = idx.data(StationManagementModel::StatusRole).toInt();
        const QString name = idx.data(StationManagementModel::NameRole).toString();

        const bool offline = (status == 2);
        const int targetStatus = offline ? 1 : 2; // 状态取反：正常运营 <-> 维护中

        auto *actions = new QWidget(m_tableView);
        auto *lay = new QHBoxLayout(actions);
        lay->setContentsMargins(2, 2, 2, 2);
        lay->setSpacing(6);

        QPushButton *btnDetail = new QPushButton(tr("详情"), actions);
        btnDetail->setCursor(Qt::PointingHandCursor);
        btnDetail->setStyleSheet(kDetailBtnStyle);
        connect(btnDetail, &QPushButton::clicked, this,
                [this, stationId, name]() { showSalesDetail(stationId, name); });

        QPushButton *btnToggle = new QPushButton(offline ? tr("上线") : tr("下线"), actions);
        btnToggle->setCursor(Qt::PointingHandCursor);
        btnToggle->setStyleSheet(offline ? kOnlineBtnStyle : kOfflineBtnStyle);
        // 按值捕获目标操作，避免行号随刷新变化带来的错位
        connect(btnToggle, &QPushButton::clicked, this,
                [this, stationId, name, targetStatus]() {
                    confirmAndSetStatus(stationId, name, targetStatus);
                });

        lay->addWidget(btnDetail);
        lay->addWidget(btnToggle);
        // 模型行被移除/重建时，视图会自动删除旧按钮容器，无需手动管理
        m_tableView->setIndexWidget(idx, actions);
    }
}

// ------------- 分页栏刷新 -------------
void StationManagementWidget::updatePager(int total, int page, int pageSize)
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
void StationManagementWidget::onStationsReady(const QJsonArray &stations, int total,
                                              int page, int pageSize)
{
    Q_UNUSED(stations);
    // 若当前页超出总页数（如筛选后数据变少），自动回退到最后一页重新拉取
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

// ------------- Model 回调：操作成功（上线/下线） -------------
void StationManagementWidget::onOperationSuccess(const QString &msg)
{
    QMessageBox::information(this, tr("操作成功"), msg);
    // 成功后刷新当前页，保证状态列与操作按钮同步
    applyFiltersAndFetch(m_page);
}

// ------------- Model 回调：错误 -------------
void StationManagementWidget::onErrorOccurred(const QString &errorMsg)
{
    QMessageBox::critical(this, tr("操作失败"), errorMsg);
}

// ------------- 工具栏槽 -------------
void StationManagementWidget::onQueryClicked()
{
    // 搜索重置为第 1 页
    applyFiltersAndFetch(1);
}

void StationManagementWidget::onRefreshClicked()
{
    applyFiltersAndFetch(m_page);
}

void StationManagementWidget::onStatusFilterChanged(int index)
{
    Q_UNUSED(index);
    // 状态筛选变化：重置为第 1 页并自动查询
    applyFiltersAndFetch(1);
}

// ------------- 分页槽 -------------
void StationManagementWidget::onPrevPage()
{
    if (m_page > 1) {
        applyFiltersAndFetch(m_page - 1);
    }
}

void StationManagementWidget::onNextPage()
{
    if (m_page < m_totalPages) {
        applyFiltersAndFetch(m_page + 1);
    }
}

// ------------- 上线/下线确认与执行 -------------
void StationManagementWidget::confirmAndSetStatus(int stationId, const QString &name,
                                                  int targetStatus)
{
    const bool online = (targetStatus == 1);
    const QString actionText = online ? tr("上线") : tr("下线");
    const bool isMock = (stationId < 0);

    const QMessageBox::StandardButton ret = QMessageBox::question(
        this, QStringLiteral("%1充电站").arg(actionText),
        QStringLiteral("确认将充电站 %1（ID: %2）%3？%4")
            .arg(name.isEmpty() ? QStringLiteral("-") : name,
                 QString::number(stationId), actionText,
                 isMock ? QStringLiteral("\n（模拟电站：仅修改本地内存状态）") : QString()),
        QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
    if (ret != QMessageBox::Yes) {
        return;
    }

    m_model->setStationStatus(stationId, targetStatus);
}

// ------------- 新增模拟电站对话框 -------------
void StationManagementWidget::onAddStationClicked()
{
    showAddStationDialog();
}

void StationManagementWidget::showAddStationDialog()
{
    QDialog dlg(this);
    dlg.setWindowTitle(tr("新增电站（本地模拟）"));
    dlg.setMinimumWidth(420);
    // 深色科技风弹窗样式
    dlg.setStyleSheet(QStringLiteral(
        "QDialog{background-color:#0f1b2d;}"
        "QLabel{color:#b8c2d1;background:transparent;}"
        "QLineEdit{background-color:#0a1424;color:#e6e9ef;"
        "border:1px solid #2a3b55;border-radius:4px;padding:6px 10px;}"
        "QLineEdit:focus{border:1px solid #00d4ff;}"
        "QPushButton{background-color:#1a2740;color:#b8c2d1;"
        "border:1px solid #2a3b55;border-radius:4px;padding:6px 18px;min-width:72px;}"
        "QPushButton:hover{border:1px solid #00d4ff;color:#ffffff;}"));

    auto *form = new QFormLayout(&dlg);
    form->setContentsMargins(20, 20, 20, 20);
    form->setSpacing(12);

    auto *nameEdit = new QLineEdit(&dlg);
    nameEdit->setPlaceholderText(tr("必填，如：XX科技园充电站"));
    auto *addrEdit = new QLineEdit(&dlg);
    addrEdit->setPlaceholderText(tr("必填，如：深圳市南山区XX路 1 号"));
    auto *latEdit = new QLineEdit(&dlg);
    latEdit->setPlaceholderText(tr("可选，如：22.5431"));
    auto *lonEdit = new QLineEdit(&dlg);
    lonEdit->setPlaceholderText(tr("可选，如：113.9527"));

    form->addRow(tr("站名："), nameEdit);
    form->addRow(tr("地址："), addrEdit);
    form->addRow(tr("纬度："), latEdit);
    form->addRow(tr("经度："), lonEdit);
    form->addRow(new QLabel(tr("注：其余字段使用默认值，仅保存于本地内存，程序退出后自动清除。"), &dlg));

    auto *btnBox = new QDialogButtonBox(
        QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dlg);
    btnBox->button(QDialogButtonBox::Ok)->setText(tr("确认"));
    btnBox->button(QDialogButtonBox::Cancel)->setText(tr("取消"));
    form->addRow(btnBox);

    // 校验：站名/地址必填，通过后才关闭对话框
    connect(btnBox, &QDialogButtonBox::accepted, &dlg, [&dlg, nameEdit, addrEdit, latEdit, lonEdit, this]() {
        const QString name = nameEdit->text().trimmed();
        const QString addr = addrEdit->text().trimmed();
        if (name.isEmpty() || addr.isEmpty()) {
            QMessageBox::warning(&dlg, tr("输入不完整"), tr("站名与地址为必填项，请补充后再确认。"));
            return;
        }
        bool latOk = true, lonOk = true;
        const double lat = latEdit->text().trimmed().toDouble(&latOk);
        const double lon = lonEdit->text().trimmed().toDouble(&lonOk);

        StationManagementModel::StationInfo info;
        info.station_name = name;
        info.address = addr;
        info.latitude = latOk ? lat : 0.0;
        info.longitude = lonOk ? lon : 0.0;
        m_model->addMockStation(info); // 其余字段由 Model 填充默认值并分配负数 ID

        QMessageBox::information(&dlg, tr("操作成功"),
                                 tr("模拟电站新增成功（仅本地内存，程序退出后自动清除）"));
        dlg.accept();
    });
    connect(btnBox, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);

    if (dlg.exec() == QDialog::Accepted) {
        // 新电站显示在列表最前面，回到第 1 页刷新
        applyFiltersAndFetch(1);
    }
}

// ------------- 销售详情对话框 -------------
void StationManagementWidget::showSalesDetail(int stationId, const QString &stationName)
{
    // 模拟电站：直接提示无销售记录（提供"关闭"按钮）
    if (stationId < 0) {
        QMessageBox box(this);
        box.setWindowTitle(tr("销售详情"));
        box.setText(tr("该电站为本地模拟数据，暂无销售记录"));
        box.addButton(tr("关闭"), QMessageBox::AcceptRole);
        box.exec();
        return;
    }

    // 清理旧弹窗（QPointer 保证悬挂安全）
    if (m_detailDialog) {
        m_detailDialog->deleteLater();
        m_detailDialog.clear();
    }

    m_detailDialog = new QDialog(this);
    QDialog *dlg = m_detailDialog.data();
    dlg->setWindowTitle(QStringLiteral("充电站销售详情 - %1（ID: %2）")
                            .arg(stationName.isEmpty() ? QStringLiteral("-") : stationName)
                            .arg(stationId));
    dlg->setModal(true);
    dlg->setMinimumSize(680, 460);
    // 深色科技风弹窗样式（QTabWidget + QTableWidget）
    dlg->setStyleSheet(QStringLiteral(
        "QDialog{background-color:#0f1b2d;}"
        "QTabWidget::pane{border:1px solid #1e2d45;background-color:#0f1b2d;border-radius:6px;}"
        "QTabBar::tab{background-color:#162238;color:#8b9bb4;padding:8px 24px;"
        "border:1px solid #1e2d45;border-bottom:none;border-top-left-radius:6px;border-top-right-radius:6px;}"
        "QTabBar::tab:selected{color:#00d4ff;border-bottom:2px solid #00d4ff;}"
        "QTableWidget{background-color:#0f1b2d;alternate-background-color:#12203a;"
        "color:#e6e9ef;gridline-color:#1e2d45;border:none;}"
        "QTableWidget QHeaderView::section{background-color:#162238;color:#8b9bb4;"
        "border:none;border-bottom:1px solid #2a3b55;padding:8px;font-weight:600;}"
        "QPushButton{background-color:#1a2740;color:#b8c2d1;"
        "border:1px solid #2a3b55;border-radius:4px;padding:6px 18px;min-width:72px;}"
        "QPushButton:hover{border:1px solid #00d4ff;color:#ffffff;}"));

    auto *lay = new QVBoxLayout(dlg);
    lay->setContentsMargins(16, 16, 16, 16);
    lay->setSpacing(12);

    m_detailTabs = new QTabWidget(dlg);
    m_todayTable = new QTableWidget(m_detailTabs);
    m_weekTable  = new QTableWidget(m_detailTabs);
    m_monthTable = new QTableWidget(m_detailTabs);
    m_pileListTable = new QTableWidget(m_detailTabs);
    m_detailTabs->addTab(m_todayTable, tr("今日"));
    m_detailTabs->addTab(m_weekTable,  tr("近7天"));
    m_detailTabs->addTab(m_monthTable, tr("近30天"));
    m_detailTabs->addTab(m_pileListTable, tr("电桩列表"));
    lay->addWidget(m_detailTabs, 1);

    // 点击"电桩列表"Tab 内任意行：提示前往"充电桩管理"模块（不关闭详情弹窗）
    connect(m_pileListTable, &QTableWidget::cellClicked, dlg, [this](int, int) {
        QMessageBox box(this);
        box.setWindowTitle(tr("提示"));
        box.setText(tr("该充电桩的详细信息请前往「充电桩管理」模块查看"));
        box.addButton(tr("知道了"), QMessageBox::AcceptRole);
        box.exec();
    });

    // 切换 Tab 时按需加载：前 3 个 Tab 加载对应时间维度销售数据，
    // 第 4 个 Tab（电桩列表）首次切换时加载该站全部电桩
    connect(m_detailTabs, &QTabWidget::currentChanged, dlg, [this](int index) {
        if (index == 3) { // "电桩列表" Tab
            if (!m_pileListLoaded) {
                m_pileListModel->fetchPiles(1, 100, m_detailStationId);
            }
            return;
        }
        static const QStringList ranges = { QStringLiteral("today"),
                                            QStringLiteral("7d"),
                                            QStringLiteral("30d") };
        const QString range = ranges.value(index);
        if (!range.isEmpty() && !m_salesLoaded.contains(range)) {
            requestSalesRange(range);
        }
    });

    auto *btnClose = new QPushButton(tr("关闭"), dlg);
    connect(btnClose, &QPushButton::clicked, dlg, &QDialog::close);
    auto *btnLay = new QHBoxLayout();
    btnLay->addStretch(1);
    btnLay->addWidget(btnClose);
    lay->addLayout(btnLay);

    m_detailStationId = stationId;
    m_salesLoaded.clear();
    m_pendingSalesRange.clear();
    m_pileListLoaded = false; // "电桩列表"Tab 需针对新电站重新加载

    // 弹窗关闭后自动销毁（QPointer 自动置空，后续回调安全跳过）
    connect(dlg, &QDialog::finished, dlg, &QObject::deleteLater);

    // 首屏先加载"今日"维度
    requestSalesRange(QStringLiteral("today"));

    dlg->show(); // 模态但异步：不阻塞事件循环，销售数据到达后按需填充
}

void StationManagementWidget::requestSalesRange(const QString &timeRange)
{
    m_pendingSalesRange = timeRange;
    m_model->fetchStationSales(m_detailStationId, timeRange);
}

QTableWidget *tableForRange_helper();

void StationManagementWidget::onSalesDataReady(const QJsonObject &data, int stationId)
{
    // 弹窗已关闭 / 已切换到其他电站的响应：直接丢弃
    if (m_detailDialog.isNull() || stationId != m_detailStationId) {
        return;
    }

    // 与文档结构不一致时打印原始响应
    const QString raw = QJsonDocument(data).toJson(QJsonDocument::Compact);
    Q_UNUSED(raw);

    QTableWidget *target = nullptr;
    if (m_pendingSalesRange == QStringLiteral("today")) {
        target = m_todayTable;
    } else if (m_pendingSalesRange == QStringLiteral("7d")) {
        target = m_weekTable;
    } else if (m_pendingSalesRange == QStringLiteral("30d")) {
        target = m_monthTable;
    }
    if (!target) {
        qWarning().noquote() << "[StationManagementWidget] 销售数据无对应 Tab, 原始响应:"
                             << raw;
        return;
    }

    fillSalesTable(target, data);
    m_salesLoaded.insert(m_pendingSalesRange);
    m_pendingSalesRange.clear();
}

void StationManagementWidget::fillSalesTable(QTableWidget *table, const QJsonObject &data)
{
    table->clear();
    table->setColumnCount(4);
    table->setHorizontalHeaderLabels({ tr("日期"), tr("营收(元)"),
                                       tr("充电量(kWh)"), tr("订单数") });
    table->setSelectionBehavior(QAbstractItemView::SelectRows);
    table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    table->setAlternatingRowColors(true);
    table->setWordWrap(false);
    table->verticalHeader()->setVisible(false);
    table->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);

    const QJsonObject timeline = data.value(QStringLiteral("timeline")).toObject();
    const QJsonArray slotLabels = timeline.value(QStringLiteral("time_slots")).toArray();
    const QJsonArray rev = timeline.value(QStringLiteral("revenue_series")).toArray();
    const QJsonArray energy = timeline.value(QStringLiteral("energy_series")).toArray();
    const QJsonArray orders = timeline.value(QStringLiteral("order_series")).toArray();

    // 四条序列按最短长度对齐，防止越界
    const qsizetype n = qMin(qMin(slotLabels.size(), rev.size()),
                             qMin(energy.size(), orders.size()));

    if (n <= 0) {
        // 无数据占位行
        table->setRowCount(1);
        table->setSpan(0, 0, 1, 4);
        auto *placeholder = new QTableWidgetItem(tr("暂无数据"));
        placeholder->setTextAlignment(Qt::AlignCenter);
        placeholder->setForeground(QColor(0x5a, 0x6b, 0x85));
        table->setItem(0, 0, placeholder);
        return;
    }

    table->setRowCount(static_cast<int>(n));
    for (qsizetype i = 0; i < n; ++i) {
        auto *slotItem = new QTableWidgetItem(slotLabels.at(i).toString());
        slotItem->setTextAlignment(Qt::AlignCenter);
        auto *revItem = new QTableWidgetItem(
            QString::number(rev.at(i).toDouble(), 'f', 2));
        revItem->setTextAlignment(Qt::AlignRight | Qt::AlignVCenter);
        auto *engItem = new QTableWidgetItem(
            QString::number(energy.at(i).toDouble(), 'f', 2));
        engItem->setTextAlignment(Qt::AlignRight | Qt::AlignVCenter);
        auto *ordItem = new QTableWidgetItem(
            QString::number(orders.at(i).toInt()));
        ordItem->setTextAlignment(Qt::AlignCenter);
        table->setItem(static_cast<int>(i), 0, slotItem);
        table->setItem(static_cast<int>(i), 1, revItem);
        table->setItem(static_cast<int>(i), 2, engItem);
        table->setItem(static_cast<int>(i), 3, ordItem);
    }
}

// ------------- 详情弹窗"电桩列表"Tab：数据就绪与填充 -------------
void StationManagementWidget::onStationPilesReady(const QJsonArray &piles)
{
    // 弹窗已关闭：丢弃响应（电桩列表请求由详情弹窗发起，无需再校验电站 ID）
    if (m_detailDialog.isNull()) {
        return;
    }
    fillPileListTable(piles);
}

void StationManagementWidget::fillPileListTable(const QJsonArray &piles)
{
    // 6 列只读表格：桩编号 / 类型 / 功率(kW) / 状态 / 累计充电次数 / 累计充电时长
    m_pileListTable->clear();
    m_pileListTable->setColumnCount(6);
    m_pileListTable->setHorizontalHeaderLabels({ tr("桩编号"), tr("类型"),
                                                 tr("功率(kW)"), tr("状态"),
                                                 tr("累计充电次数"), tr("累计充电时长") });
    m_pileListTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_pileListTable->setSelectionMode(QAbstractItemView::SingleSelection);
    m_pileListTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_pileListTable->setAlternatingRowColors(true);
    m_pileListTable->setWordWrap(false);
    m_pileListTable->verticalHeader()->setVisible(false);
    m_pileListTable->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);

    if (piles.isEmpty()) {
        // 无数据占位行
        m_pileListTable->setRowCount(1);
        m_pileListTable->setSpan(0, 0, 1, 6);
        auto *placeholder = new QTableWidgetItem(tr("暂无电桩"));
        placeholder->setTextAlignment(Qt::AlignCenter);
        placeholder->setForeground(QColor(0x5a, 0x6b, 0x85));
        m_pileListTable->setItem(0, 0, placeholder);
        m_pileListLoaded = true;
        return;
    }

    m_pileListTable->setRowCount(static_cast<int>(piles.size()));
    for (qsizetype i = 0; i < piles.size(); ++i) {
        const QJsonObject p = piles.at(i).toObject();
        const int row = static_cast<int>(i);

        const QString pileId = p.value(QStringLiteral("pile_id")).toString();
        const QString type = p.value(QStringLiteral("type")).toString();
        const double powerKw = p.value(QStringLiteral("power_kw")).toDouble();
        const QString status = p.value(QStringLiteral("current_status")).toString();
        const int chargeCount = p.value(QStringLiteral("total_charge_count")).toInt();
        const double chargeHours = p.value(QStringLiteral("total_charge_hours")).toDouble();

        // 桩编号
        auto *idItem = new QTableWidgetItem(pileId);
        idItem->setTextAlignment(Qt::AlignCenter);
        // 类型（快充青 / 慢充灰）
        auto *typeItem = new QTableWidgetItem(PileManagementModel::pileTypeText(type));
        typeItem->setTextAlignment(Qt::AlignCenter);
        typeItem->setForeground(PileManagementModel::pileTypeColor(type));
        // 功率（保留 1 位小数）
        auto *powerItem = new QTableWidgetItem(QString::number(powerKw, 'f', 1));
        powerItem->setTextAlignment(Qt::AlignCenter);
        // 状态（带颜色）
        auto *stItem = new QTableWidgetItem(PileManagementModel::pileStatusText(status));
        stItem->setTextAlignment(Qt::AlignCenter);
        stItem->setForeground(PileManagementModel::pileStatusColor(status));
        // 累计充电次数
        auto *countItem = new QTableWidgetItem(QString::number(chargeCount));
        countItem->setTextAlignment(Qt::AlignCenter);
        // 累计充电时长（如 1240.5h）
        auto *hoursItem = new QTableWidgetItem(
            QStringLiteral("%1h").arg(QString::number(chargeHours, 'f', 1)));
        hoursItem->setTextAlignment(Qt::AlignCenter);

        m_pileListTable->setItem(row, 0, idItem);
        m_pileListTable->setItem(row, 1, typeItem);
        m_pileListTable->setItem(row, 2, powerItem);
        m_pileListTable->setItem(row, 3, stItem);
        m_pileListTable->setItem(row, 4, countItem);
        m_pileListTable->setItem(row, 5, hoursItem);
    }
    m_pileListLoaded = true;
}
