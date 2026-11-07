#pragma once

#include <QString>
#include <QStringList>

namespace mcstudio {

// "assets/create/blockstates/foo.json" -> "create"; empty when the path is
// not below assets/.
QString namespaceFromAssetPath(const QString &path);

// Unique, case-insensitively sorted namespaces found below assets/.
QStringList namespacesFromPaths(const QStringList &paths);

} // namespace mcstudio
