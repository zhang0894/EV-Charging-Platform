#ifndef TOKENMANAGER_H
#define TOKENMANAGER_H

#include <QObject>
#include <QString>
#include <QNetworkRequest>
#include <functional>

class QNetworkAccessManager;
class QNetworkReply;

/**
 * @brief Token 管理单例：统一管理 access_token / refresh_token 与请求重试。
 *
 * 职责：
 *   1. 存储 access_token / refresh_token（登录成功后由 main.cpp 设置）。
 *   2. 提供 prepareRequest() 为请求注入 Authorization 头。
 *   3. 提供 get/post/put 回调式请求方法，自动拦截 401 / code=40001/40002。
 *   4. 触发 refresh_token 刷新，刷新期间后续请求排队，刷新成功后统一重试。
 *   5. 刷新失败时发射 refreshFailed()，由 MainWindow 弹出登录页。
 *
 * 刷新流程：
 *   请求 → 响应401/40001/40002 → 入队 → 若未刷新中则发刷新请求 →
 *   成功：更新 access_token，重试所有排队请求（各重试1次）
 *   失败：发射 refreshFailed()
 *
 * 线程安全：本类运行在 GUI 线程，信号槽均为直连，无需加锁。
 */
class TokenManager : public QObject
{
    Q_OBJECT
public:
    static TokenManager *instance();

    /** 登录成功后调用，同时保存 access_token 和 refresh_token */
    void setTokens(const QString &accessToken, const QString &refreshToken);
    void clearTokens();

    QString accessToken() const  { return m_accessToken; }
    QString refreshToken() const { return m_refreshToken; }
    bool   isRefreshing() const  { return m_isRefreshing; }

    /** 为请求设置 Content-Type / Accept / Authorization 头 */
    void prepareRequest(QNetworkRequest *request) const;

    // ---- 回调式请求方法 ----
    // 回调在请求最终完成（含重试）后被调用，reply 的生命周期由回调方
    // 负责 deleteLater()。若刷新失败，回调不会被调用，而是发射 refreshFailed()。
    void get(const QNetworkRequest &req,
             std::function<void(QNetworkReply *)> cb);
    void post(const QNetworkRequest &req, const QByteArray &body,
              std::function<void(QNetworkReply *)> cb);
    void put(const QNetworkRequest &req, const QByteArray &body,
             std::function<void(QNetworkReply *)> cb);

signals:
    /** Token 刷新成功，所有排队请求已开始重试 */
    void tokensRefreshed();
    /** Token 刷新失败，需要重新登录 */
    void refreshFailed();

private:
    explicit TokenManager(QObject *parent = nullptr);

    void issueRequest(const QString &method, const QNetworkRequest &req,
                      const QByteArray &body,
                      std::function<void(QNetworkReply *)> cb);
    void handleReply(QNetworkReply *reply, const QString &method,
                     QNetworkRequest req, const QByteArray &body,
                     std::function<void(QNetworkReply *)> cb, bool retried);
    void startRefresh();
    void onRefreshReply();

    static bool isAuthError(int httpStatus, const QByteArray &body);

    QNetworkAccessManager *m_nam;
    QString m_accessToken;
    QString m_refreshToken;
    bool m_isRefreshing = false;

    // 刷新期间排队的请求
    struct PendingRequest {
        QString method;
        QNetworkRequest request;
        QByteArray body;
        std::function<void(QNetworkReply *)> callback;
    };
    QList<PendingRequest> m_pendingQueue;

    static const QString kServerBase;   // "http://62.234.84.145:8080"
};

#endif // TOKENMANAGER_H
