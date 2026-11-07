#pragma once

#include <QString>

namespace mcstudio {
namespace AppPaths {

QString appDir();
QString dataRoot();
QString projectsRoot();
QString cacheRoot();
QString sourceCacheRoot();
QString sessionsRoot();
QString layoutsRoot();
QString sessionFile(const QString &projectId);

// Game environment cache (roots / instances / mods / jars). Kept outside
// projects so importing a resource pack never has to touch it, and never
// written into a pack ZIP.
QString gameRoot();
QString gameRootsRoot();
QString gameInstancesRoot();
QString gameModsRoot();
QString gameJarsRoot();
QString gameNamespacesRoot();
QString gameScansRoot();

bool ensureDataDirs(QString *error = nullptr);

} // namespace AppPaths
} // namespace mcstudio
