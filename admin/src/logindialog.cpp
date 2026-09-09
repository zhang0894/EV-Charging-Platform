#include "logindialog.h"

#include <QLineEdit>
#include <QPushButton>
#include <QLabel>
#include <QCheckBox>
#include <QFrame>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGridLayout>
#include <QGraphicsDropShadowEffect>
#include <QAction>
#include <QIcon>
#include <QPixmap>
#include <QSettings>
#include <QMessageBox>
#include <QNetworkAccessManager>
#include <QNetworkRequest>
#include <QNetworkReply>
#include <QJsonDocument>
#include <QJsonObject>

// 服务器地址（与 DashboardModel 保持一致）
static const QString kLoginUrl =
    QStringLiteral("http://62.234.84.145:8080/api/v1/admin/auth/login");

LoginDialog::LoginDialog(QWidget *parent)
    : QDialog(parent)
    , m_networkManager(new QNetworkAccessManager(this))
{
    setWindowTitle(QStringLiteral("管理员登录 - 充电桩运营平台"));
    setFixedSize(880, 540);

    buildUi();
    applyStyle();

    // 回车触发登录
    connect(m_loginButton, &QPushButton::clicked, this, &LoginDialog::onLoginClicked);
    connect(m_accountEdit, &QLineEdit::returnPressed, this, &LoginDialog::onLoginClicked);
    connect(m_passwordEdit, &QLineEdit::returnPressed, this, &LoginDialog::onLoginClicked);
    connect(m_togglePwdAction, &QAction::triggered, this, &LoginDialog::onTogglePassword);
    connect(m_forgotLink, &QLabel::linkActivated, this, &LoginDialog::onForgotPassword);
    // 账号输入变化时，匹配已记住的密码自动填充
    // 注意：必须在 loadRemembered() 之前连接，否则 setText 触发的 textChanged 不会被处理
    connect(m_accountEdit, &QLineEdit::textChanged,
            this, &LoginDialog::onAccountChanged);

    // 加载已记住的账号密码（内部 setText 会触发 onAccountChanged 自动填密码）
    loadRemembered();
}

QString LoginDialog::account() const
{
    return m_accountEdit ? m_accountEdit->text() : QString();
}

void LoginDialog::buildUi()
{
    // ===== 根布局：左右分栏 =====
    auto *root = new QHBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(0);

    // ===== 左侧品牌区（40%） =====
    m_brandPanel = new QFrame(this);
    m_brandPanel->setObjectName(QStringLiteral("brandPanel"));
    m_brandPanel->setFixedWidth(360);

    auto *brandLayout = new QVBoxLayout(m_brandPanel);
    brandLayout->setContentsMargins(48, 56, 48, 56);
    brandLayout->setSpacing(0);

    // 平台 Logo
    m_logoLabel = new QLabel(m_brandPanel);
    m_logoLabel->setObjectName(QStringLiteral("logoLabel"));
    QPixmap logo(QStringLiteral(":/img/app-logo.svg"));
    m_logoLabel->setPixmap(logo.scaled(96, 96, Qt::KeepAspectRatio,
                                       Qt::SmoothTransformation));
    m_logoLabel->setAlignment(Qt::AlignLeft);

    // 平台名称
    m_brandTitle = new QLabel(QStringLiteral("充电桩运营\n管理平台"), m_brandPanel);
    m_brandTitle->setObjectName(QStringLiteral("brandTitle"));
    m_brandTitle->setWordWrap(true);

    // Slogan
    m_brandSlogan = new QLabel(QStringLiteral("智能充电 · 高效运营\n让每一度电都创造价值"),
                               m_brandPanel);
    m_brandSlogan->setObjectName(QStringLiteral("brandSlogan"));
    m_brandSlogan->setWordWrap(true);

    brandLayout->addWidget(m_logoLabel);
    brandLayout->addSpacing(32);
    brandLayout->addWidget(m_brandTitle);
    brandLayout->addSpacing(16);
    brandLayout->addWidget(m_brandSlogan);
    brandLayout->addStretch();

    // ===== 右侧登录卡片（60%） =====
    auto *rightArea = new QWidget(this);
    auto *rightLayout = new QVBoxLayout(rightArea);
    rightLayout->setContentsMargins(40, 40, 40, 40);

    m_card = new QFrame(rightArea);
    m_card->setObjectName(QStringLiteral("loginCard"));

    // 柔和阴影
    auto *shadow = new QGraphicsDropShadowEffect(m_card);
    shadow->setBlurRadius(48);
    shadow->setColor(QColor(22, 119, 255, 28));
    shadow->setOffset(0, 12);
    m_card->setGraphicsEffect(shadow);

    auto *cardLayout = new QVBoxLayout(m_card);
    cardLayout->setContentsMargins(56, 48, 56, 40);
    cardLayout->setSpacing(0);

    // 卡片标题
    m_cardTitle = new QLabel(QStringLiteral("欢迎登录"), m_card);
    m_cardTitle->setObjectName(QStringLiteral("cardTitle"));

    m_cardSubtitle = new QLabel(QStringLiteral("请输入您的管理员账号和密码"), m_card);
    m_cardSubtitle->setObjectName(QStringLiteral("cardSubtitle"));

    // 账号输入框（带用户图标）
    m_accountEdit = new QLineEdit(m_card);
    m_accountEdit->setObjectName(QStringLiteral("accountEdit"));
    m_accountEdit->setPlaceholderText(QStringLiteral("请输入账号"));
    m_accountEdit->setClearButtonEnabled(true);
    m_accountEdit->addAction(QIcon(QStringLiteral(":/img/user.svg")),
                             QLineEdit::LeadingPosition);

    // 密码输入框（带锁图标 + 显示/隐藏切换）
    m_passwordEdit = new QLineEdit(m_card);
    m_passwordEdit->setObjectName(QStringLiteral("passwordEdit"));
    m_passwordEdit->setPlaceholderText(QStringLiteral("请输入密码"));
    m_passwordEdit->setEchoMode(QLineEdit::Password);
    m_passwordEdit->addAction(QIcon(QStringLiteral(":/img/lock.svg")),
                              QLineEdit::LeadingPosition);
    m_togglePwdAction = m_passwordEdit->addAction(
        QIcon(QStringLiteral(":/img/eye-off.svg")), QLineEdit::TrailingPosition);

    // 记住密码 + 忘记密码
    auto *optionRow = new QHBoxLayout;
    optionRow->setContentsMargins(0, 0, 0, 0);

    m_rememberCheck = new QCheckBox(QStringLiteral("记住密码"), m_card);
    m_rememberCheck->setObjectName(QStringLiteral("rememberCheck"));

    m_forgotLink = new QLabel(
        QStringLiteral("<a href=\"#\" style=\"color:#1677FF; text-decoration:none;\">忘记密码？</a>"),
        m_card);
    m_forgotLink->setObjectName(QStringLiteral("forgotLink"));
    m_forgotLink->setTextInteractionFlags(Qt::TextBrowserInteraction);
    m_forgotLink->setOpenExternalLinks(false);

    optionRow->addWidget(m_rememberCheck);
    optionRow->addStretch();
    optionRow->addWidget(m_forgotLink);

    // 登录按钮
    m_loginButton = new QPushButton(QStringLiteral("登 录"), m_card);
    m_loginButton->setObjectName(QStringLiteral("btnLogin"));
    m_loginButton->setCursor(Qt::PointingHandCursor);
    m_loginButton->setMinimumHeight(44);

    // 错误提示
    m_errorLabel = new QLabel(m_card);
    m_errorLabel->setObjectName(QStringLiteral("errorLabel"));
    m_errorLabel->setAlignment(Qt::AlignCenter);
    m_errorLabel->hide();

    // 底部版权
    m_copyrightLabel = new QLabel(
        QStringLiteral("© 2026  第九组出品  ·  All Rights Reserved"), m_card);
    m_copyrightLabel->setObjectName(QStringLiteral("copyrightLabel"));
    m_copyrightLabel->setAlignment(Qt::AlignCenter);

    cardLayout->addWidget(m_cardTitle);
    cardLayout->addSpacing(8);
    cardLayout->addWidget(m_cardSubtitle);
    cardLayout->addSpacing(32);
    cardLayout->addWidget(m_accountEdit);
    cardLayout->addSpacing(16);
    cardLayout->addWidget(m_passwordEdit);
    cardLayout->addSpacing(16);
    cardLayout->addLayout(optionRow);
    cardLayout->addSpacing(24);
    cardLayout->addWidget(m_loginButton);
    cardLayout->addSpacing(12);
    cardLayout->addWidget(m_errorLabel);
    cardLayout->addStretch();
    cardLayout->addWidget(m_copyrightLabel);

    rightLayout->addWidget(m_card);

    root->addWidget(m_brandPanel);
    root->addWidget(rightArea, 1);
}

void LoginDialog::applyStyle()
{
    const QString style = QStringLiteral(R"(
        QDialog {
            background-color: #f0f2f5;
        }

        /* ===== 左侧品牌区：蓝绿渐变 ===== */
        #brandPanel {
            background-color: qlineargradient(x1:0, y1:0, x2:1, y2:1,
                               stop:0 #1677FF, stop:0.5 #1677FF, stop:1 #13c2c2);
            border-top-left-radius: 12px;
            border-bottom-left-radius: 12px;
        }
        #brandTitle {
            color: #ffffff;
            font-size: 28px;
            font-weight: 700;
            line-height: 1.3;
            background-color: transparent;
        }
        #brandSlogan {
            color: rgba(255, 255, 255, 0.82);
            font-size: 14px;
            line-height: 1.6;
            background-color: transparent;
        }
        #logoLabel {
            background-color: transparent;
        }

        /* ===== 右侧登录卡片 ===== */
        #loginCard {
            background-color: #ffffff;
            border-radius: 16px;
        }
        #cardTitle {
            color: #1a2332;
            font-size: 24px;
            font-weight: 700;
            background-color: transparent;
        }
        #cardSubtitle {
            color: #8c98a8;
            font-size: 13px;
            background-color: transparent;
        }

        /* ===== 输入框：聚焦变主题色 + 轻微阴影 ===== */
        #accountEdit, #passwordEdit {
            background-color: #f7f9fc;
            border: 1.5px solid #e4e8ed;
            border-radius: 8px;
            padding: 11px 12px 11px 12px;
            font-size: 14px;
            color: #1a2332;
        }
        #accountEdit:focus, #passwordEdit:focus {
            border: 1.5px solid #1677FF;
            background-color: #ffffff;
        }
        #accountEdit:hover, #passwordEdit:hover {
            border: 1.5px solid #1677FF;
        }

        /* ===== 记住密码复选框 ===== */
        #rememberCheck {
            color: #5a6475;
            font-size: 13px;
            spacing: 6px;
        }
        #rememberCheck::indicator {
            width: 16px;
            height: 16px;
            border-radius: 4px;
            border: 1.5px solid #c4cad4;
            background-color: #ffffff;
        }
        #rememberCheck::indicator:checked {
            background-color: #1677FF;
            border: 1.5px solid #1677FF;
            image: url(:/img/check.svg);
        }

        /* ===== 忘记密码链接 ===== */
        #forgotLink {
            background-color: transparent;
            font-size: 13px;
        }

        /* ===== 登录按钮：渐变蓝 + hover 上浮 ===== */
        #btnLogin {
            background-color: qlineargradient(x1:0, y1:0, x2:1, y2:0,
                               stop:0 #1677FF, stop:1 #4096FF);
            color: #ffffff;
            border: none;
            border-radius: 8px;
            font-size: 15px;
            font-weight: 600;
            letter-spacing: 2px;
        }
        #btnLogin:hover {
            background-color: qlineargradient(x1:0, y1:0, x2:1, y2:0,
                               stop:0 #4096FF, stop:1 #69b1ff);
        }
        #btnLogin:pressed {
            background-color: qlineargradient(x1:0, y1:0, x2:1, y2:0,
                               stop:0 #0958d9, stop:1 #1677FF);
        }
        #btnLogin:disabled {
            background-color: #bfbfbf;
            color: rgba(255,255,255,0.85);
        }

        /* ===== 错误提示 ===== */
        #errorLabel {
            color: #ef4444;
            font-size: 12px;
            background-color: transparent;
        }

        /* ===== 版权信息 ===== */
        #copyrightLabel {
            color: #b8c2d1;
            font-size: 11px;
            background-color: transparent;
        }
    )");
    setStyleSheet(style);
}

void LoginDialog::loadRemembered()
{
    QSettings settings(QStringLiteral("ChargingPileAdmin"), QStringLiteral("Login"));

    // 加载所有已记住的 账号->密码 映射
    m_rememberedAccounts.clear();
    settings.beginGroup(QStringLiteral("accounts"));
    const QStringList accounts = settings.childKeys();
    for (const QString &acc : accounts) {
        m_rememberedAccounts.insert(acc, settings.value(acc).toString());
    }
    settings.endGroup();

    // 启动时账号密码框清空，不预填任何默认值
    m_accountEdit->clear();
    m_passwordEdit->clear();
    m_rememberCheck->setChecked(false);

    // 如果有记住过的账号，把上次登录的账号填入账号框；
    // setText 会触发 onAccountChanged，自动匹配并填充对应密码。
    const QString lastAccount = settings.value(QStringLiteral("lastAccount")).toString();
    if (!lastAccount.isEmpty() && m_rememberedAccounts.contains(lastAccount)) {
        m_accountEdit->setText(lastAccount);
    }
}

void LoginDialog::onAccountChanged(const QString &text)
{
    // 账号变化时：若该账号已记住过密码，自动填充密码并勾选"记住密码"；
    // 否则清空密码并取消勾选，避免残留旧密码。
    auto it = m_rememberedAccounts.constFind(text);
    if (it != m_rememberedAccounts.constEnd()) {
        m_passwordEdit->setText(it.value());
        m_rememberCheck->setChecked(true);
    } else {
        m_passwordEdit->clear();
        m_rememberCheck->setChecked(false);
    }
}

void LoginDialog::onTogglePassword()
{
    m_passwordVisible = !m_passwordVisible;
    m_passwordEdit->setEchoMode(m_passwordVisible
                                    ? QLineEdit::Normal
                                    : QLineEdit::Password);
    m_togglePwdAction->setIcon(QIcon(m_passwordVisible
                                         ? QStringLiteral(":/img/eye.svg")
                                         : QStringLiteral(":/img/eye-off.svg")));
}

void LoginDialog::onForgotPassword()
{
    QMessageBox::information(this, QStringLiteral("忘记密码"),
        QStringLiteral("请联系系统管理员重置密码。\n管理员将为您生成新的临时密码。"));
}

void LoginDialog::onLoginClicked()
{
    const QString account = m_accountEdit->text().trimmed();
    const QString password = m_passwordEdit->text();

    if (account.isEmpty() || password.isEmpty()) {
        m_errorLabel->setText(QStringLiteral("请输入账号和密码"));
        m_errorLabel->show();
        return;
    }

    // 构造请求体（对应文档 3.1 节）
    QJsonObject body;
    body["account"] = account;
    body["password"] = password;
    const QByteArray postData = QJsonDocument(body).toJson(QJsonDocument::Compact);

    QNetworkRequest request(kLoginUrl);
    request.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
    request.setRawHeader("Accept", "application/json");

    // 请求期间禁用输入
    m_loginButton->setEnabled(false);
    m_loginButton->setText(QStringLiteral("登录中..."));
    m_accountEdit->setEnabled(false);
    m_passwordEdit->setEnabled(false);
    m_errorLabel->hide();

    QNetworkReply *reply = m_networkManager->post(request, postData);
    connect(reply, &QNetworkReply::finished, this, [this, reply, account, password]() {
        // 恢复输入
        m_loginButton->setEnabled(true);
        m_loginButton->setText(QStringLiteral("登 录"));
        m_accountEdit->setEnabled(true);
        m_passwordEdit->setEnabled(true);

        const QNetworkReply::NetworkError netError = reply->error();
        const int httpStatus = reply->attribute(
            QNetworkRequest::HttpStatusCodeAttribute).toInt();
        const QByteArray responseBody = reply->readAll();
        reply->deleteLater();

        // 尝试解析 JSON 响应（统一信封 {code, msg, data, timestamp}）
        QJsonParseError parseError;
        const QJsonDocument doc = QJsonDocument::fromJson(responseBody, &parseError);
        const bool jsonOk =
            (parseError.error == QJsonParseError::NoError && doc.isObject());

        if (jsonOk) {
            const QJsonObject root = doc.object();
            const int code = root.value("code").toInt(-1);
            const QString accessToken =
                root.value("data").toObject().value("access_token").toString();

            // 登录成功：code == 0 且包含 access_token
            if (code == 0 && !accessToken.isEmpty()) {
                m_token = accessToken;
                m_refreshToken = root.value("data").toObject()
                                     .value("refresh_token").toString();

                // 记住密码：根据复选框状态保存/清除该账号的密码
                QSettings settings(QStringLiteral("ChargingPileAdmin"),
                                   QStringLiteral("Login"));
                settings.beginGroup(QStringLiteral("accounts"));
                if (m_rememberCheck->isChecked()) {
                    settings.setValue(account, password);
                } else {
                    settings.remove(account);
                }
                settings.endGroup();
                // 记录上次登录的账号，下次启动自动回填
                settings.setValue(QStringLiteral("lastAccount"), account);

                accept();
                return;
            }
            // code != 0 或缺少 token → 登录失败
            m_errorLabel->setText(QStringLiteral("账号或密码错误"));
        } else if (netError != QNetworkReply::NoError && httpStatus == 0) {
            // 网络层失败（服务器不可达 / 超时）
            m_errorLabel->setText(QStringLiteral("无法连接服务器，请检查网络"));
        } else {
            // 收到了 HTTP 响应但非 JSON（如 401 直接返回）
            m_errorLabel->setText(QStringLiteral("账号或密码错误"));
        }

        m_errorLabel->show();
        m_passwordEdit->setFocus();
        m_passwordEdit->selectAll();
    });
}
