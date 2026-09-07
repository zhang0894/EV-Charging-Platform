#include "ui/stationlistpage.h"

#include <QComboBox>
#include <QHBoxLayout>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QPushButton>
#include <QTimer>
#include <QUrlQuery>
#include <QVBoxLayout>
#include <algorithm>

namespace {
const QString MapKey = QStringLiteral("YUIBZ-VBM3U-EGRVW-GPKTQ-M6LEE-4EFQJ");

// 区域选择属于软件 GPS 模拟，不应依赖外部 HTTPS 请求；这样主界面可立即显示。
bool simulatedBeijingLocation(const QString &address, double *lat, double *lng)
{
    struct Location { const char *district; double lat; double lng; };
    static const Location locations[] = {
        {"海淀区", 39.9834, 116.3229}, {"西城区", 39.9120, 116.3668},
        {"东城区", 39.9288, 116.4160}, {"朝阳区", 39.9219, 116.4436},
        {"丰台区", 39.8636, 116.2869}, {"石景山区", 39.9146, 116.2230},
        {"通州区", 39.9025, 116.6564}, {"昌平区", 40.2208, 116.2312},
        {"大兴区", 39.7269, 116.3414}, {"顺义区", 40.1289, 116.6535},
        {"房山区", 39.7355, 116.1392}, {"门头沟区", 39.9406, 116.1014},
        {"怀柔区", 40.3160, 116.6372}, {"平谷区", 40.1441, 117.1127},
        {"密云区", 40.3763, 116.8434}, {"延庆区", 40.4569, 115.9746}
    };
    for (const auto &location : locations) {
        if (address.contains(QString::fromUtf8(location.district))) {
            *lat = location.lat; *lng = location.lng; return true;
        }
    }
    return false;
}
}

StationListPage::StationListPage(QWidget *p) : QWidget(p), m_network(new QNetworkAccessManager(this))
{
    auto *layout = new QVBoxLayout(this); layout->setContentsMargins(20, 24, 20, 14);
    auto *title = new QLabel(QStringLiteral("附近充电站")); title->setObjectName("H1"); layout->addWidget(title);
    auto *row = new QHBoxLayout;
    m_region = new QComboBox;
    m_region->addItems({QStringLiteral("距离最近"), QStringLiteral("空闲充电桩最多"), QStringLiteral("电价最便宜")});
    row->addWidget(m_region);
    m_address = new QLineEdit; m_address->setPlaceholderText(QStringLiteral("输入北京具体地址定位")); row->addWidget(m_address, 1);
    auto *locateButton = new QPushButton(QStringLiteral("定位")); row->addWidget(locateButton); layout->addLayout(row);
    m_tip = new QLabel; m_tip->setObjectName("Cap"); layout->addWidget(m_tip);
    m_list = new QListWidget; layout->addWidget(m_list, 1);
    connect(locateButton, &QPushButton::clicked, this, &StationListPage::locate);
    connect(m_address, &QLineEdit::returnPressed, this, &StationListPage::locate);
    connect(m_region, qOverload<int>(&QComboBox::currentIndexChanged), this, [this](int) { reload(); });
    // 卡片进入该站的充电桩详情；路线规划从详情页的“一键导航”进入。
    connect(m_list, &QListWidget::itemClicked, this, [this](QListWidgetItem *item) { emit stationSelected(item->data(Qt::UserRole).toInt()); });
    // 首次进入自动按公网 IP 获取城市级位置；失败时回退到默认位置。
    locateByIp();
}

void StationListPage::locateByIp()
{
    m_tip->setText(QStringLiteral("正在通过公网 IP 获取当前位置…"));
    // ip-api 返回国家、省、市和 lat/lon；IP 定位通常只能达到城市/区域级精度。
    // ipapi.co 在部分网络环境会触发 Cloudflare challenge，无法被 Qt 直接读取。
    auto *reply = m_network->get(QNetworkRequest(QUrl(QStringLiteral("http://ip-api.com/json/?fields=status,message,country,regionName,city,lat,lon"))));
    QTimer::singleShot(8000, reply, [reply] { if (!reply->isFinished()) reply->abort(); });
    connect(reply, &QNetworkReply::finished, this, [this, reply] {
        const QJsonObject data = QJsonDocument::fromJson(reply->readAll()).object();
        const double lat = data.value(QStringLiteral("lat")).toDouble();
        const double lng = data.value(QStringLiteral("lon")).toDouble();
        const QString city = data.value(QStringLiteral("city")).toString();
        const QString region = data.value(QStringLiteral("regionName")).toString();
        const bool ok = reply->error() == QNetworkReply::NoError && data.value(QStringLiteral("status")).toString() == QStringLiteral("success") && lat >= -90 && lat <= 90 && lng >= -180 && lng <= 180 && (lat != 0 || lng != 0);
        const bool timeout = reply->error() == QNetworkReply::OperationCanceledError;
        reply->deleteLater();
        if (!ok) {
            m_address->setText(QStringLiteral("海淀区"));
            applyLocation(39.9834, 116.3229, timeout ? QStringLiteral("海淀区（IP 定位超时，默认位置）") : QStringLiteral("海淀区（IP 定位失败，默认位置）"));
            return;
        }
        m_address->setText(city.isEmpty() ? region : city);
        ChargeService::setUserLocation(lat, lng);
        m_tip->setText(QStringLiteral("当前位置：纬度 %1，经度 %2")
                       .arg(lat, 0, 'f', 6).arg(lng, 0, 'f', 6));
        reload();
    });
}

void StationListPage::locate()
{
    const QString text = m_address->text().trimmed();
    if (text.isEmpty()) { m_tip->setText(QStringLiteral("请输入地址")); return; }
    double lat = 0, lng = 0;
    if (simulatedBeijingLocation(text, &lat, &lng)) { applyLocation(lat, lng, text + QStringLiteral("（模拟 GPS）")); return; }

    // 腾讯地图仅用于用户输入的具体地址，整个过程异步，超时也不会影响应用窗口。
    m_tip->setText(QStringLiteral("正在通过腾讯地图定位…"));
    QUrl url(QStringLiteral("https://apis.map.qq.com/ws/geocoder/v1/"));
    QUrlQuery query; query.addQueryItem("address", text);
    query.addQueryItem("key", qEnvironmentVariable("TENCENT_MAP_KEY", MapKey)); query.addQueryItem("output", "json"); url.setQuery(query);
    auto *reply = m_network->get(QNetworkRequest(url));
    QTimer::singleShot(8000, reply, [reply] { if (!reply->isFinished()) reply->abort(); });
    connect(reply, &QNetworkReply::finished, this, [this, reply, text] {
        const QJsonObject root = QJsonDocument::fromJson(reply->readAll()).object();
        const QJsonObject point = root.value("result").toObject().value("location").toObject();
        const bool ok = reply->error() == QNetworkReply::NoError && root.value("status").toInt(-1) == 0;
        const bool timeout = reply->error() == QNetworkReply::OperationCanceledError;
        reply->deleteLater();
        if (!ok) {
            double fallbackLat = 39.9834, fallbackLng = 116.3229;
            applyLocation(fallbackLat, fallbackLng, QStringLiteral("海淀区（默认位置）"));
            m_tip->setText(timeout ? QStringLiteral("地址解析超时，已使用默认位置") : QStringLiteral("地址解析失败，已使用默认位置"));
            return;
        }
        applyLocation(point.value("lat").toDouble(), point.value("lng").toDouble(), text);
    });
}

void StationListPage::applyLocation(double lat, double lng, const QString &label)
{
    ChargeService::setUserLocation(lat, lng); m_tip->setText(QStringLiteral("当前位置：%1").arg(label)); reload();
}

void StationListPage::reload()
{
    m_list->clear(); double lat, lng; ChargeService::userLocation(lat, lng);
    ChargeService::stationOptionsAtAsync(lat, lng, [this](QList<ChargeService::StationOpt> stations, const QString &error) {
        if (stations.isEmpty()) { m_tip->setText(error.isEmpty() ? QStringLiteral("当前定位附近暂无电站") : error); return; }
        switch (m_region->currentIndex()) {
        case 1: std::sort(stations.begin(), stations.end(), [](const auto &a, const auto &b) { return a.freeCount > b.freeCount; }); break;
        case 2: std::sort(stations.begin(), stations.end(), [](const auto &a, const auto &b) { return a.price < b.price; }); break;
        default: std::sort(stations.begin(), stations.end(), [](const auto &a, const auto &b) { return a.distanceKm < b.distanceKm; }); break;
        }
        for (const auto &station : stations) {
            auto *item = new QListWidgetItem(QStringLiteral("%1\n%2\n电价 %3 元/度   电桩 %4 台   空闲 %5 台\n距当前位置 %6 km · 点击进入电站详情").arg(station.name, station.address).arg(station.price, 0, 'f', 2).arg(station.totalCount).arg(station.freeCount).arg(station.distanceKm, 0, 'f', 1));
            item->setData(Qt::UserRole, station.id); item->setData(Qt::UserRole + 2, station.latitude); item->setData(Qt::UserRole + 3, station.longitude); item->setData(Qt::UserRole + 4, station.name); item->setSizeHint(QSize(0, 110)); m_list->addItem(item);
        }
    });
}
