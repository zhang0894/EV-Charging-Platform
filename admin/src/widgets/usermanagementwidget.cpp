#include "usermanagementwidget.h"
#include "usermanagementmodel.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QFrame>
#include <QLineEdit>
#include <QComboBox>
#include <QPushButton>
#include <QTableView>
#include <QHeaderView>
#include <QLabel>
#include <QMessageBox>

// 表格内操作按钮样式（冻结=红 / 解冻=绿），与深色科技感主题一致
static const QString kFreezeBtnStyle = QStringLiteral(
    "QPushButton{background-color:#2a1622;color:#ff6b6b;"
    "border:1px solid #5c2a3a;border-radius:4px;padding:3px 14px;min-width:52px;}"
    "QPushButton:hover{background-color:#5c2a3a;color:#ffffff;border-color:#ff6b6b;}");

static const QString kUnfreezeBtnStyle = QStringLiteral(
    "QPushButton{background-color:#12291f;color:#2ecc71;"
    "border:1px solid #1f5c40;border-radius:4px;padding:3px 14px;min-width:52px;}"
    "QPushButton:hover{background-color:#1f5c40;color:#ffffff;border-color:#2ecc71;}");

UserManagementWidget::UserManagementWidget(QWidget *parent)
    : QWidget(parent)
    , m_model(new UserManagementModel(this))
{
    buildUi();

    // Model 信号 -> 界面刷新
    connect(m_model, &UserManagementModel::usersReady,
            this, &UserManagementWidget::onUsersReady);
    connect(m_model, &UserManagementModel::operationSuccess,
            this, &UserManagementWidget::onOperationSuccess);
    connect(m_model, &UserManagementModel::errorOccurred,
            this, &UserManagementWidget::onErrorOccurred);

    // 工具栏交互
    connect(m_btnQuery, &QPushButton::clicked, this, &UserManagementWidget::onQueryClicked);
    connect(m_btnRefresh, &QPushButton::clicked, this, &UserManagementWidget::onRefreshClicked);
    connect(m_searchEdit, &QLineEdit::returnPressed, this, &UserManagementWidget::onQueryClicked);
    connect(m_statusCombo, &QComboBox::currentIndexChanged,
            this, &UserManagementWidget::onStatusFilterChanged);

    // 分页交互
    connect(m_btnPrev, &QPushButton::clicked, this, &UserManagementWidget::onPrevPage);
    connect(m_btnNext, &QPushButton::clicked, this, &UserManagementWidget::onNextPage);
}

void UserManagementWidget::setAuthToken(const QString &token)
{
    m_model->setAuthToken(token);
    // 登录成功后首次拉取第 1 页（默认条件：无手机号筛选 / 全部状态）
    applyFiltersAndFetch(1);
}

// ------------- 界面构建（配色参考 DashboardWidget 深色科技风） -------------
void UserManagementWidget::buildUi()
{
    setObjectName(QStringLiteral("userManagementPage"));

    setStyleSheet(QStringLiteral(
        /* 顶部工具栏容器 */
        "QFrame#userToolbar{background-color:#0f1b2d;border:1px solid #1e2d45;border-radius:8px;}"
        /* 搜索框 */
        "QLineEdit#userSearchEdit{background-color:#0a1424;color:#e6e9ef;"
        "border:1px solid #2a3b55;border-radius:4px;padding:6px 10px;}"
        "QLineEdit#userSearchEdit:focus{border:1px solid #00d4ff;}"
        /* 状态筛选下拉框 */
        "QComboBox#userStatusCombo{background-color:#0a1424;color:#e6e9ef;"
        "border:1px solid #2a3b55;border-radius:4px;padding:6px 12px;}"
        "QComboBox#userStatusCombo:hover{border:1px solid #00d4ff;}"
        "QComboBox#userStatusCombo QAbstractItemView{background-color:#0f1b2d;"
        "color:#e6e9ef;selection-background-color:#1e3a5f;selection-color:#ffffff;}"
        /* 查询/刷新按钮（与 Dashboard 趋势切换按钮同风格） */
        "QPushButton#btnUserQuery,QPushButton#btnUserRefresh{background-color:#1a2740;"
        "color:#b8c2d1;border:1px solid #2a3b55;border-radius:4px;padding:6px 18px;min-width:72px;}"
        "QPushButton#btnUserQuery:hover,QPushButton#btnUserRefresh:hover{border:1px solid #00d4ff;color:#ffffff;}"
        /* 表格 */
        "QTableView#userTable{background-color:#0f1b2d;alternate-background-color:#12203a;"
        "color:#e6e9ef;gridline-color:#1e2d45;border:1px solid #1e2d45;border-radius:8px;"
        "selection-background-color:#1e3a5f;selection-color:#ffffff;}"
        "QTableView#userTable QHeaderView::section{background-color:#162238;color:#8b9bb4;"
        "border:none;border-bottom:1px solid #2a3b55;padding:8px;font-weight:600;}"
        "QTableView#userTable QTableCornerButton::section{background-color:#162238;border:none;}"
        /* 分页按钮 */
        "QPushButton#btnUserPrev,QPushButton#btnUserNext{background-color:#1a2740;"
        "color:#b8c2d1;border:1px solid #2a3b55;border-radius:4px;padding:5px 16px;}"
        "QPushButton#btnUserPrev:hover:enabled,QPushButton#btnUserNext:hover:enabled{border:1px solid #00d4ff;color:#ffffff;}"
        "QPushButton#btnUserPrev:disabled,QPushButton#btnUserNext:disabled{color:#5a6b85;border-color:#1e2d45;}"
        /* 页码信息 */
        "QLabel#userPageLabel{color:#8b9bb4;font-size:13px;}"));

    auto *rootLayout = new QVBoxLayout(this);
    rootLayout->setContentsMargins(20, 16, 20, 12);
    rootLayout->setSpacing(12);

    // ---------------- 顶部工具栏 ----------------
    auto *toolbar = new QFrame(this);
    toolbar->setObjectName(QStringLiteral("userToolbar"));
    auto *toolLayout = new QHBoxLayout(toolbar);
    toolLayout->setContentsMargins(12, 10, 12, 10);
    toolLayout->setSpacing(10);

    m_searchEdit = new QLineEdit(toolbar);
    m_searchEdit->setObjectName(QStringLiteral("userSearchEdit"));
    m_searchEdit->setPlaceholderText(QStringLiteral("按手机号搜索"));
    m_searchEdit->setClearButtonEnabled(true);
    m_searchEdit->setFixedWidth(220);

    m_statusCombo = new QComboBox(toolbar);
    m_statusCombo->setObjectName(QStringLiteral("userStatusCombo"));
    // itemData: -1=全部(不携带 status 参数), 1=正常, 2=冻结
    m_statusCombo->addItem(QStringLiteral("全部"), -1);
    m_statusCombo->addItem(QStringLiteral("正常"), 1);
    m_statusCombo->addItem(QStringLiteral("已冻结"), 2);

    m_btnQuery = new QPushButton(QStringLiteral("查询"), toolbar);
    m_btnQuery->setObjectName(QStringLiteral("btnUserQuery"));
    m_btnRefresh = new QPushButton(QStringLiteral("刷新"), toolbar);
    m_btnRefresh->setObjectName(QStringLiteral("btnUserRefresh"));

    toolLayout->addWidget(m_searchEdit);
    toolLayout->addWidget(m_statusCombo);
    toolLayout->addWidget(m_btnQuery);
    toolLayout->addWidget(m_btnRefresh);
    toolLayout->addStretch(1);
    rootLayout->addWidget(toolbar);

    // ---------------- 中间表格 ----------------
    m_tableView = new QTableView(this);
    m_tableView->setObjectName(QStringLiteral("userTable"));
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
    // 操作列固定宽度，其余列均分拉伸
    m_tableView->horizontalHeader()->setSectionResizeMode(
        UserManagementModel::ActionCol, QHeaderView::Fixed);
    m_tableView->setColumnWidth(UserManagementModel::ActionCol, 120);
    rootLayout->addWidget(m_tableView, 1);

    // ---------------- 底部分页栏 ----------------
    auto *pagerBar = new QWidget(this);
    auto *pagerLayout = new QHBoxLayout(pagerBar);
    pagerLayout->setContentsMargins(0, 0, 0, 0);
    pagerLayout->setSpacing(12);

    m_btnPrev = new QPushButton(QStringLiteral("上一页"), pagerBar);
    m_btnPrev->setObjectName(QStringLiteral("btnUserPrev"));
    m_pageLabel = new QLabel(QStringLiteral("暂无数据"), pagerBar);
    m_pageLabel->setObjectName(QStringLiteral("userPageLabel"));
    m_pageLabel->setAlignment(Qt::AlignCenter);
    m_btnNext = new QPushButton(QStringLiteral("下一页"), pagerBar);
    m_btnNext->setObjectName(QStringLiteral("btnUserNext"));

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
void UserManagementWidget::applyFiltersAndFetch(int page)
{
    const QString phone = m_searchEdit ? m_searchEdit->text().trimmed() : QString();
    const int statusFilter = m_statusCombo
        ? m_statusCombo->currentData().toInt() : -1;
    m_model->fetchUsers(page, m_pageSize, phone, statusFilter);
}

// ------------- 每行最后一列安装"冻结"/"解冻"按钮 -------------
void UserManagementWidget::installActionButtons()
{
    QStandardItemModel *tm = m_model->getModel();
    for (int r = 0; r < tm->rowCount(); ++r) {
        const QModelIndex idx = tm->index(r, UserManagementModel::ActionCol);
        const int userId = idx.data(UserManagementModel::UserIdRole).toInt();
        const int status = idx.data(UserManagementModel::StatusRole).toInt();
        const QString phone = idx.data(UserManagementModel::PhoneRole).toString();

        const bool frozen = (status == 2);
        const int targetStatus = frozen ? 1 : 2; // 状态取反：冻结 <-> 正常

        QPushButton *btn = new QPushButton(frozen ? tr("解冻") : tr("冻结"), m_tableView);
        btn->setCursor(Qt::PointingHandCursor);
        btn->setStyleSheet(frozen ? kUnfreezeBtnStyle : kFreezeBtnStyle);
        // 按值捕获目标操作，避免行号随刷新变化带来的错位
        connect(btn, &QPushButton::clicked, this,
                [this, userId, phone, targetStatus]() {
                    confirmAndSetStatus(userId, phone, targetStatus);
                });
        // 模型行被移除/重建时，视图会自动删除旧按钮，无需手动管理
        m_tableView->setIndexWidget(idx, btn);
    }
}

// ------------- 分页栏刷新 -------------
void UserManagementWidget::updatePager(int total, int page, int pageSize)
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
void UserManagementWidget::onUsersReady(const QJsonArray &users, int total,
                                        int page, int pageSize)
{
    // 若当前页超出总页数（如筛选后数据变少），自动回退到最后一页重新拉取
    int totalPages = (pageSize > 0) ? (total + pageSize - 1) / pageSize : 1;
    if (totalPages < 1) totalPages = 1;
    if (users.isEmpty() && total > 0 && page > totalPages) {
        applyFiltersAndFetch(totalPages);
        return;
    }
    updatePager(total, page, pageSize);
    installActionButtons();
}

// ------------- Model 回调：操作成功 -------------
void UserManagementWidget::onOperationSuccess(const QString &msg)
{
    QMessageBox::information(this, tr("操作成功"), msg);
    // 成功后刷新当前页，保证状态列与操作按钮同步
    applyFiltersAndFetch(m_page);
}

// ------------- Model 回调：错误 -------------
void UserManagementWidget::onErrorOccurred(const QString &errorMsg)
{
    QMessageBox::critical(this, tr("操作失败"), errorMsg);
}

// ------------- 工具栏槽 -------------
void UserManagementWidget::onQueryClicked()
{
    // 搜索重置为第 1 页
    applyFiltersAndFetch(1);
}

void UserManagementWidget::onRefreshClicked()
{
    applyFiltersAndFetch(m_page);
}

void UserManagementWidget::onStatusFilterChanged(int index)
{
    Q_UNUSED(index);
    // 状态筛选变化：重置为第 1 页并自动查询
    applyFiltersAndFetch(1);
}

// ------------- 分页槽 -------------
void UserManagementWidget::onPrevPage()
{
    if (m_page > 1) {
        applyFiltersAndFetch(m_page - 1);
    }
}

void UserManagementWidget::onNextPage()
{
    if (m_page < m_totalPages) {
        applyFiltersAndFetch(m_page + 1);
    }
}

// ------------- 冻结/解冻确认与执行 -------------
void UserManagementWidget::confirmAndSetStatus(int userId, const QString &phone,
                                               int targetStatus)
{
    const bool freeze = (targetStatus == 2);
    const QString actionText = freeze ? tr("冻结") : tr("解冻");
    // 默认操作原因（记录到后台审计日志）
    const QString reason = freeze ? tr("运营后台人工冻结") : tr("运营后台人工解冻");

    const QMessageBox::StandardButton ret = QMessageBox::question(
        this, QStringLiteral("%1用户").arg(actionText),
        QStringLiteral("确认将用户 %1（ID: %2）%3？\n操作原因：%4")
            .arg(phone.isEmpty() ? QStringLiteral("-") : phone,
                 QString::number(userId), actionText, reason),
        QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
    if (ret != QMessageBox::Yes) {
        return;
    }

    m_model->setUserStatus(userId, targetStatus, reason);
}
