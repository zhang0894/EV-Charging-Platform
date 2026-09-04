#include "mainwindow.h"
#include "logindialog.h"

#include <QApplication>
#include <QFile>
#include <QTextStream>
#include <QDateTime>

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

    // 先弹出登录对话框，登录成功后才进入主界面
    LoginDialog login;
    if (login.exec() != QDialog::Accepted) {
        return 0;  // 用户取消或关闭登录窗口，直接退出
    }

    const QString token = login.token();

    MainWindow w(token);
    w.show();
    return a.exec();
}
