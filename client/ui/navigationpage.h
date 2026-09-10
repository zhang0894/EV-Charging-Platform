#ifndef NAVIGATIONPAGE_H
#define NAVIGATIONPAGE_H

#include <QWidget>
#include <QUrl>
class QLabel; class QComboBox; class QPushButton; class QVBoxLayout;
#ifdef NCS_HAS_WEBENGINE
class QWebEngineView;
#endif
class NavigationPage: public QWidget {
    Q_OBJECT
public:
    explicit NavigationPage(QWidget *parent=nullptr);
public slots:
    void setDestination(double lat,double lng,const QString &name);
signals:
    void backRequested();
private:
    void ensureMapView();
    void loadRoute();
    double m_lat=0,m_lng=0; QString m_name; QUrl m_routeUrl;
    QLabel *m_summary; QComboBox *m_mode; QPushButton *m_openBrowser;
    QVBoxLayout *m_layout; QLabel *m_mapPlaceholder;
#ifdef NCS_HAS_WEBENGINE
 QWebEngineView *m_map = nullptr;
#else
 QLabel *m_map;
#endif
};
#endif
