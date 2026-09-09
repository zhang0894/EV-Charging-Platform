#include "core/chargeservice.h"
#include "ui/firstprofilesetuppage.h"
#include "ui/loginwindow.h"
#include "ui/mainwindow.h"
#include "ui/theme.h"

#include <QApplication>
#include <QCoreApplication>
#include <QMessageBox>
#include <QNetworkProxy>
#include <QSysInfo>
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
    // 腾讯 GL 地图需要 WebGL2。之前写死 --use-gl=swiftshader，在新版
    // Chromium/macOS 上会直接 qFatal 崩溃退出（"not supported with the
    // current configuration"）。ANGLE 的具体后端是按操作系统区分的：
    // Metal 只有 macOS 认，Windows 要用 d3d11，其余平台（含机房 Linux）
    // 干脆不强制后端，让 Chromium 自己按当前显卡/驱动挑一个能跑的。
    // 用户可通过环境变量自行覆盖这些启动参数（例如虚拟机里强制软件渲染）。
    if (qEnvironmentVariableIsEmpty("QTWEBENGINE_CHROMIUM_FLAGS")) {
#if defined(Q_OS_MACOS)
        qputenv("QTWEBENGINE_CHROMIUM_FLAGS", "--use-gl=angle --use-angle=metal --enable-webgl --no-sandbox");
#elif defined(Q_OS_WIN)
        qputenv("QTWEBENGINE_CHROMIUM_FLAGS", "--use-gl=angle --use-angle=d3d11 --enable-webgl --no-sandbox");
#else
        qputenv("QTWEBENGINE_CHROMIUM_FLAGS", "--enable-webgl --no-sandbox");
#endif
    }
    QCoreApplication::setAttribute(Qt::AA_ShareOpenGLContexts);
    QApplication app(argc, argv);
    // 云端 API 是直连服务；有的电脑 Qt 会捡到过期的系统代理导致所有请求卡到超时，
    // 这里强制不走代理（B 发现的问题）
    QNetworkProxy::setApplicationProxy(QNetworkProxy::NoProxy);
    app.setApplicationName(QStringLiteral("NCS User Client (Cloud)"));
    app.setStyleSheet(Theme::qss());

    // 截图自检：跳过登录界面，直接免密登录后抓图退出
    if (!qEnvironmentVariableIsEmpty("NCS_SHOT")) {
        // NCS_SHOT_LOGIN=1：只抓登录页本身（不连服务器，报告截图用）
        if (!qEnvironmentVariableIsEmpty("NCS_SHOT_LOGIN")) {
            auto *login = new LoginWindow;
            login->show();
            const QString loginFile = qEnvironmentVariable("NCS_SHOT");
            QTimer::singleShot(800, login, [login, loginFile] {
                login->grab().save(loginFile);
                qApp->quit();
            });
            return app.exec();
        }
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
        // NCS_SHOT_STATION=<站 id>：直接打开该站的选桩页（不填 = 用最近的一站）
        const int shotStation = qEnvironmentVariable("NCS_SHOT_STATION").toInt();
        // NCS_SHOT_DELAY=毫秒：推迟切页时间（默认 800），留时间给外部动作（如管理端冻结）
        const int delay = qMax(200, qEnvironmentVariable("NCS_SHOT_DELAY", "800").toInt());
        QTimer::singleShot(delay, win, [win, page, shotStation] {
            if (page == 1 && shotStation >= 0 && !qEnvironmentVariableIsEmpty("NCS_SHOT_STATION")) {
                int sid = shotStation;
                if (sid == 0) {
                    const auto stations = ChargeService::stationOptions();
                    if (!stations.isEmpty()) sid = stations.first().id;
                }
                win->openChargeForStation(sid);
            } else {
                win->showPage(page);
            }
        });
        // NCS_SHOT_WAIT=毫秒：切页后等多久再截图（默认 2200；充满自动结算等同步流程要给长一点）
        const int wait = qMax(500, qEnvironmentVariable("NCS_SHOT_WAIT", "2200").toInt());
        QTimer::singleShot(delay + wait, win, [win, file] {
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
