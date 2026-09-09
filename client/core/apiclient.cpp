#include "core/apiclient.h"

#include "core/session.h"

#include <QEventLoop>
#include <QJsonDocument>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QSettings>
#include <QTimer>
#include <QJsonArray>
#include <memory>

namespace {

QNetworkAccessManager *nam()
{
    static QNetworkAccessManager m;   // 整个程序共用一个连接管理器
    return &m;
}

QNetworkRequest makeRequest(const QString &path)
{
    QNetworkRequest req{QUrl(Api::baseUrl() + path)};
    req.setHeader(QNetworkRequest::ContentTypeHeader,
                  QStringLiteral("application/json"));
    const QString token = Session::i().token();
    if (!token.isEmpty())                       // 登录后所有请求都带 Bearer token
        req.setRawHeader("Authorization", ("Bearer " + token).toUtf8());
    return req;
}

} // namespace

namespace Api {

// server 的 msg 是英文，界面必须显示中文 —— 按文档 1.2 的业务错误码矩阵翻译
// 没收录的码就原样显示 msg（至少不丢信息）
QString chineseForCode(int code, const QString &fallback)
{
    switch (code) {
    case 10001: return QStringLiteral("用户不存在");
    case 10002: return QStringLiteral("账户已冻结，请联系管理员");
    case 10003: return QStringLiteral("手机号格式错误，请输入 11 位有效手机号");
    case 10004: return QStringLiteral("用户名或密码错误");
    case 10005: return QStringLiteral("该手机号已注册，请直接登录");
    case 10006: return QStringLiteral("尚未上传头像");
    case 10007: return QStringLiteral("头像图片不能超过 1 MB");
    case 20001: return QStringLiteral("充电站不存在或已下线");
    case 20002: return QStringLiteral("充电桩不存在");
    case 20003: return QStringLiteral("该电桩正被占用或已被预约，请重新选择");
    case 20004: return QStringLiteral("该电桩处于故障维护状态");
    case 20005: return QStringLiteral("您有未完成的充电订单或预约，请先处理");
    case 20006: return QStringLiteral("未找到进行中的充电订单或预约");
    case 20007: return QStringLiteral("订单不存在");
    case 20008: return QStringLiteral("当前订单状态不能结束");
    case 20009: return QStringLiteral("订单不是待结算状态");
    case 30001: return QStringLiteral("钱包余额不足 20 元，请先充值");
    case 30002: return QStringLiteral("请勿重复提交");
    case 30003: return QStringLiteral("充值金额非法");
    case 30004: return QStringLiteral("订单已退款，不可重复操作");
    case 30005: return QStringLiteral("退款金额非法");
    case 40001: return QStringLiteral("登录状态无效，请重新登录");
    case 40002: return QStringLiteral("登录已过期，请重新登录");
    case 40003: return QStringLiteral("没有权限执行该操作");
    case 40004: return QStringLiteral("登录凭证格式错误，请重新登录");
    case 50001: return QStringLiteral("服务器数据库异常，请稍后再试");
    case 50002: return QStringLiteral("电桩通讯超时，请稍后再试");
    case 50003: return QStringLiteral("服务器内部错误，请稍后再试");
    case 50004: return QStringLiteral("请求数据格式错误");
    case 50005: return QStringLiteral("接口不存在（客户端与服务端版本不一致？）");
    case 50006: return QStringLiteral("请求参数错误");
    default:    return fallback;
    }
}

void Events::reportFrozen()
{
    if (m_pending) return;
    m_pending = true;
    QTimer::singleShot(0, this, [this] { m_pending = false; emit accountFrozen(); });
}

Events *events()
{
    static Events e;
    return &e;
}

} // namespace Api

namespace {

// 业务错误统一出口：翻中文 + 冻结码上报
QString errorForCode(int code, const QJsonObject &root)
{
    if (code == 10002)
        Api::events()->reportFrozen();
    return Api::chineseForCode(code, root.value(QStringLiteral("msg"))
                                         .toString(QStringLiteral("服务端返回异常")));
}

// 等 reply 完成（最多 8 秒），按 {code,msg,data} 解析
// 注意：server 对业务失败可能用 HTTP 4xx 但仍带标准 JSON，
// 所以先看 code/msg，再兜底当成网络错误
bool waitAndParse(QNetworkReply *reply, QJsonObject *data, QString *err, int *serverCode = nullptr)
{
    QEventLoop loop;
    QTimer::singleShot(8000, &loop, &QEventLoop::quit);
    QObject::connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
    loop.exec();

    reply->deleteLater();
    if (!reply->isFinished()) {
        reply->abort();
        if (err) *err = QStringLiteral("请求超时，请检查网络");
        return false;
    }
    const QByteArray raw = reply->readAll();
    const QJsonObject root = QJsonDocument::fromJson(raw).object();
    const int code = root.value(QStringLiteral("code")).toInt(-1);
    if (serverCode) *serverCode = code;
    if (code != 0 && !raw.isEmpty()) {
        const QString msg = errorForCode(code, root);
        if (err) *err = msg;
        return false;
    }
    if (reply->error() != QNetworkReply::NoError) {
        if (err) *err = QStringLiteral("网络连接失败，请检查网络（%1）")
                            .arg(reply->errorString());
        return false;
    }
    if (data) *data = root.value(QStringLiteral("data")).toObject();
    return true;
}

bool tokenExpiredCode(int code)
{
    return code == 40001 || code == 40002 || code == 40004;
}

void persistRefreshedSession()
{
    // refresh token 可能轮换；按 user_id 找到原先以手机号命名的本机会话并覆盖。
    QSettings settings;
    settings.beginGroup(QStringLiteral("sessions"));
    const QStringList phones = settings.childGroups();
    for (const QString &phone : phones) {
        settings.beginGroup(phone);
        if (settings.value(QStringLiteral("user_id")).toInt() == Session::i().userId()) {
            settings.setValue(QStringLiteral("access_token"), Session::i().token());
            settings.setValue(QStringLiteral("refresh_token"), Session::i().refreshToken());
        }
        settings.endGroup();
    }
    settings.endGroup();
}

// 刷新请求刻意直接走 QNetworkAccessManager，避免刷新 token 本身又触发自动刷新。
bool refreshAccessTokenSync(QString *err)
{
    const QString refresh = Session::i().refreshToken();
    if (refresh.isEmpty()) { if (err) *err = QStringLiteral("登录已过期，请重新登录"); return false; }
    QNetworkRequest request{QUrl(Api::baseUrl() + QStringLiteral("/api/v1/auth/refresh"))};
    request.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));
    QJsonObject data;
    int code = -1;
    if (!waitAndParse(nam()->post(request, QJsonDocument(QJsonObject{{QStringLiteral("refresh_token"), refresh}}).toJson(QJsonDocument::Compact)), &data, err, &code))
        return false;
    const QString access = data.value(QStringLiteral("access_token")).toString();
    if (access.isEmpty()) { if (err) *err = QStringLiteral("刷新登录凭证失败，请重新登录"); return false; }
    Session::i().setToken(access);
    Session::i().setRefreshToken(data.value(QStringLiteral("refresh_token")).toString(refresh));
    Session::i().setUserId(data.value(QStringLiteral("user_id")).toInt(Session::i().userId()));
    persistRefreshedSession();
    return true;
}

struct AsyncState { bool done = false; bool timedOut = false; };

void finishAsync(QNetworkReply *reply, const Api::Callback &callback,
                 const std::shared_ptr<AsyncState> &state,
                 const std::function<void()> &retry = {})
{
    if (state->done) return;
    state->done = true;
    const QByteArray raw = reply->readAll();
    QString error;
    QJsonObject data;
    bool ok = reply->error() == QNetworkReply::NoError;
    if (state->timedOut) {
        ok = false;
        error = QStringLiteral("请求超时（8秒）：%1").arg(reply->url().toString());
    }
    const QJsonObject root = QJsonDocument::fromJson(raw).object();
    const int code = root.value(QStringLiteral("code")).toInt(-1);
    if (!state->timedOut && tokenExpiredCode(code) && retry) {
        reply->deleteLater();
        retry();
        return;
    }
    if (!state->timedOut && code != 0 && !raw.isEmpty()) {
        ok = false;
        error = errorForCode(code, root);
    } else if (!ok && !state->timedOut) {
        error = QStringLiteral("网络连接失败，请检查网络（%1）")
                    .arg(reply->errorString());
    }
    if (ok) data = root.value(QStringLiteral("data")).toObject();
    reply->deleteLater();
    callback(ok, data, error);
}

void refreshAccessTokenAsync(std::function<void(bool, const QString &)> callback)
{
    const QString refresh = Session::i().refreshToken();
    if (refresh.isEmpty()) { callback(false, QStringLiteral("登录已过期，请重新登录")); return; }
    QNetworkRequest request{QUrl(Api::baseUrl() + QStringLiteral("/api/v1/auth/refresh"))};
    request.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));
    QNetworkReply *reply = nam()->post(request, QJsonDocument(QJsonObject{{QStringLiteral("refresh_token"), refresh}}).toJson(QJsonDocument::Compact));
    QObject::connect(reply, &QNetworkReply::finished, reply, [reply, refresh, callback] {
        const QJsonObject root = QJsonDocument::fromJson(reply->readAll()).object();
        const int code = root.value(QStringLiteral("code")).toInt(-1);
        const QJsonObject data = root.value(QStringLiteral("data")).toObject();
        const bool ok = reply->error() == QNetworkReply::NoError && code == 0 && !data.value(QStringLiteral("access_token")).toString().isEmpty();
        const QString error = ok ? QString() : errorForCode(code, root);
        reply->deleteLater();
        if (ok) {
            Session::i().setToken(data.value(QStringLiteral("access_token")).toString());
            Session::i().setRefreshToken(data.value(QStringLiteral("refresh_token")).toString(refresh));
            Session::i().setUserId(data.value(QStringLiteral("user_id")).toInt(Session::i().userId()));
            persistRefreshedSession();
        }
        callback(ok, error.isEmpty() ? QStringLiteral("登录已过期，请重新登录") : error);
    });
}

} // namespace

namespace Api {

QString baseUrl()
{
    const QString env = qEnvironmentVariable("NCS_API_BASE");
    return env.isEmpty() ? QStringLiteral("http://62.234.84.145:8080") : env;
}

bool get(const QString &path, QJsonObject *data, QString *err)
{
    int code = -1;
    if (waitAndParse(nam()->get(makeRequest(path)), data, err, &code)) return true;
    return tokenExpiredCode(code) && refreshAccessTokenSync(err)
        ? waitAndParse(nam()->get(makeRequest(path)), data, err) : false;
}

bool post(const QString &path, const QJsonObject &body,
          QJsonObject *data, QString *err, const QString &idemKey)
{
    QNetworkRequest req = makeRequest(path);
    if (!idemKey.isEmpty())
        req.setRawHeader("Idempotency-Key", idemKey.toUtf8());
    int code = -1;
    if (waitAndParse(nam()->post(req, QJsonDocument(body).toJson(QJsonDocument::Compact)), data, err, &code)) return true;
    if (!tokenExpiredCode(code) || !refreshAccessTokenSync(err)) return false;
    req = makeRequest(path); if (!idemKey.isEmpty()) req.setRawHeader("Idempotency-Key", idemKey.toUtf8());
    return waitAndParse(nam()->post(req, QJsonDocument(body).toJson(QJsonDocument::Compact)), data, err);
}

bool postBinary(const QString &path, const QByteArray &body,
                const QString &contentType, QJsonObject *data, QString *err)
{
    QNetworkRequest req = makeRequest(path);
    req.setHeader(QNetworkRequest::ContentTypeHeader, contentType);
    int code = -1;
    if (waitAndParse(nam()->post(req, body), data, err, &code)) return true;
    if (!tokenExpiredCode(code) || !refreshAccessTokenSync(err)) return false;
    req = makeRequest(path); req.setHeader(QNetworkRequest::ContentTypeHeader, contentType);
    return waitAndParse(nam()->post(req, body), data, err);
}

bool put(const QString &path, const QJsonObject &body,
         QJsonObject *data, QString *err)
{
    int code = -1;
    if (waitAndParse(nam()->put(makeRequest(path), QJsonDocument(body).toJson(QJsonDocument::Compact)), data, err, &code)) return true;
    return tokenExpiredCode(code) && refreshAccessTokenSync(err)
        ? waitAndParse(nam()->put(makeRequest(path), QJsonDocument(body).toJson(QJsonDocument::Compact)), data, err) : false;
}

void getAsync(const QString &path, Callback callback)
{
    auto retried = std::make_shared<bool>(false);
    auto send = std::make_shared<std::function<void()>>();
    *send = [path, callback, retried, send] {
        QNetworkReply *reply = nam()->get(makeRequest(path)); auto state = std::make_shared<AsyncState>();
        const auto retry = [callback, retried, send] {
            if (*retried) { callback(false, {}, QStringLiteral("登录已过期，请重新登录")); return; }
            *retried = true; refreshAccessTokenAsync([callback, send](bool ok, const QString &error) { if (ok) (*send)(); else callback(false, {}, error); });
        };
        QObject::connect(reply, &QNetworkReply::finished, reply, [reply, callback, state, retry] { finishAsync(reply, callback, state, retry); });
        QTimer::singleShot(8000, reply, [reply, callback, state, retry] { if (!reply->isFinished()) { state->timedOut = true; reply->abort(); finishAsync(reply, callback, state, retry); } });
    };
    (*send)();
}

void postAsync(const QString &path, const QJsonObject &body, Callback callback,
               const QByteArray &idempotencyKey)
{
    auto retried = std::make_shared<bool>(false);
    auto send = std::make_shared<std::function<void()>>();
    *send = [path, body, callback, idempotencyKey, retried, send] {
        QNetworkRequest request = makeRequest(path); if (!idempotencyKey.isEmpty()) request.setRawHeader("Idempotency-Key", idempotencyKey);
        QNetworkReply *reply = nam()->post(request, QJsonDocument(body).toJson(QJsonDocument::Compact)); auto state = std::make_shared<AsyncState>();
        const auto retry = [callback, retried, send] {
            if (*retried) { callback(false, {}, QStringLiteral("登录已过期，请重新登录")); return; }
            *retried = true; refreshAccessTokenAsync([callback, send](bool ok, const QString &error) { if (ok) (*send)(); else callback(false, {}, error); });
        };
        QObject::connect(reply, &QNetworkReply::finished, reply, [reply, callback, state, retry] { finishAsync(reply, callback, state, retry); });
        QTimer::singleShot(8000, reply, [reply, callback, state, retry] { if (!reply->isFinished()) { state->timedOut = true; reply->abort(); finishAsync(reply, callback, state, retry); } });
    };
    (*send)();
}

} // namespace Api
