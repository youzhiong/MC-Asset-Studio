#include "core/Project.h"

#include "core/AppPaths.h"

#include <QDateTime>
#include <QCryptographicHash>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>
#include <QSet>

namespace mcstudio {
namespace {

QString normalizedRelative(const QString &path)
{
    QString result = QDir::fromNativeSeparators(path);
    while (result.startsWith(QLatin1Char('/')))
        result.remove(0, 1);
    return result;
}

QString jsonString(const QJsonObject &object, const QString &key)
{
    return object.value(key).toString();
}

QVector<int> supportedFormatsFromPackMeta(const QString &path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return {};

    QByteArray data = file.readAll();
    static const char kUtf8Bom[] = "\xEF\xBB\xBF";
    if (data.startsWith(kUtf8Bom))
        data.remove(0, 3);

    const QJsonDocument document = QJsonDocument::fromJson(data);
    if (!document.isObject())
        return {};

    const QJsonObject pack = document.object().value(QStringLiteral("pack")).toObject();
    QVector<int> formats;

    const QJsonValue supported = pack.value(QStringLiteral("supported_formats"));
    if (supported.isArray()) {
        for (const QJsonValue &value : supported.toArray()) {
            const int format = value.toInt();
            if (format > 0 && !formats.contains(format))
                formats.append(format);
        }
    } else if (supported.isObject()) {
        const QJsonObject range = supported.toObject();
        const int min = range.value(QStringLiteral("min_inclusive")).toInt();
        const int max = range.value(QStringLiteral("max_inclusive")).toInt(min);
        for (int format = min; format <= max && format - min < 256; ++format) {
            if (format > 0 && !formats.contains(format))
                formats.append(format);
        }
    }
    return formats;
}

} // namespace

bool Project::load(const QString &projectDir, QString *error)
{
    valid = false;
    lastError.clear();
    rootDir = QDir(projectDir).absolutePath();
    sourceDir = QDir(rootDir).filePath(QStringLiteral("source"));
    packDir = QDir(rootDir).filePath(QStringLiteral("pack"));

    QFile file(QDir(rootDir).filePath(QStringLiteral("project.json")));
    if (!file.open(QIODevice::ReadOnly)) {
        lastError = QStringLiteral("无法读取项目描述：%1").arg(file.fileName());
        if (error)
            *error = lastError;
        return false;
    }

    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(file.readAll(), &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
        lastError = QStringLiteral("project.json 格式错误：%1").arg(parseError.errorString());
        if (error)
            *error = lastError;
        return false;
    }

    const QJsonObject object = document.object();
    id = jsonString(object, QStringLiteral("id"));
    name = jsonString(object, QStringLiteral("name"));
    sourceZip = jsonString(object, QStringLiteral("sourceZip"));
    zipSha1 = jsonString(object, QStringLiteral("zipSha1"));
    packRoot = jsonString(object, QStringLiteral("packRoot"));
    origin = jsonString(object, QStringLiteral("origin"));
    if (origin.isEmpty())
        origin = QStringLiteral("zip");
    originPath = jsonString(object, QStringLiteral("originPath"));
    for (const QJsonValue &value :
         object.value(QStringLiteral("supportedNamespaces")).toArray()) {
        const QString ns = value.toString();
        if (!ns.isEmpty() && !supportedNamespaces.contains(ns))
            supportedNamespaces.append(ns);
    }
    description = jsonString(object, QStringLiteral("description"));
    cacheManifestPath = jsonString(object, QStringLiteral("cacheManifestPath"));
    createdAt = jsonString(object, QStringLiteral("createdAt"));
    updatedAt = jsonString(object, QStringLiteral("updatedAt"));
    packFormat = object.value(QStringLiteral("packFormat")).toInt(15);
    supportedFormats.clear();
    for (const QJsonValue &value : object.value(QStringLiteral("supportedFormats")).toArray()) {
        const int format = value.toInt();
        if (format > 0 && !supportedFormats.contains(format))
            supportedFormats.append(format);
    }
    // Projects imported before supported_formats was persisted can recover it
    // from the extracted pack.mcmeta.
    if (supportedFormats.isEmpty()) {
        supportedFormats = supportedFormatsFromPackMeta(
            QDir(sourceDir).filePath(QStringLiteral("pack.mcmeta")));
    }

    // Recompute instead of trusting a stored absolute path so the app folder
    // can be moved or updated without orphaning the source cache.
    if (!zipSha1.isEmpty())
        cacheManifestPath = QDir(AppPaths::sourceCacheRoot())
                                .filePath(zipSha1 + QStringLiteral(".json"));

    if (id.isEmpty())
        id = QFileInfo(rootDir).fileName();
    if (name.isEmpty())
        name = id;

    if (!QFileInfo::exists(QDir(sourceDir).filePath(QStringLiteral("pack.mcmeta")))) {
        lastError = QStringLiteral("项目缺少 source/pack.mcmeta：%1").arg(sourceDir);
        if (error)
            *error = lastError;
        return false;
    }

    scan();
    loadReferenceConfig();
    valid = true;
    return true;
}

bool Project::save(QString *error) const
{
    QJsonObject object;
    object.insert(QStringLiteral("format"), 2);
    object.insert(QStringLiteral("id"), id);
    object.insert(QStringLiteral("name"), name);
    object.insert(QStringLiteral("sourceZip"), sourceZip);
    object.insert(QStringLiteral("zipSha1"), zipSha1);
    object.insert(QStringLiteral("packRoot"), packRoot);
    object.insert(QStringLiteral("origin"), origin);
    object.insert(QStringLiteral("originPath"), originPath);
    QJsonArray supportedNamespacesArray;
    for (const QString &ns : supportedNamespaces)
        supportedNamespacesArray.append(ns);
    object.insert(QStringLiteral("supportedNamespaces"), supportedNamespacesArray);
    object.insert(QStringLiteral("description"), description);
    object.insert(QStringLiteral("cacheManifestPath"), cacheManifestPath);
    object.insert(QStringLiteral("packFormat"), packFormat);
    QJsonArray supportedFormatsArray;
    for (int format : supportedFormats)
        supportedFormatsArray.append(format);
    object.insert(QStringLiteral("supportedFormats"), supportedFormatsArray);
    object.insert(QStringLiteral("sourceDir"), QStringLiteral("source"));
    object.insert(QStringLiteral("packDir"), QStringLiteral("pack"));
    object.insert(QStringLiteral("createdAt"), createdAt);
    object.insert(QStringLiteral("updatedAt"),
                  updatedAt.isEmpty() ? QDateTime::currentDateTimeUtc().toString(Qt::ISODate)
                                      : QDateTime::currentDateTimeUtc().toString(Qt::ISODate));

    QSaveFile file(QDir(rootDir).filePath(QStringLiteral("project.json")));
    if (!file.open(QIODevice::WriteOnly)) {
        if (error)
            *error = QStringLiteral("无法写入 project.json：%1").arg(file.errorString());
        return false;
    }
    file.write(QJsonDocument(object).toJson(QJsonDocument::Indented));
    if (!file.commit()) {
        if (error)
            *error = QStringLiteral("提交 project.json 失败：%1").arg(file.errorString());
        return false;
    }
    return true;
}

bool Project::loadReferenceConfig(QString *error)
{
    referenceInstanceKey.clear();
    const QString path = QDir(rootDir).filePath(QStringLiteral("reference.json"));
    QFile file(path);
    if (!file.exists())
        return true;
    if (!file.open(QIODevice::ReadOnly)) {
        if (error)
            *error = QStringLiteral("无法读取 reference.json：%1").arg(file.errorString());
        return false;
    }

    QByteArray data = file.readAll();
    static const char kUtf8Bom[] = "\xEF\xBB\xBF";
    if (data.startsWith(kUtf8Bom))
        data.remove(0, 3);

    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(data, &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
        if (error)
            *error = QStringLiteral("reference.json 格式错误：%1")
                         .arg(parseError.errorString());
        return false;
    }
    referenceInstanceKey =
        document.object().value(QStringLiteral("instanceKey")).toString();
    return true;
}

bool Project::saveReferenceConfig(QString *error) const
{
    QJsonObject object;
    object.insert(QStringLiteral("format"), 1);
    object.insert(QStringLiteral("instanceKey"), referenceInstanceKey);
    object.insert(QStringLiteral("mode"),
                  referenceInstanceKey.isEmpty() ? QStringLiteral("auto")
                                                 : QStringLiteral("manual"));
    object.insert(QStringLiteral("updatedAt"),
                  QDateTime::currentDateTimeUtc().toString(Qt::ISODate));

    QSaveFile file(QDir(rootDir).filePath(QStringLiteral("reference.json")));
    if (!file.open(QIODevice::WriteOnly)) {
        if (error)
            *error = QStringLiteral("无法写入 reference.json：%1").arg(file.errorString());
        return false;
    }
    file.write(QJsonDocument(object).toJson(QJsonDocument::Indented));
    if (!file.commit()) {
        if (error)
            *error = QStringLiteral("提交 reference.json 失败：%1").arg(file.errorString());
        return false;
    }
    return true;
}

void Project::scan()
{
    m_files.clear();
    m_paths.clear();

    const auto scanRoot = [this](const QString &root, const QString &sourceName) {
        if (!QFileInfo::exists(root))
            return;
        QDir base(root);
        QDirIterator iterator(root, QDir::Files | QDir::NoDotAndDotDot,
                              QDirIterator::Subdirectories);
        while (iterator.hasNext()) {
            const QString absolute = iterator.next();
            const QFileInfo info(absolute);
            ProjectFile file;
            file.path = normalizedRelative(base.relativeFilePath(absolute));
            file.source = sourceName;
            file.size = info.size();
            file.mtime = info.lastModified().toMSecsSinceEpoch();
            file.isTexture = file.path.endsWith(QLatin1String(".png"), Qt::CaseInsensitive);
            file.isJson = file.path.endsWith(QLatin1String(".json"), Qt::CaseInsensitive)
                          || file.path.endsWith(QLatin1String(".mcmeta"), Qt::CaseInsensitive);
            m_files.insert(file.path, file);
        }
    };

    // Source first, then the output overlay wins on duplicate paths.
    scanRoot(sourceDir, QStringLiteral("source"));
    scanRoot(packDir, QStringLiteral("pack"));

    m_paths = m_files.keys();
    m_paths.sort(Qt::CaseInsensitive);
    fileCount = m_paths.size();

    QCryptographicHash fingerprint(QCryptographicHash::Sha1);
    for (const QString &path : std::as_const(m_paths)) {
        const ProjectFile &file = m_files.value(path);
        fingerprint.addData(path.toUtf8());
        fingerprint.addData(file.source.toUtf8());
        fingerprint.addData(QByteArray::number(file.size));
        fingerprint.addData(QByteArray::number(file.mtime));
    }
    contentFingerprint = QString::fromLatin1(fingerprint.result().toHex());
}

bool Project::exists(const QString &relativePath) const
{
    const QString rel = normalizedRelative(relativePath);
    return QFileInfo::exists(QDir(packDir).filePath(rel))
           || QFileInfo::exists(QDir(sourceDir).filePath(rel));
}

QByteArray Project::readBytes(const QString &relativePath) const
{
    const QString rel = normalizedRelative(relativePath);
    const QString packed = QDir(packDir).filePath(rel);
    QFile file(packed);
    if (!file.exists())
        file.setFileName(QDir(sourceDir).filePath(rel));
    if (!file.open(QIODevice::ReadOnly))
        return {};
    return file.readAll();
}

QString Project::absolutePath(const QString &relativePath) const
{
    const QString rel = normalizedRelative(relativePath);
    const QString packed = QDir(packDir).filePath(rel);
    if (QFileInfo::exists(packed))
        return packed;
    return QDir(sourceDir).filePath(rel);
}

QString Project::absoluteSourcePath(const QString &relativePath) const
{
    return QDir(sourceDir).filePath(normalizedRelative(relativePath));
}

QString Project::absoluteOutputPath(const QString &relativePath) const
{
    return QDir(packDir).filePath(normalizedRelative(relativePath));
}

bool Project::writeOutputFile(const QString &relativePath, const QByteArray &bytes,
                              QString *error) const
{
    const QString rel = normalizedRelative(relativePath);
    if (rel.isEmpty() || rel.contains(QLatin1String(".."))
        || rel.contains(QLatin1Char(':')) || QDir::isAbsolutePath(rel)) {
        if (error)
            *error = QStringLiteral("非法资源路径：%1").arg(relativePath);
        return false;
    }

    const QString target = QDir(packDir).filePath(rel);
    if (!QDir().mkpath(QFileInfo(target).absolutePath())) {
        if (error)
            *error = QStringLiteral("无法创建目录：%1")
                         .arg(QFileInfo(target).absolutePath());
        return false;
    }

    QSaveFile file(target);
    if (!file.open(QIODevice::WriteOnly)) {
        if (error)
            *error = QStringLiteral("无法写入 %1：%2").arg(rel, file.errorString());
        return false;
    }
    if (file.write(bytes) != bytes.size()) {
        if (error)
            *error = QStringLiteral("写入不完整：%1").arg(rel);
        file.cancelWriting();
        return false;
    }
    if (!file.commit()) {
        if (error)
            *error = QStringLiteral("提交 %1 失败：%2").arg(rel, file.errorString());
        return false;
    }
    return true;
}

bool Project::removeOutputFile(const QString &relativePath) const
{
    const QString rel = normalizedRelative(relativePath);
    if (rel.isEmpty() || rel.contains(QLatin1String(".."))
        || rel.contains(QLatin1Char(':')) || QDir::isAbsolutePath(rel)) {
        return false;
    }
    const QString target = QDir(packDir).filePath(rel);
    if (!QFileInfo::exists(target))
        return true;
    return QFile::remove(target);
}

} // namespace mcstudio
