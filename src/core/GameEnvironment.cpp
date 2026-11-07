#include "core/GameEnvironment.h"

#include "core/AppPaths.h"
#include "core/PathUtils.h"

#include <QCryptographicHash>
#include <QDateTime>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QSaveFile>
#include <QSet>
#include <QTextStream>
#include <algorithm>

namespace mcstudio {
namespace {

constexpr int kMaxInheritanceDepth = 8;

struct VersionJsonInfo {
    bool ok = false;
    QString id;
    QString clientVersion;
    QString assetIndex;
    QString jarField;
    QString inheritsFrom;
    QString mainClass;
    QStringList libraries;
};

struct ParentVersion {
    QString id;
    VersionJsonInfo info;
};

struct LoaderCandidate {
    QString loader;
    QString version;
    int priority = 0;
};

QByteArray readBytesStrippedBom(const QString &path, bool *ok = nullptr)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        if (ok)
            *ok = false;
        return {};
    }
    QByteArray data = file.readAll();
    // pack.mcmeta / version JSON files in the wild may carry a UTF-8 BOM.
    static const char kUtf8Bom[] = "\xEF\xBB\xBF";
    if (data.startsWith(kUtf8Bom))
        data.remove(0, 3);
    if (ok)
        *ok = true;
    return data;
}

QJsonObject readJsonObject(const QString &path, bool *ok = nullptr)
{
    bool readOk = false;
    const QByteArray data = readBytesStrippedBom(path, &readOk);
    if (!readOk) {
        if (ok)
            *ok = false;
        return {};
    }

    QJsonParseError error;
    const QJsonDocument document = QJsonDocument::fromJson(data, &error);
    if (error.error != QJsonParseError::NoError || !document.isObject()) {
        if (ok)
            *ok = false;
        return {};
    }
    if (ok)
        *ok = true;
    return document.object();
}

VersionJsonInfo parseVersionJson(const QString &path)
{
    VersionJsonInfo info;
    const QJsonObject object = readJsonObject(path, &info.ok);
    if (!info.ok)
        return info;

    info.id = object.value(QStringLiteral("id")).toString();
    info.clientVersion =
        object.value(QStringLiteral("clientVersion")).toString().trimmed();
    if (info.clientVersion.isEmpty())
        info.clientVersion = object.value(QStringLiteral("release_target")).toString().trimmed();

    const QJsonValue assetIndex = object.value(QStringLiteral("assetIndex"));
    if (assetIndex.isString())
        info.assetIndex = assetIndex.toString();
    else if (assetIndex.isObject())
        info.assetIndex = assetIndex.toObject().value(QStringLiteral("id")).toString();

    info.jarField = object.value(QStringLiteral("jar")).toString();
    info.inheritsFrom = object.value(QStringLiteral("inheritsFrom")).toString();
    info.mainClass = object.value(QStringLiteral("mainClass")).toString();

    for (const QJsonValue &value : object.value(QStringLiteral("libraries")).toArray()) {
        const QString name = value.toObject().value(QStringLiteral("name")).toString();
        if (!name.isEmpty())
            info.libraries.append(name);
    }
    return info;
}

// Forge versions embed the Minecraft version in the artifact version
// ("1.20.1-47.4.16" / "1.12.2-14.23.5.2860"); keep only the loader part.
QString forgeLoaderVersion(const QString &version)
{
    const int dash = version.indexOf(QLatin1Char('-'));
    if (dash > 0 && dash + 1 < version.size())
        return version.mid(dash + 1);
    return version;
}

LoaderCandidate detectLoader(const QStringList &libraries)
{
    LoaderCandidate best;
    for (const QString &name : libraries) {
        const QStringList parts = name.split(QLatin1Char(':'));
        if (parts.size() < 3)
            continue;
        const QString group = parts.at(0);
        const QString artifact = parts.at(1);
        const QString version = parts.at(2);

        LoaderCandidate candidate;
        if (group == QLatin1String("net.neoforged") && artifact == QLatin1String("neoforge")) {
            candidate.loader = QStringLiteral("neoforge");
            candidate.version = version;
            candidate.priority = 40;
        } else if (group == QLatin1String("net.minecraftforge")
                   && artifact == QLatin1String("fmlloader")) {
            candidate.loader = QStringLiteral("forge");
            candidate.version = forgeLoaderVersion(version);
            candidate.priority = 30;
        } else if (group == QLatin1String("net.minecraftforge")
                   && artifact == QLatin1String("forge")) {
            candidate.loader = QStringLiteral("forge");
            candidate.version = forgeLoaderVersion(version);
            candidate.priority = 30;
        } else if (group == QLatin1String("net.fabricmc")
                   && artifact == QLatin1String("fabric-loader")) {
            candidate.loader = QStringLiteral("fabric");
            candidate.version = version;
            candidate.priority = 20;
        } else if (group == QLatin1String("org.quiltmc")
                   && artifact == QLatin1String("quilt-loader")) {
            candidate.loader = QStringLiteral("quilt");
            candidate.version = version;
            candidate.priority = 10;
        }
        if (candidate.priority > best.priority)
            best = candidate;
    }
    return best;
}

QString loaderFromMainClass(const QString &mainClass)
{
    const QString lower = mainClass.toLower();
    if (lower.contains(QLatin1String("quiltmc")))
        return QStringLiteral("quilt");
    if (lower.contains(QLatin1String("fabricmc"))
        || lower.contains(QLatin1String("knotclient"))) {
        return QStringLiteral("fabric");
    }
    if (lower.contains(QLatin1String("bootstraplauncher"))
        || lower.contains(QLatin1String("launchwrapper"))
        || lower.contains(QLatin1String("forge"))) {
        return QStringLiteral("forge");
    }
    return QStringLiteral("vanilla");
}

DirFingerprint fingerprintDir(const QString &path)
{
    DirFingerprint fingerprint;
    fingerprint.path = toForwardSlashes(path);
    const QFileInfo info(path);
    fingerprint.exists = info.isDir();
    if (!fingerprint.exists)
        return fingerprint;
    fingerprint.mtime = info.lastModified().toMSecsSinceEpoch();
    fingerprint.count =
        static_cast<int>(QDir(path).entryList(QDir::Files | QDir::Dirs | QDir::NoDotAndDotDot).size());
    return fingerprint;
}

bool sameFingerprint(const DirFingerprint &a, const DirFingerprint &b)
{
    return a.exists == b.exists && a.mtime == b.mtime && a.count == b.count;
}

void collectMods(const QString &dirPath, const QString &origin, QVector<ModFileInfo> &target)
{
    const QDir dir(dirPath);
    if (!dir.exists())
        return;

    const QFileInfoList entries =
        dir.entryInfoList(QDir::Files | QDir::NoDotAndDotDot, QDir::Name | QDir::IgnoreCase);
    for (const QFileInfo &entry : entries) {
        const QString name = entry.fileName();
        const bool enabledJar = name.endsWith(QLatin1String(".jar"), Qt::CaseInsensitive);
        const bool disabledJar =
            name.endsWith(QLatin1String(".jar.disabled"), Qt::CaseInsensitive)
            || (name.endsWith(QLatin1String(".disabled"), Qt::CaseInsensitive)
                && name.contains(QLatin1String(".jar"), Qt::CaseInsensitive));
        if (!enabledJar && !disabledJar)
            continue;

        ModFileInfo mod;
        mod.path = toForwardSlashes(entry.absoluteFilePath());
        mod.fileName = name;
        mod.origin = origin;
        mod.size = entry.size();
        mod.mtime = entry.lastModified().toMSecsSinceEpoch();
        mod.disabled = disabledJar;
        target.append(mod);
    }
}

void collectResourcePacks(const QString &dirPath, QVector<ResourcePackFileInfo> &target)
{
    const QDir dir(dirPath);
    if (!dir.exists())
        return;

    const QFileInfoList entries = dir.entryInfoList(
        QDir::Files | QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name | QDir::IgnoreCase);
    for (const QFileInfo &entry : entries) {
        const QString name = entry.fileName();
        if (name.endsWith(QLatin1String(".disabled"), Qt::CaseInsensitive))
            continue;

        ResourcePackFileInfo pack;
        if (entry.isDir()) {
            pack.isDirectory = true;
        } else if (name.endsWith(QLatin1String(".zip"), Qt::CaseInsensitive)) {
            pack.isDirectory = false;
        } else {
            continue;
        }
        pack.path = toForwardSlashes(entry.absoluteFilePath());
        pack.fileName = name;
        pack.size = entry.isDir() ? 0 : entry.size();
        pack.mtime = entry.lastModified().toMSecsSinceEpoch();
        target.append(pack);
    }
}

void sortMods(QVector<ModFileInfo> &mods)
{
    std::sort(mods.begin(), mods.end(), [](const ModFileInfo &a, const ModFileInfo &b) {
        const int byName = a.fileName.compare(b.fileName, Qt::CaseInsensitive);
        if (byName != 0)
            return byName < 0;
        const int byOrigin = a.origin.compare(b.origin, Qt::CaseInsensitive);
        if (byOrigin != 0)
            return byOrigin < 0;
        return a.path.compare(b.path, Qt::CaseInsensitive) < 0;
    });
}

void sortResourcePacks(QVector<ResourcePackFileInfo> &packs)
{
    std::sort(packs.begin(), packs.end(),
              [](const ResourcePackFileInfo &a, const ResourcePackFileInfo &b) {
                  return a.fileName.compare(b.fileName, Qt::CaseInsensitive) < 0;
              });
}

void resolveParents(const QString &versionsDir, const QString &inheritsFrom,
                    QVector<ParentVersion> &parents, QStringList &warnings)
{
    QString parentId = inheritsFrom;
    QSet<QString> seen;
    while (!parentId.isEmpty() && parents.size() < kMaxInheritanceDepth) {
        if (seen.contains(parentId)) {
            warnings.append(QStringLiteral("版本继承存在循环：%1").arg(parentId));
            break;
        }
        seen.insert(parentId);

        const QString dir = QDir(versionsDir).filePath(parentId);
        const QString json = QDir(dir).filePath(parentId + QStringLiteral(".json"));
        VersionJsonInfo info = parseVersionJson(json);
        if (!info.ok) {
            warnings.append(QStringLiteral("找不到父版本 JSON：%1").arg(parentId));
            break;
        }
        ParentVersion parent;
        parent.id = parentId;
        parent.info = info;
        parents.append(parent);
        parentId = info.inheritsFrom;
    }
    if (parents.size() >= kMaxInheritanceDepth)
        warnings.append(QStringLiteral("版本继承层级超过 %1").arg(kMaxInheritanceDepth));
}

QString resolveClientJar(const QString &versionsDir, const QString &versionId,
                         const VersionJsonInfo &info, const QVector<ParentVersion> &parents)
{
    const auto directJar = [&versionsDir](const QString &id) -> QString {
        if (id.isEmpty())
            return {};
        const QFileInfo file(
            QDir(versionsDir).filePath(id + QLatin1Char('/') + id + QStringLiteral(".jar")));
        return file.isFile() ? toForwardSlashes(file.absoluteFilePath()) : QString();
    };
    const auto jarFromField = [&directJar](const QString &jarField) -> QString {
        if (jarField.isEmpty())
            return {};
        const QFileInfo field(jarField);
        if (field.isAbsolute())
            return field.isFile() ? toForwardSlashes(field.absoluteFilePath()) : QString();
        return directJar(jarField);
    };

    // 1. own <id>.jar
    if (const QString own = directJar(versionId); !own.isEmpty())
        return own;
    // 2. the version JSON "jar" field
    if (const QString fromField = jarFromField(info.jarField); !fromField.isEmpty())
        return fromField;
    // 3. inherited parent jars (Forge/NeoForge often ship no full client jar)
    for (const ParentVersion &parent : parents) {
        if (const QString jar = directJar(parent.id); !jar.isEmpty())
            return jar;
        if (const QString fromField = jarFromField(parent.info.jarField); !fromField.isEmpty())
            return fromField;
    }
    return {};
}

bool instanceCacheIsValid(const InstanceInfo &cached, const GameRootInfo &root)
{
    if (cached.key.isEmpty())
        return false;
    if (cached.rootPath.compare(root.path, Qt::CaseInsensitive) != 0)
        return false;
    if (cached.instancePath.isEmpty() || !QFileInfo(cached.instancePath).isDir())
        return false;

    const QFileInfo versionJson(cached.versionJson);
    if (!versionJson.isFile())
        return false;
    if (versionJson.size() != cached.versionJsonSize)
        return false;
    if (versionJson.lastModified().toMSecsSinceEpoch() != cached.versionJsonMtime)
        return false;

    if (!sameFingerprint(cached.instanceDir, fingerprintDir(cached.instancePath)))
        return false;
    if (!sameFingerprint(cached.modsDir, fingerprintDir(cached.instancePath + QStringLiteral("/mods"))))
        return false;
    if (!sameFingerprint(cached.resourcePacksDir,
                         fingerprintDir(cached.instancePath + QStringLiteral("/resourcepacks")))) {
        return false;
    }
    if (cached.hasClientJar && !QFileInfo(cached.clientJar).isFile())
        return false;
    if (!cached.hasClientJar) {
        // A jar may have appeared since the cache was written.
        const QFileInfo ownJar(QDir(cached.instancePath).filePath(cached.name + QStringLiteral(".jar")));
        if (ownJar.isFile())
            return false;
    }
    return true;
}

QJsonObject rootToJson(const GameRootInfo &root)
{
    QJsonObject object;
    object.insert(QStringLiteral("schema"), 1);
    object.insert(QStringLiteral("key"), root.key);
    object.insert(QStringLiteral("path"), root.path);
    object.insert(QStringLiteral("name"), root.name);
    object.insert(QStringLiteral("scannedAt"), root.scannedAt);
    object.insert(QStringLiteral("hasVersions"), root.hasVersions);
    object.insert(QStringLiteral("hasAssets"), root.hasAssets);
    object.insert(QStringLiteral("hasLibraries"), root.hasLibraries);
    object.insert(QStringLiteral("hasRootMods"), root.hasRootMods);
    object.insert(QStringLiteral("hasRootResourcePacks"), root.hasRootResourcePacks);
    object.insert(QStringLiteral("rootModsDir"), root.rootModsDir.toJson());
    object.insert(QStringLiteral("rootResourcePacksDir"), root.rootResourcePacksDir.toJson());

    QJsonArray warnings;
    for (const QString &warning : root.warnings)
        warnings.append(warning);
    object.insert(QStringLiteral("warnings"), warnings);

    QJsonArray instances;
    for (const InstanceInfo &instance : root.instances)
        instances.append(instance.toJson());
    object.insert(QStringLiteral("instances"), instances);
    return object;
}

GameRootInfo rootFromJson(const QJsonObject &object)
{
    GameRootInfo root;
    root.key = object.value(QStringLiteral("key")).toString();
    root.path = object.value(QStringLiteral("path")).toString();
    root.name = object.value(QStringLiteral("name")).toString();
    root.scannedAt = object.value(QStringLiteral("scannedAt")).toString();
    root.hasVersions = object.value(QStringLiteral("hasVersions")).toBool();
    root.hasAssets = object.value(QStringLiteral("hasAssets")).toBool();
    root.hasLibraries = object.value(QStringLiteral("hasLibraries")).toBool();
    root.hasRootMods = object.value(QStringLiteral("hasRootMods")).toBool();
    root.hasRootResourcePacks =
        object.value(QStringLiteral("hasRootResourcePacks")).toBool();
    root.rootModsDir = DirFingerprint::fromJson(
        object.value(QStringLiteral("rootModsDir")).toObject());
    root.rootResourcePacksDir = DirFingerprint::fromJson(
        object.value(QStringLiteral("rootResourcePacksDir")).toObject());
    for (const QJsonValue &value : object.value(QStringLiteral("warnings")).toArray())
        root.warnings.append(value.toString());
    for (const QJsonValue &value : object.value(QStringLiteral("instances")).toArray())
        root.instances.append(InstanceInfo::fromJson(value.toObject()));
    return root;
}

bool writeJsonFile(const QString &path, const QJsonObject &object)
{
    if (!QDir().mkpath(QFileInfo(path).absolutePath()))
        return false;
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly))
        return false;
    file.write(QJsonDocument(object).toJson(QJsonDocument::Indented));
    return file.commit();
}

QString sha1Hex(const QString &text)
{
    return QString::fromLatin1(
        QCryptographicHash::hash(text.toUtf8(), QCryptographicHash::Sha1).toHex());
}

} // namespace

QString GameEnvironment::rootKey(const QString &rootPath)
{
    const QString canonical = canonicalPathIfPossible(rootPath);
    if (canonical.isEmpty())
        return {};
    return sha1Hex(canonical.toLower()).left(16);
}

QString GameEnvironment::instanceKeyFor(const QString &rootPath, const QString &instancePath)
{
    const QString normalizedRoot = normalizedPath(rootPath);
    const QString normalizedInstance = normalizedPath(instancePath);
    if (normalizedRoot.isEmpty() || normalizedInstance.isEmpty())
        return {};
    const QString relative = QDir(normalizedRoot).relativeFilePath(normalizedInstance);
    return rootKey(normalizedRoot) + QLatin1Char(':') + toForwardSlashes(relative);
}

QString GameEnvironment::rootCacheFile(const QString &rootKeyValue)
{
    if (rootKeyValue.isEmpty())
        return {};
    return QDir(AppPaths::gameRootsRoot()).filePath(rootKeyValue + QStringLiteral(".json"));
}

QString GameEnvironment::instanceCacheFile(const QString &instanceKeyValue)
{
    if (instanceKeyValue.isEmpty())
        return {};
    return QDir(AppPaths::gameInstancesRoot()).filePath(sha1Hex(instanceKeyValue) + QStringLiteral(".json"));
}

GameEnvironmentSnapshot GameEnvironment::scanRoots(const QStringList &roots,
                                                   const GameScanOptions &options,
                                                   const GameScanProgress &progress,
                                                   const std::atomic_bool *cancel)
{
    GameEnvironmentSnapshot snapshot;
    if (roots.isEmpty())
        return snapshot;

    const auto isCancelled = [cancel]() {
        return cancel && cancel->load(std::memory_order_relaxed);
    };

    struct RootPlan {
        QString path;
        QString key;
        GameRootInfo cached;
        bool cacheUsable = false;
        QStringList versionIds;
        DirFingerprint rootMods;
        DirFingerprint rootPacks;
        QString warning;
    };

    QVector<RootPlan> plans;
    int totalInstances = 0;

    for (const QString &rawPath : roots) {
        if (isCancelled()) {
            snapshot.warnings.append(QStringLiteral("扫描已取消"));
            return snapshot;
        }

        RootPlan plan;
        plan.path = normalizedPath(rawPath);
        if (plan.path.isEmpty() || !QFileInfo(plan.path).isDir()) {
            snapshot.warnings.append(QStringLiteral("游戏目录不存在：%1").arg(rawPath));
            continue;
        }

        plan.key = rootKey(plan.path);
        plan.rootMods = fingerprintDir(plan.path + QStringLiteral("/mods"));
        plan.rootPacks = fingerprintDir(plan.path + QStringLiteral("/resourcepacks"));
        if (options.useCache && !options.forceRefresh) {
            plan.cached = loadCachedRoot(plan.path);
            plan.cacheUsable = !plan.cached.key.isEmpty();
        }

        const QDir versionsDir(plan.path + QStringLiteral("/versions"));
        if (versionsDir.exists()) {
            const QFileInfoList dirs = versionsDir.entryInfoList(
                QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name | QDir::IgnoreCase);
            for (const QFileInfo &dir : dirs)
                plan.versionIds.append(dir.fileName());
        } else {
            plan.warning = QStringLiteral("没有 versions/ 目录");
        }

        totalInstances += static_cast<int>(plan.versionIds.size());
        plans.append(plan);
    }

    if (plans.isEmpty())
        return snapshot;

    const int progressTotal = qMax(1, totalInstances);
    int done = 0;

    for (RootPlan &plan : plans) {
        if (isCancelled()) {
            snapshot.warnings.append(QStringLiteral("扫描已取消"));
            break;
        }

        GameRootInfo root;
        root.key = plan.key;
        root.path = plan.path;
        root.name = QFileInfo(plan.path).fileName();
        root.hasVersions = QDir(plan.path + QStringLiteral("/versions")).exists();
        root.hasAssets = QDir(plan.path + QStringLiteral("/assets")).exists();
        root.hasLibraries = QDir(plan.path + QStringLiteral("/libraries")).exists();
        root.rootModsDir = plan.rootMods;
        root.rootResourcePacksDir = plan.rootPacks;
        root.hasRootMods = plan.rootMods.exists;
        root.hasRootResourcePacks = plan.rootPacks.exists;
        if (!plan.warning.isEmpty())
            root.warnings.append(plan.warning);

        QVector<ModFileInfo> rootMods;
        collectMods(plan.path + QStringLiteral("/mods"), QStringLiteral("root"), rootMods);
        QVector<ResourcePackFileInfo> rootPacks;
        collectResourcePacks(plan.path + QStringLiteral("/resourcepacks"), rootPacks);

        const bool rootListsChanged =
            plan.cacheUsable
            && (!sameFingerprint(plan.cached.rootModsDir, root.rootModsDir)
                || !sameFingerprint(plan.cached.rootResourcePacksDir, root.rootResourcePacksDir));

        bool allFromCache = !plan.versionIds.isEmpty();
        bool complete = true;
        const QDir versionsDir(plan.path + QStringLiteral("/versions"));

        for (const QString &versionId : plan.versionIds) {
            if (isCancelled()) {
                complete = false;
                break;
            }

            const QString instancePath = versionsDir.filePath(versionId);
            bool reused = false;
            InstanceInfo instance;

            if (plan.cacheUsable && !options.forceRefresh && !rootListsChanged) {
                const InstanceInfo *cached = plan.cached.findInstanceByPath(instancePath);
                if (cached && instanceCacheIsValid(*cached, root)) {
                    instance = *cached;
                    reused = true;
                }
            }

            if (!reused) {
                instance = scanInstance(plan.path, instancePath, rootMods, rootPacks);
                allFromCache = false;
            }

            root.instances.append(instance);
            ++done;
            if (progress) {
                progress(done, progressTotal,
                         reused ? QStringLiteral("已复用缓存 · %1").arg(instance.name)
                                : QStringLiteral("已扫描 · %1").arg(instance.name));
            }
        }

        std::sort(root.instances.begin(), root.instances.end(),
                  [](const InstanceInfo &a, const InstanceInfo &b) {
                      return a.name.compare(b.name, Qt::CaseInsensitive) < 0;
                  });

        root.scannedAt = QDateTime::currentDateTimeUtc().toString(Qt::ISODate);
        root.fromCache = complete && allFromCache;

        if (complete) {
            saveRoot(root);
        } else {
            snapshot.warnings.append(QStringLiteral("扫描未完成：%1").arg(root.path));
        }
        snapshot.roots.append(root);

        if (!complete)
            break;
    }

    if (progress && !snapshot.roots.isEmpty())
        progress(progressTotal, progressTotal, QStringLiteral("环境扫描完成"));

    return snapshot;
}

GameEnvironmentSnapshot GameEnvironment::loadCachedSnapshot(const QStringList &roots)
{
    GameEnvironmentSnapshot snapshot;
    for (const QString &rootPath : roots) {
        const GameRootInfo root = loadCachedRoot(rootPath);
        if (!root.key.isEmpty())
            snapshot.roots.append(root);
    }
    return snapshot;
}

GameRootInfo GameEnvironment::loadCachedRoot(const QString &rootPath)
{
    GameRootInfo cached;
    const QString path = normalizedPath(rootPath);
    if (path.isEmpty())
        return cached;

    const QString key = rootKey(path);
    if (key.isEmpty())
        return cached;

    bool ok = false;
    const QJsonObject object = readJsonObject(rootCacheFile(key), &ok);
    if (!ok)
        return cached;

    const QString storedPath = normalizedPath(object.value(QStringLiteral("path")).toString());
    if (storedPath.compare(path, Qt::CaseInsensitive) != 0)
        return {};

    cached = rootFromJson(object);
    cached.key = key;
    cached.path = path;
    if (cached.name.isEmpty())
        cached.name = QFileInfo(path).fileName();
    cached.fromCache = true;
    return cached;
}

bool GameEnvironment::saveRoot(const GameRootInfo &root, QString *error)
{
    if (root.key.isEmpty())
        return false;

    if (!QDir().mkpath(AppPaths::gameRootsRoot()) || !QDir().mkpath(AppPaths::gameInstancesRoot())
        || !QDir().mkpath(AppPaths::gameScansRoot())) {
        if (error)
            *error = QStringLiteral("无法创建环境缓存目录");
        return false;
    }

    if (!writeJsonFile(rootCacheFile(root.key), rootToJson(root))) {
        if (error)
            *error = QStringLiteral("无法写入环境缓存：%1").arg(rootCacheFile(root.key));
        return false;
    }

    for (const InstanceInfo &instance : root.instances)
        saveInstance(instance);

    QFile logFile(QDir(AppPaths::gameScansRoot()).filePath(root.key + QStringLiteral(".log")));
    if (logFile.open(QIODevice::WriteOnly | QIODevice::Append | QIODevice::Text)) {
        QTextStream stream(&logFile);
        stream << QDateTime::currentDateTime().toString(Qt::ISODate)
               << " path=" << root.path << " instances=" << root.instanceCount()
               << " mods=" << root.totalMods()
               << " packs=" << root.totalResourcePacks()
               << " cached=" << (root.fromCache ? "yes" : "no") << '\n';
    }
    return true;
}

void GameEnvironment::saveInstance(const InstanceInfo &instance)
{
    if (instance.key.isEmpty())
        return;
    writeJsonFile(instanceCacheFile(instance.key), instance.toJson());
}

void GameEnvironment::clearRootCache(const QString &rootPath)
{
    const QString path = normalizedPath(rootPath);
    if (path.isEmpty())
        return;

    const GameRootInfo cached = loadCachedRoot(path);
    for (const InstanceInfo &instance : cached.instances)
        QFile::remove(instanceCacheFile(instance.key));
    QFile::remove(rootCacheFile(rootKey(path)));
}

void GameEnvironment::clearAllCache()
{
    QDir(AppPaths::gameRoot()).removeRecursively();
    AppPaths::ensureDataDirs();
}

qint64 GameEnvironment::cacheSizeBytes()
{
    qint64 total = 0;
    QDirIterator iterator(AppPaths::gameRoot(), QDir::Files,
                          QDirIterator::Subdirectories);
    while (iterator.hasNext()) {
        iterator.next();
        total += iterator.fileInfo().size();
    }
    return total;
}

int GameEnvironment::cacheFileCount()
{
    int count = 0;
    QDirIterator iterator(AppPaths::gameRoot(), QDir::Files,
                          QDirIterator::Subdirectories);
    while (iterator.hasNext()) {
        iterator.next();
        ++count;
    }
    return count;
}

InstanceInfo GameEnvironment::scanInstance(const QString &rootPath,
                                           const QString &instancePath,
                                           const QVector<ModFileInfo> &rootMods,
                                           const QVector<ResourcePackFileInfo> &rootResourcePacks)
{
    InstanceInfo instance;
    instance.rootPath = normalizedPath(rootPath);
    instance.instancePath = normalizedPath(instancePath);
    instance.name = QFileInfo(instance.instancePath).fileName();
    instance.key = instanceKeyFor(instance.rootPath, instance.instancePath);

    const QString versionsDir = instance.rootPath + QStringLiteral("/versions");
    instance.versionJson = instance.instancePath + QLatin1Char('/') + instance.name
                           + QStringLiteral(".json");
    const QFileInfo versionJsonInfo(instance.versionJson);
    const bool hasVersionJson = versionJsonInfo.isFile();
    if (hasVersionJson) {
        instance.versionJsonSize = versionJsonInfo.size();
        instance.versionJsonMtime = versionJsonInfo.lastModified().toMSecsSinceEpoch();
    } else {
        instance.warnings.append(
            QStringLiteral("缺少版本 JSON：%1").arg(instance.name + QStringLiteral(".json")));
    }

    instance.instanceDir = fingerprintDir(instance.instancePath);
    instance.modsDir = fingerprintDir(instance.instancePath + QStringLiteral("/mods"));
    instance.resourcePacksDir =
        fingerprintDir(instance.instancePath + QStringLiteral("/resourcepacks"));
    instance.isolated = instance.modsDir.exists || instance.resourcePacksDir.exists;

    VersionJsonInfo info = parseVersionJson(instance.versionJson);
    QVector<ParentVersion> parents;
    if (info.ok) {
        resolveParents(versionsDir, info.inheritsFrom, parents, instance.warnings);
    } else if (hasVersionJson) {
        instance.warnings.append(QStringLiteral("版本 JSON 无法解析"));
    }

    // Fill metadata from the inheritance chain; child values always win.
    QString clientVersion = info.clientVersion;
    QString assetIndex = info.assetIndex;
    QString mainClass = info.mainClass;
    LoaderCandidate loader = detectLoader(info.libraries);

    for (const ParentVersion &parent : parents) {
        if (clientVersion.isEmpty())
            clientVersion = parent.info.clientVersion;
        if (assetIndex.isEmpty())
            assetIndex = parent.info.assetIndex;
        if (mainClass.isEmpty())
            mainClass = parent.info.mainClass;
        if (loader.priority == 0)
            loader = detectLoader(parent.info.libraries);
    }
    if (loader.priority == 0) {
        loader.loader = loaderFromMainClass(mainClass);
        loader.version.clear();
    }

    instance.clientVersion = clientVersion;
    instance.assetIndex = assetIndex;
    instance.loader = loader.loader.isEmpty() ? QStringLiteral("vanilla") : loader.loader;
    instance.loaderVersion = loader.version;

    instance.clientJar = resolveClientJar(versionsDir, instance.name, info, parents);
    instance.hasClientJar = !instance.clientJar.isEmpty();
    if (!instance.hasClientJar)
        instance.warnings.append(QStringLiteral("未找到客户端 JAR"));

    // Root mods/resourcepacks are shared; instance directories are merged on
    // top so version-isolated launchers (PCL / HMCL) keep working.
    instance.mods = rootMods;
    collectMods(instance.instancePath + QStringLiteral("/mods"), QStringLiteral("instance"),
                instance.mods);
    sortMods(instance.mods);

    instance.resourcePacks = rootResourcePacks;
    collectResourcePacks(instance.instancePath + QStringLiteral("/resourcepacks"),
                         instance.resourcePacks);
    sortResourcePacks(instance.resourcePacks);

    instance.warnings.removeDuplicates();
    return instance;
}

} // namespace mcstudio
