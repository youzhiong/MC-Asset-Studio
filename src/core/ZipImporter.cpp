#include "core/ZipImporter.h"

#include "core/Project.h"
#include "core/ZipArchive.h"

#include <QCryptographicHash>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QSaveFile>
#include <QSet>

namespace mcstudio {
namespace {

bool writeJson(const QString &path, const QJsonObject &object, QString *error)
{
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly)) {
        if (error)
            *error = QStringLiteral("无法写入 %1：%2").arg(path, file.errorString());
        return false;
    }
    file.write(QJsonDocument(object).toJson(QJsonDocument::Indented));
    if (!file.commit()) {
        if (error)
            *error = QStringLiteral("提交 %1 失败：%2").arg(path, file.errorString());
        return false;
    }
    return true;
}

QString fileSha1(const QString &path, QString *error)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        if (error)
            *error = QStringLiteral("无法读取 ZIP：%1").arg(path);
        return {};
    }

    QCryptographicHash hash(QCryptographicHash::Sha1);
    while (!file.atEnd()) {
        const QByteArray chunk = file.read(1024 * 1024);
        if (chunk.isEmpty() && file.error() != QFileDevice::NoError) {
            if (error)
                *error = QStringLiteral("读取 ZIP 失败：%1").arg(file.errorString());
            return {};
        }
        hash.addData(chunk);
    }
    return QString::fromLatin1(hash.result().toHex());
}

QString cleanEntryPath(const QString &path)
{
    QString result = QDir::fromNativeSeparators(path);
    while (result.startsWith(QLatin1Char('/')))
        result.remove(0, 1);
    return result;
}

QString safeTarget(const QString &baseDir, const QString &relative, bool *ok)
{
    *ok = false;
    const QString rel = cleanEntryPath(relative);
    if (rel.isEmpty() || rel.contains(QLatin1Char(':'))
        || rel.split(QLatin1Char('/')).contains(QStringLiteral(".."))) {
        return {};
    }

    const QString base = QDir(baseDir).absolutePath();
    const QString target = QDir::cleanPath(QDir(base).filePath(rel));
    if (target.compare(base, Qt::CaseInsensitive) == 0
        || !target.startsWith(base + QLatin1Char('/'), Qt::CaseInsensitive)) {
        return {};
    }
    *ok = true;
    return target;
}

QString detectPackRoot(const QList<ZipEntry> &entries)
{
    QString root;
    bool foundAtRoot = false;

    for (const ZipEntry &entry : entries) {
        if (entry.isDir)
            continue;
        const QString path = cleanEntryPath(entry.path);
        if (path.startsWith(QLatin1String("__MACOSX/")))
            continue;
        if (path == QLatin1String("pack.mcmeta")) {
            foundAtRoot = true;
            break;
        }
        if (path.endsWith(QLatin1String("/pack.mcmeta"))) {
            const QString dir = path.left(path.size()
                                          - QStringLiteral("pack.mcmeta").size());
            if (root.isEmpty() || dir.size() < root.size())
                root = dir;
        }
    }

    if (foundAtRoot)
        return {};
    return root;
}

QString descriptionFromJson(const QJsonValue &value)
{
    if (value.isString())
        return value.toString();
    if (value.isObject())
        return QString::fromUtf8(QJsonDocument(value.toObject()).toJson(QJsonDocument::Compact));
    return {};
}

} // namespace

ImportResult ZipImporter::import(const QString &zipPath, const QString &dataRoot,
                                 const Progress &progress)
{
    ImportResult result;
    result.sourceZip = QFileInfo(zipPath).absoluteFilePath();

    const QFileInfo zipInfo(result.sourceZip);
    if (!zipInfo.exists() || !zipInfo.isFile()) {
        result.error = QStringLiteral("ZIP 不存在：%1").arg(zipPath);
        return result;
    }

    if (progress)
        progress(0, 1, QStringLiteral("计算 ZIP 指纹…"));
    QString hashError;
    const QString sha1 = fileSha1(result.sourceZip, &hashError);
    if (sha1.isEmpty()) {
        result.error = hashError;
        return result;
    }

    // Content hash finds the shared source manifest; adding the canonical ZIP
    // path keeps two copies of the same pack as separate projects, so "覆盖原
    // ZIP" always points back to the file the user actually imported.
    QCryptographicHash idHash(QCryptographicHash::Sha1);
    idHash.addData(sha1.toLatin1());
    idHash.addData("|");
    idHash.addData(zipInfo.canonicalFilePath().toUtf8());
    result.projectId = QString::fromLatin1(idHash.result().toHex().left(16));

    result.projectDir = QDir(dataRoot).filePath(
        QStringLiteral("projects/%1").arg(result.projectId));
    const QString projectFile = QDir(result.projectDir).filePath(QStringLiteral("project.json"));
    const QString sourceDir = QDir(result.projectDir).filePath(QStringLiteral("source"));
    const QString packDir = QDir(result.projectDir).filePath(QStringLiteral("pack"));
    result.cacheManifestPath = QDir(dataRoot).filePath(
        QStringLiteral("cache/sources/%1.json").arg(sha1));

    // The source manifest is the only persistent cache (2A). If it is valid,
    // reuse the existing working copy instead of extracting the ZIP again.
    if (QFileInfo::exists(projectFile)
        && QFileInfo::exists(QDir(sourceDir).filePath(QStringLiteral("pack.mcmeta")))) {
        Project project;
        if (project.load(result.projectDir, &result.error)) {
            result.ok = true;
            result.name = project.name;
            result.description = project.description;
            result.packFormat = project.packFormat;
            result.supportedFormats = project.supportedFormats;
            result.fileCount = project.fileCount;
            result.fromCache = true;
            if (progress)
                progress(1, 1, QStringLiteral("已复用项目缓存"));
            return result;
        }
    }

    ZipArchive archive(result.sourceZip);
    if (!archive.isOpen()) {
        result.error = archive.error();
        return result;
    }

    const QList<ZipEntry> entries = archive.entries();
    const QString packRoot = detectPackRoot(entries);
    if (packRoot.isNull() && entries.isEmpty()) {
        result.error = QStringLiteral("ZIP 为空");
        return result;
    }

    bool hasPackMeta = false;
    for (const ZipEntry &entry : entries) {
        if (!entry.isDir && cleanEntryPath(entry.path) == packRoot + QStringLiteral("pack.mcmeta")) {
            hasPackMeta = true;
            break;
        }
    }
    if (!hasPackMeta) {
        result.error = QStringLiteral("不是有效的 Minecraft 资源包：找不到 pack.mcmeta");
        return result;
    }

    QSet<QString> seen;
    QList<ZipEntry> files;
    qint64 totalBytes = 0;
    for (const ZipEntry &entry : entries) {
        if (entry.isDir)
            continue;
        const QString path = cleanEntryPath(entry.path);
        if (!path.startsWith(packRoot))
            continue;
        const QString rel = path.mid(packRoot.size());
        if (rel.isEmpty())
            continue;
        if (rel.startsWith(QLatin1String("__MACOSX/"))
            || rel == QLatin1String(".DS_Store")) {
            continue;
        }
        if (seen.contains(rel))
            continue;
        seen.insert(rel);
        files.append(entry);
        totalBytes += entry.size;
    }

    const QByteArray packMetaBytes = archive.read(packRoot + QStringLiteral("pack.mcmeta"));
    QJsonParseError metaError;
    const QJsonDocument packMeta = QJsonDocument::fromJson(packMetaBytes, &metaError);
    if (metaError.error != QJsonParseError::NoError || !packMeta.isObject()) {
        result.error = QStringLiteral("pack.mcmeta 不是有效 JSON：%1")
                           .arg(metaError.errorString());
        return result;
    }

    const QJsonObject packObject = packMeta.object()
                                       .value(QStringLiteral("pack")).toObject();
    const QJsonValue rawFormat = packObject.value(QStringLiteral("pack_format"));
    if (rawFormat.isObject()) {
        const QJsonObject range = rawFormat.toObject();
        const int min = range.value(QStringLiteral("min_inclusive")).toInt(15);
        const int max = range.value(QStringLiteral("max_inclusive")).toInt(min);
        result.packFormat = min;
        for (int format = min; format <= max && format - min < 256; ++format)
            result.supportedFormats.append(format);
    } else {
        result.packFormat = rawFormat.toInt(15);
    }

    const QJsonValue supportedFormats = packObject.value(QStringLiteral("supported_formats"));
    if (supportedFormats.isArray()) {
        for (const QJsonValue &value : supportedFormats.toArray()) {
            const int format = value.toInt();
            if (format > 0 && !result.supportedFormats.contains(format))
                result.supportedFormats.append(format);
        }
    } else if (supportedFormats.isObject()) {
        const QJsonObject range = supportedFormats.toObject();
        const int min = range.value(QStringLiteral("min_inclusive")).toInt();
        const int max = range.value(QStringLiteral("max_inclusive")).toInt(min);
        for (int format = min; format <= max && format - min < 256; ++format) {
            if (format > 0 && !result.supportedFormats.contains(format))
                result.supportedFormats.append(format);
        }
    }
    result.description = descriptionFromJson(packObject.value(QStringLiteral("description")));
    result.name = zipInfo.completeBaseName();
    if (result.name.isEmpty())
        result.name = result.projectId;

    if (QFileInfo::exists(sourceDir))
        QDir(sourceDir).removeRecursively();
    QDir().mkpath(sourceDir);
    QDir().mkpath(packDir);
    QDir().mkpath(QFileInfo(result.cacheManifestPath).absolutePath());

    if (progress)
        progress(0, files.size(), QStringLiteral("准备解包…"));

    QJsonArray manifestEntries;
    int done = 0;
    for (const ZipEntry &entry : files) {
        const QString path = cleanEntryPath(entry.path);
        const QString rel = path.mid(packRoot.size());
        bool safe = false;
        const QString target = safeTarget(sourceDir, rel, &safe);
        if (!safe) {
            result.error = QStringLiteral("ZIP 包含非法路径：%1").arg(entry.path);
            QDir(sourceDir).removeRecursively();
            return result;
        }

        if (!QDir().mkpath(QFileInfo(target).absolutePath())) {
            result.error = QStringLiteral("无法创建目录：%1")
                               .arg(QFileInfo(target).absolutePath());
            QDir(sourceDir).removeRecursively();
            return result;
        }

        const QByteArray bytes = archive.read(entry.path);
        QFile file(target);
        if (!file.open(QIODevice::WriteOnly)) {
            result.error = QStringLiteral("无法写入 %1：%2").arg(target, file.errorString());
            QDir(sourceDir).removeRecursively();
            return result;
        }
        if (file.write(bytes) != bytes.size()) {
            result.error = QStringLiteral("写入不完整：%1").arg(target);
            QDir(sourceDir).removeRecursively();
            return result;
        }
        file.close();

        QJsonObject item;
        item.insert(QStringLiteral("path"), rel);
        item.insert(QStringLiteral("size"), entry.size);
        item.insert(QStringLiteral("crc"), static_cast<qint64>(entry.crc));
        manifestEntries.append(item);

        ++done;
        if (progress && (done % 20 == 0 || done == files.size()))
            progress(done, files.size(), QStringLiteral("解包 %1").arg(rel));
    }

    QJsonObject manifest;
    manifest.insert(QStringLiteral("schema"), 1);
    manifest.insert(QStringLiteral("projectId"), result.projectId);
    manifest.insert(QStringLiteral("zipPath"), result.sourceZip);
    manifest.insert(QStringLiteral("zipSha1"), sha1);
    manifest.insert(QStringLiteral("zipSize"), static_cast<qint64>(zipInfo.size()));
    manifest.insert(QStringLiteral("zipMtime"),
                    static_cast<qint64>(zipInfo.lastModified().toMSecsSinceEpoch()));
    manifest.insert(QStringLiteral("packRoot"), packRoot);
    manifest.insert(QStringLiteral("packFormat"), result.packFormat);
    manifest.insert(QStringLiteral("totalBytes"), totalBytes);
    manifest.insert(QStringLiteral("entries"), manifestEntries);
    manifest.insert(QStringLiteral("importedAt"),
                    QDateTime::currentDateTimeUtc().toString(Qt::ISODate));

    if (!writeJson(result.cacheManifestPath, manifest, &result.error))
        return result;

    Project project;
    project.id = result.projectId;
    project.name = result.name;
    project.rootDir = result.projectDir;
    project.sourceDir = sourceDir;
    project.packDir = packDir;
    project.sourceZip = result.sourceZip;
    project.zipSha1 = sha1;
    project.packRoot = packRoot;
    project.description = result.description;
    project.cacheManifestPath = result.cacheManifestPath;
    project.createdAt = QDateTime::currentDateTimeUtc().toString(Qt::ISODate);
    project.updatedAt = project.createdAt;
    project.packFormat = result.packFormat;
    project.supportedFormats = result.supportedFormats;
    project.scan();

    if (!project.save(&result.error))
        return result;

    result.ok = true;
    result.fileCount = project.fileCount;
    if (progress)
        progress(files.size(), files.size(), QStringLiteral("导入完成"));
    return result;
}

} // namespace mcstudio
