#include "core/apiclient.h"

#include "core/session.h"

#include <QEventLoop>
#include <QJsonDocument>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QTimer>

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

// 等 reply 完成（最多 8 秒），按 {code,msg,data} 解析
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
    if (reply->error() != QNetworkReply::NoError && raw.isEmpty()) {
        if (err) *err = QStringLiteral("网络错误：%1").arg(reply->errorString());
        return false;
    }
    const QJsonObject root = QJsonDocument::fromJson(raw).object();
    if (root.value(QStringLiteral("code")).toInt(-1) != 0) {
        if (err)
            *err = root.value(QStringLiteral("msg"))
                       .toString(QStringLiteral("服务端返回异常"));
        return false;
    }
    if (data) *data = root.value(QStringLiteral("data")).toObject();
    return true;
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
          QJsonObject *data, QString *err)
{
    return waitAndParse(
        nam()->post(makeRequest(path),
                    QJsonDocument(body).toJson(QJsonDocument::Compact)),
        data, err);
}

} // namespace Api
