#ifndef LOGINDIALOG_H
#define LOGINDIALOG_H

#include <QDialog>

QT_BEGIN_NAMESPACE
class QLineEdit;
class QPushButton;
class QLabel;
class QNetworkAccessManager;
QT_END_NAMESPACE

/**
 * @brief 管理员登录对话框
 *
 * 启动时弹出，登录成功（接口返回 code==0 且含 access_token）后关闭，
 * 随后主界面可通过 token() 获取令牌传递给各数据模型。
 *
 * 所有账号校验完全依赖云服务器接口 POST /api/v1/admin/auth/login，
 * 客户端不做任何本地密码判断。
 */
class LoginDialog : public QDialog
{
    Q_OBJECT
public:
    explicit LoginDialog(QWidget *parent = nullptr);

    /** 登录成功后获取 access_token */
    QString token() const { return m_token; }

private slots:
    void onLoginClicked();

private:
    QLineEdit           *m_accountEdit;
    QLineEdit           *m_passwordEdit;
    QPushButton         *m_loginButton;
    QLabel              *m_errorLabel;
    QNetworkAccessManager *m_networkManager;
    QString             m_token;
};

#endif // LOGINDIALOG_H
