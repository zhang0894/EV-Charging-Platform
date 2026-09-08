#include "usermanagementmodel.h"
#include "tokenmanager.h"

#include <QDebug>
#include <QDateTime>
#include <QUuid>

#include <QNetworkRequest>
#include <QNetworkReply>
#include <QUrl>
#include <QUrlQuery>
#include <QJsonDocument>
#include <QJsonObject>

UserManagementModel::UserManagementModel(QObject *parent)
    : QObject(parent)
{
    m_tableModel = new QStandardItemModel(this);
    m_tableModel->setColumnCount(ColCount);
    m_tableModel->setHorizontalHeaderLabels({
        tr("用户ID"), tr("手机号"), tr("昵称"),
        tr("余额(元)"), tr("状态"), tr("注册时间"), tr("操作")
    });

    // 注意：网络请求在 Widget 调用 fetchUsers() 后才发起（构造阶段尚无 Token）。
}

QStandardItemModel *UserManagementModel::getModel()
{
    return m_tableModel;
}

void UserManagementModel::setAuthToken(const QString &token)
{
    // Token 由 TokenManager 单例统一管理（登录后由 main.cpp 设置），
    // Model 不再保存 Token；首次拉取由 Widget 触发。
    Q_UNUSED(token);
}

// ============================================================================
// 分页查询用户列表（文档 3.5 节）
//   GET /api/v1/admin/users?page=&page_size=&phone=&status=
// ============================================================================

void UserManagementModel::fetchUsers(int page, int pageSize,
                                     const QString &phoneFilter, int statusFilter)
{
    m_page = qMax(1, page);
    m_pageSize = qMax(1, pageSize);
    m_phoneFilter = phoneFilter.trimmed();
    m_statusFilter = statusFilter;

    QUrl url(m_serverBase + QStringLiteral("/api/v1/admin/users"));
    QUrlQuery query;
    query.addQueryItem(QStringLiteral("page"), QString::number(m_page));
    query.addQueryItem(QStringLiteral("page_size"), QString::number(m_pageSize));
    if (!m_phoneFilter.isEmpty()) {
        query.addQueryItem(QStringLiteral("phone"), m_phoneFilter);
    }
    if (m_statusFilter >= 1) {
        query.addQueryItem(QStringLiteral("status"), QString::number(m_statusFilter));
    }
    url.setQuery(query);

    qDebug().noquote() << "[UserManagementModel] fetchUsers() -"
                       << url.toString();

    QNetworkRequest request(url);
    // prepareRequest 由 TokenManager::get 内部统一处理
    TokenManager::instance()->get(request, [this](QNetworkReply *reply) {
        handleUsersReply(reply);
    });
}

// ============================================================================
// 冻结/解冻用户（文档 3.5 节）
//   PUT /api/v1/admin/users/{user_id}/status   Body: {status, reason}
// ============================================================================

void UserManagementModel::setUserStatus(int userId, int newStatus, const QString &reason)
{
    const QUrl url(m_serverBase
                   + QStringLiteral("/api/v1/admin/users/%1/status").arg(userId));
    QNetworkRequest request(url);
    // prepareRequest 由 TokenManager::put 内部统一处理

    QJsonObject body;
    body.insert(QStringLiteral("status"), newStatus);
    body.insert(QStringLiteral("reason"), reason);

    TokenManager::instance()->put(
        request, QJsonDocument(body).toJson(QJsonDocument::Compact),
        [this, userId, newStatus](QNetworkReply *reply) {
            handleStatusReply(reply, userId, newStatus);
        });
}

// ============================================================================
// 管理员手动调账 / 余额补偿（文档 3.5 节第 3 部分）
//   POST /api/v1/admin/users/{user_id}/adjust-wallet
//   请求头: Idempotency-Key（ADJ-<UUID>，文档要求，防重复提交）
//   请求体: { amount, amount_cents, remark }
//   金额支持负数（扣减），服务端校验调整后余额不可为负（InsufficientBalance）
// ============================================================================

void UserManagementModel::adjustUserWallet(int userId, double amount,
                                           const QString &remark)
{
    const QUrl url(m_serverBase
                   + QStringLiteral("/api/v1/admin/users/%1/adjust-wallet").arg(userId));
    QNetworkRequest request(url);
    // 幂等键：每次请求生成唯一 ID，服务端可据此去重（文档要求）
    // 在调用 TokenManager 前设置，prepareRequest 只补充鉴权头，保留自定义头
    request.setRawHeader("Idempotency-Key",
                         (QStringLiteral("ADJ-")
                          + QUuid::createUuid().toString(QUuid::WithoutBraces))
                             .toUtf8());

    const qint64 amountCents = qRound64(amount * 100.0);
    QJsonObject body;
    body.insert(QStringLiteral("amount"), amount);
    body.insert(QStringLiteral("amount_cents"), amountCents);
    body.insert(QStringLiteral("remark"), remark);

    qDebug().noquote() << "[UserManagementModel] adjustUserWallet() -"
                       << url.toString() << "amount_cents:" << amountCents;

    TokenManager::instance()->post(
        request, QJsonDocument(body).toJson(QJsonDocument::Compact),
        [this, userId](QNetworkReply *reply) {
            handleAdjustReply(reply, userId);
        });
}

// ============================================================================
// 内部辅助
// ============================================================================

void UserManagementModel::populateUsers(const QJsonArray &users)
{
    m_tableModel->removeRows(0, m_tableModel->rowCount());

    for (qsizetype i = 0; i < users.size(); ++i) {
        const QJsonObject u = users.at(i).toObject();

        const int userId = u.value(QStringLiteral("user_id")).toInt();
        const QString phone = u.value(QStringLiteral("phone")).toString();
        const QString nickname = u.value(QStringLiteral("nickname")).toString();
        const double balance = u.value(QStringLiteral("balance")).toDouble();
        const int status = u.value(QStringLiteral("status")).toInt();

        // 注册时间：毫秒级 Unix 时间戳 -> yyyy-MM-dd hh:mm:ss
        const qint64 createdAtMs =
            u.value(QStringLiteral("created_at")).toVariant().toLongLong();
        const QString createdText = (createdAtMs > 0)
            ? QDateTime::fromMSecsSinceEpoch(createdAtMs)
                  .toString(QStringLiteral("yyyy-MM-dd hh:mm:ss"))
            : QStringLiteral("-");

        // 用户ID 列
        QStandardItem *idItem = new QStandardItem(QString::number(userId));
        idItem->setTextAlignment(Qt::AlignCenter);
        // 手机号列
        QStandardItem *phoneItem = new QStandardItem(phone);
        phoneItem->setTextAlignment(Qt::AlignCenter);
        // 昵称列
        QStandardItem *nickItem = new QStandardItem(nickname);
        nickItem->setTextAlignment(Qt::AlignCenter);
        // 余额列（元，保留两位小数）
        QStandardItem *balItem = new QStandardItem(
            QStringLiteral("%1 元").arg(QString::number(balance, 'f', 2)));
        balItem->setTextAlignment(Qt::AlignRight | Qt::AlignVCenter);
        // 状态列：1=正常（绿） 2=冻结（红），原始值存 StatusRole
        const bool frozen = (status == 2);
        QStandardItem *stItem = new QStandardItem(
            frozen ? tr("冻结") : tr("正常"));
        stItem->setTextAlignment(Qt::AlignCenter);
        stItem->setForeground(frozen ? QColor(0xdc, 0x26, 0x26)
                                     : QColor(0x16, 0xa3, 0x4a));
        stItem->setData(status, StatusRole);
        // 注册时间列
        QStandardItem *timeItem = new QStandardItem(createdText);
        timeItem->setTextAlignment(Qt::AlignCenter);
        // 操作列占位：按钮由 Widget 依据本行的角色数据动态安装
        QStandardItem *actItem = new QStandardItem(QString());
        actItem->setData(userId, UserIdRole);
        actItem->setData(status, StatusRole);
        actItem->setData(phone, PhoneRole);
        actItem->setData(balance, BalanceRole);
        actItem->setTextAlignment(Qt::AlignCenter);

        m_tableModel->appendRow({idItem, phoneItem, nickItem,
                                 balItem, stItem, timeItem, actItem});
    }
}

void UserManagementModel::handleUsersReply(QNetworkReply *reply)
{
    // 先取走全部所需信息，再 deleteLater() 释放 reply
    const QNetworkReply::NetworkError netError = reply->error();
    const QString netErrorString = reply->errorString();
    const int httpStatus =
        reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    const QByteArray body = reply->readAll();
    reply->deleteLater();

    const QString apiTag = QStringLiteral("GET /api/v1/admin/users");

    if (netError != QNetworkReply::NoError) {
        const QString msg = QStringLiteral("%1 网络请求失败 (HTTP %2): %3")
                                .arg(apiTag).arg(httpStatus).arg(netErrorString);
        qWarning() << "[UserManagementModel]" << msg;
        emit errorOccurred(msg);
        return;
    }
    if (httpStatus < 200 || httpStatus >= 300) {
        const QString msg = QStringLiteral("%1 服务器返回异常状态 HTTP %2")
                                .arg(apiTag).arg(httpStatus);
        qWarning().noquote() << "[UserManagementModel]" << msg
                             << "原始响应:" << QString::fromUtf8(body);
        emit errorOccurred(msg);
        return;
    }

    QJsonObject data;
    if (!extractData(body, apiTag, data)) {
        return; // extractData() 内部已 emit errorOccurred
    }
    // 与文档结构不一致时打印原始响应（users 字段缺失视为异常）
    if (!data.contains(QStringLiteral("users"))) {
        qWarning().noquote()
            << "[UserManagementModel]" << apiTag
            << "响应缺少 users 字段, 原始响应:" << QString::fromUtf8(body);
    }

    const int total = data.value(QStringLiteral("total")).toInt(0);
    const int page = data.value(QStringLiteral("page")).toInt(m_page);
    const int pageSize = data.value(QStringLiteral("page_size")).toInt(m_pageSize);
    const QJsonArray users = data.value(QStringLiteral("users")).toArray();

    populateUsers(users);

    qDebug().noquote() << "[UserManagementModel] 用户列表更新成功 - 本页:"
                       << users.size() << "条, 总数:" << total;
    emit usersReady(users, total, page, pageSize);
}

void UserManagementModel::handleStatusReply(QNetworkReply *reply,
                                            int userId, int newStatus)
{
    const QNetworkReply::NetworkError netError = reply->error();
    const QString netErrorString = reply->errorString();
    const int httpStatus =
        reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    const QByteArray body = reply->readAll();
    reply->deleteLater();

    const QString apiTag = QStringLiteral("PUT /api/v1/admin/users/%1/status").arg(userId);

    if (netError != QNetworkReply::NoError) {
        const QString msg = QStringLiteral("%1 网络请求失败 (HTTP %2): %3")
                                .arg(apiTag).arg(httpStatus).arg(netErrorString);
        qWarning() << "[UserManagementModel]" << msg;
        emit errorOccurred(msg);
        return;
    }
    if (httpStatus < 200 || httpStatus >= 300) {
        const QString msg = QStringLiteral("%1 服务器返回异常状态 HTTP %2")
                                .arg(apiTag).arg(httpStatus);
        qWarning().noquote() << "[UserManagementModel]" << msg
                             << "原始响应:" << QString::fromUtf8(body);
        emit errorOccurred(msg);
        return;
    }

    QJsonObject data;
    if (!extractData(body, apiTag, data)) {
        return;
    }

    const QString actionText = (newStatus == 2) ? tr("冻结") : tr("解冻");
    const QString msg = QStringLiteral("用户 %1 %2成功").arg(userId).arg(actionText);
    qDebug().noquote() << "[UserManagementModel]" << msg;
    emit operationSuccess(msg);
}

void UserManagementModel::handleAdjustReply(QNetworkReply *reply, int userId)
{
    const QNetworkReply::NetworkError netError = reply->error();
    const QString netErrorString = reply->errorString();
    const int httpStatus =
        reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    const QByteArray body = reply->readAll();
    reply->deleteLater();

    const QString apiTag =
        QStringLiteral("POST /api/v1/admin/users/%1/adjust-wallet").arg(userId);

    if (netError != QNetworkReply::NoError) {
        const QString msg = QStringLiteral("%1 网络请求失败 (HTTP %2): %3")
                                .arg(apiTag).arg(httpStatus).arg(netErrorString);
        qWarning() << "[UserManagementModel]" << msg;
        emit errorOccurred(msg);
        return;
    }
    if (httpStatus < 200 || httpStatus >= 300) {
        const QString msg = QStringLiteral("%1 服务器返回异常状态 HTTP %2")
                                .arg(apiTag).arg(httpStatus);
        qWarning().noquote() << "[UserManagementModel]" << msg
                             << "原始响应:" << QString::fromUtf8(body);
        emit errorOccurred(msg);
        return;
    }

    QJsonObject data;
    if (!extractData(body, apiTag, data)) {
        return;
    }
    // 与文档不一致时打印原始响应（transaction_id 缺失视为异常）
    if (!data.contains(QStringLiteral("transaction_id"))) {
        qWarning().noquote()
            << "[UserManagementModel]" << apiTag
            << "响应缺少 transaction_id 字段, 原始响应:" << QString::fromUtf8(body);
    }

    const double adjustAmount = data.value(QStringLiteral("adjust_amount")).toDouble();
    const double balanceBefore = data.value(QStringLiteral("balance_before")).toDouble();
    const double balanceAfter = data.value(QStringLiteral("balance_after")).toDouble();
    const QString txId = data.value(QStringLiteral("transaction_id")).toString();

    const QString actionText = adjustAmount >= 0 ? tr("充值补偿") : tr("扣减");
    const QString msg = QStringLiteral(
        "用户 %1 钱包%2成功\n流水号：%3\n调整金额：%4 元\n"
        "调整前余额：%5 元 -> 调整后余额：%6 元")
        .arg(QString::number(userId), actionText, txId.isEmpty() ? QStringLiteral("-") : txId,
             QString::number(adjustAmount, 'f', 2),
             QString::number(balanceBefore, 'f', 2),
             QString::number(balanceAfter, 'f', 2));
    qDebug().noquote() << "[UserManagementModel] 调账成功 - 用户" << userId
                       << "adjust:" << adjustAmount
                       << "balance:" << balanceBefore << "->" << balanceAfter;
    emit adjustSuccess(msg);
}

bool UserManagementModel::extractData(const QByteArray &body,
                                      const QString &apiTag, QJsonObject &outData)
{
    QJsonParseError parseError;
    const QJsonDocument doc = QJsonDocument::fromJson(body, &parseError);
    if (parseError.error != QJsonParseError::NoError || !doc.isObject()) {
        const QString reason = QStringLiteral("%1 响应 JSON 解析失败: %2")
                                   .arg(apiTag, parseError.errorString());
        qWarning().noquote() << "[UserManagementModel]" << reason
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
        qWarning() << "[UserManagementModel]" << reason;
        emit errorOccurred(reason);
        return false;
    }

    outData = root.value(QStringLiteral("data")).toObject();
    return true;
}
