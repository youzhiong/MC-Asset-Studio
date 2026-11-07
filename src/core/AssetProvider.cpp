#include "core/AssetProvider.h"

#include "core/Project.h"
#include "core/ZipArchive.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>

#include <algorithm>

namespace mcstudio {
namespace {

QString normalizeAssetPath(const QString &path)
{
    QString normalized = QDir::fromNativeSeparators(path);
    while (normalized.startsWith(QLatin1Char('/')))
        normalized.remove(0, 1);
    return normalized;
}

// Paths are sorted case-insensitively; find the prefix range without scanning
// every jar entry for each namespace lookup.
QStringList pathsWithPrefix(const QStringList &sortedPaths, const QString &prefix)
{
    QStringList result;
    const QString normalizedPrefix = normalizeAssetPath(prefix);
    auto it = std::lower_bound(sortedPaths.cbegin(), sortedPaths.cend(), normalizedPrefix,
                               [](const QString &path, const QString &value) {
                                   return path.compare(value, Qt::CaseInsensitive) < 0;
                               });
    while (it != sortedPaths.cend()
           && it->startsWith(normalizedPrefix, Qt::CaseInsensitive)) {
        result.append(*it);
        ++it;
    }
    return result;
}

QByteArray readFileBytes(const QString &path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return {};
    QByteArray data = file.readAll();
    static const char kUtf8Bom[] = "\xEF\xBB\xBF";
    if (data.startsWith(kUtf8Bom))
        data.remove(0, 3);
    return data;
}

} // namespace

QString assetOriginLabel(AssetOrigin origin)
{
    switch (origin) {
    case AssetOrigin::Pack:
        return QStringLiteral("资源包");
    case AssetOrigin::Reference:
        return QStringLiteral("参考实例");
    case AssetOrigin::Vanilla:
        return QStringLiteral("原版资源");
    case AssetOrigin::Conflict:
        return QStringLiteral("来源冲突");
    case AssetOrigin::Missing:
        break;
    }
    return QStringLiteral("缺失");
}

QString AssetProvider::detail(const QString &path) const
{
    Q_UNUSED(path);
    return label();
}

ProjectAssetProvider::ProjectAssetProvider(const Project &project)
    : m_project(project)
{
}

bool ProjectAssetProvider::exists(const QString &path) const
{
    // The project file index is built once in Project::scan(); using it here
    // avoids thousands of filesystem stats while resolving models.
    return m_project.fileIndex().contains(normalizeAssetPath(path));
}

QByteArray ProjectAssetProvider::read(const QString &path) const
{
    return m_project.readBytes(path);
}

QStringList ProjectAssetProvider::list(const QString &prefix) const
{
    QStringList result;
    const QString normalizedPrefix = normalizeAssetPath(prefix);
    const QHash<QString, ProjectFile> &index = m_project.fileIndex();
    for (auto it = index.constBegin(); it != index.constEnd(); ++it) {
        if (it.key().startsWith(normalizedPrefix))
            result.append(it.key());
    }
    result.sort(Qt::CaseInsensitive);
    return result;
}

QString ProjectAssetProvider::label() const
{
    return QStringLiteral("资源包");
}

AssetOrigin ProjectAssetProvider::origin() const
{
    return AssetOrigin::Pack;
}

QString ProjectAssetProvider::detail(const QString &path) const
{
    const ProjectFile file = m_project.fileIndex().value(normalizeAssetPath(path));
    if (file.source == QLatin1String("pack"))
        return QStringLiteral("资源包 · pack 覆盖层");
    return QStringLiteral("资源包 · source 解包源");
}

JarAssetProvider::JarAssetProvider(const QString &jarPath, const QString &label,
                                   AssetOrigin origin)
    : m_jarPath(normalizeAssetPath(jarPath))
    , m_label(label)
    , m_origin(origin)
{
}

JarAssetProvider::~JarAssetProvider() = default;

void JarAssetProvider::ensureLoaded() const
{
    if (m_loaded)
        return;
    m_loaded = true;

    m_archive = std::make_unique<ZipArchive>(m_jarPath);
    if (!m_archive->isOpen()) {
        m_error = m_archive->error();
        m_archive.reset();
        return;
    }
    m_open = true;

    for (const ZipEntry &entry : m_archive->entries()) {
        if (entry.isDir)
            continue;
        const QString normalized = normalizeAssetPath(entry.path);
        m_entryLookup.insert(normalized, entry.path);
        m_paths.append(normalized);
    }
    m_paths.sort(Qt::CaseInsensitive);
}

bool JarAssetProvider::exists(const QString &path) const
{
    ensureLoaded();
    return m_open && m_entryLookup.contains(normalizeAssetPath(path));
}

QByteArray JarAssetProvider::read(const QString &path) const
{
    ensureLoaded();
    if (!m_open)
        return {};
    const QString entry = m_entryLookup.value(normalizeAssetPath(path));
    if (entry.isEmpty())
        return {};
    return m_archive->read(entry);
}

QStringList JarAssetProvider::list(const QString &prefix) const
{
    ensureLoaded();
    QStringList result;
    const QString normalizedPrefix = normalizeAssetPath(prefix);
    for (const QString &path : m_paths) {
        if (path.startsWith(normalizedPrefix))
            result.append(path);
    }
    return result;
}

QString JarAssetProvider::label() const
{
    return m_label;
}

AssetOrigin JarAssetProvider::origin() const
{
    return m_origin;
}

QString JarAssetProvider::detail(const QString &path) const
{
    Q_UNUSED(path);
    return m_label + QStringLiteral(" · ") + QFileInfo(m_jarPath).fileName();
}

AssetObjectsProvider::AssetObjectsProvider(const QString &rootPath,
                                           const QString &assetIndexId)
    : m_rootPath(normalizeAssetPath(rootPath))
    , m_assetIndexId(assetIndexId.trimmed())
{
}

AssetObjectsProvider::~AssetObjectsProvider() = default;

void AssetObjectsProvider::ensureLoaded() const
{
    if (m_loaded)
        return;
    m_loaded = true;
    if (m_assetIndexId.isEmpty())
        return;

    const QString indexPath = QDir(m_rootPath)
                                  .filePath(QStringLiteral("assets/indexes/%1.json")
                                                .arg(m_assetIndexId));
    const QByteArray data = readFileBytes(indexPath);
    if (data.isEmpty())
        return;

    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(data, &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject())
        return;

    const QJsonObject objects = document.object().value(QStringLiteral("objects")).toObject();
    for (auto it = objects.constBegin(); it != objects.constEnd(); ++it) {
        const QString hash = it.value().toObject().value(QStringLiteral("hash")).toString();
        if (hash.isEmpty())
            continue;
        const QString path = QStringLiteral("assets/") + it.key();
        m_objects.insert(path, hash);
        m_paths.append(path);
    }
    m_paths.sort(Qt::CaseInsensitive);
}

bool AssetObjectsProvider::exists(const QString &path) const
{
    ensureLoaded();
    return m_objects.contains(normalizeAssetPath(path));
}

QByteArray AssetObjectsProvider::read(const QString &path) const
{
    ensureLoaded();
    const QString hash = m_objects.value(normalizeAssetPath(path));
    if (hash.size() < 2)
        return {};
    const QString objectPath =
        QDir(m_rootPath)
            .filePath(QStringLiteral("assets/objects/%1/%2").arg(hash.left(2), hash));
    return readFileBytes(objectPath);
}

QStringList AssetObjectsProvider::list(const QString &prefix) const
{
    ensureLoaded();
    QStringList result;
    const QString normalizedPrefix = normalizeAssetPath(prefix);
    for (const QString &path : m_paths) {
        if (path.startsWith(normalizedPrefix))
            result.append(path);
    }
    return result;
}

QString AssetObjectsProvider::label() const
{
    return QStringLiteral("原版 assets 对象");
}

AssetOrigin AssetObjectsProvider::origin() const
{
    return AssetOrigin::Vanilla;
}

QString AssetObjectsProvider::detail(const QString &path) const
{
    const QString hash = m_objects.value(normalizeAssetPath(path));
    if (hash.isEmpty())
        return label();
    return QStringLiteral("%1 · %2").arg(label(), hash.left(8));
}

} // namespace mcstudio
