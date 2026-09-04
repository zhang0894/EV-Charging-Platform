#include "logindialog.h"

#include <QLineEdit>
#include <QPushButton>
#include <QLabel>
#include <QFormLayout>
#include <QVBoxLayout>
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
    , m_accountEdit(new QLineEdit(this))
    , m_passwordEdit(new QLineEdit(this))
    , m_loginButton(new QPushButton(QStringLiteral("登录"), this))
    , m_errorLabel(new QLabel(this))
    , m_networkManager(new QNetworkAccessManager(this))
{
    setWindowTitle(QStringLiteral("管理员登录 - 充电桩运营平台"));
    setFixedSize(380, 200);

    // 预填默认账号（仅为输入便利，校验完全由服务器决定）
    m_accountEdit->setText(QStringLiteral("admin"));
    m_accountEdit->setPlaceholderText(QStringLiteral("请输入账号"));

    m_passwordEdit->setText(QStringLiteral("123456"));
    m_passwordEdit->setEchoMode(QLineEdit::Password);
    m_passwordEdit->setPlaceholderText(QStringLiteral("请输入密码"));

    m_loginButton->setObjectName(QStringLiteral("btnLogin"));
    m_loginButton->setCursor(Qt::PointingHandCursor);

    m_errorLabel->setStyleSheet(QStringLiteral("color: #ff6b6b; font-size: 12px;"));
    m_errorLabel->setAlignment(Qt::AlignCenter);
    m_errorLabel->hide();

    auto *form = new QFormLayout;
    form->addRow(QStringLiteral("账号"), m_accountEdit);
    form->addRow(QStringLiteral("密码"), m_passwordEdit);

    auto *layout = new QVBoxLayout(this);
    layout->addLayout(form);
    layout->addWidget(m_errorLabel);
    layout->addWidget(m_loginButton);

    // 回车触发登录
    connect(m_loginButton, &QPushButton::clicked, this, &LoginDialog::onLoginClicked);
    connect(m_accountEdit, &QLineEdit::returnPressed, this, &LoginDialog::onLoginClicked);
    connect(m_passwordEdit, &QLineEdit::returnPressed, this, &LoginDialog::onLoginClicked);
}

void LoginDialog::onLoginClicked()
{
    const QString account = m_accountEdit->text().trimmed();
    const QString password = m_passwordEdit->text();

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
    m_accountEdit->setEnabled(false);
    m_passwordEdit->setEnabled(false);
    m_errorLabel->hide();

    QNetworkReply *reply = m_networkManager->post(request, postData);
    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        // 恢复输入
        m_loginButton->setEnabled(true);
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
