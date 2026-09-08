#include "ordermanagementwidget.h"
#include "ordermanagementmodel.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QFormLayout>
#include <QFrame>
#include <QLineEdit>
#include <QComboBox>
#include <QDateEdit>
#include <QPushButton>
#include <QDoubleSpinBox>
#include <QTableView>
#include <QHeaderView>
#include <QLabel>
#include <QMessageBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QStandardItemModel>
#include <QRegularExpression>
#include <QRegularExpressionValidator>
#include <QDateTime>

// 表格内退款按钮样式（浅红底红字，警示敏感操作），与轻量专业风主题一致
static const QString kRefundBtnStyle = QStringLiteral(
    "QPushButton{background-color:#fee2e2;color:#dc2626;"
    "border:1px solid #fecaca;border-radius:4px;padding:3px 10px;min-width:44px;}"
    "QPushButton:hover{background-color:#fecaca;color:#b91c1c;border-color:#f87171;}");

// 表格内"详情"按钮样式（浅蓝底蓝字，普通查看操作）
static const QString kDetailBtnStyle = QStringLiteral(
    "QPushButton{background-color:#e8f0fe;color:#1a5cff;"
    "border:1px solid #d6e2ff;border-radius:4px;padding:3px 10px;min-width:44px;}"
    "QPushButton:hover{background-color:#d6e2ff;color:#1546b8;border-color:#b8ccff;}");

// 详情弹窗内"退款"按钮（复用退款警示色）
static const QString kDetailRefundBtnStyle = QStringLiteral(
    "QPushButton{background-color:#fee2e2;color:#dc2626;"
    "border:1px solid #fecaca;border-radius:6px;padding:6px 22px;min-width:88px;}"
    "QPushButton:hover{background-color:#fecaca;color:#b91c1c;border-color:#f87171;}");

// 详情弹窗内"关闭"按钮（浅色主按钮）
static const QString kDetailCloseBtnStyle = QStringLiteral(
    "QPushButton{background-color:#2b7bff;color:#ffffff;"
    "border:1px solid #2b7bff;border-radius:6px;padding:6px 22px;min-width:88px;}"
    "QPushButton:hover{background-color:#1a5cff;border-color:#1a5cff;}");

OrderManagementWidget::OrderManagementWidget(QWidget *parent)
    : QWidget(parent)
    , m_model(new OrderManagementModel(this))
{
    buildUi();

    // Model 信号 -> 界面刷新 / 错误提示
    connect(m_model, &OrderManagementModel::ordersReady,
            this, &OrderManagementWidget::onOrdersReady);
    connect(m_model, &OrderManagementModel::orderDetailReady,
            this, &OrderManagementWidget::onOrderDetailReady);
    connect(m_model, &OrderManagementModel::refundSuccess,
            this, &OrderManagementWidget::onRefundSuccess);
    connect(m_model, &OrderManagementModel::errorOccurred,
            this, &OrderManagementWidget::onErrorOccurred);

    // 工具栏交互
    connect(m_btnQuery, &QPushButton::clicked, this, &OrderManagementWidget::onQueryClicked);
    connect(m_btnRefresh, &QPushButton::clicked, this, &OrderManagementWidget::onRefreshClicked);
    connect(m_stationEdit, &QLineEdit::returnPressed, this, &OrderManagementWidget::onQueryClicked);
    connect(m_statusCombo, &QComboBox::currentIndexChanged,
            this, &OrderManagementWidget::onStatusFilterChanged);
    // 按用户查询（第二期）
    connect(m_btnUserQuery, &QPushButton::clicked, this, &OrderManagementWidget::onUserQueryClicked);
    connect(m_phoneEdit, &QLineEdit::returnPressed, this, &OrderManagementWidget::onUserQueryClicked);

    // 分页交互
    connect(m_btnPrev, &QPushButton::clicked, this, &OrderManagementWidget::onPrevPage);
    connect(m_btnNext, &QPushButton::clicked, this, &OrderManagementWidget::onNextPage);
}

void OrderManagementWidget::setAuthToken(const QString &token)
{
    m_model->setAuthToken(token);
    // 登录成功后首次拉取第 1 页（默认条件：全部状态 / 不限站点 / 不限日期）
    applyFiltersAndFetch(1);
}

// ------------- 界面构建（轻量专业风：白底 + 浅边框 + 蓝色主色） -------------
void OrderManagementWidget::buildUi()
{
    setObjectName(QStringLiteral("orderManagementPage"));

    setStyleSheet(QStringLiteral(
        /* 顶部工具栏容器：白底浅边框 */
        "QFrame#orderToolbar{background-color:#ffffff;border:1px solid #e8ecf0;border-radius:8px;}"
        /* 输入框：站ID / 用户ID / 手机号 */
        "QLineEdit#orderStationEdit,QLineEdit#orderUserIdEdit,QLineEdit#orderPhoneEdit{background-color:#ffffff;color:#1a2332;"
        "border:1px solid #d9dee5;border-radius:6px;padding:6px 10px;}"
        "QLineEdit#orderStationEdit:focus,QLineEdit#orderUserIdEdit:focus,QLineEdit#orderPhoneEdit:focus{border:1px solid #2b7bff;}"
        /* 状态筛选下拉框 */
        "QComboBox#orderStatusCombo{background-color:#ffffff;color:#1a2332;"
        "border:1px solid #d9dee5;border-radius:6px;padding:6px 12px;}"
        "QComboBox#orderStatusCombo:hover{border:1px solid #2b7bff;}"
        "QComboBox#orderStatusCombo QAbstractItemView{background-color:#ffffff;"
        "color:#1a2332;selection-background-color:#e8f0fe;selection-color:#1a5cff;}"
        /* 日期范围选择 */
        "QDateEdit#orderStartDate,QDateEdit#orderEndDate{background-color:#ffffff;"
        "color:#1a2332;border:1px solid #d9dee5;border-radius:6px;padding:6px 8px;}"
        "QDateEdit#orderStartDate:hover,QDateEdit#orderEndDate:hover{border:1px solid #2b7bff;}"
        /* 查询/刷新/查用户订单按钮：白底浅描边，hover 蓝色 */
        "QPushButton#btnOrderQuery,QPushButton#btnOrderRefresh,QPushButton#btnUserOrderQuery{background-color:#ffffff;"
        "color:#1a2332;border:1px solid #d9dee5;border-radius:6px;padding:6px 18px;min-width:72px;}"
        "QPushButton#btnOrderQuery:hover,QPushButton#btnOrderRefresh:hover,QPushButton#btnUserOrderQuery:hover{border:1px solid #2b7bff;color:#2b7bff;}"
        /* 表格：极简浅色 */
        "QTableView#orderTable{background-color:#ffffff;alternate-background-color:#f8fafc;"
        "color:#1a2332;gridline-color:#eef1f5;border:1px solid #e8ecf0;border-radius:8px;"
        "selection-background-color:#e8f0fe;selection-color:#1a5cff;}"
        "QTableView#orderTable QHeaderView::section{background-color:#f8fafc;color:#4a5a6e;"
        "border:none;border-bottom:1px solid #e8ecf0;padding:8px;font-weight:600;}"
        "QTableView#orderTable QTableCornerButton::section{background-color:#f8fafc;border:none;}"
        /* 分页按钮 */
        "QPushButton#btnOrderPrev,QPushButton#btnOrderNext{background-color:#ffffff;"
        "color:#1a2332;border:1px solid #d9dee5;border-radius:6px;padding:5px 16px;}"
        "QPushButton#btnOrderPrev:hover:enabled,QPushButton#btnOrderNext:hover:enabled{border:1px solid #2b7bff;color:#2b7bff;}"
        "QPushButton#btnOrderPrev:disabled,QPushButton#btnOrderNext:disabled{color:#8a9aa8;border-color:#e8ecf0;}"
        /* 页码信息 */
        "QLabel#orderPageLabel{color:#4a5a6e;font-size:13px;}"));

    auto *rootLayout = new QVBoxLayout(this);
    rootLayout->setContentsMargins(20, 16, 20, 12);
    rootLayout->setSpacing(12);

    // ---------------- 顶部工具栏 ----------------
    auto *toolbar = new QFrame(this);
    toolbar->setObjectName(QStringLiteral("orderToolbar"));
    auto *toolLayout = new QHBoxLayout(toolbar);
    toolLayout->setContentsMargins(12, 10, 12, 10);
    toolLayout->setSpacing(10);

    m_statusCombo = new QComboBox(toolbar);
    m_statusCombo->setObjectName(QStringLiteral("orderStatusCombo"));
    // itemData: ""=全部(不携带 order_status 参数)，其余为文档 3.6.1 状态枚举
    m_statusCombo->addItem(QStringLiteral("全部"), QString());
    m_statusCombo->addItem(QStringLiteral("充电中"), QStringLiteral("CHARGING"));
    m_statusCombo->addItem(QStringLiteral("待结算"), QStringLiteral("UNSETTLED"));
    m_statusCombo->addItem(QStringLiteral("已完成"), QStringLiteral("COMPLETED"));
    m_statusCombo->addItem(QStringLiteral("已退款"), QStringLiteral("REFUNDED"));

    m_stationEdit = new QLineEdit(toolbar);
    m_stationEdit->setObjectName(QStringLiteral("orderStationEdit"));
    m_stationEdit->setPlaceholderText(QStringLiteral("按站ID筛选"));
    m_stationEdit->setClearButtonEnabled(true);
    m_stationEdit->setFixedWidth(120);
    // 仅允许输入数字（站点 ID 为整数）
    m_stationEdit->setValidator(new QRegularExpressionValidator(
        QRegularExpression(QStringLiteral("\\d*")), m_stationEdit));

    auto *dateStartLabel = new QLabel(QStringLiteral("开始:"), toolbar);
    m_startDateEdit = new QDateEdit(toolbar);
    m_startDateEdit->setObjectName(QStringLiteral("orderStartDate"));
    m_startDateEdit->setCalendarPopup(true);
    m_startDateEdit->setDisplayFormat(QStringLiteral("yyyy-MM-dd"));
    // 最小日期 + 特殊文案实现"不限"语义：等于最小日期时不携带 start_date 参数
    m_startDateEdit->setMinimumDate(QDate(2000, 1, 1));
    m_startDateEdit->setSpecialValueText(QStringLiteral("不限"));
    m_startDateEdit->setDate(QDate(2000, 1, 1));
    m_startDateEdit->setFixedWidth(130);

    auto *dateEndLabel = new QLabel(QStringLiteral("结束:"), toolbar);
    m_endDateEdit = new QDateEdit(toolbar);
    m_endDateEdit->setObjectName(QStringLiteral("orderEndDate"));
    m_endDateEdit->setCalendarPopup(true);
    m_endDateEdit->setDisplayFormat(QStringLiteral("yyyy-MM-dd"));
    m_endDateEdit->setMinimumDate(QDate(2000, 1, 1));
    m_endDateEdit->setSpecialValueText(QStringLiteral("不限"));
    m_endDateEdit->setDate(QDate(2000, 1, 1));
    m_endDateEdit->setFixedWidth(130);

    m_btnQuery = new QPushButton(QStringLiteral("查询"), toolbar);
    m_btnQuery->setObjectName(QStringLiteral("btnOrderQuery"));
    m_btnRefresh = new QPushButton(QStringLiteral("刷新"), toolbar);
    m_btnRefresh->setObjectName(QStringLiteral("btnOrderRefresh"));

    // 按用户查询区（第二期）：用户ID / 手机号 / 查用户订单按钮
    m_userIdEdit = new QLineEdit(toolbar);
    m_userIdEdit->setObjectName(QStringLiteral("orderUserIdEdit"));
    m_userIdEdit->setPlaceholderText(QStringLiteral("按用户ID查询"));
    m_userIdEdit->setClearButtonEnabled(true);
    m_userIdEdit->setFixedWidth(130);
    m_userIdEdit->setValidator(new QRegularExpressionValidator(
        QRegularExpression(QStringLiteral("\\d{0,12}")), m_userIdEdit));

    m_phoneEdit = new QLineEdit(toolbar);
    m_phoneEdit->setObjectName(QStringLiteral("orderPhoneEdit"));
    m_phoneEdit->setPlaceholderText(QStringLiteral("按手机号查询（11位）"));
    m_phoneEdit->setClearButtonEnabled(true);
    m_phoneEdit->setFixedWidth(160);
    m_phoneEdit->setMaxLength(11);
    m_phoneEdit->setValidator(new QRegularExpressionValidator(
        QRegularExpression(QStringLiteral("\\d{0,11}")), m_phoneEdit));

    m_btnUserQuery = new QPushButton(QStringLiteral("查用户订单"), toolbar);
    m_btnUserQuery->setObjectName(QStringLiteral("btnUserOrderQuery"));
    m_btnUserQuery->setToolTip(
        QStringLiteral("按用户ID或手机号查询该用户的历史订单，两项至少填写一项"));

    // 细分隔线：区分左侧"全局筛选"与右侧"用户查询"
    auto *toolbarSep = new QFrame(toolbar);
    toolbarSep->setFrameShape(QFrame::VLine);
    toolbarSep->setFrameShadow(QFrame::Sunken);
    toolbarSep->setStyleSheet(QStringLiteral("color:#e8ecf0;"));

    toolLayout->addWidget(m_statusCombo);
    toolLayout->addWidget(m_stationEdit);
    toolLayout->addWidget(dateStartLabel);
    toolLayout->addWidget(m_startDateEdit);
    toolLayout->addWidget(dateEndLabel);
    toolLayout->addWidget(m_endDateEdit);
    toolLayout->addWidget(m_btnQuery);
    toolLayout->addWidget(m_btnRefresh);
    toolLayout->addWidget(toolbarSep);
    toolLayout->addWidget(m_userIdEdit);
    toolLayout->addWidget(m_phoneEdit);
    toolLayout->addWidget(m_btnUserQuery);
    toolLayout->addStretch(1);
    rootLayout->addWidget(toolbar);

    // ---------------- 中间表格 ----------------
    m_tableView = new QTableView(this);
    m_tableView->setObjectName(QStringLiteral("orderTable"));
    m_tableView->setModel(m_model->getModel());
    m_tableView->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_tableView->setSelectionMode(QAbstractItemView::SingleSelection);
    m_tableView->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_tableView->setAlternatingRowColors(true);
    m_tableView->setWordWrap(false);
    m_tableView->verticalHeader()->setVisible(false);
    m_tableView->verticalHeader()->setDefaultSectionSize(44);
    m_tableView->horizontalHeader()->setHighlightSections(false);
    m_tableView->horizontalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);
    // 操作列固定宽度（详情 + 退款两个按钮并排）
    m_tableView->horizontalHeader()->setSectionResizeMode(
        OrderManagementModel::ActionCol, QHeaderView::Fixed);
    m_tableView->setColumnWidth(OrderManagementModel::ActionCol, 170);
    m_tableView->horizontalHeader()->setStretchLastSection(false);
    rootLayout->addWidget(m_tableView, 1);

    // ---------------- 底部分页栏 ----------------
    auto *pagerBar = new QWidget(this);
    auto *pagerLayout = new QHBoxLayout(pagerBar);
    pagerLayout->setContentsMargins(0, 0, 0, 0);
    pagerLayout->setSpacing(12);

    m_btnPrev = new QPushButton(QStringLiteral("上一页"), pagerBar);
    m_btnPrev->setObjectName(QStringLiteral("btnOrderPrev"));
    m_pageLabel = new QLabel(QStringLiteral("暂无数据"), pagerBar);
    m_pageLabel->setObjectName(QStringLiteral("orderPageLabel"));
    m_pageLabel->setAlignment(Qt::AlignCenter);
    m_btnNext = new QPushButton(QStringLiteral("下一页"), pagerBar);
    m_btnNext->setObjectName(QStringLiteral("btnOrderNext"));

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
void OrderManagementWidget::applyFiltersAndFetch(int page)
{
    const QString status = m_statusCombo
        ? m_statusCombo->currentData().toString() : QString();

    // 站ID：空串或非法输入视为"全部"（-1）
    int stationId = -1;
    if (m_stationEdit) {
        const QString sidText = m_stationEdit->text().trimmed();
        if (!sidText.isEmpty()) {
            bool ok = false;
            const int parsed = sidText.toInt(&ok);
            if (ok && parsed > 0) {
                stationId = parsed;
            }
        }
    }

    // 日期等于最小值（显示"不限"）时不携带参数
    const QString startDate =
        (m_startDateEdit && m_startDateEdit->date() > m_startDateEdit->minimumDate())
            ? m_startDateEdit->date().toString(QStringLiteral("yyyy-MM-dd"))
            : QString();
    const QString endDate =
        (m_endDateEdit && m_endDateEdit->date() > m_endDateEdit->minimumDate())
            ? m_endDateEdit->date().toString(QStringLiteral("yyyy-MM-dd"))
            : QString();

    m_model->fetchOrders(page, m_pageSize, stationId, status, startDate, endDate);
}

// ------------- 按用户查询（第二期，接口 3.6.2） -------------
void OrderManagementWidget::applyUserQueryAndFetch(int page)
{
    m_model->fetchOrdersByUser(m_curUserId, m_curPhone, page, m_pageSize);
}

// ------------- 按当前查询模式分发翻页/刷新请求 -------------
void OrderManagementWidget::dispatchCurrentQuery(int page)
{
    if (m_queryMode == QueryMode::User) {
        applyUserQueryAndFetch(page);
    } else {
        applyFiltersAndFetch(page);
    }
}

// ------------- "已完成"订单安装"退款"按钮，所有行安装"详情"按钮 -------------
void OrderManagementWidget::installActionButtons()
{
    QStandardItemModel *tm = m_model->getModel();
    for (int r = 0; r < tm->rowCount(); ++r) {
        const QModelIndex idx = tm->index(r, OrderManagementModel::ActionCol);
        const QString orderId = idx.data(OrderManagementModel::OrderIdRole).toString();
        const QString status = idx.data(OrderManagementModel::StatusRole).toString();
        const double totalFee = idx.data(OrderManagementModel::TotalFeeRole).toDouble();

        // 容器：详情按钮（每行都有）+ 退款按钮（仅"已完成"订单）
        auto *panel = new QWidget(m_tableView);
        auto *lay = new QHBoxLayout(panel);
        lay->setContentsMargins(2, 2, 2, 2);
        lay->setSpacing(4);

        auto *btnDetail = new QPushButton(tr("详情"), panel);
        btnDetail->setCursor(Qt::PointingHandCursor);
        btnDetail->setStyleSheet(kDetailBtnStyle);
        // 按值捕获订单号，避免行号随刷新变化带来的错位
        connect(btnDetail, &QPushButton::clicked, this,
                [this, orderId]() { showOrderDetail(orderId); });
        lay->addWidget(btnDetail);

        // 仅"已完成"订单可退款（文档 3.6.3：服务端校验状态必须为 COMPLETED）
        if (status == QLatin1String("COMPLETED")) {
            auto *btnRefund = new QPushButton(tr("退款"), panel);
            btnRefund->setCursor(Qt::PointingHandCursor);
            btnRefund->setStyleSheet(kRefundBtnStyle);
            connect(btnRefund, &QPushButton::clicked, this,
                    [this, orderId, totalFee]() { showRefundDialog(orderId, totalFee); });
            lay->addWidget(btnRefund);
        }
        lay->addStretch(1);
        panel->setLayout(lay);
        m_tableView->setIndexWidget(idx, panel);
    }
}

// ------------- 分页栏刷新 -------------
void OrderManagementWidget::updatePager(int total, int page, int pageSize)
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
void OrderManagementWidget::onOrdersReady(const QJsonArray &orders, int total,
                                          int page, int pageSize)
{
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

    // 手机号精确查询结果为空（含 404 未命中置空的场景）：分页栏给出明确提示。
    // 互斥约定下 m_curPhone 非空即代表本次为手机号查询。
    if (total == 0 && m_queryMode == QueryMode::User && !m_curPhone.isEmpty()) {
        m_pageLabel->setText(tr("未找到该用户的订单"));
    }
}

// ------------- Model 回调：退款成功 -------------
void OrderManagementWidget::onRefundSuccess(const QString &msg)
{
    QMessageBox::information(this, tr("退款成功"), msg);
    // 成功后按当前模式刷新当前页：订单状态变为"已退款"，退款按钮同步消失
    dispatchCurrentQuery(m_page);
}

// ------------- Model 回调：错误 -------------
void OrderManagementWidget::onErrorOccurred(const QString &errorMsg)
{
    m_detailBusy = false; // 详情请求失败时允许再次发起
    QMessageBox::critical(this, tr("操作失败"), errorMsg);
}

// ------------- 工具栏槽 -------------
void OrderManagementWidget::onQueryClicked()
{
    // 全局列表查询：切回全局模式，重置为第 1 页
    m_queryMode = QueryMode::Global;
    applyFiltersAndFetch(1);
}

// ------------- 外部跳转入口：按用户筛选订单（跨页联动） -------------
void OrderManagementWidget::setFilterByUser(qint64 userId, const QString &phone)
{
    // 优先使用 user_id，phone 作为备选（与工具栏"互斥填一项"的约定一致）
    if (userId > 0) {
        m_userIdEdit->setText(QString::number(userId));
        m_phoneEdit->clear();
    } else if (!phone.trimmed().isEmpty()) {
        m_phoneEdit->setText(phone.trimmed());
        m_userIdEdit->clear();
    } else {
        return; // 无有效条件，不发起查询
    }
    // 复用"查用户订单"的校验与查询逻辑（自动切到用户模式并重置第 1 页）
    onUserQueryClicked();
}

void OrderManagementWidget::onUserQueryClicked()
{
    // 按用户查询（第二期）：用户ID 与手机号互斥，只能填其中一项
    const QString uidText = m_userIdEdit->text().trimmed();
    const QString phone = m_phoneEdit->text().trimmed();

    if (uidText.isEmpty() && phone.isEmpty()) {
        QMessageBox::information(this, tr("提示"), tr("请输入用户ID或手机号"));
        return;
    }
    if (!uidText.isEmpty() && !phone.isEmpty()) {
        QMessageBox::information(this, tr("提示"),
                                 tr("用户ID与手机号只能填写其中一项"));
        return;
    }
    // 后端 phone 参数仅支持完整 11 位手机号精确匹配，不符合格式时不发请求
    if (!phone.isEmpty() && phone.length() != 11) {
        QMessageBox::warning(this, tr("格式错误"),
                             tr("请输入完整的 11 位手机号"));
        m_phoneEdit->setFocus();
        m_phoneEdit->selectAll();
        return;
    }

    // 记录当前用户查询上下文（互斥：只保留填写的一项），翻页/刷新沿用同一参数
    m_curUserId = uidText.isEmpty() ? 0 : uidText.toLongLong();
    m_curPhone = uidText.isEmpty() ? phone : QString();
    m_queryMode = QueryMode::User;
    applyUserQueryAndFetch(1); // 用户查询重置为第 1 页
}

void OrderManagementWidget::onRefreshClicked()
{
    dispatchCurrentQuery(m_page);
}

void OrderManagementWidget::onStatusFilterChanged(int index)
{
    Q_UNUSED(index);
    // 状态筛选变化：重置为第 1 页并自动查询
    m_queryMode = QueryMode::Global;
    applyFiltersAndFetch(1);
}

void OrderManagementWidget::onPrevPage()
{
    if (m_page > 1) {
        dispatchCurrentQuery(m_page - 1);
    }
}

void OrderManagementWidget::onNextPage()
{
    if (m_page < m_totalPages) {
        dispatchCurrentQuery(m_page + 1);
    }
}

// ------------- 退款弹窗 -------------
void OrderManagementWidget::showRefundDialog(const QString &orderId, double totalFee)
{
    QDialog dialog(this);
    dialog.setWindowTitle(QStringLiteral("订单退款"));
    dialog.setMinimumWidth(380);

    auto *form = new QFormLayout(&dialog);
    form->setSpacing(10);

    auto *orderIdLabel = new QLabel(orderId, &dialog);
    orderIdLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
    auto *totalLabel = new QLabel(
        QStringLiteral("%1 元").arg(QString::number(totalFee, 'f', 2)), &dialog);

    auto *amountSpin = new QDoubleSpinBox(&dialog);
    amountSpin->setRange(0.01, qMax(0.01, totalFee)); // 上限为订单实付金额（服务端同时校验）
    amountSpin->setDecimals(2);
    amountSpin->setSingleStep(0.01);
    amountSpin->setValue(qMax(0.01, totalFee)); // 默认全额退款
    amountSpin->setSuffix(QStringLiteral(" 元"));

    auto *reasonEdit = new QLineEdit(&dialog);
    reasonEdit->setPlaceholderText(QStringLiteral("请输入退款原因（必填，写入审计流水）"));
    reasonEdit->setMinimumWidth(240);

    form->addRow(QStringLiteral("订单号:"), orderIdLabel);
    form->addRow(QStringLiteral("订单总额:"), totalLabel);
    form->addRow(QStringLiteral("退款金额:"), amountSpin);
    form->addRow(QStringLiteral("退款原因:"), reasonEdit);

    auto *buttons = new QDialogButtonBox(
        QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
    buttons->button(QDialogButtonBox::Ok)->setText(QStringLiteral("确认退款"));
    buttons->button(QDialogButtonBox::Cancel)->setText(QStringLiteral("取消"));
    connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    form->addRow(buttons);

    // 输入校验 + 二次确认循环：原因必填，未通过则重新弹出表单
    QString reason;
    double amount = 0.0;
    bool confirmed = false;
    while (dialog.exec() == QDialog::Accepted) {
        reason = reasonEdit->text().trimmed();
        if (reason.isEmpty()) {
            QMessageBox::warning(this, tr("缺少退款原因"),
                                 tr("退款原因不能为空，请填写后再确认。"));
            continue;
        }
        amount = amountSpin->value();

        const QMessageBox::StandardButton answer = QMessageBox::question(
            this, tr("确认退款"),
            tr("确认对订单 %1 执行退款吗？\n退款金额: %2 元\n退款将原路返还至用户钱包，且不可撤销。")
                .arg(orderId, QString::number(amount, 'f', 2)),
            QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
        if (answer == QMessageBox::Yes) {
            confirmed = true;
        }
        break;
    }
    if (!confirmed) {
        return;
    }

    m_model->refundOrder(orderId, amount, reason);
}

// ------------- 订单详情弹窗（第二期，接口 3.6.4） -------------
void OrderManagementWidget::showOrderDetail(const QString &orderId)
{
    // 详情请求进行中则忽略重复点击（防止双击导致两次弹窗）
    if (m_detailBusy) {
        return;
    }
    m_detailBusy = true;
    m_model->fetchOrderDetail(orderId);
}

void OrderManagementWidget::onOrderDetailReady(const QJsonObject &data)
{
    m_detailBusy = false;

    QDialog *dialog = buildDetailDialog(data);
    const int result = dialog->exec();
    const bool refundRequested = (result == QDialog::Accepted);
    const QString orderId = data.value(QStringLiteral("order_id")).toString();
    const double totalAmount = data.value(QStringLiteral("total_amount")).toDouble();
    delete dialog;

    // 已完成订单可在详情弹窗内发起退款（复用既有退款弹窗与流程）
    if (refundRequested) {
        showRefundDialog(orderId, totalAmount);
    }
}

QDialog *OrderManagementWidget::buildDetailDialog(const QJsonObject &data)
{
    auto *dialog = new QDialog(this);
    dialog->setWindowTitle(QStringLiteral("订单详情"));
    dialog->setMinimumWidth(440);

    auto *form = new QFormLayout(dialog);
    form->setSpacing(8);
    form->setContentsMargins(18, 16, 18, 14);

    // 可选中复制的只读文本标签
    auto makeValueLabel = [dialog](const QString &text) {
        auto *label = new QLabel(text, dialog);
        label->setTextInteractionFlags(Qt::TextSelectableByMouse);
        label->setWordWrap(true);
        return label;
    };
    auto fmtMs = [](qint64 ms) {
        return ms > 0
            ? QDateTime::fromMSecsSinceEpoch(ms).toString(QStringLiteral("yyyy-MM-dd hh:mm:ss"))
            : QStringLiteral("-");
    };
    auto fmtMoney = [](double v) { return QStringLiteral("%1 元").arg(QString::number(v, 'f', 2)); };

    const QString orderId = data.value(QStringLiteral("order_id")).toString();
    const QString status = data.value(QStringLiteral("order_status")).toString();
    const QJsonValue uidVal = data.value(QStringLiteral("user_id"));
    const QString userIdText = (uidVal.isNull() || uidVal.isUndefined())
        ? QStringLiteral("-") : QString::number(uidVal.toVariant().toLongLong());
    const QString phone = data.value(QStringLiteral("user_phone")).toString();
    const double energy = data.value(QStringLiteral("charged_energy_kwh")).toDouble();

    form->addRow(QStringLiteral("订单ID:"), makeValueLabel(orderId));
    form->addRow(QStringLiteral("用户ID:"), makeValueLabel(userIdText));
    form->addRow(QStringLiteral("手机号:"), makeValueLabel(phone.isEmpty() ? QStringLiteral("-") : phone));
    form->addRow(QStringLiteral("充电站:"), makeValueLabel(
        data.value(QStringLiteral("station_name")).toString(QStringLiteral("-"))));

    const QString pileId = data.value(QStringLiteral("pile_id")).toString();
    const QString pileType = OrderManagementModel::pileTypeText(
        data.value(QStringLiteral("pile_type")).toString());
    form->addRow(QStringLiteral("充电桩:"), makeValueLabel(
        pileType.isEmpty() ? pileId
                           : QStringLiteral("%1（%2）").arg(pileId, pileType)));

    form->addRow(QStringLiteral("充电量(kWh):"), makeValueLabel(QString::number(energy, 'f', 2)));
    form->addRow(QStringLiteral("电费:"), makeValueLabel(fmtMoney(
        data.value(QStringLiteral("electricity_fee")).toDouble())));
    form->addRow(QStringLiteral("服务费:"), makeValueLabel(fmtMoney(
        data.value(QStringLiteral("service_fee")).toDouble())));
    form->addRow(QStringLiteral("超时费:"), makeValueLabel(fmtMoney(
        data.value(QStringLiteral("overtime_fee")).toDouble())));

    auto *totalLabel = makeValueLabel(fmtMoney(
        data.value(QStringLiteral("total_amount")).toDouble()));
    QFont boldFont = totalLabel->font();
    boldFont.setBold(true);
    totalLabel->setFont(boldFont);
    form->addRow(QStringLiteral("总金额:"), totalLabel);

    // 状态：富文本着色展示
    auto *statusLabel = new QLabel(dialog);
    statusLabel->setTextFormat(Qt::RichText);
    statusLabel->setText(QStringLiteral("<span style=\"color:%1;font-weight:600;\">%2</span>")
                             .arg(OrderManagementModel::orderStatusColor(status).name(),
                                  OrderManagementModel::orderStatusText(status)));
    form->addRow(QStringLiteral("状态:"), statusLabel);

    form->addRow(QStringLiteral("开始时间:"), makeValueLabel(fmtMs(
        static_cast<qint64>(data.value(QStringLiteral("start_time")).toDouble(0)))));
    form->addRow(QStringLiteral("结束时间:"), makeValueLabel(fmtMs(
        static_cast<qint64>(data.value(QStringLiteral("end_time")).toDouble(0)))));

    // 退款信息（文档 3.6.4：已退款订单展示退款单号/金额/原因/操作人）。
    // 实测当前服务端对已退款订单可能不返回退款明细块，缺失时显示提示行。
    static const struct {
        const char *title;
        std::initializer_list<const char *> keys;
    } refundFields[] = {
        { "退款单号:", { "refund_transaction_id", "refund_id", "refund_no" } },
        { "退款金额:", { "refund_amount", "refund_amount_cents" } },
        { "退款原因:", { "refund_reason", "reason" } },
        { "操作人:",   { "refund_operator", "refund_operator_id", "operator_id" } },
    };

    QStringList refundRows;
    for (const auto &field : refundFields) {
        QString value;
        for (const char *key : field.keys) {
            const QString keyStr = QString::fromLatin1(key);
            const QJsonValue v = data.value(keyStr);
            if (v.isNull() || v.isUndefined()) {
                continue;
            }
            if (!keyStr.contains(QStringLiteral("cents"))) {
                value = v.isDouble()
                    ? QString::number(v.toDouble(), 'f', 2) // 金额字段保留两位小数
                    : v.toVariant().toString();
            } else {
                // *_cents 分单位字段：转换为元展示
                value = QStringLiteral("%1 元").arg(
                    QString::number(v.toDouble() / 100.0, 'f', 2));
            }
            break;
        }
        if (!value.isEmpty()) {
            refundRows << QStringLiteral("%1 %2")
                              .arg(QString::fromLatin1(field.title), value);
        }
    }

    if (!refundRows.isEmpty()) {
        auto *sep = new QFrame(dialog);
        sep->setFrameShape(QFrame::HLine);
        sep->setFrameShadow(QFrame::Sunken);
        sep->setStyleSheet(QStringLiteral("color:#e8ecf0;"));
        form->addRow(sep);
        auto *refundTitle = new QLabel(QStringLiteral("<b>退款信息</b>"), dialog);
        form->addRow(QString(), refundTitle);
        for (const QString &row : refundRows) {
            form->addRow(QString(), makeValueLabel(row));
        }
    } else if (status == QLatin1String("REFUNDED")) {
        auto *sep = new QFrame(dialog);
        sep->setFrameShape(QFrame::HLine);
        sep->setFrameShadow(QFrame::Sunken);
        sep->setStyleSheet(QStringLiteral("color:#e8ecf0;"));
        form->addRow(sep);
        auto *note = new QLabel(QStringLiteral("该订单已退款（服务端未返回退款明细）"), dialog);
        note->setStyleSheet(QStringLiteral("color:#8a9aa8;"));
        form->addRow(QString(), note);
    }

    // 底部按钮：已完成订单可退款，所有状态可关闭
    auto *btnRow = new QHBoxLayout();
    btnRow->setSpacing(10);
    if (status == QLatin1String("COMPLETED")) {
        auto *btnRefund = new QPushButton(QStringLiteral("退款"), dialog);
        btnRefund->setStyleSheet(kDetailRefundBtnStyle);
        btnRefund->setCursor(Qt::PointingHandCursor);
        connect(btnRefund, &QPushButton::clicked, dialog, &QDialog::accept);
        btnRow->addWidget(btnRefund);
    }
    auto *btnClose = new QPushButton(QStringLiteral("关闭"), dialog);
    btnClose->setStyleSheet(kDetailCloseBtnStyle);
    btnClose->setCursor(Qt::PointingHandCursor);
    connect(btnClose, &QPushButton::clicked, dialog, &QDialog::reject);
    btnRow->addWidget(btnClose);
    btnRow->addStretch(1);
    form->addRow(btnRow);

    return dialog;
}
