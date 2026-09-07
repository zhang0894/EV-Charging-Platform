#include "core/chargeservice.h"
#include "ui/firstprofilesetuppage.h"
#include "ui/loginwindow.h"
#include "ui/mainwindow.h"
#include "ui/theme.h"

#include <QApplication>
#include <QCoreApplication>
#include <QMessageBox>
#include <QNetworkProxy>
#include <QTimer>

#include <functional>

// 充电用户端（云端版）—— 不再打开本地数据库，数据全部来自 server API
//   换服务器：NCS_API_BASE=http://x.x.x.x:8080 ./ncs_client_cloud
//   换测试手机号：NCS_PHONE=13800001234
//   截图自检：NCS_SHOT=a.png NCS_SHOT_PAGE=1
//   预约超时由 server 固定 120 秒
//   截图演示预约页：NCS_DEMO_RESERVE=1（自动预约第一个空闲桩）
int main(int argc, char *argv[])
{
    // 虚拟机常没有可用 GPU，且 Chromium 沙箱可能因运行环境限制而退出。
    // 仅在用户未自行设置时启用兼容参数；正式部署可通过环境变量覆盖。
    if (qEnvironmentVariableIsEmpty("QTWEBENGINE_CHROMIUM_FLAGS"))
        qputenv("QTWEBENGINE_CHROMIUM_FLAGS", "--disable-gpu --no-sandbox");
    QCoreApplication::setAttribute(Qt::AA_ShareOpenGLContexts);
    QApplication app(argc, argv);
    // 云端 API 是直连服务；有的电脑 Qt 会捡到过期的系统代理导致所有请求卡到超时，
    // 这里强制不走代理（B 发现的问题）
    QNetworkProxy::setApplicationProxy(QNetworkProxy::NoProxy);
    app.setApplicationName(QStringLiteral("NCS User Client (Cloud)"));
    app.setStyleSheet(Theme::qss());

    // 截图自检：跳过登录界面，直接免密登录后抓图退出
    if (!qEnvironmentVariableIsEmpty("NCS_SHOT")) {
        QString err;
        if (!ChargeService::devLogin(&err)) {
            QMessageBox::critical(nullptr, QStringLiteral("无法连接服务器"), err);
            return 1;
        }
        // 演示预约页：先自动预约第一个空闲桩，再抓图
        if (!qEnvironmentVariableIsEmpty("NCS_DEMO_RESERVE")) {
            using namespace ChargeService;
            Reservation existing;
            if (!activeReservation(&existing))
            for (const StationOpt &s : stationOptions()) {
                bool done = false;
                for (const PileOpt &p : freePiles(s.id))
                    if (p.statusCode == 1) {
                        done = reservePile(s.id, p, s.name, &err);
                        break;
                    }
                if (done)
                    break;
            }
        }
        auto *win = new MainWindow;
        win->show();
        const QString file = qEnvironmentVariable("NCS_SHOT");
        const int page = qEnvironmentVariable("NCS_SHOT_PAGE", "1").toInt();
        QTimer::singleShot(800, win, [win, page] { win->showPage(page); });
        QTimer::singleShot(3000, win, [win, file] {
            win->grab().save(file);
            qApp->quit();
        });
        return app.exec();
    }

    MainWindow *win = nullptr;
    std::function<void()> openMain;
    std::function<void()> showFreshLogin;

    // 每次进入登录页都新建窗口，而不是把上一次登录过的窗口重新 show()。
    // 这样手机号、验证码、倒计时和“注册中”状态都会回到初始值。
    showFreshLogin = [&] {
        auto *login = new LoginWindow;
        login->setAttribute(Qt::WA_DeleteOnClose);
        QObject::connect(login, &LoginWindow::loginSucceeded, login,
                         [&, login](bool isNew) {
            login->close();       // 登录成功后销毁这一轮登录窗口
            if (!isNew) { openMain(); return; }
            auto *profile = new FirstProfileSetupPage;
            profile->setAttribute(Qt::WA_DeleteOnClose);
            QObject::connect(profile, &FirstProfileSetupPage::completed, profile,
                             [profile, &openMain] { profile->close(); openMain(); });
            profile->show();
        });
        login->show();
    };

    openMain = [&] {
        if (!win) {
            win = new MainWindow;
            QObject::connect(win, &MainWindow::logoutRequested, win, [&] {
                if (win) { win->close(); win->deleteLater(); win = nullptr; }
                showFreshLogin();  // 退出登录后显示全新 LoginWindow 实例
            });
            win->show();
        }
    };
    showFreshLogin();

    return app.exec();
}
