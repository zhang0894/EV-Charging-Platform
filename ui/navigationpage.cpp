#include "ui/navigationpage.h"
#include "core/chargeservice.h"

#include <QComboBox>
#include <QDesktopServices>
#include <QLabel>
#include <QPushButton>
#include <QUrlQuery>
#include <QVBoxLayout>
#include <QtMath>
#ifdef NCS_HAS_WEBENGINE
#include <QWebEngineView>
#include <QWebEngineSettings>
#endif

namespace { const QString MapKey = QStringLiteral("WKXBZ-QOBLU-WZWV4-GA45R-GKEEQ-23BGU"); }

NavigationPage::NavigationPage(QWidget *p) : QWidget(p)
{
    auto *layout = new QVBoxLayout(this); m_layout = layout; layout->setContentsMargins(14, 14, 14, 10); layout->setSpacing(8);
    auto *back = new QPushButton(QStringLiteral("‹ 返回电站详情")); back->setObjectName("Ghost"); layout->addWidget(back);
    auto *title = new QLabel(QStringLiteral("路线规划")); title->setObjectName("H1"); layout->addWidget(title);
    m_mode = new QComboBox; m_mode->addItems({QStringLiteral("驾车"), QStringLiteral("步行"), QStringLiteral("公交")}); layout->addWidget(m_mode);
    m_summary = new QLabel; m_summary->setWordWrap(true); m_summary->setObjectName("Card"); m_summary->setMargin(10); layout->addWidget(m_summary);
#ifdef NCS_HAS_WEBENGINE
    // WebEngine 内核很重，导航页创建时不立刻启动；第一次真正规划路线时再创建。
    m_mapPlaceholder = new QLabel(QStringLiteral("路线地图将在打开导航后加载…"), this);
    m_mapPlaceholder->setAlignment(Qt::AlignCenter); m_mapPlaceholder->setObjectName("Card"); layout->addWidget(m_mapPlaceholder, 1);
#else
    m_map = new QLabel(QStringLiteral("当前环境使用安全导航模式。\n可在系统浏览器中打开腾讯地图路线规划。"));
    m_mapPlaceholder = nullptr;
    m_map->setWordWrap(true); m_map->setAlignment(Qt::AlignCenter); layout->addWidget(m_map, 1);
#endif
    m_openBrowser = new QPushButton(QStringLiteral("在系统浏览器中打开腾讯地图")); layout->addWidget(m_openBrowser);
    connect(back, &QPushButton::clicked, this, &NavigationPage::backRequested);
    connect(m_mode, qOverload<int>(&QComboBox::currentIndexChanged), this, &NavigationPage::loadRoute);
    connect(m_openBrowser, &QPushButton::clicked, this, [this] { if (m_routeUrl.isValid()) QDesktopServices::openUrl(m_routeUrl); });
}

void NavigationPage::ensureMapView()
{
#ifdef NCS_HAS_WEBENGINE
    if (m_map) return;
    m_map = new QWebEngineView(this);
    m_map->setMinimumHeight(330);
    m_map->setFocusPolicy(Qt::StrongFocus);
    m_map->settings()->setAttribute(QWebEngineSettings::JavascriptEnabled, true);
    m_map->settings()->setAttribute(QWebEngineSettings::LocalContentCanAccessRemoteUrls, true);
    if (m_mapPlaceholder) { m_layout->replaceWidget(m_mapPlaceholder, m_map); m_layout->setStretchFactor(m_map, 1); m_mapPlaceholder->deleteLater(); m_mapPlaceholder = nullptr; }
    // 腾讯 URI 页在小窗口下会裁切内容；加载完成后明确允许其页面本身滚动。
    connect(m_map, &QWebEngineView::loadFinished, m_map, [this](bool ok) {
        if (ok) m_map->page()->runJavaScript(QStringLiteral("document.documentElement.style.overflow='auto';document.body.style.overflow='auto';"));
    });
#endif
}

void NavigationPage::setDestination(double lat, double lng, const QString &name)
{
    m_lat = lat; m_lng = lng; m_name = name; loadRoute();
}

void NavigationPage::loadRoute()
{
    if (m_name.isEmpty()) return;
    double fromLat, fromLng; ChargeService::userLocation(fromLat, fromLng);
    const QString type = m_mode->currentIndex() == 0 ? QStringLiteral("drive") : m_mode->currentIndex() == 1 ? QStringLiteral("walk") : QStringLiteral("bus");
    const QString modeText = m_mode->currentText();
    const double lat1 = qDegreesToRadians(fromLat), lat2 = qDegreesToRadians(m_lat);
    const double a = qPow(qSin((lat2 - lat1) / 2), 2) + qCos(lat1) * qCos(lat2) * qPow(qSin(qDegreesToRadians(m_lng - fromLng) / 2), 2);
    const double straightKm = 6371.0 * 2 * qAtan2(qSqrt(a), qSqrt(1 - a));
    m_summary->setText(QStringLiteral("起点：当前位置（%1，%2）\n终点：%3（%4，%5）\n出行方式：%6\n直线距离：%7 km")
        .arg(fromLat, 0, 'f', 5).arg(fromLng, 0, 'f', 5).arg(m_name).arg(m_lat, 0, 'f', 5).arg(m_lng, 0, 'f', 5).arg(modeText).arg(straightKm, 0, 'f', 1));
    QUrl url(QStringLiteral("https://apis.map.qq.com/uri/v1/routeplan")); QUrlQuery query;
    query.addQueryItem("type", type); query.addQueryItem("fromcoord", QStringLiteral("%1,%2").arg(fromLat, 0, 'f', 6).arg(fromLng, 0, 'f', 6));
    query.addQueryItem("tocoord", QStringLiteral("%1,%2").arg(m_lat, 0, 'f', 6).arg(m_lng, 0, 'f', 6)); query.addQueryItem("to", m_name); query.addQueryItem("referer", qEnvironmentVariable("TENCENT_MAP_KEY", MapKey)); url.setQuery(query); m_routeUrl = url;
#ifdef NCS_HAS_WEBENGINE
    ensureMapView();
    m_map->setUrl(m_routeUrl);
#endif
}
