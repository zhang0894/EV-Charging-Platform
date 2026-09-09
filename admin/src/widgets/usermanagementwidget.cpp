#include "usermanagementwidget.h"
#include "usermanagementmodel.h"

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
#include <QDoubleValidator>

// 表格内操作按钮样式（查看订单=青 / 调账=蓝 / 冻结=红 / 解冻=绿），与轻量专业风主题一致
static const QString kViewOrdersBtnStyle = QStringLiteral(
    "QPushButton{background-color:#e6f7f8;color:#0e8a94;"
    "border:1px solid #bfe8ec;border-radius:4px;padding:3px 12px;min-width:64px;}"
    "QPushButton:hover{background-color:#cdeff2;color:#0b6e76;border-color:#8fd8de;}");

static const QString kAdjustBtnStyle = QStringLiteral(
    "QPushButton{background-color:#e8f0fe;color:#1a5cff;"
    "border:1px solid #d6e2ff;border-radius:4px;padding:3px 12px;min-width:48px;}"
    "QPushButton:hover{background-color:#d6e2ff;color:#1546b8;border-color:#b8ccff;}");

static const QString kFreezeBtnStyle = QStringLiteral(
    "QPushButton{background-color:#fdecec;color:#dc2626;"
    "border:1px solid #f6c8c8;border-radius:4px;padding:3px 14px;min-width:52px;}"
    "QPushButton:hover{background-color:#fbd8d8;color:#b91c1c;border-color:#ef4444;}");

static const QString kUnfreezeBtnStyle = QStringLiteral(
    "QPushButton{background-color:#e8f9ee;color:#16a34a;"
    "border:1px solid #b7ebc9;border-radius:4px;padding:3px 14px;min-width:52px;}"
    "QPushButton:hover{background-color:#c9f2d7;color:#15803d;border-color:#22c55e;}");

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
    connect(m_model, &UserManagementModel::adjustSuccess,
            this, &UserManagementWidget::onAdjustSuccess);
    connect(m_model, &UserManagementModel::errorOccurred,
            this, &UserManagementWidget::onErrorOccurred);

    // 日志转发：查询/操作事件 + 失败信息统一上抛主窗口日志区
    connect(m_model, &UserManagementModel::logRequested,
            this, &UserManagementWidget::logMessage);
    connect(m_model, &UserManagementModel::errorOccurred, this,
            [this](const QString &msg) {
                emit logMessage(tr("失败：%1").arg(msg));
            });

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

// ------------- 界面构建（轻量专业风：白底 + 浅边框 + 蓝色主色） -------------
void UserManagementWidget::buildUi()
{
    setObjectName(QStringLiteral("userManagementPage"));

    setStyleSheet(QStringLiteral(
        /* 顶部工具栏容器：白底浅边框 */
        "QFrame#userToolbar{background-color:#ffffff;border:1px solid #e8ecf0;border-radius:8px;}"
        /* 搜索框 */
        "QLineEdit#userSearchEdit{background-color:#ffffff;color:#1a2332;"
        "border:1px solid #d9dee5;border-radius:6px;padding:6px 10px;}"
        "QLineEdit#userSearchEdit:focus{border:1px solid #2b7bff;}"
        /* 状态筛选下拉框 */
        "QComboBox#userStatusCombo{background-color:#ffffff;color:#1a2332;"
        "border:1px solid #d9dee5;border-radius:6px;padding:6px 12px;}"
        "QComboBox#userStatusCombo:hover{border:1px solid #2b7bff;}"
        "QComboBox#userStatusCombo QAbstractItemView{background-color:#ffffff;"
        "color:#1a2332;selection-background-color:#e8f0fe;selection-color:#1a5cff;}"
        /* 查询/刷新按钮：白底浅描边，hover 蓝色 */
        "QPushButton#btnUserQuery,QPushButton#btnUserRefresh{background-color:#ffffff;"
        "color:#1a2332;border:1px solid #d9dee5;border-radius:6px;padding:6px 18px;min-width:72px;}"
        "QPushButton#btnUserQuery:hover,QPushButton#btnUserRefresh:hover{border:1px solid #2b7bff;color:#2b7bff;}"
        /* 表格：极简浅色 */
        "QTableView#userTable{background-color:#ffffff;alternate-background-color:#f8fafc;"
        "color:#1a2332;gridline-color:#eef1f5;border:1px solid #e8ecf0;border-radius:8px;"
        "selection-background-color:#e8f0fe;selection-color:#1a5cff;}"
        "QTableView#userTable QHeaderView::section{background-color:#f8fafc;color:#4a5a6e;"
        "border:none;border-bottom:1px solid #e8ecf0;padding:8px;font-weight:600;}"
        "QTableView#userTable QTableCornerButton::section{background-color:#f8fafc;border:none;}"
        /* 分页按钮 */
        "QPushButton#btnUserPrev,QPushButton#btnUserNext{background-color:#ffffff;"
        "color:#1a2332;border:1px solid #d9dee5;border-radius:6px;padding:5px 16px;}"
        "QPushButton#btnUserPrev:hover:enabled,QPushButton#btnUserNext:hover:enabled{border:1px solid #2b7bff;color:#2b7bff;}"
        "QPushButton#btnUserPrev:disabled,QPushButton#btnUserNext:disabled{color:#8a9aa8;border-color:#e8ecf0;}"
        /* 页码信息 */
        "QLabel#userPageLabel{color:#4a5a6e;font-size:13px;}"));

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
    // 操作列固定宽度：三个按钮实际需求约 258px（查看订单 90 + 调账 74 + 冻结 82 + 间距边距），
    // 取 260 保证"查看订单 / 调账 / 冻结"完整显示不重叠，其余列均分拉伸
    m_tableView->horizontalHeader()->setSectionResizeMode(
        UserManagementModel::ActionCol, QHeaderView::Fixed);
    m_tableView->setColumnWidth(UserManagementModel::ActionCol, 260);
    // 余额列固定压缩至 90px 作为代偿（金额列不需要太宽）
    m_tableView->horizontalHeader()->setSectionResizeMode(
        UserManagementModel::BalanceCol, QHeaderView::Fixed);
    m_tableView->setColumnWidth(UserManagementModel::BalanceCol, 90);
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

// ------------- 每行最后一列安装"调账"+"冻结/解冻"按钮 -------------
void UserManagementWidget::installActionButtons()
{
    QStandardItemModel *tm = m_model->getModel();
    for (int r = 0; r < tm->rowCount(); ++r) {
        const QModelIndex idx = tm->index(r, UserManagementModel::ActionCol);
        const int userId = idx.data(UserManagementModel::UserIdRole).toInt();
        const int status = idx.data(UserManagementModel::StatusRole).toInt();
        const QString phone = idx.data(UserManagementModel::PhoneRole).toString();
        const double balance = idx.data(UserManagementModel::BalanceRole).toDouble();

        const bool frozen = (status == 2);
        const int targetStatus = frozen ? 1 : 2; // 状态取反：冻结 <-> 正常

        // 容器：查看订单（每行都有）+ 调账按钮（每行都有）+ 冻结/解冻按钮
        auto *panel = new QWidget(m_tableView);
        auto *lay = new QHBoxLayout(panel);
        lay->setContentsMargins(2, 2, 2, 2);
        lay->setSpacing(4);

        // 跨页入口：跳转订单管理页并自动按该用户筛选（MainWindow 接收）
        auto *btnOrders = new QPushButton(tr("查看订单"), panel);
        btnOrders->setCursor(Qt::PointingHandCursor);
        btnOrders->setStyleSheet(kViewOrdersBtnStyle);
        // 按值捕获目标用户，避免行号随刷新变化带来的错位
        connect(btnOrders, &QPushButton::clicked, this,
                [this, userId, phone]() {
                    emit viewOrdersRequested(userId, phone);
                });
        lay->addWidget(btnOrders);

        auto *btnAdjust = new QPushButton(tr("调账"), panel);
        btnAdjust->setCursor(Qt::PointingHandCursor);
        btnAdjust->setStyleSheet(kAdjustBtnStyle);
        // 按值捕获目标用户与当前余额，避免行号随刷新变化带来的错位
        connect(btnAdjust, &QPushButton::clicked, this,
                [this, userId, phone, balance]() {
                    showAdjustDialog(userId, phone, balance);
                });
        lay->addWidget(btnAdjust);

        QPushButton *btnStatus = new QPushButton(frozen ? tr("解冻") : tr("冻结"), panel);
        btnStatus->setCursor(Qt::PointingHandCursor);
        btnStatus->setStyleSheet(frozen ? kUnfreezeBtnStyle : kFreezeBtnStyle);
        connect(btnStatus, &QPushButton::clicked, this,
                [this, userId, phone, targetStatus]() {
                    confirmAndSetStatus(userId, phone, targetStatus);
                });
        lay->addWidget(btnStatus);

        lay->addStretch(1);
        panel->setLayout(lay);
        // 模型行被移除/重建时，视图会自动删除旧按钮，无需手动管理
        m_tableView->setIndexWidget(idx, panel);
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

// ------------- Model 回调：调账成功 -------------
void UserManagementWidget::onAdjustSuccess(const QString &msg)
{
    QMessageBox::information(this, tr("调账成功"), msg);
    // 成功后刷新当前页，余额列同步更新
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

// ------------- 手动调账 / 余额补偿弹窗（文档 3.5.3） -------------
void UserManagementWidget::showAdjustDialog(int userId, const QString &phone,
                                            double balance)
{
    QDialog dialog(this);
    dialog.setWindowTitle(tr("手动调账 / 余额补偿"));
    dialog.setMinimumWidth(380);

    auto *form = new QFormLayout(&dialog);
    form->setSpacing(10);
    form->setContentsMargins(18, 16, 18, 12);

    // 用户信息行（只读参考）
    auto *infoLabel = new QLabel(
        QStringLiteral("用户：%1（ID: %2）\n当前余额：%3 元")
            .arg(phone.isEmpty() ? QStringLiteral("-") : phone,
                 QString::number(userId), QString::number(balance, 'f', 2)),
        &dialog);
    infoLabel->setStyleSheet(QStringLiteral("color:#4a5a6e;"));
    form->addRow(infoLabel);

    // 调整金额：支持负数（扣减），最多两位小数
    auto *amountEdit = new QLineEdit(&dialog);
    amountEdit->setPlaceholderText(tr("正数=充值补偿，负数=扣减，不可为 0"));
    auto *amountValidator = new QDoubleValidator(-1000000.0, 1000000.0, 2, amountEdit);
    amountValidator->setNotation(QDoubleValidator::StandardNotation);
    amountEdit->setValidator(amountValidator);
    form->addRow(tr("调整金额(元):"), amountEdit);

    // 调账备注（审计流水必填）
    auto *remarkEdit = new QLineEdit(&dialog);
    remarkEdit->setPlaceholderText(tr("请输入调账原因，将记入审计流水"));
    remarkEdit->setMaxLength(100);
    form->addRow(tr("调账备注:"), remarkEdit);

    auto *btnBox = new QDialogButtonBox(
        QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
    btnBox->button(QDialogButtonBox::Ok)->setText(tr("确认调账"));
    btnBox->button(QDialogButtonBox::Cancel)->setText(tr("取消"));
    connect(btnBox, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect(btnBox, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    form->addRow(btnBox);

    if (dialog.exec() != QDialog::Accepted) {
        return;
    }

    // 校验：金额合法且非 0
    const QString amountText = amountEdit->text().trimmed();
    const double amount = amountText.toDouble();
    if (amountText.isEmpty() || amount == 0.0) {
        QMessageBox::warning(this, tr("输入有误"), tr("调整金额不能为 0，请重新输入"));
        return;
    }
    // 校验：备注必填（写入审计流水）
    const QString remark = remarkEdit->text().trimmed();
    if (remark.isEmpty()) {
        QMessageBox::warning(this, tr("输入有误"), tr("请填写调账备注"));
        return;
    }
    // 前置余额校验：扣减后不可为负（服务端同样校验，此处提前拦截）
    if (amount < 0 && balance + amount < 0) {
        QMessageBox::warning(this, tr("输入有误"),
                             tr("扣减后余额将为负数（当前余额 %1 元），操作被拒绝")
                                 .arg(QString::number(balance, 'f', 2)));
        return;
    }

    // 二次确认（敏感资金操作）
    const QString actionText = amount > 0 ? tr("充值补偿") : tr("扣减");
    const QMessageBox::StandardButton ret = QMessageBox::question(
        this, tr("确认调账"),
        QStringLiteral("确认对用户 %1（ID: %2）执行%3 %4 元？\n调账备注：%5\n\n"
                       "该操作将立即生效并记入资金流水，请谨慎确认。")
            .arg(phone.isEmpty() ? QStringLiteral("-") : phone,
                 QString::number(userId), actionText,
                 QString::number(amount, 'f', 2), remark),
        QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
    if (ret != QMessageBox::Yes) {
        return;
    }

    m_model->adjustUserWallet(userId, amount, remark);
}

// ------------- 白天/夜晚主题切换 -------------
void UserManagementWidget::applyTheme(bool dark)
{
    if (!dark) {
        setStyleSheet(QStringLiteral(
            "QFrame#userToolbar{background-color:#ffffff;border:1px solid #e8ecf0;border-radius:8px;}"
            "QLineEdit#userSearchEdit{background-color:#ffffff;color:#1a2332;"
            "border:1px solid #d9dee5;border-radius:6px;padding:6px 10px;}"
            "QLineEdit#userSearchEdit:focus{border:1px solid #2b7bff;}"
            "QComboBox#userStatusCombo{background-color:#ffffff;color:#1a2332;"
            "border:1px solid #d9dee5;border-radius:6px;padding:6px 12px;}"
            "QComboBox#userStatusCombo:hover{border:1px solid #2b7bff;}"
            "QComboBox#userStatusCombo QAbstractItemView{background-color:#ffffff;"
            "color:#1a2332;selection-background-color:#e8f0fe;selection-color:#1a5cff;}"
            "QPushButton#btnUserQuery,QPushButton#btnUserRefresh{background-color:#ffffff;"
            "color:#1a2332;border:1px solid #d9dee5;border-radius:6px;padding:6px 18px;min-width:72px;}"
            "QPushButton#btnUserQuery:hover,QPushButton#btnUserRefresh:hover{border:1px solid #2b7bff;color:#2b7bff;}"
            "QTableView#userTable{background-color:#ffffff;alternate-background-color:#f8fafc;"
            "color:#1a2332;gridline-color:#eef1f5;border:1px solid #e8ecf0;border-radius:8px;"
            "selection-background-color:#e8f0fe;selection-color:#1a5cff;}"
            "QTableView#userTable QHeaderView::section{background-color:#f8fafc;color:#4a5a6e;"
            "border:none;border-bottom:1px solid #e8ecf0;padding:8px;font-weight:600;}"
            "QTableView#userTable QTableCornerButton::section{background-color:#f8fafc;border:none;}"
            "QPushButton#btnUserPrev,QPushButton#btnUserNext{background-color:#ffffff;"
            "color:#1a2332;border:1px solid #d9dee5;border-radius:6px;padding:5px 16px;}"
            "QPushButton#btnUserPrev:hover:enabled,QPushButton#btnUserNext:hover:enabled{border:1px solid #2b7bff;color:#2b7bff;}"
            "QPushButton#btnUserPrev:disabled,QPushButton#btnUserNext:disabled{color:#8a9aa8;border-color:#e8ecf0;}"
            "QLabel#userPageLabel{color:#4a5a6e;font-size:13px;}"));
    } else {
        setStyleSheet(QStringLiteral(
            "QFrame#userToolbar{background-color:#252a33;border:1px solid #3a4050;border-radius:8px;}"
            "QLineEdit#userSearchEdit{background-color:#2a3040;color:#e8ecf0;"
            "border:1px solid #3a4050;border-radius:6px;padding:6px 10px;}"
            "QLineEdit#userSearchEdit:focus{border:1px solid #2b7bff;}"
            "QComboBox#userStatusCombo{background-color:#2a3040;color:#e8ecf0;"
            "border:1px solid #3a4050;border-radius:6px;padding:6px 12px;}"
            "QComboBox#userStatusCombo:hover{border:1px solid #2b7bff;}"
            "QComboBox#userStatusCombo QAbstractItemView{background-color:#2a3040;"
            "color:#e8ecf0;selection-background-color:#1e3a5f;selection-color:#4d9bff;}"
            "QPushButton#btnUserQuery,QPushButton#btnUserRefresh{background-color:#2a3040;"
            "color:#e8ecf0;border:1px solid #3a4050;border-radius:6px;padding:6px 18px;min-width:72px;}"
            "QPushButton#btnUserQuery:hover,QPushButton#btnUserRefresh:hover{border:1px solid #2b7bff;color:#4d9bff;}"
            "QTableView#userTable{background-color:#252a33;alternate-background-color:#2a2f38;"
            "color:#e8ecf0;gridline-color:#3a4050;border:1px solid #3a4050;border-radius:8px;"
            "selection-background-color:#1e3a5f;selection-color:#4d9bff;}"
            "QTableView#userTable QHeaderView::section{background-color:#2a2f38;color:#a0a8b8;"
            "border:none;border-bottom:1px solid #3a4050;padding:8px;font-weight:600;}"
            "QTableView#userTable QTableCornerButton::section{background-color:#2a2f38;border:none;}"
            "QPushButton#btnUserPrev,QPushButton#btnUserNext{background-color:#2a3040;"
            "color:#e8ecf0;border:1px solid #3a4050;border-radius:6px;padding:5px 16px;}"
            "QPushButton#btnUserPrev:hover:enabled,QPushButton#btnUserNext:hover:enabled{border:1px solid #2b7bff;color:#4d9bff;}"
            "QPushButton#btnUserPrev:disabled,QPushButton#btnUserNext:disabled{color:#7a8290;border-color:#3a4050;}"
            "QLabel#userPageLabel{color:#a0a8b8;font-size:13px;}"));
    }
}
