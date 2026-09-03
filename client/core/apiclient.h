#ifndef APICLIENT_H
#define APICLIENT_H

#include <QJsonObject>
#include <QString>

// 访问云端 server 的小工具（阻塞式：内部用 QEventLoop 等到响应再返回）
// 统一解析服务端返回结构 {code, msg, data}（端口设计文档 1.1）
// 服务器地址可用环境变量 NCS_API_BASE 覆盖
namespace Api {

QString baseUrl();
bool get(const QString &path, QJsonObject *data, QString *err = nullptr);
bool post(const QString &path, const QJsonObject &body,
          QJsonObject *data, QString *err = nullptr);

} // namespace Api

#endif // APICLIENT_H
