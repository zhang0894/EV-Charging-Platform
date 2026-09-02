#ifndef CHARGESERVICE_H
#define CHARGESERVICE_H

#include <QList>
#include <QString>

// 充电业务逻辑（A 负责）—— 所有 SQL 只出现在这里，UI 层禁止写 SQL
// 订单状态（见 db/README 数据字典）：0=充电中  1=待结算  2=已结算
namespace ChargeService {

struct StationOpt {          // 选桩页的电站下拉框（B 的电站列表接入前先用）
    int     id = 0;
    QString name;
    double  price = 0;       // 元/度
    int     freeCount = 0;   // 空闲桩数
};

struct PileOpt {             // 某站的一个空闲桩
    int     id = 0;
    QString code;
    QString typeText;        // 快充 / 慢充
    double  powerKw = 0;
};

struct ActiveOrder {         // 当前用户的未完成订单（状态 0 或 1）
    int     id = 0;
    int     pileId = 0;
    QString pileCode;
    QString stationName;
    QString startTime;       // "yyyy-MM-dd HH:mm:ss"
    double  unitPrice = 0;
    double  powerKw = 0;
    int     status = 0;      // 0=充电中 1=待结算
    int     durationMin = 0;
    double  kwh = 0;
    double  amount = 0;
};

int    timeScale();          // 时间加速倍率，NCS_TIME_SCALE，默认 60
double minStartBalance();    // 最低起充余额，默认 5 元

bool               findUnfinished(int userId, ActiveOrder *out);   // 充电前检查
QList<StationOpt>  stationOptions();
QList<PileOpt>     freePiles(int stationId);

// B 的登录/充值接入前的开发辅助：挑一个「正常、无未完成订单、余额>=20」的可测试用户
// 找不到就给 1 号补足余额。B 的登录页接入后删除
int devDefaultUser();

// TODO：UC-U-07 开始充电 / UC-U-08 计费 / UC-U-09 结算

} // namespace ChargeService

#endif // CHARGESERVICE_H
