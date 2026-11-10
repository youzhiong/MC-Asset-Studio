#include "core/AppPaths.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QSettings>
#include <QStandardPaths>
#include <QStringList>
#include <QtGlobal>

namespace mcstudio {
namespace AppPaths {
namespace {

QString g_dataRoot;
QString g_projectsRoot;

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

bool makeWritableDir(const QString &path)
{
    return !path.isEmpty() && ensureDir(path) && canWrite(path);
}

QString envPath(const char *name)
{
    const QString value = qEnvironmentVariable(name).trimmed();
    if (value.isEmpty())
        return {};
    return QFileInfo(value).absoluteFilePath();
}

bool copyDirMerge(const QString &fromDir, const QString &toDir)
{
    QDir source(fromDir);
    if (!source.exists())
        return true;
    if (!ensureDir(toDir))
        return false;

    const QFileInfoList entries = source.entryInfoList(
        QDir::Files | QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name);
    for (const QFileInfo &entry : entries) {
        const QString target = QDir(toDir).filePath(entry.fileName());
        if (entry.isDir()) {
            if (!copyDirMerge(entry.absoluteFilePath(), target))
                return false;
        } else {
            if (QFileInfo::exists(target))
                continue;
            if (!QFile::copy(entry.absoluteFilePath(), target))
                return false;
        }
    }
    return true;
}

QStringList legacyDataRoots()
{
    QStringList roots;

    // Old portable layout: <program>/data.
    const QString portable = QDir(appDir()).filePath(QStringLiteral("data"));
    if (QFileInfo(portable).isDir())
        roots.append(QDir::cleanPath(portable));

    // Old fallback layout: %LOCALAPPDATA%/<org>/<app>/data.
    const QString oldFallback =
        QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation);
    if (!oldFallback.isEmpty()) {
        const QString legacy = QDir(oldFallback).filePath(QStringLiteral("data"));
        if (QFileInfo(legacy).isDir())
            roots.append(QDir::cleanPath(legacy));
    }

    roots.removeDuplicates();
    return roots;
}

void rewriteLegacyProjectPaths(const QString &legacyDataRoot)
{
    const QString settingsPath = QDir(dataRoot()).filePath(QStringLiteral("settings.ini"));
    if (!QFileInfo::exists(settingsPath))
        return;

    const QString legacyProjects =
        QDir::fromNativeSeparators(QDir(legacyDataRoot).filePath(QStringLiteral("projects")));
    const QString newProjects = QDir::fromNativeSeparators(projectsRoot());

    const auto rewriteOne = [&](const QString &path) {
        const QString normalized = QDir::fromNativeSeparators(path);
        if (!normalized.startsWith(legacyProjects, Qt::CaseInsensitive))
            return path;
        QString result = normalized;
        result.replace(0, legacyProjects.size(), newProjects);
        return result;
    };

    QSettings settings(settingsPath, QSettings::IniFormat);

    const QString lastDir = settings.value(QStringLiteral("project/lastDir")).toString();
    if (!lastDir.isEmpty())
        settings.setValue(QStringLiteral("project/lastDir"), rewriteOne(lastDir));

    const QString recentRaw = settings.value(QStringLiteral("project/recent")).toString();
    if (!recentRaw.isEmpty()) {
        const QJsonDocument document = QJsonDocument::fromJson(recentRaw.toUtf8());
        if (document.isArray()) {
            QJsonArray recent;
            for (const QJsonValue &value : document.array())
                recent.append(rewriteOne(value.toString()));
            settings.setValue(QStringLiteral("project/recent"),
                              QString::fromUtf8(
                                  QJsonDocument(recent).toJson(QJsonDocument::Compact)));
        }
    }

    settings.sync();
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

    // Settings and regenerable caches belong in the per-user local AppData
    // folder, like a normal Windows application.
    const QString fromEnv = envPath("MCSTUDIO_APPDATA_DIR");
    if (makeWritableDir(fromEnv)) {
        g_dataRoot = QDir(fromEnv).absolutePath();
        return g_dataRoot;
    }

    QString base = QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation);
    if (base.isEmpty())
        base = qEnvironmentVariable("LOCALAPPDATA");
    if (base.isEmpty())
        base = QDir::homePath() + QStringLiteral("/AppData/Local");
    const QString root = QDir(base).filePath(QStringLiteral("MCStudio"));
    if (makeWritableDir(root)) {
        g_dataRoot = root;
        return g_dataRoot;
    }

    // Last-resort fallback. Never write next to the executable.
    const QString fallback = QDir::homePath() + QStringLiteral("/.mcstudio");
    ensureDir(fallback);
    g_dataRoot = fallback;
    return g_dataRoot;
}

QString projectsRoot()
{
    if (!g_projectsRoot.isEmpty())
        return g_projectsRoot;

    const QString fromEnv = envPath("MCSTUDIO_PROJECTS_DIR");
    if (makeWritableDir(fromEnv)) {
        g_projectsRoot = QDir(fromEnv).absolutePath();
        return g_projectsRoot;
    }

    QString documents =
        QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation);
    if (documents.isEmpty())
        documents = QDir::homePath() + QStringLiteral("/Documents");
    const QString root = QDir(documents).filePath(QStringLiteral("MC Asset Studio"));
    if (makeWritableDir(root)) {
        g_projectsRoot = root;
        return g_projectsRoot;
    }

    // Documents may be redirected to a read-only network share.
    const QString fallback = QDir(dataRoot()).filePath(QStringLiteral("projects"));
    ensureDir(fallback);
    g_projectsRoot = fallback;
    return g_projectsRoot;
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
    return QDir(cacheRoot()).filePath(QStringLiteral("game"));
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

QString migrateLegacyData()
{
    const QString marker =
        QDir(dataRoot()).filePath(QStringLiteral(".migrated-from-legacy-data"));
    if (QFileInfo::exists(marker))
        return {};

    QString error;
    if (!ensureDataDirs(&error))
        return error;

    const QStringList legacyRoots = legacyDataRoots();
    bool copied = false;
    bool ok = true;

    for (const QString &legacy : legacyRoots) {
        if (QDir::cleanPath(legacy) == QDir::cleanPath(dataRoot())
            || QDir::cleanPath(legacy) == QDir::cleanPath(projectsRoot())) {
            continue;
        }

        const QString legacySettings =
            QDir(legacy).filePath(QStringLiteral("settings.ini"));
        const QString newSettings =
            QDir(dataRoot()).filePath(QStringLiteral("settings.ini"));
        if (QFileInfo::exists(legacySettings) && !QFileInfo::exists(newSettings)) {
            if (QFile::copy(legacySettings, newSettings))
                copied = true;
            else
                ok = false;
        }

        const QString legacyCache = QDir(legacy).filePath(QStringLiteral("cache"));
        const QString legacyGame = QDir(legacy).filePath(QStringLiteral("game"));
        const QString legacyProjects = QDir(legacy).filePath(QStringLiteral("projects"));

        if (QFileInfo(legacyCache).isDir() || QFileInfo(legacyGame).isDir()
            || QFileInfo(legacyProjects).isDir()) {
            copied = true;
        }

        if (!copyDirMerge(legacyCache, cacheRoot()))
            ok = false;
        if (!copyDirMerge(legacyGame, gameRoot()))
            ok = false;
        if (!copyDirMerge(legacyProjects, projectsRoot()))
            ok = false;
        rewriteLegacyProjectPaths(legacy);
    }

    if (ok && copied) {
        QFile file(marker);
        if (file.open(QIODevice::WriteOnly))
            file.write("copied from legacy data\n");
    }

    if (!ok) {
        return QStringLiteral(
                   "旧数据自动复制未完全成功；旧 data 目录保留未动，可手动复制到 %1 和 %2。")
            .arg(QDir::toNativeSeparators(dataRoot()),
                 QDir::toNativeSeparators(projectsRoot()));
    }
    if (!copied)
        return {};
    return QStringLiteral("已把旧数据复制到：%1（设置/缓存）、%2（项目）；旧 data 目录保留未动。")
        .arg(QDir::toNativeSeparators(dataRoot()),
             QDir::toNativeSeparators(projectsRoot()));
}

} // namespace AppPaths
} // namespace mcstudio
