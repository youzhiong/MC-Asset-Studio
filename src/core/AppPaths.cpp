#include "core/AppPaths.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QStandardPaths>

namespace mcstudio {
namespace AppPaths {
namespace {

QString g_dataRoot;

bool ensureDir(const QString &path)
{
    return QDir().mkpath(path);
}

bool canWrite(const QString &dir)
{
    QFile probe(QDir(dir).filePath(QStringLiteral(".mcstudio-write-test")));
    if (!probe.open(QIODevice::WriteOnly))
        return false;
    probe.close();
    probe.remove();
    return true;
}

} // namespace

QString appDir()
{
    return QCoreApplication::applicationDirPath();
}

QString dataRoot()
{
    if (!g_dataRoot.isEmpty())
        return g_dataRoot;

    // Portable layout: keep imported working copies and the lightweight source
    // manifest next to the executable, not inside the resource pack.
    const QString portable = QDir(appDir()).filePath(QStringLiteral("data"));
    if (ensureDir(portable) && canWrite(portable)) {
        g_dataRoot = portable;
        return g_dataRoot;
    }

    // Fallback for read-only installs such as Program Files.
    QString fallback = QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation);
    if (fallback.isEmpty())
        fallback = QDir::homePath() + QStringLiteral("/.mcstudio");
    fallback = QDir(fallback).filePath(QStringLiteral("data"));
    ensureDir(fallback);
    g_dataRoot = fallback;
    return g_dataRoot;
}

QString projectsRoot()
{
    return QDir(dataRoot()).filePath(QStringLiteral("projects"));
}

QString cacheRoot()
{
    return QDir(dataRoot()).filePath(QStringLiteral("cache"));
}

QString sourceCacheRoot()
{
    return QDir(cacheRoot()).filePath(QStringLiteral("sources"));
}

QString sessionsRoot()
{
    return QDir(cacheRoot()).filePath(QStringLiteral("sessions"));
}

QString layoutsRoot()
{
    return QDir(cacheRoot()).filePath(QStringLiteral("layouts"));
}

QString sessionFile(const QString &projectId)
{
    return QDir(sessionsRoot()).filePath(projectId + QStringLiteral(".json"));
}

QString gameRoot()
{
    return QDir(dataRoot()).filePath(QStringLiteral("game"));
}

QString gameRootsRoot()
{
    return QDir(gameRoot()).filePath(QStringLiteral("roots"));
}

QString gameInstancesRoot()
{
    return QDir(gameRoot()).filePath(QStringLiteral("instances"));
}

QString gameModsRoot()
{
    return QDir(gameRoot()).filePath(QStringLiteral("mods"));
}

QString gameJarsRoot()
{
    return QDir(gameRoot()).filePath(QStringLiteral("jars"));
}

QString gameNamespacesRoot()
{
    return QDir(gameRoot()).filePath(QStringLiteral("namespaces"));
}

QString gameScansRoot()
{
    return QDir(gameRoot()).filePath(QStringLiteral("scans"));
}

bool ensureDataDirs(QString *error)
{
    const QStringList dirs = {
        dataRoot(),
        projectsRoot(),
        cacheRoot(),
        sourceCacheRoot(),
        sessionsRoot(),
        layoutsRoot(),
        gameRoot(),
        gameRootsRoot(),
        gameInstancesRoot(),
        gameModsRoot(),
        gameJarsRoot(),
        gameNamespacesRoot(),
        gameScansRoot(),
    };
    for (const QString &dir : dirs) {
        if (!ensureDir(dir)) {
            if (error)
                *error = QStringLiteral("无法创建数据目录：%1").arg(dir);
            return false;
        }
    }
    return true;
}

} // namespace AppPaths
} // namespace mcstudio
