#ifndef APICLIENT_H
#define APICLIENT_H

#include <QJsonObject>
#include <QString>
#include <functional>

// 访问云端 server 的小工具（A+B 合并版）
//   同步：get / post / put（内部用 QEventLoop 等到响应再返回）
//   异步：getAsync / postAsync（B 的页面用，回调在主线程触发）
// 统一解析 {code, msg, data}；业务错误码按文档 1.2 翻成中文
// 服务器地址可用环境变量 NCS_API_BASE 覆盖
namespace Api {

using Callback = std::function<void(bool ok, const QJsonObject &data, const QString &error)>;

QString baseUrl();
bool get(const QString &path, QJsonObject *data, QString *err = nullptr);
// idemKey：写操作的幂等键（文档 1.6），同一个键重复提交只会生效一次
bool post(const QString &path, const QJsonObject &body,
          QJsonObject *data, QString *err = nullptr,
          const QString &idemKey = QString());
bool put(const QString &path, const QJsonObject &body,
         QJsonObject *data, QString *err = nullptr);
void getAsync(const QString &path, Callback callback);
void postAsync(const QString &path, const QJsonObject &body, Callback callback,
               const QByteArray &idempotencyKey = {});

} // namespace Api

#endif // APICLIENT_H
