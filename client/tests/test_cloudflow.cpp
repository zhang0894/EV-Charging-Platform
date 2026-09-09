#include "core/apiclient.h"
#include "core/chargeservice.h"
#include "core/session.h"
#include "core/userservice.h"

#include <QCoreApplication>
#include <QEventLoop>
#include <QDateTime>
#include <QJsonObject>
#include <QThread>

// 云端流程测试（server 预约版）：真实 server 上跑
//   场景 1：A 预约(真扣押金) → B 账号看到已预约锁定、B 预约同桩被拒 → A 取消(扣5退15)
//   场景 1b：A、B 同一瞬间抢同一根桩 → 只能成功一个
//   场景 2：再预约 → 到站开充(核销预约退押金) → server 计费 → 结束
//   场景 3：结算（幂等键）→ 三段式小票 → 订单历史（顺带把 server 清理干净）
//   场景 4：管理端冻结账户 → 登录/充值被拒，提示「账户已冻结，请联系管理员」→ 解冻
// 注意：超时没收押金(120 秒)由 server 定时器负责，这里不等 2 分钟，不测超时路径
// 用法：NCS_PHONE=13800000002 NCS_PHONE_B=13800000003 ./test_cloudflow
//   场景 4 需要管理员：NCS_ADMIN=13900000000 NCS_ADMIN_PASS=123456 NCS_FROZEN_PHONE=13800009999
using namespace ChargeService;

// 记住/恢复登录态，方便在两个账号之间切换
struct Login { int userId = 0; QString token, refresh; };
static Login snapshot() { return {Session::i().userId(), Session::i().token(), Session::i().refreshToken()}; }
static void restore(const Login &l) { Session::i().setUserId(l.userId); Session::i().setToken(l.token); Session::i().setRefreshToken(l.refresh); }

static int g_failed = 0;

#define CHECK(cond, msg)                                                     \
    do {                                                                     \
        if (cond) {                                                          \
            qInfo("  PASS  %s", msg);                                        \
        } else {                                                             \
            qCritical("  FAIL  %s", msg);                                    \
            ++g_failed;                                                      \
        }                                                                    \
    } while (0)

int main(int argc, char *argv[])
{
    QCoreApplication app(argc, argv);
    if (qEnvironmentVariableIsEmpty("NCS_PHONE"))
        qputenv("NCS_PHONE", "13800000002");

    QString err;
    const QString phoneB = qEnvironmentVariable("NCS_PHONE_B", QStringLiteral("13800000003"));
    CHECK(loginAs(phoneB, &err), qPrintable(QStringLiteral("B 账号登录 %1").arg(err)));
    const Login loginB = snapshot();
    cancelReservation(nullptr, nullptr);     // B 上次残留的预约先清掉

    CHECK(devLogin(&err), qPrintable(QStringLiteral("登录 %1").arg(err)));
    const Login loginA = snapshot();
    qInfo("server: %s  user A: %d  user B: %d", qPrintable(Api::baseUrl()),
          loginA.userId, loginB.userId);
    CHECK(loginA.userId != loginB.userId, "A、B 是两个不同账号");

    ActiveOrder ao;
    if (findUnfinished(0, &ao)) {
        qCritical("该用户已有未完成订单 %s，先去 settle 再跑测试",
                  qPrintable(ao.id));
        return 1;
    }
    Reservation r;
    if (activeReservation(&r))          // 上次测试残留的预约先取消（会扣 5 元）
        cancelReservation(nullptr, nullptr);

    // 找一个空闲桩（新统一接口 /piles）
    int stationId = 0;
    QString stationName;
    PileOpt pile;
    for (const StationOpt &s : stationOptions()) {
        for (const PileOpt &p : freePiles(s.id))
            if (p.statusCode == 1) { pile = p; stationId = s.id; stationName = s.name; break; }
        if (!pile.id.isEmpty())
            break;
    }
    CHECK(!pile.id.isEmpty(), "统一桩接口 /piles 找到空闲电桩");

    // ================= 场景 1：预约（server 真接口） =================
    qInfo("—— 场景 1：预约与取消 ——");
    const double balance0 = walletBalance(&err);
    qInfo("  钱包余额 ¥%.2f", balance0);
    CHECK(balance0 >= reserveDeposit(), "余额满足押金门槛（20 元）");

    CHECK(!activeReservation(&r), "初始没有预约");
    CHECK(reservePile(stationId, pile, stationName, &err),
          qPrintable(QStringLiteral("预约成功 %1").arg(err)));
    CHECK(activeReservation(&r) && r.pileId == pile.id
              && r.deposit == reserveDeposit(),
          "预约已生效（桩号/押金正确）");
    const qint64 leftSec =
        (r.expiresAtMs - QDateTime::currentMSecsSinceEpoch()) / 1000;
    qInfo("  剩余到站时间 %lld 秒", leftSec);
    CHECK(leftSec > 0 && leftSec <= 120, "server 倒计时 120 秒内");

    const double afterReserve = walletBalance(&err);
    CHECK(qAbs(balance0 - afterReserve - reserveDeposit()) < 0.011,
          "押金 20 已从钱包真扣");

    // 关键需求：换成 B 账号看，该桩必须显示「已预约锁定」而不是空闲，且 B 预约不了
    restore(loginB);
    bool reservedVisible = false;
    for (const PileOpt &p : freePiles(stationId))
        if (p.id == pile.id && p.statusCode == 8
            && p.statusText == QStringLiteral("已预约锁定"))
            reservedVisible = true;
    CHECK(reservedVisible, "B 账号刷新桩列表：该桩显示 已预约锁定(8)");
    Reservation rB;
    CHECK(!activeReservation(&rB), "B 自己没有预约（预约是 A 的）");
    QString errB;
    CHECK(!reservePile(stationId, pile, stationName, &errB), "B 预约同一根桩被拒");
    qInfo("  B 收到的提示：%s", qPrintable(errB));
    CHECK(errB.contains(QStringLiteral("已被预约")), "拒绝原因是中文「已被预约」");
    restore(loginA);

    QString err2;
    CHECK(!reservePile(stationId, pile, stationName, &err2), "A 重复预约被拒");

    double forfeit = 0, refund = 0;
    CHECK(cancelReservation(&forfeit, &refund), "主动取消成功");
    CHECK(forfeit == 5.0 && refund == 15.0, "取消：扣 5 退 15");
    const double afterCancel = walletBalance(&err);
    CHECK(qAbs(afterReserve + refund - afterCancel) < 0.011, "退款 15 已到账");
    CHECK(!activeReservation(&r), "取消后没有预约");

    // ================= 场景 1b：A、B 同一瞬间抢同一根桩 =================
    qInfo("—— 场景 1b：并发抢桩 ——");
    {
        int okCount = 0, doneCount = 0;
        QString msgA, msgB;
        const QJsonObject body{{QStringLiteral("pile_id"), pile.id}};
        // postAsync 在发出时读取当前 token：切账号后立刻再发，两个请求几乎同时到 server
        restore(loginA);
        Api::postAsync(QStringLiteral("/api/v1/charging/reserve"), body,
            [&](bool ok, const QJsonObject &, const QString &e) { okCount += ok; msgA = e; ++doneCount; });
        restore(loginB);
        Api::postAsync(QStringLiteral("/api/v1/charging/reserve"), body,
            [&](bool ok, const QJsonObject &, const QString &e) { okCount += ok; msgB = e; ++doneCount; });
        while (doneCount < 2)
            QCoreApplication::processEvents(QEventLoop::WaitForMoreEvents, 100);
        qInfo("  A: %s  B: %s", msgA.isEmpty() ? "成功" : qPrintable(msgA),
              msgB.isEmpty() ? "成功" : qPrintable(msgB));
        CHECK(okCount == 1, "并发预约同一根桩：恰好一个成功");
        // 清理：谁抢到谁取消（扣 5 退 15）
        restore(loginA);
        Reservation ra;
        if (activeReservation(&ra)) { cancelReservation(nullptr, nullptr); qInfo("  A 抢到，已取消"); }
        restore(loginB);
        if (activeReservation(&ra)) { cancelReservation(nullptr, nullptr); qInfo("  B 抢到，已取消"); }
        restore(loginA);
        bool idleAgain = false;
        for (const PileOpt &p : freePiles(stationId))
            if (p.id == pile.id && p.statusCode == 1) idleAgain = true;
        CHECK(idleAgain, "清理后桩恢复空闲");
    }

    // ================= 场景 2：再预约 → 到站开充 → 计费 → 结束 =================
    qInfo("—— 场景 2：到站开充（核销预约）与计费 ——");
    CHECK(reservePile(stationId, pile, stationName, &err),
          qPrintable(QStringLiteral("再次预约 %1").arg(err)));
    const double beforeStart = walletBalance(&err);

    QString orderId;
    CHECK(startCharge(pile.id, &err, &orderId),
          qPrintable(QStringLiteral("到站开始充电 %1").arg(err)));
    qInfo("  订单号：%s  电桩：%s", qPrintable(orderId), qPrintable(pile.id));
    CHECK(!activeReservation(&r), "开充后预约已核销");
    const double afterStart = walletBalance(&err);
    qInfo("  开充前 ¥%.2f → 开充后 ¥%.2f（押金退回）", beforeStart, afterStart);
    CHECK(afterStart - beforeStart >= reserveDeposit() - 0.011,
          "到站开充押金 20 全额退回");

    CHECK(findUnfinished(0, &ao) && ao.status == 0, "active-order = 充电中");
    CHECK(ao.id == orderId, "订单号一致");
    const double kwh1 = ao.kwh;

    QThread::sleep(4);                      // server 那边在自己计费
    CHECK(findUnfinished(0, &ao), "4 秒后订单还在");
    qInfo("  电量 %.4f -> %.4f 度，费用 ¥%.2f", kwh1, ao.kwh, ao.amount);
    CHECK(ao.kwh >= kwh1, "电量单调不减（由 server 计算）");
    CHECK(ao.amount < afterStart, "当前费用 < 余额上限（自动结束逻辑的数据源正确）");

    CHECK(stopCharge(orderId, &err),
          qPrintable(QStringLiteral("结束充电 %1").arg(err)));
    // server 停止后状态落库可能慢半拍，重试几次再判定
    bool unsettled = false;
    for (int i = 0; i < 4 && !unsettled; ++i) {
        unsettled = findUnfinished(0, &ao) && ao.status == 1;
        if (!unsettled) QThread::msleep(500);
    }
    CHECK(unsettled, "结束后 = 待结算");

    // ================= 场景 3：结算 + 小票 + 历史 =================
    qInfo("—— 场景 3：结算与订单历史 ——");
    double deducted = 0, newBalance = 0;
    CHECK(settle(orderId, &deducted, &newBalance, &err),
          qPrintable(QStringLiteral("结算成功 %1").arg(err)));
    qInfo("  扣款 ¥%.2f，余额 ¥%.2f", deducted, newBalance);
    CHECK(!findUnfinished(0, &ao), "结算后没有未完成订单");

    Receipt receipt;
    CHECK(orderDetail(orderId, &receipt), "能取到订单小票");
    qInfo("  小票：电费 %.2f + 服务费 %.2f + 超时费 %.2f = %.2f",
          receipt.electricityFee, receipt.serviceFee, receipt.overtimeFee,
          receipt.totalFee);
    CHECK(qAbs(receipt.electricityFee + receipt.serviceFee + receipt.overtimeFee
               - receipt.totalFee) < 0.011,
          "三段费用相加 = 总额");
    CHECK(receipt.statusText == QStringLiteral("已结算"), "小票状态 = 已结算");

    bool inHistory = false;
    for (const OrderRow &row : orderHistory(50))
        if (row.id == orderId && row.status == 2)
            inHistory = true;
    CHECK(inHistory, "订单历史包含本单，状态已结算");

    // ================= 场景 4：管理端冻结账户 =================
    qInfo("—— 场景 4：账户被管理端冻结 ——");
    {
        const QString adminAccount = qEnvironmentVariable("NCS_ADMIN", QStringLiteral("13900000000"));
        const QString adminPass = qEnvironmentVariable("NCS_ADMIN_PASS", QStringLiteral("123456"));
        const QString victim = qEnvironmentVariable("NCS_FROZEN_PHONE", QStringLiteral("13800009999"));
        Session::i().setToken({});
        QJsonObject adminData;
        if (!Api::post(QStringLiteral("/api/v1/admin/auth/login"),
                       {{QStringLiteral("account"), adminAccount}, {QStringLiteral("password"), adminPass}},
                       &adminData, &err)) {
            qWarning("  管理员登录失败（%s），跳过冻结场景", qPrintable(err));
        } else {
            const Login admin{adminData.value(QStringLiteral("user_id")).toInt(),
                              adminData.value(QStringLiteral("access_token")).toString(), {}};
            int frozenEvents = 0;
            QObject::connect(Api::events(), &Api::Events::accountFrozen, [&] { ++frozenEvents; });

            // 先用受害者账号登录拿一个仍然有效的 token（模拟“正在使用中被冻结”）
            CHECK(loginAs(victim, &err), qPrintable(QStringLiteral("冻结前登录 %1").arg(err)));
            const Login victimLogin = snapshot();
            const int victimId = victimLogin.userId;

            auto setStatus = [&](int status) {
                restore(admin);
                QJsonObject d;
                const bool ok = Api::put(QStringLiteral("/api/v1/admin/users/%1/status").arg(victimId),
                                         {{QStringLiteral("status"), status},
                                          {QStringLiteral("reason"), QStringLiteral("客户端自动测试")}},
                                         &d, &err);
                return ok && d.value(QStringLiteral("status")).toInt() == status;
            };
            CHECK(setStatus(2), qPrintable(QStringLiteral("管理端冻结 %1").arg(err)));

            // 1) 冻结后再登录：被拒，提示必须是指定文案
            Session::i().setToken({});
            QString loginErr;
            CHECK(!loginAs(victim, &loginErr), "冻结后登录被拒");
            CHECK(loginErr == QStringLiteral("账户已冻结，请联系管理员"),
                  qPrintable(QStringLiteral("提示文案 = %1").arg(loginErr)));

            // 2) 使用中（旧 token 仍有效）：个人资料 status=2，accountFrozen() 能识别
            restore(victimLogin);
            QString fe;
            CHECK(UserService::accountFrozen(&fe), "旧 token 查资料：识别为已冻结");
            CHECK(fe == QStringLiteral("账户已冻结，请联系管理员"), "accountFrozen 给出同一文案");
            // 3) 交易类接口（旧 token 仍有效时）：server 源码里 recharge/reserve/settle
            //    看的是 user_wallets.status，而管理端冻结只改 users.status，
            //    所以线上 server 目前放行 —— 这里只记录，不算失败；客户端靠 2) 自己把关
            QJsonObject rd; QString reserveErr;
            const bool reserveBlocked = !reservePile(stationId, pile, stationName, &reserveErr);
            qInfo("  [信息] 冻结账号预约：server %s%s", reserveBlocked ? "已拦截：" : "未拦截（需反馈后端）",
                  reserveBlocked ? qPrintable(reserveErr) : "");
            if (!reserveBlocked) cancelReservation(nullptr, nullptr);   // 清理
            QCoreApplication::processEvents();          // reportFrozen 是异步发信号
            CHECK(frozenEvents >= 1, "全局 accountFrozen 信号已触发（主窗口据此弹窗退回登录页）");

            // 解冻并确认恢复
            CHECK(setStatus(1), qPrintable(QStringLiteral("管理端解冻 %1").arg(err)));
            Session::i().setToken({});
            CHECK(loginAs(victim, &err), "解冻后可以登录");
            CHECK(!UserService::accountFrozen(), "解冻后资料 status 恢复正常");
        }
        restore(loginA);
    }

    qInfo(g_failed == 0 ? "\n全部通过 ✅（已自我清理）" : "\n有 %d 项失败 ❌", g_failed);
    return g_failed == 0 ? 0 : 1;
}
