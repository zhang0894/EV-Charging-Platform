#ifndef STATIONMAPBRIDGE_H
#define STATIONMAPBRIDGE_H

#include <QObject>
#include <functional>

class StationMapBridge final : public QObject
{
    Q_OBJECT
public:
    explicit StationMapBridge(std::function<void(int)> stationClicked, QObject *parent = nullptr)
        : QObject(parent), m_stationClicked(std::move(stationClicked)) {}
public slots:
    void openStation(int stationId) { if (stationId > 0) m_stationClicked(stationId); }
private:
    std::function<void(int)> m_stationClicked;
};

#endif
