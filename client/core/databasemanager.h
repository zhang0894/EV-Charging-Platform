#ifndef DATABASEMANAGER_H
#define DATABASEMANAGER_H

#include <QString>

namespace DatabaseManager{
bool init(QString *err = nullptr);
QString dbPath();
}

#endif // DATABASEMANAGER_H
