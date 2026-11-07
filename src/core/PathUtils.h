#pragma once

#include <QDir>
#include <QFileInfo>
#include <QString>

namespace mcstudio {

// Environment paths are stored with forward slashes so the cache JSON stays
// portable and comparable across Windows APIs.
inline QString toForwardSlashes(const QString &path)
{
    return QDir::fromNativeSeparators(path);
}

inline QString normalizedPath(const QString &path)
{
    const QString trimmed = path.trimmed();
    if (trimmed.isEmpty())
        return {};
    return QDir::cleanPath(QDir(toForwardSlashes(trimmed)).absolutePath());
}

inline bool samePath(const QString &a, const QString &b)
{
    return normalizedPath(a).compare(normalizedPath(b), Qt::CaseInsensitive) == 0;
}

inline QString canonicalPathIfPossible(const QString &path)
{
    const QString normalized = normalizedPath(path);
    if (normalized.isEmpty())
        return {};
    const QString canonical = QFileInfo(normalized).canonicalFilePath();
    return canonical.isEmpty() ? normalized : QDir::fromNativeSeparators(canonical);
}

} // namespace mcstudio
