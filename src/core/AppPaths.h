#pragma once

#include <QString>

namespace mcstudio {
namespace AppPaths {

QString appDir();

// Settings and regenerable caches live under %LOCALAPPDATA%\MCStudio on
// Windows. User projects are kept in Documents\MC Asset Studio instead.
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

// One-time copy of the old portable data/ folder (settings, game/mod cache,
// projects) into the new AppData and Documents locations. The old folder is
// left untouched. Returns a human-readable summary; empty when nothing ran.
QString migrateLegacyData();

} // namespace AppPaths
} // namespace mcstudio
