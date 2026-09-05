#ifndef PILESTATUSMODEL_H
#define PILESTATUSMODEL_H

#include <QObject>
#include <QString>
#include <QJsonObject>

QT_BEGIN_NAMESPACE
class QNetworkAccessManager;
class QNetworkReply;
class QNetworkRequest;
QT_END_NAMESPACE

/**
 * @brief 电桩状态概览数据模型（PC 运营后台 - 电桩状态模块）
 *
 * 继承自 QObject，负责从后台拉取电桩状态概览数据，
 * 解析后通过 dataReady() 信号将整个 data 对象发射给 Widget 展示。
 *
 * 接口对应《端口设计文档》3.2 节：
 *   GET /api/v1/admin/dashboard/pile-status-overview
 *   请求头: Authorization: Bearer <token>
 *
 * 统一响应信封：{ code, msg, data, timestamp }，code==0 表示成功。
 * data 字段：
 *   total_piles / in_use_count / in_use_percentage
 *   idle_count / idle_percentage
 *   fault_count / fault_percentage
 *   online_rate
 *
 * 网络错误 / HTTP 异常状态 / JSON 解析失败时通过 errorOccurred() 通知界面；
 * 响应结构与文档不一致时在日志中打印原始响应字符串。
 */
class PileStatusModel : public QObject
{
    Q_OBJECT
public:
    explicit PileStatusModel(QObject *parent = nullptr);

    /**
     * @brief 设置管理员鉴权 Token（Bearer Token，由 MainWindow 传入）
     * 仅保存 Token，不自动发起请求；首次拉取由 Widget 的 showEvent 触发。
     */
    void setAuthToken(const QString &token);

    /** 发起电桩状态概览请求 */
    void fetchData();

signals:
    /** 数据解析成功，data 为响应信封中的 data 对象（含全部概览字段） */
    void dataReady(const QJsonObject &data);

    /** 网络请求失败 / HTTP 异常状态 / 响应解析失败 / 业务错误码非 0 */
    void errorOccurred(const QString &errorMsg);

private:
    /** 懒初始化 QNetworkAccessManager（以 this 为 parent，随 Model 释放） */
    void ensureNetworkManager();

    /** 为请求填充公共头（Content-Type / Accept / Authorization） */
    void prepareRequest(QNetworkRequest *request) const;

    /** 处理响应：校验 -> 解析 -> 发 dataReady */
    void handleReply(QNetworkReply *reply);

    /**
     * @brief 解析统一响应信封 {code,msg,data}
     * @return true 表示 code==0 且 data 已取出；false 表示失败（已 emit errorOccurred）
     * 解析失败时在日志中打印原始响应，便于排查文档与实现不一致的情况。
     */
    bool extractData(const QByteArray &body, const QString &apiTag, QJsonObject &outData);

    QNetworkAccessManager *m_networkManager = nullptr; // HTTP 请求管理器（懒创建）
    QString m_authToken;                        // 管理员 Bearer Token

    // 服务器地址：当前写死，后续再改为可配置
    const QString m_serverBase = QStringLiteral("http://62.234.84.145:8080");
};

#endif // PILESTATUSMODEL_H
