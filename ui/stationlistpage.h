#ifndef STATIONLISTPAGE_H
#define STATIONLISTPAGE_H
#include <QWidget>
#include "core/chargeservice.h"
class QListWidget; class QLabel; class QLineEdit; class QComboBox; class QNetworkAccessManager; class QPushButton; class QTimer; class StationMapCard;
class StationListPage : public QWidget {
    Q_OBJECT
public:
    explicit StationListPage(QWidget *parent = nullptr);
signals:
    void stationSelected(int stationId);
    void navigationRequested(const ChargeService::StationOpt &station);
    void navigationRequested(double latitude, double longitude, const QString &name);
private slots:
    void reload();
    void locate();
    void loadMore();
private:
    void locateByIp();
    void applyLocation(double latitude, double longitude, const QString &label);
    void requestPage(bool reset);
    void refreshVisibleStations();
    void renderStations();
    QListWidget *m_list; QLabel *m_tip; QLineEdit *m_address; QComboBox *m_region; QNetworkAccessManager *m_network;
    StationMapCard *m_map; QPushButton *m_more;
    QTimer *m_refreshTimer;
    QList<ChargeService::StationOpt> m_stations;
    int m_page = 0; bool m_loading = false; bool m_hasMore = true; bool m_geocoding = false;
};
#endif
