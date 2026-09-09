#include "core/userservice.h"
#include "core/apiclient.h"
#include "core/session.h"
#include <QFile>
#include <QFileInfo>
#include <QJsonObject>
#include <QRegularExpression>
#include <QUuid>
#include <QUrl>
#include <QSettings>
#include <QtMath>

namespace UserService {
namespace {
QString cloudAvatarUrl(const QString &value)
{
    if (value.isEmpty()) return {};
    QUrl url(value);
    // 兼容旧服务端返回的默认头像路径。该文件已从服务器移除，
    // 若继续请求会在个人中心显示 404；空字符串会触发客户端内置默认头像。
    const QString path = url.path();
    if (path.endsWith(QStringLiteral("/static/avatars/default.png"), Qt::CaseInsensitive)
        || path.endsWith(QStringLiteral("/static/avatars/default.jpg"), Qt::CaseInsensitive)
        || path.endsWith(QStringLiteral("/static/avatars/default.jpeg"), Qt::CaseInsensitive))
        return {};
    const QUrl base(Api::baseUrl());
    if (url.isRelative()) return base.resolved(url).toString();
    if (url.host() == QStringLiteral("localhost") || url.host() == QStringLiteral("127.0.0.1")) {
        url.setScheme(base.scheme()); url.setHost(base.host()); url.setPort(base.port());
    }
    return url.toString();
}

// /user/profile 回包 → UserProfile（status 2 = 冻结，status_desc = FROZEN）
UserProfile parseProfile(const QJsonObject &data)
{
    UserProfile p;
    p.phone = data.value(QStringLiteral("phone")).toString();
    p.nickname = data.value(QStringLiteral("nickname")).toString();
    p.avatarUrl = cloudAvatarUrl(data.value(QStringLiteral("avatar_url")).toString());
    p.createdAt = data.value(QStringLiteral("created_at")).toString();
    p.balance = data.value(QStringLiteral("balance")).toDouble();
    p.status = data.value(QStringLiteral("status")).toInt(1);
    p.frozen = p.status == 2
            || data.value(QStringLiteral("status_desc")).toString() == QStringLiteral("FROZEN");
    return p;
}

CloudUser saveLoginResult(const QString &phone, const QJsonObject &data)
{
    Session::i().setUserId(data.value(QStringLiteral("user_id")).toInt());
    Session::i().setToken(data.value(QStringLiteral("access_token")).toString());
    Session::i().setRefreshToken(data.value(QStringLiteral("refresh_token")).toString());
    CloudUser user;
    user.id = Session::i().userId(); user.phone = phone;
    user.nickname = data.value(QStringLiteral("nickname")).toString();
    user.avatarUrl = cloudAvatarUrl(data.value(QStringLiteral("avatar_url")).toString());
    user.refreshToken = data.value(QStringLiteral("refresh_token")).toString();
    // 按手机号保存会话，供下次启动时尝试 Access/Refresh Token 恢复。
    if (!phone.isEmpty()) {
        QSettings settings;
        settings.beginGroup(QStringLiteral("sessions/%1").arg(phone));
        settings.setValue(QStringLiteral("user_id"), user.id);
        settings.setValue(QStringLiteral("access_token"), Session::i().token());
        settings.setValue(QStringLiteral("refresh_token"), user.refreshToken);
        settings.endGroup();
    }
    user.isNew = data.value(QStringLiteral("is_new_user")).toBool();
    return user;
}
}
bool login(const QString &phone, const QString &code, CloudUser *user, QString *error)
{
    if (!QRegularExpression(QStringLiteral("^1\\d{10}$")).match(phone).hasMatch()) { if (error) *error = QStringLiteral("手机号格式错误，请输入 1 开头的 11 位数字"); return false; }
    Q_UNUSED(code); // 验证码由登录界面生成并校验；云端当前不提供短信验证码端点。
    QJsonObject data;
    if (!Api::post(QStringLiteral("/api/v1/auth/login"), {{QStringLiteral("phone"), phone}, {QStringLiteral("auth_type"), QStringLiteral("passwordless")}}, &data, error)) return false;
    if (user) *user = saveLoginResult(phone, data);
    return true;
}
void loginAsync(const QString &phone, const QString &code,
                std::function<void(bool, const CloudUser &, const QString &)> callback)
{
    CloudUser empty;
    if (!QRegularExpression(QStringLiteral("^1\\d{10}$")).match(phone).hasMatch()) { callback(false, empty, QStringLiteral("手机号格式错误，请输入 1 开头的 11 位数字")); return; }
    Q_UNUSED(code); // 验证码由登录界面生成并校验。
    Api::postAsync(QStringLiteral("/api/v1/auth/login"), {{QStringLiteral("phone"), phone}, {QStringLiteral("auth_type"), QStringLiteral("passwordless")}},
        [phone, callback](bool ok, const QJsonObject &data, const QString &error) {
            CloudUser user;
            if (ok) user = saveLoginResult(phone, data);
            callback(ok, user, error);
        });
}
void passwordLoginAsync(const QString &phone, const QString &password,
                        std::function<void(bool, const CloudUser &, const QString &)> callback)
{
    CloudUser empty;
    if (!QRegularExpression(QStringLiteral("^1\\d{10}$")).match(phone).hasMatch()) {
        callback(false, empty, QStringLiteral("手机号格式错误，请输入 1 开头的 11 位数字")); return;
    }
    if (password.isEmpty()) { callback(false, empty, QStringLiteral("请输入密码")); return; }
    // 云端源码规定的密码登录端点不是 auth/login + auth_type=password。
    Api::postAsync(QStringLiteral("/api/v1/auth/login-password"),
                   {{QStringLiteral("phone"), phone},
                    {QStringLiteral("password"), password},
                    {QStringLiteral("auth_type"), QStringLiteral("password")}},
        [phone, callback](bool ok, const QJsonObject &data, const QString &error) {
            CloudUser user; if (ok) user = saveLoginResult(phone, data); callback(ok, user, error);
        });
}
void refreshTokenAsync(const QString &refreshToken,
                       std::function<void(bool, const CloudUser &, const QString &)> callback)
{
    CloudUser empty;
    if (refreshToken.isEmpty()) { callback(false, empty, QStringLiteral("Refresh Token 为空")); return; }
    Api::postAsync(QStringLiteral("/api/v1/auth/refresh"),
                   {{QStringLiteral("refresh_token"), refreshToken}},
        [refreshToken, callback](bool ok, const QJsonObject &data, const QString &error) {
            CloudUser user;
            if (ok) {
                Session::i().setUserId(data.value(QStringLiteral("user_id")).toInt(Session::i().userId()));
                Session::i().setToken(data.value(QStringLiteral("access_token")).toString());
                Session::i().setRefreshToken(data.value(QStringLiteral("refresh_token")).toString(refreshToken));
                user.id = Session::i().userId(); user.refreshToken = Session::i().refreshToken();
            }
            callback(ok, user, error);
        });
}
void checkPhoneAsync(const QString &phone, std::function<void(bool, bool, const QString &)> callback)
{
    if (!QRegularExpression(QStringLiteral("^1\\d{10}$")).match(phone).hasMatch()) {
        callback(false, false, QStringLiteral("手机号格式错误，请输入 1 开头的 11 位数字")); return;
    }
    Api::postAsync(QStringLiteral("/api/v1/auth/check-phone"),
                   {{QStringLiteral("phone"), phone}},
        [callback](bool ok, const QJsonObject &data, const QString &error) {
            callback(ok, data.value(QStringLiteral("is_registered")).toBool(data.value(QStringLiteral("is_exists")).toBool()), error);
        });
}
void registerAsync(const QString &phone, const QString &password,
                   std::function<void(bool, const CloudUser &, const QString &)> callback)
{
    CloudUser empty;
    if (!QRegularExpression(QStringLiteral("^1\\d{10}$")).match(phone).hasMatch()) { callback(false, empty, QStringLiteral("手机号格式错误，请输入 1 开头的 11 位数字")); return; }
    // 注册密码必须同时包含大小写字母、数字和特殊字符，长度至少 6 位。
    if (!QRegularExpression(QStringLiteral("^(?=.*[a-z])(?=.*[A-Z])(?=.*\\d)(?=.*[^A-Za-z\\d]).{6,}$")).match(password).hasMatch()) {
        callback(false, empty, QStringLiteral("密码不合格：至少 6 位，且必须包含大写字母、小写字母、数字和特殊字符")); return;
    }
    const QString nickname = QStringLiteral("用户") + phone.right(4);
    Api::postAsync(QStringLiteral("/api/v1/auth/register"), {{QStringLiteral("phone"), phone}, {QStringLiteral("password"), password}, {QStringLiteral("nickname"), nickname}},
        [phone, callback](bool ok, const QJsonObject &data, const QString &error) { CloudUser user; if (ok) { user = saveLoginResult(phone, data); user.isNew = true; } callback(ok, user, error); });
}
bool updateProfile(const QString &nickname, QString *error) { QJsonObject ignored; return Api::put(QStringLiteral("/api/v1/user/profile"), {{QStringLiteral("nickname"), nickname}}, &ignored, error); }
bool uploadAvatar(const QString &filePath, QString *avatarUrl, QString *error) {
    constexpr qint64 MaxAvatarBytes = 1LL * 1024 * 1024;
    if (QFileInfo(filePath).size() > MaxAvatarBytes) { if (error) *error = QStringLiteral("头像图片不能超过 1 MB"); return false; }
    QFile f(filePath); if (!f.open(QIODevice::ReadOnly)) { if(error)*error=QStringLiteral("无法读取头像文件"); return false; }
    QString type = QFileInfo(filePath).suffix().toLower();
    if (type == QStringLiteral("jpg")) type = QStringLiteral("jpeg");
    if (type != QStringLiteral("png") && type != QStringLiteral("jpeg") && type != QStringLiteral("webp"))
        type = QStringLiteral("jpeg");
    const QByteArray bytes = f.readAll();
    const QString contentType = QStringLiteral("image/%1").arg(type);
    QJsonObject data;
    if (!Api::postBinary(QStringLiteral("/api/v1/user/avatar"), bytes, contentType, &data, error)) return false;
    if (avatarUrl) *avatarUrl = cloudAvatarUrl(data.value(QStringLiteral("avatar_url")).toString()); return true;
}
void profileAsync(std::function<void(bool, const UserProfile &, const QString &)> callback)
{
    Api::getAsync(QStringLiteral("/api/v1/user/profile"), [callback](bool ok, const QJsonObject &data, const QString &error) {
        UserProfile profile;
        if (ok) { profile = parseProfile(data); if (profile.frozen) Api::events()->reportFrozen(); }
        callback(ok, profile, error);
    });
}
bool accountFrozen(QString *error)
{
    QJsonObject data;
    QString err;
    if (!Api::get(QStringLiteral("/api/v1/user/profile"), &data, &err)) {
        // 新版 server 对冻结账号的所有鉴权接口直接回 10002（含查资料本身）
        const bool frozen = err == Api::chineseForCode(10002, {});
        if (error) *error = err;
        return frozen;
    }
    const UserProfile profile = parseProfile(data);
    if (profile.frozen) { Api::events()->reportFrozen(); if (error) *error = Api::chineseForCode(10002, {}); }
    return profile.frozen;
}
void rechargeAsync(double amount, std::function<void(bool, double, const QString &)> callback)
{
    if (amount <= 0) { callback(false, 0, QStringLiteral("请输入大于 0 的充值金额")); return; }
    const QJsonObject body{{QStringLiteral("amount"), amount}, {QStringLiteral("amount_cents"), qRound64(amount * 100)}, {QStringLiteral("payment_method"), QStringLiteral("MOCK_PAY")}, {QStringLiteral("remark"), QStringLiteral("客户端模拟充值")}};
    const QByteArray key = QUuid::createUuid().toString(QUuid::WithoutBraces).toUtf8();
    Api::postAsync(QStringLiteral("/api/v1/wallet/recharge"), body,
        [callback](bool ok, const QJsonObject &data, const QString &error) {
            // 新旧服务端分别可能返回 number/string 类型的 new_balance，或 balance_after。
            const QJsonValue value = data.contains(QStringLiteral("new_balance"))
                ? data.value(QStringLiteral("new_balance")) : data.value(QStringLiteral("balance_after"));
            bool converted = false;
            const double balance = value.toVariant().toDouble(&converted);
            callback(ok, converted ? balance : 0.0, error);
        }, key);
}
void changePasswordAsync(const QString &oldPassword, const QString &newPassword,
                         std::function<void(bool, const QString &)> callback)
{
    if (oldPassword.isEmpty()) { callback(false, QStringLiteral("请输入旧密码")); return; }
    if (newPassword.size() < 6) { callback(false, QStringLiteral("新密码至少需要 6 位")); return; }
    Api::postAsync(QStringLiteral("/api/v1/user/password"),
        {{QStringLiteral("old_password"), oldPassword}, {QStringLiteral("new_password"), newPassword}},
        [callback](bool ok, const QJsonObject &, const QString &error) { callback(ok, error); });
}
}
