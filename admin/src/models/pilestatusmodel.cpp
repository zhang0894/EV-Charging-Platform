#include "pilestatusmodel.h"

#include <QDebug>

#include <QNetworkAccessManager>
#include <QNetworkRequest>
#include <QNetworkReply>
#include <QUrl>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>

PileStatusModel::PileStatusModel(QObject *parent)
    : QObject(parent)
{
    // 注意：网络请求在 Widget 的 showEvent / 刷新按钮触发 fetchData() 后发起
    // （构造阶段尚无管理员 Token）。
}

void PileStatusModel::setAuthToken(const QString &token)
{
    m_authToken = token.trimmed();
}

// ============================================================================
// 电桩状态概览（文档 3.2 节）
//   GET /api/v1/admin/dashboard/pile-status-overview
// ============================================================================

void PileStatusModel::fetchData()
{
    ensureNetworkManager();

    const QUrl url(m_serverBase
                   + QStringLiteral("/api/v1/admin/dashboard/pile-status-overview"));
    qDebug().noquote() << "[PileStatusModel] fetchData() -" << url.toString();

    QNetworkRequest request(url);
    prepareRequest(&request);

    QNetworkReply *reply = m_networkManager->get(request);
    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        handleReply(reply);
    });
}

// ============================================================================
// 内部辅助
// ============================================================================

void PileStatusModel::ensureNetworkManager()
{
    if (!m_networkManager) {
        m_networkManager = new QNetworkAccessManager(this);
    }
}

void PileStatusModel::prepareRequest(QNetworkRequest *request) const
{
    request->setHeader(QNetworkRequest::ContentTypeHeader,
                       QStringLiteral("application/json"));
    request->setRawHeader("Accept", "application/json");
    // 受保护接口需携带 Bearer Token；未设置 Token 时不带头（本地联调用）
    if (!m_authToken.isEmpty()) {
        request->setRawHeader("Authorization",
                              (QStringLiteral("Bearer ") + m_authToken).toUtf8());
    }
}

void PileStatusModel::handleReply(QNetworkReply *reply)
{
    // 先取走全部所需信息，再 deleteLater() 释放 reply
    const QNetworkReply::NetworkError netError = reply->error();
    const QString netErrorString = reply->errorString();
    const int httpStatus =
        reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    const QByteArray body = reply->readAll();
    reply->deleteLater();

    const QString apiTag = QStringLiteral("GET /api/v1/admin/dashboard/pile-status-overview");

    if (netError != QNetworkReply::NoError) {
        const QString msg = QStringLiteral("%1 网络请求失败 (HTTP %2): %3")
                                .arg(apiTag).arg(httpStatus).arg(netErrorString);
        qWarning() << "[PileStatusModel]" << msg;
        emit errorOccurred(msg);
        return;
    }
    if (httpStatus < 200 || httpStatus >= 300) {
        const QString msg = QStringLiteral("%1 服务器返回异常状态 HTTP %2")
                                .arg(apiTag).arg(httpStatus);
        qWarning().noquote() << "[PileStatusModel]" << msg
                             << "原始响应:" << QString::fromUtf8(body);
        emit errorOccurred(msg);
        return;
    }

    QJsonObject data;
    if (!extractData(body, apiTag, data)) {
        return; // extractData() 内部已 emit errorOccurred
    }

    qDebug().noquote() << "[PileStatusModel] 电桩状态概览更新成功 - 总桩数:"
                       << data.value(QStringLiteral("total_piles")).toInt()
                       << "在用:" << data.value(QStringLiteral("in_use_count")).toInt()
                       << "闲置:" << data.value(QStringLiteral("idle_count")).toInt()
                       << "故障:" << data.value(QStringLiteral("fault_count")).toInt()
                       << "在线率:" << data.value(QStringLiteral("online_rate")).toDouble();
    emit dataReady(data);
}

bool PileStatusModel::extractData(const QByteArray &body,
                                  const QString &apiTag, QJsonObject &outData)
{
    QJsonParseError parseError;
    const QJsonDocument doc = QJsonDocument::fromJson(body, &parseError);
    if (parseError.error != QJsonParseError::NoError || !doc.isObject()) {
        const QString reason = QStringLiteral("%1 响应 JSON 解析失败: %2")
                                   .arg(apiTag, parseError.errorString());
        qWarning().noquote() << "[PileStatusModel]" << reason
                             << "原始响应:" << QString::fromUtf8(body);
        emit errorOccurred(reason);
        return false;
    }

    const QJsonObject root = doc.object();
    const int code = root.value(QStringLiteral("code")).toInt(-1);
    if (code != 0) {
        const QString reason = QStringLiteral("%1 业务错误 code=%2, msg=%3")
                .arg(apiTag)
                .arg(code)
                .arg(root.value(QStringLiteral("msg"))
                         .toString(QStringLiteral("unknown")));
        qWarning() << "[PileStatusModel]" << reason;
        emit errorOccurred(reason);
        return false;
    }

    outData = root.value(QStringLiteral("data")).toObject();
    return true;
}
