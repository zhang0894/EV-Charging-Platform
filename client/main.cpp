#include "core/apiclient.h"
#include "core/chargeservice.h"
#include "ui/mainwindow.h"
#include "ui/theme.h"

#include <QApplication>
#include <QMessageBox>
#include <QTimer>

// 充电用户端（云端版）—— 不再打开本地数据库，数据全部来自 server API
//   换服务器：NCS_API_BASE=http://x.x.x.x:8080 ./ncs_client_cloud
//   换测试手机号：NCS_PHONE=13800001234
//   截图自检：NCS_SHOT=a.png NCS_SHOT_PAGE=1
int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
    app.setApplicationName(QStringLiteral("NCS User Client (Cloud)"));
    app.setStyleSheet(Theme::qss());

    // B 的登录页接入前，启动时直接免密登录拿 token
    QString err;
    if (!ChargeService::devLogin(&err)) {
        QMessageBox::critical(nullptr, QStringLiteral("无法连接服务器"),
                              err + QStringLiteral("\n\n服务器：") + Api::baseUrl());
        return 1;

    }

    MainWindow win;
    win.show();

    // 截图自检：延迟一点让页面加载完，抓图后退出
    if (!qEnvironmentVariableIsEmpty("NCS_SHOT")) {
        const QString file = qEnvironmentVariable("NCS_SHOT");
        const int page = qEnvironmentVariable("NCS_SHOT_PAGE", "1").toInt();
        QTimer::singleShot(600, &win, [&win, page] { win.showPage(page); });
        QTimer::singleShot(2500, &win, [&win, file] {
            win.grab().save(file);
            qApp->quit();
        });
    }

    return app.exec();
}
