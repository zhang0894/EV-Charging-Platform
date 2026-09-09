#ifndef APICLIENT_H
#define APICLIENT_H

#include <QJsonObject>
#include <QObject>
#include <QString>
#include <functional>

// 访问云端 server 的小工具
//   同步：get / post / put（内部用 QEventLoop 等到响应再返回）
//   异步：getAsync / postAsync（回调在主线程触发）
// 统一解析 {code, msg, data}；业务错误码按文档 1.2 翻成中文
// 服务器地址可用环境变量 NCS_API_BASE 覆盖
namespace Api {

using Callback = std::function<void(bool ok, const QJsonObject &data, const QString &error)>;

// 全局事件：任何接口返回 10002（账户被管理端冻结）都会发 accountFrozen()
// 主窗口接这个信号弹提示并退回登录页；各页面不用自己判断
class Events : public QObject
{
    Q_OBJECT
public:
    void reportFrozen();      // 异步发信号（避免在网络回调里直接弹窗）
signals:
    void accountFrozen();
private:
    bool m_pending = false;
};
Events *events();

QString baseUrl();
QString chineseForCode(int code, const QString &fallback);   // 业务错误码 → 中文

bool get(const QString &path, QJsonObject *data, QString *err = nullptr);
bool post(const QString &path, const QJsonObject &body,
          QJsonObject *data, QString *err = nullptr,
          const QString &idemKey = QString());
bool postBinary(const QString &path, const QByteArray &body,
                const QString &contentType, QJsonObject *data = nullptr,
                QString *err = nullptr);
bool put(const QString &path, const QJsonObject &body,
         QJsonObject *data, QString *err = nullptr);
void getAsync(const QString &path, Callback callback);
void postAsync(const QString &path, const QJsonObject &body, Callback callback,
               const QByteArray &idempotencyKey = QByteArray());

} // namespace Api

#endif // APICLIENT_H
