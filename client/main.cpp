#include "core/chargeservice.h"
#include "core/databasemanager.h"
#include "core/session.h"
#include "ui/mainwindow.h"
#include "ui/theme.h"

#include <QApplication>
#include <QMessageBox>

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
    app.setApplicationName(QStringLiteral("NCS User Client"));
    app.setStyleSheet(Theme::qss());

    QString err;
    if (!DatabaseManager::init(&err)) {
        QMessageBox::critical(nullptr, QStringLiteral("数据库错误"),
                              err + QStringLiteral("\n\n数据库路径：")
                                  + DatabaseManager::dbPath());
        return 1;
    }

    // B 的登录页接入前，用环境变量指定用户；不指定就自动挑一个可测试的用户
    bool ok = false;
    int userId = qEnvironmentVariable("NCS_USER").toInt(&ok);
    if (!ok || userId <= 0)
        userId = ChargeService::devDefaultUser();
    Session::i().setUserId(userId);


    MainWindow win;
    win.show();
    return app.exec();
}