#include "core/databasemanager.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSqlDatabase>
#include <QSqlError>
#include <QSqlQuery>
#include <QTextStream>

namespace {

// 去掉一行里的 "--" 注释（引号内的 "--" 不算注释）
QString stripLineComment(const QString &line)
{
    bool inString = false;
    for (int i = 0; i + 1 < line.size(); ++i) {
        if (line[i] == QLatin1Char('\''))
            inString = !inString;
        else if (!inString && line[i] == QLatin1Char('-') && line[i + 1] == QLatin1Char('-'))
            return line.left(i);
    }
    return line;
}

// 执行 qrc 里的 SQL 脚本：逐行去注释 → 按 ; 切成单条语句 → 逐条执行
bool runScript(const QString &resPath, QString *err)
{
    QFile f(resPath);
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) {
        if (err) *err = QStringLiteral("无法读取脚本 %1").arg(resPath);
        return false;
    }
    QTextStream in(&f);
#if QT_VERSION < QT_VERSION_CHECK(6, 0, 0)
    in.setCodec("UTF-8");
#endif
    QString cleaned;
    while (!in.atEnd())
        cleaned += stripLineComment(in.readLine()) + QLatin1Char('\n');

    QSqlQuery q;
    for (const QString &raw : cleaned.split(QLatin1Char(';'))) {
        const QString stmt = raw.trimmed();
        if (stmt.isEmpty())
            continue;
        if (!q.exec(stmt)) {
            if (err)
                *err = QStringLiteral("%1 执行失败：%2\n语句：%3")
                           .arg(resPath, q.lastError().text(), stmt.left(120));
            return false;
        }
    }
    return true;
}

} // namespace

namespace DatabaseManager {

QString dbPath()
{
    const QString env = qEnvironmentVariable("NCS_DB");
    if (!env.isEmpty())
        return env;
    return QDir::homePath() + QStringLiteral("/.evcharge-ncs/evcharge.db");
}

bool init(QString *err)
{
    const QString path = dbPath();
    QDir().mkpath(QFileInfo(path).absolutePath());
    const bool existed = QFile::exists(path);

    QSqlDatabase db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"));
    db.setDatabaseName(path);
    if (!db.open()) {
        if (err) *err = QStringLiteral("打开数据库失败：%1").arg(db.lastError().text());
        return false;
    }

    QSqlQuery q;
    q.exec(QStringLiteral("PRAGMA foreign_keys = ON"));    // SQLite 默认不查外键
    q.exec(QStringLiteral("PRAGMA journal_mode = WAL"));   // 两个客户端同时读写
    q.exec(QStringLiteral("PRAGMA busy_timeout = 3000"));

    if (!existed) {
        if (!runScript(QStringLiteral(":/db/01_schema.sql"), err))
            return false;
        if (!runScript(QStringLiteral(":/db/02_seed.sql"), err))
            return false;
    }
    return true;
}

} // namespace DatabaseManager