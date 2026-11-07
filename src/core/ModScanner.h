#pragma once

#include "core/InstanceInfo.h"
#include "core/ModInfo.h"

#include <functional>

namespace mcstudio {

// Reads mod metadata from the jar central directory + a few known entries.
// Large jars are never extracted; only the candidate metadata entries and the
// logo presence checks touch entry data.
class ModScanner
{
public:
    static JarModScan scanJar(const ModFileInfo &file, const QString &instanceKey,
                              const QString &instanceLoader, bool forceRefresh = false,
                              bool useCache = true);

    static QString cacheDirectory();
    static QString cacheFile(const QString &jarPath, const QString &instanceLoader);
    static void clearCache();
    static int cacheFileCount();
};

} // namespace mcstudio
