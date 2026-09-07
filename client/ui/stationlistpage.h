#ifndef STATIONLISTPAGE_H
#define STATIONLISTPAGE_H
#include <QWidget>
#include "core/chargeservice.h"
class QListWidget; class QLabel; class QLineEdit; class QComboBox; class QNetworkAccessManager;
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
private:
    void locateByIp();
    void applyLocation(double latitude, double longitude, const QString &label);
    QListWidget *m_list; QLabel *m_tip; QLineEdit *m_address; QComboBox *m_region; QNetworkAccessManager *m_network;
};
#endif
