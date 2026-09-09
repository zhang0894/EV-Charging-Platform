#include "ui/stationlistpage.h"

#include <QComboBox>
#include <QCryptographicHash>
#include <QBrush>
#include <QColor>
#include <QHBoxLayout>
#include <QJsonDocument>
#include <QJsonArray>
#include <QJsonObject>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QNetworkAccessManager>
#include <QNetworkProxy>
#include <QNetworkReply>
#include <QPushButton>
#include <QPainter>
#include <QRegularExpression>
#include <QSettings>
#include <QTimer>
#include <QUrlQuery>
#include <QVBoxLayout>
#include <algorithm>
#include <memory>
#ifdef NCS_HAS_WEBENGINE
#include <QWebEngineView>
#include <QWebEngineSettings>
#endif

namespace {
// 项目统一使用此腾讯地图 Key（地图展示与地址地理编码）。
const QStringList MapKeys = {
    QStringLiteral("WKXBZ-QOBLU-WZWV4-GA45R-GKEEQ-23BGU")
};

QString addressCacheKey(const QString &address)
{
    return QString::fromLatin1(QCryptographicHash::hash(address.trimmed().toUtf8(),
                                                          QCryptographicHash::Sha256).toHex());
}

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

// server 的电站数据只有北京市；IP 出口在外地/开代理时坐标会跑到北京以外，
// 这时按"距离最近"排出来全是几十公里外的边缘站，所以退回默认位置
bool insideBeijing(double lat, double lng)
{
    return lat >= 39.3 && lat <= 41.1 && lng >= 115.3 && lng <= 117.6;
}

} // namespace

// 不依赖外部地图瓦片的轻量示意图：红点是当前位置，绿色点为已加载电站。
class StationMapCard final : public QWidget
{
public:
    explicit StationMapCard(QWidget *parent = nullptr) : QWidget(parent) {
        setMinimumHeight(190); setObjectName("StationMap");
#ifdef NCS_HAS_WEBENGINE
        auto *layout = new QVBoxLayout(this); layout->setContentsMargins(0, 0, 0, 0);
        m_view = new QWebEngineView(this); m_view->setFocusPolicy(Qt::StrongFocus);
        m_view->settings()->setAttribute(QWebEngineSettings::JavascriptEnabled, true);
        m_view->settings()->setAttribute(QWebEngineSettings::LocalContentCanAccessRemoteUrls, true);
        layout->addWidget(m_view);
        // 在首批电站请求返回前也先装载地图，避免 WebEngine 空白视图。
        setData(m_lat, m_lng, {});
#endif
    }
    void setData(double latitude, double longitude, const QList<ChargeService::StationOpt> &stations) {
        m_lat=latitude; m_lng=longitude; m_stations=stations;
#ifdef NCS_HAS_WEBENGINE
        QJsonArray points;
        for (const auto &station : stations) points.append(QJsonObject{{"lat", station.latitude}, {"lng", station.longitude}, {"online", station.isOnline}, {"name", station.name}});
        const QString markerData = QString::fromUtf8(QJsonDocument(points).toJson(QJsonDocument::Compact));
        const QString html = QStringLiteral(R"(
<!doctype html><html><head><meta charset="utf-8"><style>html,body,#map{margin:0;width:100%;height:100%;overflow:hidden}</style></head>
<body><div id="map"></div><div id="error" style="position:absolute;inset:0;display:flex;align-items:center;justify-content:center;background:#edf5f0;color:#65736d;font:14px sans-serif">正在加载真实地图…</div><script>
const centerLat=%1, centerLng=%2, data=%3, errorBox=document.getElementById('error');
function showError(message){document.getElementById('map').style.display='none';errorBox.textContent=message;}
function createMap(){try{
  if(typeof TMap==='undefined'){showError('真实地图脚本未加载，请检查网络或地图 Key。');return;}
  const center=new TMap.LatLng(centerLat,centerLng);const map=new TMap.Map(document.getElementById('map'),{center:center,zoom:13,viewMode:'2D',pitch:0});
  new TMap.MultiMarker({map:map,styles:{user:new TMap.MarkerStyle({width:26,height:34,anchor:{x:13,y:34},color:'#D93025'})},geometries:[{id:'user',styleId:'user',position:center,title:'当前位置'}]});
  const geometries=data.map((s,i)=>({id:'station'+i,styleId:s.online?'online':'offline',position:new TMap.LatLng(s.lat,s.lng),title:s.name}));
  new TMap.MultiMarker({map:map,styles:{online:new TMap.MarkerStyle({width:18,height:24,anchor:{x:9,y:24},color:'#148B5B'}),offline:new TMap.MarkerStyle({width:18,height:24,anchor:{x:9,y:24},color:'#969E9A'})},geometries:geometries});
  errorBox.style.display='none';
}catch(e){showError('真实地图初始化失败：'+e.message);}}
const script=document.createElement('script');script.charset='utf-8';script.src='https://map.qq.com/api/gljs?v=1.exp&key=WKXBZ-QOBLU-WZWV4-GA45R-GKEEQ-23BGU';script.onload=createMap;script.onerror=()=>showError('真实地图脚本加载失败，请检查网络或地图 Key。');document.head.appendChild(script);
setTimeout(()=>{if(typeof TMap==='undefined')showError('真实地图加载超时，请检查网络或地图 Key。');},8000);
</script></body></html>)").arg(latitude, 0, 'f', 7).arg(longitude, 0, 'f', 7).arg(markerData);
        m_view->setHtml(html, QUrl(QStringLiteral("https://map.qq.com/")));
#else
        update();
#endif
    }
protected:
    void paintEvent(QPaintEvent *) override {
#ifdef NCS_HAS_WEBENGINE
        return;
#else
        QPainter p(this); p.setRenderHint(QPainter::Antialiasing);
        p.fillRect(rect(), QColor("#EAF4EF"));
        p.setPen(QPen(QColor("#D2E4DA"), 1));
        for (int x=0; x<width(); x+=32) p.drawLine(x, 0, x, height());
        for (int y=0; y<height(); y+=32) p.drawLine(0, y, width(), y);
        double minLat=m_lat, maxLat=m_lat, minLng=m_lng, maxLng=m_lng;
        for (const auto &s : m_stations) { minLat=qMin(minLat,s.latitude); maxLat=qMax(maxLat,s.latitude); minLng=qMin(minLng,s.longitude); maxLng=qMax(maxLng,s.longitude); }
        const double latSpan=qMax(0.012, maxLat-minLat), lngSpan=qMax(0.012, maxLng-minLng);
        auto point = [&](double lat,double lng) { return QPointF(18+(lng-(minLng-lngSpan*.16))/(lngSpan*1.32)*(width()-36), height()-18-(lat-(minLat-latSpan*.16))/(latSpan*1.32)*(height()-36)); };
        for (const auto &s : m_stations) { const QPointF pos=point(s.latitude,s.longitude); p.setPen(Qt::white); p.setBrush(s.isOnline ? QColor("#148B5B") : QColor("#969E9A")); p.drawEllipse(pos, 6, 6); }
        const QPointF me=point(m_lat,m_lng); p.setPen(QPen(Qt::white,2)); p.setBrush(QColor("#D93025")); p.drawEllipse(me, 8, 8);
        p.setPen(QColor("#52635B")); p.drawText(QRect(12,9,width()-24,22), Qt::AlignLeft|Qt::AlignVCenter, QStringLiteral("● 当前位置    ● 附近充电站（绿色）"));
#endif
    }
private:
    double m_lat=39.9834,m_lng=116.3229; QList<ChargeService::StationOpt> m_stations;
#ifdef NCS_HAS_WEBENGINE
    QWebEngineView *m_view = nullptr;
#endif
};

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
    m_tip = new QLabel; m_tip->setObjectName("Cap"); m_tip->setWordWrap(true); layout->addWidget(m_tip);
    m_map = new StationMapCard(this); layout->addWidget(m_map);
    m_list = new QListWidget; layout->addWidget(m_list, 1);
    m_more = new QPushButton(QStringLiteral("加载更多电站"), this); m_more->setObjectName("Ghost"); m_more->setEnabled(false); layout->addWidget(m_more);
    connect(locateButton, &QPushButton::clicked, this, &StationListPage::locate);
    connect(m_address, &QLineEdit::returnPressed, this, &StationListPage::locate);
    connect(m_region, qOverload<int>(&QComboBox::currentIndexChanged), this, [this](int) { renderStations(); });
    connect(m_more, &QPushButton::clicked, this, &StationListPage::loadMore);
    // 卡片进入该站的充电桩详情；路线规划从详情页的“一键导航”进入。
    connect(m_list, &QListWidget::itemClicked, this, [this](QListWidgetItem *item) { emit stationSelected(item->data(Qt::UserRole).toInt()); });
    // 默认位置固定为项目演示坐标，不再请求公网 IP，列表立即按此位置排序。
    applyLocation(39.735139, 116.169754, QStringLiteral("116.169754°，39.735139°"));

    // 管理端可随时上下线电站。列表可见时轮询已加载的分页，避免旧缓存
    // 继续把已下线站显示为可用（或反过来）。页面隐藏时不请求服务器。
    m_refreshTimer = new QTimer(this);
    m_refreshTimer->setInterval(5000);
    connect(m_refreshTimer, &QTimer::timeout, this, &StationListPage::refreshVisibleStations);
    m_refreshTimer->start();
}

void StationListPage::locateByIp()
{
    m_network->setProxy(QNetworkProxy::NoProxy);
    m_tip->setText(QStringLiteral("正在通过公网 IP 获取当前位置…"));
    // ip-api 返回国家、省、市和 lat/lon；IP 定位通常只能达到城市/区域级精度。
    // ipapi.co 在部分网络环境会触发 Cloudflare challenge，无法被 Qt 直接读取。
    // NCS_IPAPI_URL 可换成本地假接口，用来测试"定位在北京以外"的分支
    const QString ipApi = qEnvironmentVariable("NCS_IPAPI_URL",
        QStringLiteral("http://ip-api.com/json/?fields=status,message,country,regionName,city,lat,lon"));
    auto *reply = m_network->get(QNetworkRequest(QUrl(ipApi)));
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
        if (!insideBeijing(lat, lng)) {
            m_address->setText(QStringLiteral("海淀区"));
            applyLocation(39.9834, 116.3229, QStringLiteral("海淀区（IP 定位在北京以外，默认位置）"));
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
    if (m_geocoding) {
        m_tip->setText(QStringLiteral("正在解析地址，请稍候…"));
        return;
    }
    const QString text = m_address->text().trimmed();
    if (text.isEmpty()) { m_tip->setText(QStringLiteral("请输入地址")); return; }
    // 允许直接输入“经度°，纬度°”坐标，不必把坐标交给地址接口解析。
    static const QRegularExpression coordRe(
        QStringLiteral(R"(^\s*([+-]?\d+(?:\.\d+)?)\s*[°º度]?\s*[,，、\s]\s*([+-]?\d+(?:\.\d+)?)\s*[°º度]?\s*$)"));
    const auto coordMatch = coordRe.match(text);
    if (coordMatch.hasMatch()) {
        const double first = coordMatch.captured(1).toDouble();
        const double second = coordMatch.captured(2).toDouble();
        // 输入框约定为“经度，纬度”；同时兼容“纬度，经度”的明显范围。
        const double lng = qAbs(first) > 90 ? first : second;
        const double lat = qAbs(first) > 90 ? second : first;
        if (insideBeijing(lat, lng)) {
            applyLocation(lat, lng, text + QStringLiteral("（坐标）"));
            return;
        }
    }
    double lat = 0, lng = 0;
    if (simulatedBeijingLocation(text, &lat, &lng)) { applyLocation(lat, lng, text + QStringLiteral("（模拟 GPS）")); return; }

    QSettings cache; cache.beginGroup(QStringLiteral("map_geocode_cache"));
    const QString cached = cache.value(addressCacheKey(text)).toString(); cache.endGroup();
    const QStringList cachedPoint = cached.split(QLatin1Char(','));
    if (cachedPoint.size() == 2) {
        bool latOk = false, lngOk = false;
        const double cachedLat = cachedPoint[0].toDouble(&latOk), cachedLng = cachedPoint[1].toDouble(&lngOk);
        if (latOk && lngOk && insideBeijing(cachedLat, cachedLng)) {
            applyLocation(cachedLat, cachedLng, text + QStringLiteral("（已缓存）")); return;
        }
    }

    // 腾讯地图仅用于用户输入的具体地址，整个过程异步，超时也不会影响应用窗口。
    m_tip->setText(QStringLiteral("正在通过腾讯地图定位…"));
    const QStringList keys = MapKeys;  // 地址解析固定使用项目配置的 WKXBZ... Key。
    const int firstKey = 0;
    m_geocoding = true;
    auto requestGeocode = std::make_shared<std::function<void(int)>>();
    *requestGeocode = [this, text, keys, requestGeocode](int keyIndex) {
        QUrl url(QStringLiteral("https://apis.map.qq.com/ws/geocoder/v1/")); QUrlQuery query;
        query.addQueryItem(QStringLiteral("address"), text);
        query.addQueryItem(QStringLiteral("key"), keys.at(keyIndex));
        query.addQueryItem(QStringLiteral("output"), QStringLiteral("json"));
        url.setQuery(query);
        m_network->setProxy(QNetworkProxy::NoProxy);
        auto *reply = m_network->get(QNetworkRequest(url));
        QTimer::singleShot(8000, reply, [reply] { if (!reply->isFinished()) reply->abort(); });
        connect(reply, &QNetworkReply::finished, this, [this, reply, text, keys, keyIndex, requestGeocode] {
            const QJsonObject root = QJsonDocument::fromJson(reply->readAll()).object();
            const QJsonObject point = root.value("result").toObject().value("location").toObject();
            const int status = root.value("status").toInt(-1); const QString message = root.value("message").toString();
            const QString networkError = reply->errorString(); const bool timeout = reply->error() == QNetworkReply::OperationCanceledError;
            const double latitude = point.value("lat").toDouble(), longitude = point.value("lng").toDouble();
            const bool ok = reply->error() == QNetworkReply::NoError && status == 0 && insideBeijing(latitude, longitude);
            reply->deleteLater();
            if (ok) {
                m_geocoding = false;
                QSettings cache; cache.beginGroup(QStringLiteral("map_geocode_cache"));
                cache.setValue(addressCacheKey(text), QStringLiteral("%1,%2").arg(latitude, 0, 'f', 8).arg(longitude, 0, 'f', 8)); cache.endGroup();
                applyLocation(latitude, longitude, text); return;
            }
            // 地理编码接口对“天安门”等 POI 简称会返回 348（参数错误）。
            // 改用地点搜索接口按北京市范围检索 POI，避免把合法地点误判为失败。
            if (status == 348) {
                m_geocoding = true; // POI 检索仍在进行，继续拦住重复点击。
                QUrl placeUrl(QStringLiteral("https://apis.map.qq.com/ws/place/v1/search"));
                QUrlQuery placeQuery;
                placeQuery.addQueryItem(QStringLiteral("keyword"), text);
                placeQuery.addQueryItem(QStringLiteral("boundary"), QStringLiteral("region(北京市,0)"));
                placeQuery.addQueryItem(QStringLiteral("page_size"), QStringLiteral("1"));
                placeQuery.addQueryItem(QStringLiteral("key"), keys.at(keyIndex));
                placeQuery.addQueryItem(QStringLiteral("output"), QStringLiteral("json"));
                placeUrl.setQuery(placeQuery);
                auto *placeReply = m_network->get(QNetworkRequest(placeUrl));
                QTimer::singleShot(8000, placeReply, [placeReply] { if (!placeReply->isFinished()) placeReply->abort(); });
                connect(placeReply, &QNetworkReply::finished, this, [this, placeReply, text] {
                    const QJsonObject placeRoot = QJsonDocument::fromJson(placeReply->readAll()).object();
                    const QJsonArray data = placeRoot.value(QStringLiteral("data")).toArray();
                    const QJsonObject location = data.at(0).toObject().value(QStringLiteral("location")).toObject();
                    const double lat = location.value(QStringLiteral("lat")).toDouble();
                    const double lng = location.value(QStringLiteral("lng")).toDouble();
                    const bool found = placeReply->error() == QNetworkReply::NoError
                        && placeRoot.value(QStringLiteral("status")).toInt(-1) == 0
                        && insideBeijing(lat, lng);
                    placeReply->deleteLater();
                    if (found) {
                        QSettings cache; cache.beginGroup(QStringLiteral("map_geocode_cache"));
                        cache.setValue(addressCacheKey(text), QStringLiteral("%1,%2").arg(lat, 0, 'f', 8).arg(lng, 0, 'f', 8)); cache.endGroup();
                        applyLocation(lat, lng, text); m_geocoding = false; return;
                    }
                    m_geocoding = false;
                    applyLocation(39.9834, 116.3229, QStringLiteral("海淀区（默认位置）"));
                    m_tip->setText(QStringLiteral("未找到“%1”，已使用默认位置").arg(text));
                });
                return;
            }
            m_geocoding = false;
            applyLocation(39.9834, 116.3229, QStringLiteral("海淀区（默认位置）"));
            if (status == 121) m_tip->setText(QStringLiteral("腾讯地图返回 121：%1，请稍后重试").arg(message));
            else if (timeout) m_tip->setText(QStringLiteral("地址解析超时，已使用默认位置"));
            else if (!message.isEmpty()) m_tip->setText(QStringLiteral("腾讯地图解析失败（%1）：%2").arg(status).arg(message));
            else if (!networkError.isEmpty()) m_tip->setText(QStringLiteral("地址解析网络错误：%1").arg(networkError));
            else m_tip->setText(QStringLiteral("地址解析失败：腾讯地图未返回有效坐标或地址不在北京"));
        });
    };
    (*requestGeocode)(firstKey);
}

void StationListPage::applyLocation(double lat, double lng, const QString &label)
{
    ChargeService::setUserLocation(lat, lng); m_tip->setText(QStringLiteral("当前位置：%1").arg(label)); reload();
}

void StationListPage::reload()
{
    requestPage(true);
}

void StationListPage::loadMore() { requestPage(false); }

void StationListPage::refreshVisibleStations()
{
    if (!isVisible() || m_loading || m_page <= 0)
        return;

    // 已点击“加载更多”时，也同步所有当前展示页，保证页面内每一张卡片的
    // is_online 都来自最新服务器回包，而不是只更新前 20 条。
    m_loading = true;
    const int pageCount = m_page;
    double lat, lng;
    ChargeService::userLocation(lat, lng);
    auto refreshed = std::make_shared<QList<ChargeService::StationOpt>>();
    auto refreshPage = std::make_shared<std::function<void(int)>>();
    *refreshPage = [this, pageCount, lat, lng, refreshed, refreshPage](int page) {
        ChargeService::stationOptionsAtAsync(lat, lng,
            [this, pageCount, lat, lng, refreshed, refreshPage, page](
                const QList<ChargeService::StationOpt> &stations, const QString &error) {
                if (!error.isEmpty()) {
                    m_loading = false;  // 保留当前画面；下一个 5 秒周期会重试。
                    return;
                }
                *refreshed += stations;
                if (page < pageCount) {
                    (*refreshPage)(page + 1);
                    return;
                }
                m_loading = false;
                m_stations = *refreshed;
                m_hasMore = stations.size() == 20;
                renderStations();
                // 状态轮询只更新电站卡片。地图是位置概览，不因上下线状态
                // 变化而重载 WebEngine，否则每 5 秒会闪烁并重复加载底图。
                m_more->setVisible(m_hasMore);
                m_more->setEnabled(m_hasMore);
            }, page, 20);
    };
    (*refreshPage)(1);
}

void StationListPage::requestPage(bool reset)
{
    if (m_loading || (!reset && !m_hasMore)) return;
    if (reset) { m_page = 0; m_hasMore = true; m_stations.clear(); m_list->clear(); }
    m_loading = true; m_more->setEnabled(false); m_more->setText(QStringLiteral("正在加载…"));
    double lat, lng; ChargeService::userLocation(lat, lng);
    const int requestedPage = m_page + 1;
    ChargeService::stationOptionsAtAsync(lat, lng, [this, requestedPage, lat, lng](const QList<ChargeService::StationOpt> &stations, const QString &error) {
        m_loading = false;
        if (!error.isEmpty()) { m_tip->setText(error); m_more->setText(QStringLiteral("加载更多电站")); m_more->setEnabled(m_hasMore); return; }
        m_page = requestedPage; m_hasMore = stations.size() == 20;
        m_stations += stations; renderStations(); m_map->setData(lat, lng, m_stations);
        m_more->setVisible(m_hasMore); m_more->setText(QStringLiteral("加载更多电站")); m_more->setEnabled(m_hasMore);
        if (m_stations.isEmpty()) m_tip->setText(QStringLiteral("当前定位附近暂无电站"));
    }, requestedPage, 20);
}

void StationListPage::renderStations()
{
    QList<ChargeService::StationOpt> stations = m_stations;
    switch (m_region->currentIndex()) {
    case 1: std::sort(stations.begin(), stations.end(), [](const auto &a, const auto &b) { return a.freeCount > b.freeCount; }); break;
    case 2: std::sort(stations.begin(), stations.end(), [](const auto &a, const auto &b) { return a.price < b.price; }); break;
    default: std::sort(stations.begin(), stations.end(), [](const auto &a, const auto &b) { return a.distanceKm < b.distanceKm; }); break;
    }
    m_list->clear();
    const int count = stations.size();
    for (int index = 0; index < count; ++index) {
        const auto &station = stations.at(index);
        auto *item = new QListWidgetItem(m_list);
        item->setData(Qt::UserRole, station.id); item->setSizeHint(QSize(0, 164));
        auto *card = new QWidget(m_list);
        auto *row = new QHBoxLayout(card); row->setContentsMargins(14, 11, 14, 11); row->setSpacing(10);
        auto *details = new QVBoxLayout; details->setSpacing(4);
        auto *name = new QLabel(station.name, card); name->setStyleSheet(QStringLiteral("font-size:15px; font-weight:700;")); name->setWordWrap(true); name->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed); name->setMinimumHeight(44); details->addWidget(name);
        auto *address = new QLabel(station.address, card); address->setObjectName("Cap"); address->setWordWrap(true); details->addWidget(address);
        auto *stats = new QLabel(QStringLiteral("电价 %1 元/度   电桩 %2 台   空闲 %3 台").arg(station.price, 0, 'f', 2).arg(station.totalCount).arg(station.freeCount), card);
        stats->setObjectName("Cap"); details->addWidget(stats); row->addLayout(details, 1);
        auto *distance = new QLabel(QStringLiteral("距离\n%1 km").arg(station.distanceKm, 0, 'f', 1), card);
        distance->setAlignment(Qt::AlignCenter); distance->setFixedSize(70, 56);
        distance->setStyleSheet(QStringLiteral("background:#EAF6EF; color:#0E8A57; border:1px solid #B8DCCB; border-radius:10px; font-weight:700;"));
        row->addWidget(distance, 0, Qt::AlignVCenter);
        m_list->setItemWidget(item, card);
        if (!station.isOnline) {
            // 自定义 itemWidget 会覆盖 QListWidget::item 的背景，因此整张卡片需在这里显式灰显。
            card->setStyleSheet(QStringLiteral("background:#E8ECEA; border:1px solid #CDD4D0; border-radius:10px;"));
            item->setBackground(QBrush(QColor("#E8ECEA")));
            item->setForeground(QBrush(QColor("#8A9290")));
            name->setStyleSheet(QStringLiteral("font-size:15px; font-weight:700; color:#8A9290;"));
            address->setStyleSheet(QStringLiteral("color:#8A9290;")); stats->setStyleSheet(QStringLiteral("color:#8A9290;"));
            distance->setStyleSheet(QStringLiteral("background:#E8ECEA; color:#8A9290; border:1px solid #CDD4D0; border-radius:10px; font-weight:700;"));
        }
    }
}
