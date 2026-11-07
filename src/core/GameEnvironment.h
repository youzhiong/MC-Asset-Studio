#pragma once

#include "core/InstanceInfo.h"

#include <QStringList>
#include <atomic>
#include <functional>

namespace mcstudio {

struct GameScanOptions {
    bool forceRefresh = false;
    bool useCache = true;
};

using GameScanProgress = std::function<void(int done, int total, const QString &message)>;

// Filesystem-only environment scanner. It reads version JSON metadata and
// enumerates mods/resourcepacks; it never opens jar contents (that is the job
// of JarIndex / ModScanner in later phases).
class GameEnvironment
{
public:
    static QString rootKey(const QString &rootPath);
    static QString instanceKeyFor(const QString &rootPath, const QString &instancePath);
    static QString rootCacheFile(const QString &rootKeyValue);
    static QString instanceCacheFile(const QString &instanceKeyValue);

    static GameEnvironmentSnapshot scanRoots(const QStringList &roots,
                                             const GameScanOptions &options,
                                             const GameScanProgress &progress = {},
                                             const std::atomic_bool *cancel = nullptr);

    static GameEnvironmentSnapshot loadCachedSnapshot(const QStringList &roots);
    static GameRootInfo loadCachedRoot(const QString &rootPath);

    static bool saveRoot(const GameRootInfo &root, QString *error = nullptr);
    static void saveInstance(const InstanceInfo &instance);

    static void clearRootCache(const QString &rootPath);
    static void clearAllCache();
    static qint64 cacheSizeBytes();
    static int cacheFileCount();

    // Scans one versions/<id> directory with the root-level mods/resourcepacks
    // already enumerated. Exposed for incremental refresh and smoke tests.
    static InstanceInfo scanInstance(const QString &rootPath, const QString &instancePath,
                                     const QVector<ModFileInfo> &rootMods,
                                     const QVector<ResourcePackFileInfo> &rootResourcePacks);
};

} // namespace mcstudio
