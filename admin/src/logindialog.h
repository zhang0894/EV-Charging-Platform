#ifndef LOGINDIALOG_H
#define LOGINDIALOG_H

#include <QDialog>
#include <QMap>
#include <QString>

QT_BEGIN_NAMESPACE
class QLineEdit;
class QPushButton;
class QLabel;
class QCheckBox;
class QFrame;
class QNetworkAccessManager;
class QAction;
QT_END_NAMESPACE

/**
 * @brief 管理员登录对话框
 *
 * 启动时弹出，登录成功（接口返回 code==0 且含 access_token）后关闭，
 * 随后主界面可通过 token() 获取令牌传递给各数据模型。
 *
 * 界面采用左右分栏设计：
 *   - 左侧 40% 品牌区（蓝绿渐变背景 + 平台 logo + slogan）
 *   - 右侧 60% 登录卡片（白底 + 圆角 + 柔和阴影 + 账号/密码输入 + 记住密码/忘记密码）
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
    /** 登录成功后获取 refresh_token */
    QString refreshToken() const { return m_refreshToken; }

private slots:
    void onLoginClicked();
    void onTogglePassword();   // 切换密码明文/密文
    void onForgotPassword();   // 忘记密码链接
    void onAccountChanged(const QString &text); // 账号变化 → 匹配已记住的密码自动填充

private:
    void buildUi();            // 构建左右分栏界面
    void applyStyle();         // 应用 QSS 样式
    void loadRemembered();     // 从 QSettings 读取已记住的账号密码

    // ---- 左侧品牌区 ----
    QFrame  *m_brandPanel;
    QLabel  *m_logoLabel;
    QLabel  *m_brandTitle;
    QLabel  *m_brandSlogan;

    // ---- 右侧登录卡片 ----
    QFrame  *m_card;
    QLabel  *m_cardTitle;
    QLabel  *m_cardSubtitle;
    QLineEdit *m_accountEdit;
    QLineEdit *m_passwordEdit;
    QAction *m_togglePwdAction; // 密码框尾部的显示/隐藏切换
    QCheckBox *m_rememberCheck;
    QLabel  *m_forgotLink;
    QPushButton *m_loginButton;
    QLabel  *m_errorLabel;
    QLabel  *m_copyrightLabel;

    QNetworkAccessManager *m_networkManager;
    QString m_token;
    QString m_refreshToken;
    bool m_passwordVisible = false;

    // 已记住的账号 -> 密码 映射（支持多账号，输入已登录过的账号自动填密码）
    QMap<QString, QString> m_rememberedAccounts;
};

#endif // LOGINDIALOG_H
