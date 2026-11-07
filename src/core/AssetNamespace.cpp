#include "core/AssetNamespace.h"

#include <QDir>
#include <QSet>

namespace mcstudio {

QString namespaceFromAssetPath(const QString &path)
{
    QString normalized = QDir::fromNativeSeparators(path);
    while (normalized.startsWith(QLatin1Char('/')))
        normalized.remove(0, 1);

    if (!normalized.startsWith(QLatin1String("assets/"), Qt::CaseInsensitive))
        return {};

    const QString rest = normalized.mid(7);
    const int slash = rest.indexOf(QLatin1Char('/'));
    if (slash <= 0)
        return {};

    const QString ns = rest.left(slash);
    if (ns == QLatin1String(".") || ns == QLatin1String(".."))
        return {};
    return ns;
}

QStringList namespacesFromPaths(const QStringList &paths)
{
    QStringList result;
    QSet<QString> seen;
    for (const QString &path : paths) {
        const QString ns = namespaceFromAssetPath(path);
        if (ns.isEmpty() || seen.contains(ns))
            continue;
        seen.insert(ns);
        result.append(ns);
    }
    result.sort(Qt::CaseInsensitive);
    return result;
}

} // namespace mcstudio
