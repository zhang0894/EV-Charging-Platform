#include "tokenmanager.h"

#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QJsonDocument>
#include <QJsonObject>
#include <QDebug>

const QString TokenManager::kServerBase =
    QStringLiteral("http://62.234.84.145:8080");

TokenManager *TokenManager::instance()
{
    static TokenManager inst;
    return &inst;
}

TokenManager::TokenManager(QObject *parent)
    : QObject(parent)
    , m_nam(new QNetworkAccessManager(this))
{
}

void TokenManager::setTokens(const QString &accessToken, const QString &refreshToken)
{
    m_accessToken  = accessToken;
    m_refreshToken = refreshToken;
    qDebug().noquote() << "[TokenManager] tokens set - access:"
                       << (m_accessToken.left(12) + "...") << "refresh:"
                       << (m_refreshToken.left(12) + "...");
}

void TokenManager::clearTokens()
{
    m_accessToken.clear();
    m_refreshToken.clear();
}

void TokenManager::prepareRequest(QNetworkRequest *request) const
{
    request->setHeader(QNetworkRequest::ContentTypeHeader,
                       QStringLiteral("application/json"));
    request->setRawHeader("Accept", "application/json");
    if (!m_accessToken.isEmpty()) {
        request->setRawHeader("Authorization",
                              (QStringLiteral("Bearer ") + m_accessToken).toUtf8());
    }
}

// ============================================================================
// 回调式请求方法
// ============================================================================

void TokenManager::get(const QNetworkRequest &req,
                       std::function<void(QNetworkReply *)> cb)
{
    issueRequest(QStringLiteral("GET"), req, QByteArray(), std::move(cb));
}

void TokenManager::post(const QNetworkRequest &req, const QByteArray &body,
                        std::function<void(QNetworkReply *)> cb)
{
    issueRequest(QStringLiteral("POST"), req, body, std::move(cb));
}

void TokenManager::put(const QNetworkRequest &req, const QByteArray &body,
                       std::function<void(QNetworkReply *)> cb)
{
    issueRequest(QStringLiteral("PUT"), req, body, std::move(cb));
}

// ============================================================================
// 内部：发送请求并连接 finished 回调
// ============================================================================

void TokenManager::issueRequest(const QString &method, const QNetworkRequest &req,
                                const QByteArray &body,
                                std::function<void(QNetworkReply *)> cb)
{
    // 确保请求带有最新 Token
    QNetworkRequest request = req;
    prepareRequest(&request);

    QNetworkReply *reply = nullptr;
    if (method == QStringLiteral("GET")) {
        reply = m_nam->get(request);
    } else if (method == QStringLiteral("POST")) {
        reply = m_nam->post(request, body);
    } else if (method == QStringLiteral("PUT")) {
        reply = m_nam->put(request, body);
    } else {
        qWarning() << "[TokenManager] unknown method:" << method;
        return;
    }

    // 按值捕获 method/request副本/body/cb（request 为副本，可安全重试）
    connect(reply, &QNetworkReply::finished, this,
            [this, reply, method, request, body, cb]() {
                handleReply(reply, method, request, body, cb, false);
            });
}

// ============================================================================
// 内部：响应处理 —— 检测 401/40001/40002，触发刷新或回调
// ============================================================================

void TokenManager::handleReply(QNetworkReply *reply, const QString &method,
                               QNetworkRequest req, const QByteArray &body,
                               std::function<void(QNetworkReply *)> cb,
                               bool retried)
{
    const int httpStatus = reply->attribute(
        QNetworkRequest::HttpStatusCodeAttribute).toInt();

    // 用 peek() 检测认证错误，不消费数据（回调方仍可 readAll）
    const QByteArray bodyPreview = reply->peek(reply->bytesAvailable());

    if (isAuthError(httpStatus, bodyPreview)) {
        reply->deleteLater();

        if (retried) {
            // 重试后仍然 401 → Token 彻底无效，需要重新登录
            qWarning() << "[TokenManager] retry still 401, emit refreshFailed";
            m_isRefreshing = false;
            emit refreshFailed();
            return;
        }

        // 入队等待刷新
        qDebug() << "[TokenManager] auth error, queuing request:" << method
                 << "HTTP" << httpStatus;
        m_pendingQueue.append({method, req, body, cb});

        if (!m_isRefreshing) {
            startRefresh();
        }
        return;
    }

    // 非认证错误：直接回调，回调方自行 readAll() / deleteLater()
    cb(reply);
}

bool TokenManager::isAuthError(int httpStatus, const QByteArray &body)
{
    // HTTP 401 直接判定为认证错误
    if (httpStatus == 401) {
        return true;
    }
    // 业务 code = 40001 (Unauthorized) 或 40002 (Token expired)
    if (!body.isEmpty()) {
        const QJsonDocument doc = QJsonDocument::fromJson(body);
        if (doc.isObject()) {
            const int code = doc.object().value(QStringLiteral("code")).toInt(-1);
            if (code == 40001 || code == 40002) {
                return true;
            }
        }
    }
    return false;
}

// ============================================================================
// 内部：发起 refresh_token 刷新请求
// ============================================================================

void TokenManager::startRefresh()
{
    m_isRefreshing = true;

    if (m_refreshToken.isEmpty()) {
        qWarning() << "[TokenManager] no refresh_token, emit refreshFailed";
        m_isRefreshing = false;
        emit refreshFailed();
        return;
    }

    QUrl url(kServerBase + QStringLiteral("/api/v1/auth/refresh"));
    QNetworkRequest request(url);
    request.setHeader(QNetworkRequest::ContentTypeHeader,
                      QStringLiteral("application/json"));
    request.setRawHeader("Accept", "application/json");

    QJsonObject body;
    body.insert(QStringLiteral("refresh_token"), m_refreshToken);

    qDebug().noquote() << "[TokenManager] refreshing token...";

    QNetworkReply *reply = m_nam->post(
        request, QJsonDocument(body).toJson(QJsonDocument::Compact));
    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        onRefreshReply();
    });
}

void TokenManager::onRefreshReply()
{
    auto *reply = qobject_cast<QNetworkReply *>(sender());
    if (!reply) {
        m_isRefreshing = false;
        emit refreshFailed();
        return;
    }

    const int httpStatus = reply->attribute(
        QNetworkRequest::HttpStatusCodeAttribute).toInt();
    const QByteArray body = reply->readAll();
    reply->deleteLater();

    QJsonParseError parseError;
    const QJsonDocument doc = QJsonDocument::fromJson(body, &parseError);
    const bool ok = (parseError.error == QJsonParseError::NoError && doc.isObject());

    if (!ok || doc.object().value(QStringLiteral("code")).toInt(-1) != 0) {
        qWarning().noquote() << "[TokenManager] refresh failed - HTTP"
                             << httpStatus << "body:" << QString::fromUtf8(body);
        m_isRefreshing = false;
        // 清空排队请求（回调不会被调用）
        m_pendingQueue.clear();
        emit refreshFailed();
        return;
    }

    // 刷新成功：更新 access_token
    const QString newToken = doc.object()
                                  .value(QStringLiteral("data"))
                                  .toObject()
                                  .value(QStringLiteral("access_token"))
                                  .toString();
    if (newToken.isEmpty()) {
        qWarning() << "[TokenManager] refresh response missing access_token";
        m_isRefreshing = false;
        m_pendingQueue.clear();
        emit refreshFailed();
        return;
    }

    m_accessToken = newToken;
    qDebug().noquote() << "[TokenManager] token refreshed:"
                       << (m_accessToken.left(12) + "...");

    // 重试所有排队请求（标记 retried=true，防二次入队）
    QList<PendingRequest> queue;
    queue.swap(m_pendingQueue);
    m_isRefreshing = false;

    for (const PendingRequest &req : queue) {
        // 重新发送（issueRequest 会用最新 Token 设置 Auth 头）
        QNetworkRequest request = req.request;
        prepareRequest(&request);

        QNetworkReply *newReply = nullptr;
        if (req.method == QStringLiteral("GET")) {
            newReply = m_nam->get(request);
        } else if (req.method == QStringLiteral("POST")) {
            newReply = m_nam->post(request, req.body);
        } else if (req.method == QStringLiteral("PUT")) {
            newReply = m_nam->put(request, req.body);
        }

        if (!newReply) {
            continue;
        }

        connect(newReply, &QNetworkReply::finished, this,
                [this, newReply, method = req.method, request, body = req.body,
                 cb = req.callback]() {
                    handleReply(newReply, method, request, body, cb, true);
                });
    }

    emit tokensRefreshed();
}
