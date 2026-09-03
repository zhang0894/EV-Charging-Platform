#ifndef CHARGESERVICE_H
#define CHARGESERVICE_H

#include <QList>
#include <QString>

// 充电业务逻辑（云端版）—— 数据全部来自 server API（见 端口设计文档 2.x）
// UI 层只认识这里的函数和结构体，完全不知道数据是从网络来的
// 结构体字段和本地 SQLite 版保持一致，界面代码一行不用改
namespace ChargeService {

struct StationOpt {          // 选桩页的电站下拉框
    int     id = 0;
    QString name;
    double  price = 0;       // 元/度（price_per_kwh，服务费另计）
    int     freeCount = 0;   // idle_piles
};

struct PileOpt {             // 某站的一个空闲桩
    QString id;              // 云端桩号是字符串，如 P00101
    QString code;            // pile_name，如 01号直流快充桩
    QString typeText;        // type_desc，如 直流快充
    double  powerKw = 0;     // max_power_kw
};

struct ActiveOrder {         // 当前用户的未完成订单（active-order 接口）
    QString id;              // 云端订单号是字符串，如 ORD_20260902_1001
    QString pileCode;        // pile_id
    QString stationName;
    QString startTime;       // 已从毫秒时间戳转成 "yyyy-MM-dd HH:mm:ss"
    int     status = 0;      // 0=充电中 1=待结算（由 order_status 映射）
    double  kwh = 0;         // charged_energy_kwh
    double  amount = 0;      // current_cost
};

// 免密登录拿 token（正式登录页是 B 的，这里先直接用手机号登，NCS_PHONE 可换号）
bool devLogin(QString *err);

bool               findUnfinished(int userId, ActiveOrder *out);   // userId 仅为兼容界面签名
QList<StationOpt>  stationOptions();
QList<PileOpt>     freePiles(int stationId);

// TODO：UC-U-07 POST /charging/start  /  UC-U-08 遥测  /  UC-U-09 POST /charging/settle

} // namespace ChargeService

#endif // CHARGESERVICE_H
