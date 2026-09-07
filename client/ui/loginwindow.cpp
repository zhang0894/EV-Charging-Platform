#include "ui/loginwindow.h"
#include "core/userservice.h"
#include "core/session.h"
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QRandomGenerator>
#include <QRegularExpression>
#include <QSettings>
#include <QTimer>
#include <QVBoxLayout>

LoginWindow::LoginWindow(QWidget *p) : QWidget(p)
{
    setObjectName("LoginWindow"); setWindowTitle(QStringLiteral("登录")); setFixedSize(420, 760);
    auto *layout = new QVBoxLayout(this); layout->setContentsMargins(34, 110, 34, 56); layout->setSpacing(14);
    auto *brand = new QLabel(QStringLiteral("NCS 充电桩")); brand->setObjectName("Brand"); brand->setAlignment(Qt::AlignCenter); layout->addWidget(brand);
    auto *title = new QLabel(QStringLiteral("手机号登录")); title->setObjectName("H1"); layout->addWidget(title);
    phone = new QLineEdit; phone->setPlaceholderText(QStringLiteral("手机号（1 开头的 11 位数字）")); phone->setMaxLength(11); layout->addWidget(phone);
    auto *row = new QHBoxLayout; code = new QLineEdit; code->setPlaceholderText(QStringLiteral("验证码")); send = new QPushButton(QStringLiteral("获取验证码")); send->setObjectName("Ghost"); row->addWidget(code, 1); row->addWidget(send); layout->addLayout(row);
    password = new QLineEdit; password->setPlaceholderText(QStringLiteral("密码（至少6位，含大小写字母、数字、特殊字符）")); password->setEchoMode(QLineEdit::Password); password->hide(); layout->addWidget(password);
    confirmPassword = new QLineEdit; confirmPassword->setPlaceholderText(QStringLiteral("再次输入密码")); confirmPassword->setEchoMode(QLineEdit::Password); confirmPassword->hide(); layout->addWidget(confirmPassword);
    submit = new QPushButton(QStringLiteral("继续")); submit->setObjectName("PrimaryButton"); submit->setMinimumHeight(46); layout->addWidget(submit);
    modeSwitch = new QPushButton(QStringLiteral("使用账号密码登录")); modeSwitch->setObjectName("Ghost"); modeSwitch->hide(); layout->addWidget(modeSwitch);
    // 首屏只展示手机号输入和“继续”；查询手机号后再展开登录/注册控件。
    code->hide();
    send->hide();
    tip = new QLabel(QStringLiteral("请输入手机号后获取验证码")); tip->setObjectName("Cap"); tip->setWordWrap(true); layout->addWidget(tip); layout->addStretch();
    timer = new QTimer(this);
    connect(send, &QPushButton::clicked, this, &LoginWindow::sendCode);
    connect(submit, &QPushButton::clicked, this, &LoginWindow::login);
    connect(modeSwitch, &QPushButton::clicked, this, &LoginWindow::toggleLoginMode);
    connect(timer, &QTimer::timeout, this, [this] { if (--left <= 0) { timer->stop(); send->setEnabled(true); send->setText(QStringLiteral("获取验证码")); } else send->setText(QStringLiteral("%1秒后重试").arg(left)); });
}

void LoginWindow::continueWithPhone()
{
    const QString phoneText = phone->text().trimmed();
    if (!QRegularExpression(QStringLiteral("^1\\d{10}$")).match(phoneText).hasMatch()) {
        tip->setText(QStringLiteral("手机号格式错误，请输入 1 开头的 11 位数字")); return;
    }
    submit->setEnabled(false); tip->setText(QStringLiteral("正在查询账号状态…"));
    UserService::checkPhoneAsync(phoneText, [this, phoneText](bool ok, bool registered, const QString &error) {
        submit->setEnabled(true);
        if (!ok) { tip->setText(error); return; }
        if (!registered) {
            // 未注册：进入注册方式选择状态，手机号保留在当前输入框。
            phoneOnly = false; passwordMode = false; registering = true;
            code->setVisible(true); send->setVisible(true); password->setVisible(true); confirmPassword->setVisible(true);
            // 未注册用户进入统一注册页：手机号、验证码、密码、确认密码、确认注册。
            modeSwitch->setVisible(false); submit->setText(QStringLiteral("确认注册"));
            tip->setText(QStringLiteral("请输入验证码并设置密码完成注册"));
            return;
        }
        // 优先恢复本机保存的会话：Access Token 有效则直接进入主界面。
        QSettings settings; settings.beginGroup(QStringLiteral("sessions/%1").arg(phoneText));
        Session::i().setUserId(settings.value(QStringLiteral("user_id")).toInt());
        Session::i().setToken(settings.value(QStringLiteral("access_token")).toString());
        Session::i().setRefreshToken(settings.value(QStringLiteral("refresh_token")).toString()); settings.endGroup();
        if (!Session::i().token().isEmpty()) {
            UserService::profileAsync([this, phoneText](bool valid, const UserProfile &, const QString &) {
                if (valid) { hide(); emit loginSucceeded(false); return; }
                UserService::refreshTokenAsync(Session::i().refreshToken(), [this](bool refreshed, const CloudUser &, const QString &refreshError) {
                    if (refreshed) { hide(); emit loginSucceeded(false); }
                    else { tip->setText(QStringLiteral("登录凭证已失效，请选择验证码或密码登录")); phoneOnly = false; passwordMode = false; registering = false; code->show(); send->show(); submit->setText(QStringLiteral("登录")); modeSwitch->show(); }
                });
            });
            return;
        }
        phoneOnly = false; passwordMode = false; registering = false; code->show(); send->show(); submit->setText(QStringLiteral("登录")); modeSwitch->show(); tip->setText(QStringLiteral("账号已注册，请获取验证码登录，或切换为密码登录"));
    });
}

void LoginWindow::toggleLoginMode()
{
    passwordMode = !passwordMode;
    issuedCode.clear();
    if (timer->isActive()) timer->stop();
    send->setEnabled(true); send->setText(QStringLiteral("获取验证码"));
    code->setVisible(!passwordMode);
    send->setVisible(!passwordMode);
    password->setVisible(passwordMode);
    confirmPassword->setVisible(registering && passwordMode);
    submit->setText(registering ? (passwordMode ? QStringLiteral("账号密码注册") : QStringLiteral("验证码注册")) : QStringLiteral("登录"));
    modeSwitch->setText(passwordMode ? (registering ? QStringLiteral("使用验证码注册") : QStringLiteral("使用验证码登录")) : (registering ? QStringLiteral("使用账号密码注册") : QStringLiteral("使用账号密码登录")));
    tip->setText(passwordMode ? (registering ? QStringLiteral("请输入密码完成注册") : QStringLiteral("请输入手机号和登录密码")) : QStringLiteral("请输入手机号后获取验证码"));
}

void LoginWindow::sendCode()
{
    // 发送验证码前必须校验：首位为 1，且总共 11 位数字。校验失败不生成验证码。
    if (!QRegularExpression(QStringLiteral("^1\\d{10}$")).match(phone->text().trimmed()).hasMatch()) {
        tip->setText(QStringLiteral("手机号格式错误，请输入 1 开头的 11 位数字"));
        return;
    }
    issuedCode = QStringLiteral("%1").arg(QRandomGenerator::global()->bounded(1000000), 6, 10, QLatin1Char('0'));
    left = 60; send->setEnabled(false); send->setText(QStringLiteral("60秒后重试")); timer->start(1000);
    tip->setText(QStringLiteral("验证码已发送（模拟短信）：%1").arg(issuedCode));
}

void LoginWindow::showRegistration()
{
    registering = true; password->show(); confirmPassword->show(); submit->setText(QStringLiteral("注册并登录"));
    tip->setText(QStringLiteral("该手机号尚未注册，请设置密码。完成后可继续设置昵称与头像。"));
}

void LoginWindow::login()
{
    if (phoneOnly) { continueWithPhone(); return; }
    const QString phoneText = phone->text().trimmed();
    if (passwordMode && !registering) {
        if (!QRegularExpression(QStringLiteral("^1\\d{10}$")).match(phoneText).hasMatch()) {
            tip->setText(QStringLiteral("手机号格式错误，请输入 1 开头的 11 位数字")); return;
        }
        submit->setEnabled(false); tip->setText(QStringLiteral("正在登录…"));
        UserService::passwordLoginAsync(phoneText, password->text(), [this](bool ok, const CloudUser &user, const QString &error) {
            submit->setEnabled(true);
            if (!ok) { tip->setText(error); return; }
            hide(); emit loginSucceeded(user.isNew);
        });
        return;
    }
    if (!passwordMode) {
        if (issuedCode.isEmpty()) { tip->setText(QStringLiteral("请先获取验证码")); return; }
        if (code->text().trimmed() != issuedCode) { tip->setText(QStringLiteral("验证码错误")); return; }
    }
    if (registering) {
        if (password->text() != confirmPassword->text()) { tip->setText(QStringLiteral("两次输入的密码不一致")); return; }
        if (!QRegularExpression(QStringLiteral("^(?=.*[a-z])(?=.*[A-Z])(?=.*\\d)(?=.*[^A-Za-z\\d]).{6,}$")).match(password->text()).hasMatch()) {
            tip->setText(QStringLiteral("密码不合格：至少 6 位，且必须包含大写字母、小写字母、数字和特殊字符")); return;
        }
        submit->setEnabled(false); tip->setText(QStringLiteral("正在注册…"));
        UserService::registerAsync(phoneText, password->text(), [this](bool ok, const CloudUser &user, const QString &error) { submit->setEnabled(true); if (!ok) { tip->setText(error); return; } hide(); emit loginSucceeded(user.isNew); });
        return;
    }
    submit->setEnabled(false); tip->setText(QStringLiteral("正在登录…"));
    UserService::loginAsync(phoneText, code->text().trimmed(), [this](bool ok, const CloudUser &user, const QString &error) { submit->setEnabled(true); if (!ok) { if (error.contains(QStringLiteral("User not found"), Qt::CaseInsensitive) || error.contains(QStringLiteral("用户不存在"))) showRegistration(); else tip->setText(error); return; } hide(); emit loginSucceeded(user.isNew); });
}
