#include "ui/profilepage.h"
#include "core/chargeservice.h"
#include "core/apiclient.h"
#include "core/session.h"
#include "core/userservice.h"

#include <QDialog>
#include <QDialogButtonBox>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QLabel>
#include <QLineEdit>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QPixmap>
#include <QPainter>
#include <QPointer>
#include <QPushButton>
#include <QSettings>
#include <QUrl>
#include <QUrlQuery>
#include <QDateTime>
#include <QTimer>
#include <QVBoxLayout>

namespace {
QString maskedPhone(const QString &phone) { return phone.size() == 11 ? phone.left(3) + QStringLiteral("****") + phone.right(4) : phone; }
}

ProfilePage::ProfilePage(QWidget *parent) : QWidget(parent), m_network(new QNetworkAccessManager(this)), m_refreshTimer(new QTimer(this))
{
    auto *layout = new QVBoxLayout(this); layout->setContentsMargins(22, 28, 22, 18); layout->setSpacing(14);
    auto *title = new QLabel(QStringLiteral("个人中心")); title->setObjectName("H1"); layout->addWidget(title);
    auto *identity = new QWidget; identity->setObjectName("Card"); auto *row = new QHBoxLayout(identity); row->setContentsMargins(18, 18, 18, 18);
    m_avatar = new QLabel; m_avatar->setAlignment(Qt::AlignCenter); m_avatar->setFixedSize(72, 72); showDefaultAvatar(); row->addWidget(m_avatar);
    auto *texts = new QVBoxLayout; auto *nameRow = new QHBoxLayout; m_name = new QLabel(QStringLiteral("正在加载…")); m_name->setObjectName("CardTitle"); m_name->setWordWrap(true); nameRow->addWidget(m_name); m_editName = new QPushButton(QStringLiteral("修改")); m_editName->setObjectName("Ghost"); nameRow->addWidget(m_editName); nameRow->addStretch(); texts->addLayout(nameRow);
    m_phone = new QLabel; m_phone->setObjectName("Cap"); texts->addWidget(m_phone); row->addLayout(texts, 1);
    m_changeAvatar = new QPushButton(QStringLiteral("更换头像")); m_changeAvatar->setObjectName("Ghost"); row->addWidget(m_changeAvatar); layout->addWidget(identity);
    auto *wallet = new QWidget; wallet->setObjectName("Card"); auto *walletLayout = new QVBoxLayout(wallet); walletLayout->setContentsMargins(18, 16, 18, 16); auto *walletTitle = new QLabel(QStringLiteral("钱包余额")); walletTitle->setObjectName("Cap"); walletLayout->addWidget(walletTitle); m_balance = new QLabel(QStringLiteral("¥ --")); m_balance->setObjectName("Big"); walletLayout->addWidget(m_balance); m_depositNote = new QLabel; m_depositNote->setObjectName("Cap"); m_depositNote->setWordWrap(true); walletLayout->addWidget(m_depositNote); layout->addWidget(wallet);
    auto *rechargeCard = new QWidget; rechargeCard->setObjectName("Card"); auto *rechargeLayout = new QHBoxLayout(rechargeCard); rechargeLayout->setContentsMargins(14, 12, 14, 12); m_amount = new QLineEdit; m_amount->setPlaceholderText(QStringLiteral("充值金额（0.01 - 10000 元）")); rechargeLayout->addWidget(m_amount, 1); m_recharge = new QPushButton(QStringLiteral("充值")); rechargeLayout->addWidget(m_recharge); layout->addWidget(rechargeCard);
    m_changePassword = new QPushButton(QStringLiteral("修改密码")); m_changePassword->setObjectName("Ghost"); layout->addWidget(m_changePassword);
    m_logout = new QPushButton(QStringLiteral("退出登录")); m_logout->setObjectName("Danger"); layout->addWidget(m_logout);
    m_tip = new QLabel; m_tip->setObjectName("Cap"); m_tip->setWordWrap(true); layout->addWidget(m_tip);
    m_requireLogin = new QPushButton(QStringLiteral("刷新token"));
    m_requireLogin->setObjectName("Ghost");
    m_requireLogin->setFixedSize(130, 44);
    m_requireLogin->setEnabled(false);
    auto *tokenRow = new QHBoxLayout;
    tokenRow->setContentsMargins(0, 0, 0, 0);
    tokenRow->addStretch();
    tokenRow->addWidget(m_requireLogin);
    layout->addLayout(tokenRow);
    layout->addStretch();
    connect(m_recharge, &QPushButton::clicked, this, &ProfilePage::recharge); connect(m_changeAvatar, &QPushButton::clicked, this, &ProfilePage::changeAvatar); connect(m_editName, &QPushButton::clicked, this, &ProfilePage::editNickname); connect(m_changePassword, &QPushButton::clicked, this, &ProfilePage::changePassword);
    connect(m_requireLogin, &QPushButton::clicked, this, &ProfilePage::requireLoginNextTime);
    connect(m_logout, &QPushButton::clicked, this, [this] { Session::i().setToken({}); Session::i().setRefreshToken({}); Session::i().setUserId(0); emit logoutRequested(); });
    // 管理员可能在另一端修改资料；定期拉取保证停留在个人中心时也能看到最新数据。
    m_refreshTimer->setInterval(5000);
    connect(m_refreshTimer, &QTimer::timeout, this, [this] {
        if (isVisible())
            reload(false);
    });
    m_refreshTimer->start();
    reload();
}

void ProfilePage::reload(bool showLoading)
{
    if (m_refreshInFlight)
        return;
    m_refreshInFlight = true;
    if (showLoading)
        m_tip->setText(QStringLiteral("正在加载个人资料…"));
    // 不依赖任何电脑上的头像缓存：每次均从云端个人资料读取头像地址。
    if (showLoading)
        showDefaultAvatar();
    QPointer<ProfilePage> self(this);
    UserService::profileAsync([self, showLoading](bool ok, const UserProfile &profile, const QString &error) {
        if (!self)
            return;                                   // 页面已被销毁（切换页面/退出登录），回包时不能再碰成员
        self->m_refreshInFlight = false;
        if (!ok) { if (showLoading) self->m_tip->setText(error); return; }
        self->m_sessionPhone = profile.phone;
        self->m_requireLogin->setEnabled(!self->m_sessionPhone.isEmpty());
        self->m_name->setText(profile.nickname); self->m_phone->setText(QStringLiteral("手机号：%1").arg(maskedPhone(profile.phone))); self->applyBalance(profile.balance); self->m_tip->clear();
        // 服务端头像实际由鉴权接口直接返回二进制；profile.avatar_url 可能仍是历史默认路径。
        self->loadAvatar(Api::baseUrl() + QStringLiteral("/api/v1/user/avatar"));
    });
}

void ProfilePage::requireLoginNextTime()
{
    if (m_sessionPhone.isEmpty()) {
        m_tip->setText(QStringLiteral("当前手机号尚未加载完成，请稍后再试"));
        return;
    }
    // 只删除下次启动/登录时使用的本机凭证，不清理当前内存中的 token；
    // 因此本次会话的充电、预约等操作仍可正常进行。
    QSettings settings;
    settings.beginGroup(QStringLiteral("sessions/%1").arg(m_sessionPhone));
    settings.remove(QString());
    settings.endGroup();
    m_requireLogin->setEnabled(false);
    m_tip->setText(QStringLiteral("已设置：下次登录需要密码或验证码（当前会话不受影响）"));
}

void ProfilePage::applyBalance(double serverBalance)
{
    // 9/6 起押金由 server 真扣，余额本身已是净值，直接显示
    m_balance->setText(QStringLiteral("¥ %1").arg(serverBalance, 0, 'f', 2));
    ChargeService::Reservation r;
    m_depositNote->setText(ChargeService::activeReservation(&r)
        ? QStringLiteral("另有预约押金 ¥%1 已支付（到站开充全额退回，取消退 ¥15）")
              .arg(r.deposit, 0, 'f', 0)
        : QString());
}

void ProfilePage::loadAvatar(const QString &url)
{
    if (url.isEmpty()) return;
    QUrl avatarUrl(url);
    // 头像文件可能在服务器上被覆盖但 URL 不变；加缓存破坏参数，
    // 避免 QNetworkAccessManager/系统代理继续返回旧的默认头像。
    QUrlQuery query(avatarUrl);
    query.addQueryItem(QStringLiteral("v"),
                       QString::number(QDateTime::currentMSecsSinceEpoch()));
    avatarUrl.setQuery(query);
    QNetworkRequest request{avatarUrl};
    // 云端若将头像读接口设为鉴权接口，也随请求发送当前 access token。
    const QString token = Session::i().token();
    if (!token.isEmpty()) request.setRawHeader("Authorization", ("Bearer " + token).toUtf8());
    auto *reply = m_network->get(request);
    connect(reply, &QNetworkReply::finished, this, [this, reply] {
        const QByteArray bytes = reply->readAll(); const bool networkOk = reply->error() == QNetworkReply::NoError;
        QPixmap pixmap; pixmap.loadFromData(bytes); const QString error = reply->errorString();
        const int http = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt(); reply->deleteLater();
        if (!pixmap.isNull()) { m_avatar->setPixmap(pixmap.scaled(m_avatar->size(), Qt::KeepAspectRatioByExpanding, Qt::SmoothTransformation)); return; }
        showDefaultAvatar();
        // 没上传过头像时 server 回 404（业务码 10006），这是正常情况，不提示；
        // 只有真正的网络错误才提示
        if (!networkOk && http != 404 && !error.isEmpty())
            m_tip->setText(QStringLiteral("头像加载失败，已显示默认头像"));
    });
}

void ProfilePage::showDefaultAvatar()
{
    QPixmap avatar(QStringLiteral(":/assets/assets/default.jpeg"));
    if (!avatar.isNull()) {
        m_avatar->setPixmap(avatar.scaled(m_avatar->size(), Qt::KeepAspectRatioByExpanding, Qt::SmoothTransformation));
        return;
    }
    QPixmap fallback(m_avatar->size()); fallback.fill(QColor("#d7e2ee"));
    QPainter painter(&fallback); painter.setRenderHint(QPainter::Antialiasing); painter.setBrush(QColor("#7c94ad")); painter.setPen(Qt::NoPen);
    painter.drawEllipse(24, 13, 24, 24); painter.drawEllipse(13, 40, 46, 38); m_avatar->setPixmap(fallback);
}

void ProfilePage::changeAvatar()
{
    const QString file = QFileDialog::getOpenFileName(this, QStringLiteral("选择头像"), {}, QStringLiteral("图片 (*.png *.jpg *.jpeg *.bmp)")); if (file.isEmpty()) return;
    if (QFileInfo(file).size() > 1LL * 1024 * 1024) { m_tip->setText(QStringLiteral("图片过大，请选择 1MB 以内的图片")); return; }
    // 先立即显示本地图片，避免上传完成但云端读取稍有延迟时仍看到默认头像。
    QPixmap local(file);
    if (!local.isNull())
        m_avatar->setPixmap(local.scaled(m_avatar->size(), Qt::KeepAspectRatioByExpanding, Qt::SmoothTransformation));
    QString error; QString url; if (!UserService::uploadAvatar(file, &url, &error)) { m_tip->setText(error); return; }
    // 上传后不保存本机副本；立刻使用服务端 URL，其他电脑登录也走同一读取链路。
    if (url.isEmpty()) { reload(); return; }
    loadAvatar(url); m_tip->setText(QStringLiteral("头像已上传到云端"));
}

void ProfilePage::editNickname()
{
    bool accepted = false; const QString name = QInputDialog::getText(this, QStringLiteral("修改昵称"), QStringLiteral("昵称（1-20 字符）："), QLineEdit::Normal, m_name->text(), &accepted).trimmed();
    if (!accepted) return; if (name.isEmpty()) { m_tip->setText(QStringLiteral("昵称不能为空")); return; } if (name.size() > 20) { m_tip->setText(QStringLiteral("昵称不能超过 20 个字符")); return; }
    QString error; if (!UserService::updateProfile(name, &error)) { m_tip->setText(error); return; } m_name->setText(name); m_tip->setText(QStringLiteral("昵称已更新"));
}

void ProfilePage::changePassword()
{
    QDialog dialog(this); dialog.setWindowTitle(QStringLiteral("修改密码")); dialog.setMinimumWidth(320);
    auto *form = new QFormLayout(&dialog);
    auto *oldPassword = new QLineEdit(&dialog); oldPassword->setEchoMode(QLineEdit::Password);
    auto *newPassword = new QLineEdit(&dialog); newPassword->setEchoMode(QLineEdit::Password);
    auto *confirmPassword = new QLineEdit(&dialog); confirmPassword->setEchoMode(QLineEdit::Password);
    form->addRow(QStringLiteral("旧密码："), oldPassword); form->addRow(QStringLiteral("新密码："), newPassword); form->addRow(QStringLiteral("确认新密码："), confirmPassword);
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Cancel, &dialog); auto *save = buttons->addButton(QStringLiteral("确认修改"), QDialogButtonBox::AcceptRole); form->addRow(buttons);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    QPointer<QDialog> dialogGuard(&dialog);
    connect(save, &QPushButton::clicked, &dialog, [&, dialogGuard] {
        if (oldPassword->text().isEmpty()) { oldPassword->setFocus(); return; }
        if (newPassword->text().size() < 6) { newPassword->setFocus(); return; }
        if (newPassword->text() != confirmPassword->text()) { confirmPassword->setFocus(); return; }
        save->setEnabled(false);
        UserService::changePasswordAsync(oldPassword->text(), newPassword->text(), [dialogGuard, save, this](bool ok, const QString &error) {
            if (!dialogGuard) return;
            save->setEnabled(true); if (!ok) { m_tip->setText(error); return; }
            dialogGuard->accept(); m_tip->setText(QStringLiteral("密码修改成功"));
        });
    });
    dialog.exec();
}

void ProfilePage::recharge()
{
    bool valid = false; const double amount = m_amount->text().toDouble(&valid); if (!valid || amount < 0.01 || amount > 10000) { m_tip->setText(QStringLiteral("请输入 0.01 - 10000 之间的充值金额")); return; }
    m_recharge->setEnabled(false); m_tip->setText(QStringLiteral("正在充值…")); UserService::rechargeAsync(amount, [this](bool ok, double balance, const QString &error) { m_recharge->setEnabled(true); if (!ok) { m_tip->setText(error); return; } applyBalance(balance); m_amount->clear(); m_tip->setText(QStringLiteral("支付成功，余额已更新")); UserService::profileAsync([this](bool profileOk, const UserProfile &profile, const QString &) { if (profileOk) applyBalance(profile.balance); }); });
}
