#include "core/apiclient.h"

#include "core/session.h"

#include <QEventLoop>
#include <QJsonDocument>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QTimer>
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

// server 的 msg 是英文，界面必须显示中文 —— 按文档 1.2 的业务错误码矩阵翻译
// 没收录的码就原样显示 msg（至少不丢信息）；server 改中文后这层自动退化为兜底
QString chineseForCode(int code, const QString &fallback)
{
    switch (code) {
    case 10001: return QStringLiteral("用户不存在");
    case 10002: return QStringLiteral("账号已被冻结，请联系客服");
    case 10003: return QStringLiteral("手机号格式错误，请输入 11 位有效手机号");
    case 10004: return QStringLiteral("用户名或密码错误");
    case 20001: return QStringLiteral("充电站不存在或已下线");
    case 20002: return QStringLiteral("充电桩不存在");
    case 20003: return QStringLiteral("该电桩正被占用或已被预约，请重新选择");
    case 20004: return QStringLiteral("该电桩处于故障维护状态");
    case 20005: return QStringLiteral("您有未完成的充电订单，请先结算");
    case 20006: return QStringLiteral("未找到进行中的充电订单");
    case 30001: return QStringLiteral("钱包余额不足 20 元，无法开始充电，请先充值");
    case 30002: return QStringLiteral("请勿重复提交");
    case 30003: return QStringLiteral("充值金额非法");
    case 30004: return QStringLiteral("订单已退款，不可重复操作");
    case 30005: return QStringLiteral("退款金额非法");
    case 50005: return QStringLiteral("接口不存在（客户端与服务端版本不一致？）");
    default:    return fallback;
    }
}

// 等 reply 完成（最多 8 秒），按 {code,msg,data} 解析
// 注意：server 对业务失败可能用 HTTP 4xx 但仍带标准 JSON，
// 所以先看 code/msg，再兜底当成网络错误
bool waitAndParse(QNetworkReply *reply, QJsonObject *data, QString *err)
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
    if (code != 0 && !raw.isEmpty()) {
        if (err)
            *err = chineseForCode(code,
                       root.value(QStringLiteral("msg"))
                           .toString(QStringLiteral("服务端返回异常")));
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

struct AsyncState { bool done = false; bool timedOut = false; };

void finishAsync(QNetworkReply *reply, const Api::Callback &callback,
                 const std::shared_ptr<AsyncState> &state)
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
    if (!state->timedOut && code != 0 && !raw.isEmpty()) {
        ok = false;
        error = chineseForCode(code,
                    root.value(QStringLiteral("msg"))
                        .toString(QStringLiteral("服务端返回异常")));
    } else if (!ok && !state->timedOut) {
        error = QStringLiteral("网络连接失败，请检查网络（%1）")
                    .arg(reply->errorString());
    }
    if (ok) data = root.value(QStringLiteral("data")).toObject();
    reply->deleteLater();
    callback(ok, data, error);
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
    return waitAndParse(nam()->get(makeRequest(path)), data, err);
}

bool post(const QString &path, const QJsonObject &body,
          QJsonObject *data, QString *err, const QString &idemKey)
{
    QNetworkRequest req = makeRequest(path);
    if (!idemKey.isEmpty())
        req.setRawHeader("Idempotency-Key", idemKey.toUtf8());
    return waitAndParse(
        nam()->post(req, QJsonDocument(body).toJson(QJsonDocument::Compact)),
        data, err);
}

bool put(const QString &path, const QJsonObject &body,
         QJsonObject *data, QString *err)
{
    return waitAndParse(
        nam()->put(makeRequest(path),
                   QJsonDocument(body).toJson(QJsonDocument::Compact)),
        data, err);
}

void getAsync(const QString &path, Callback callback)
{
    QNetworkReply *reply = nam()->get(makeRequest(path));
    auto state = std::make_shared<AsyncState>();
    QObject::connect(reply, &QNetworkReply::finished, reply,
                     [reply, callback, state] { finishAsync(reply, callback, state); });
    QTimer::singleShot(8000, reply, [reply, callback, state] {
        if (!reply->isFinished()) {
            state->timedOut = true;
            reply->abort();
            finishAsync(reply, callback, state);
        }
    });
}

void postAsync(const QString &path, const QJsonObject &body, Callback callback,
               const QByteArray &idempotencyKey)
{
    QNetworkRequest request = makeRequest(path);
    if (!idempotencyKey.isEmpty())
        request.setRawHeader("Idempotency-Key", idempotencyKey);
    QNetworkReply *reply = nam()->post(
        request, QJsonDocument(body).toJson(QJsonDocument::Compact));
    auto state = std::make_shared<AsyncState>();
    QObject::connect(reply, &QNetworkReply::finished, reply,
                     [reply, callback, state] { finishAsync(reply, callback, state); });
    QTimer::singleShot(8000, reply, [reply, callback, state] {
        if (!reply->isFinished()) {
            state->timedOut = true;
            reply->abort();
            finishAsync(reply, callback, state);
        }
    });
}

} // namespace Api
