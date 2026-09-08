#include "mainwindow.h"
#include "logindialog.h"
#include "tokenmanager.h"

#include <QApplication>
#include <QFile>
#include <QTextStream>
#include <QDateTime>
#include <QMessageBox>

int main(int argc, char *argv[])
{
    // Qt 高 DPI 自适应
    QApplication::setHighDpiScaleFactorRoundingPolicy(
        Qt::HighDpiScaleFactorRoundingPolicy::PassThrough);

    QApplication a(argc, argv);
    a.setApplicationName(QStringLiteral("ChargingPileAdmin"));
    a.setApplicationVersion(QStringLiteral("1.0.0"));

    // 加载科技感 QSS 样式表（从 qrc 资源读取，路径对应 resources.qrc 中的 :/style.qss）
    QFile qssFile(QStringLiteral(":/style.qss"));
    if (qssFile.open(QIODevice::ReadOnly | QIODevice::Text)) {
        QTextStream ts(&qssFile);
        ts.setEncoding(QStringConverter::Utf8);
        const QString style = ts.readAll();
        qApp->setStyleSheet(style);
        qssFile.close();
    }

    // Token 自动刷新失败时弹出提示并重新登录
    QObject::connect(TokenManager::instance(), &TokenManager::refreshFailed,
                     qApp, []() {
        TokenManager::instance()->clearTokens();
        QMessageBox::critical(nullptr, QStringLiteral("登录过期"),
            QStringLiteral("登录凭证已过期，请重新登录。"));
        // 触发重启流程：通过安全定时器退出应用，用户重启后重新登录
        QMetaObject::invokeMethod(qApp, "quit", Qt::QueuedConnection);
    });

    // 先弹出登录对话框，登录成功后才进入主界面
    LoginDialog login;
    if (login.exec() != QDialog::Accepted) {
        return 0;  // 用户取消或关闭登录窗口，直接退出
    }

    // 登录成功：将 access_token 和 refresh_token 注入 TokenManager
    TokenManager::instance()->setTokens(login.token(), login.refreshToken());

    MainWindow w(login.token());
    w.show();
    return a.exec();
}
