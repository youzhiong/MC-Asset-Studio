#include "core/ProjectService.h"

#include "core/PathUtils.h"

#include <QDateTime>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QSaveFile>
#include <QSet>
#include <QUuid>

#include <algorithm>

namespace mcstudio {
namespace {

bool writeBytes(const QString &path, const QByteArray &bytes, QString *error)
{
    if (!QDir().mkpath(QFileInfo(path).absolutePath())) {
        if (error)
            *error = QStringLiteral("无法创建目录：%1").arg(QFileInfo(path).absolutePath());
        return false;
    }
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly)) {
        if (error)
            *error = QStringLiteral("无法写入 %1：%2").arg(path, file.errorString());
        return false;
    }
    file.write(bytes);
    if (!file.commit()) {
        if (error)
            *error = QStringLiteral("提交 %1 失败：%2").arg(path, file.errorString());
        return false;
    }
    return true;
}

QJsonObject readJson(const QString &path, bool *ok = nullptr)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        if (ok)
            *ok = false;
        return {};
    }
    QByteArray data = file.readAll();
    static const char kUtf8Bom[] = "\xEF\xBB\xBF";
    if (data.startsWith(kUtf8Bom))
        data.remove(0, 3);
    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(data, &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
        if (ok)
            *ok = false;
        return {};
    }
    if (ok)
        *ok = true;
    return document.object();
}

QString packDescription(const QJsonObject &root)
{
    const QJsonValue description =
        root.value(QStringLiteral("pack")).toObject().value(QStringLiteral("description"));
    if (description.isString())
        return description.toString();
    if (description.isObject()) {
        return QString::fromUtf8(
            QJsonDocument(description.toObject()).toJson(QJsonDocument::Compact));
    }
    if (description.isArray()) {
        return QString::fromUtf8(
            QJsonDocument(description.toArray()).toJson(QJsonDocument::Compact));
    }
    return {};
}

ProjectCreateResult failure(const QString &error)
{
    ProjectCreateResult result;
    result.error = error;
    return result;
}

} // namespace

QString ProjectService::newProjectId()
{
    const QString stamp =
        QDateTime::currentDateTime().toString(QStringLiteral("yyyyMMdd_HHmmss"));
    const QString random =
        QUuid::createUuid().toString(QUuid::WithoutBraces).left(8);
    return QStringLiteral("p_%1_%2").arg(stamp, random);
}

QByteArray ProjectService::buildPackMeta(int packFormat,
                                         const QVector<int> &supportedFormats,
                                         const QString &description)
{
    QJsonObject pack;
    pack.insert(QStringLiteral("pack_format"), packFormat);
    pack.insert(QStringLiteral("description"), description);

    if (!supportedFormats.isEmpty()) {
        QJsonArray formats;
        for (int format : supportedFormats) {
            if (format > 0)
                formats.append(format);
        }
        if (!formats.isEmpty())
            pack.insert(QStringLiteral("supported_formats"), formats);
    }

    QJsonObject root;
    root.insert(QStringLiteral("pack"), pack);
    return QJsonDocument(root).toJson(QJsonDocument::Indented);
}

ProjectCreateResult ProjectService::createBlank(const QString &projectsRoot,
                                                const NewProjectRequest &request)
{
    if (request.name.trimmed().isEmpty())
        return failure(QStringLiteral("项目名称不能为空。"));
    if (request.packFormat <= 0)
        return failure(QStringLiteral("pack_format 必须大于 0。"));

    const QString id = newProjectId();
    const QString rootDir = QDir(projectsRoot).filePath(id);
    const QString sourceDir = QDir(rootDir).filePath(QStringLiteral("source"));
    const QString packDir = QDir(rootDir).filePath(QStringLiteral("pack"));
    if (!QDir().mkpath(sourceDir) || !QDir().mkpath(packDir))
        return failure(QStringLiteral("无法创建项目目录：%1").arg(rootDir));
    QDir().mkpath(QDir(sourceDir).filePath(QStringLiteral("assets")));
    QDir().mkpath(QDir(packDir).filePath(QStringLiteral("assets")));

    QString error;
    const QByteArray packMeta =
        buildPackMeta(request.packFormat, request.supportedFormats, request.description);
    if (!writeBytes(QDir(sourceDir).filePath(QStringLiteral("pack.mcmeta")), packMeta,
                    &error)) {
        QDir(rootDir).removeRecursively();
        return failure(error);
    }
    if (!request.packPng.isEmpty()) {
        if (!writeBytes(QDir(sourceDir).filePath(QStringLiteral("pack.png")),
                        request.packPng, &error)) {
            QDir(rootDir).removeRecursively();
            return failure(error);
        }
    }

    Project project;
    project.id = id;
    project.name = request.name.trimmed();
    project.rootDir = rootDir;
    project.sourceDir = sourceDir;
    project.packDir = packDir;
    project.origin = QStringLiteral("created");
    project.originPath.clear();
    project.description = request.description;
    project.packFormat = request.packFormat;
    project.supportedFormats = request.supportedFormats;
    project.createdAt = QDateTime::currentDateTimeUtc().toString(Qt::ISODate);
    project.updatedAt = project.createdAt;
    project.scan();
    if (!project.save(&error)) {
        QDir(rootDir).removeRecursively();
        return failure(error);
    }

    ProjectCreateResult result;
    result.ok = true;
    result.projectDir = rootDir;
    result.projectId = id;
    return result;
}

ProjectCreateResult ProjectService::importFolder(const QString &folderPath,
                                                 const QString &projectsRoot,
                                                 const QString &name,
                                                 const Progress &progress)
{
    const QString sourceRoot = normalizedPath(folderPath);
    if (sourceRoot.isEmpty() || !QFileInfo(sourceRoot).isDir())
        return failure(QStringLiteral("文件夹不存在：%1").arg(folderPath));
    const QString packMetaPath =
        QDir(sourceRoot).filePath(QStringLiteral("pack.mcmeta"));
    if (!QFileInfo::exists(packMetaPath)) {
        return failure(QStringLiteral("所选文件夹不是资源包（缺少 pack.mcmeta）：%1")
                           .arg(sourceRoot));
    }

    const QString id = newProjectId();
    const QString rootDir = QDir(projectsRoot).filePath(id);
    const QString sourceDir = QDir(rootDir).filePath(QStringLiteral("source"));
    const QString packDir = QDir(rootDir).filePath(QStringLiteral("pack"));
    if (!QDir().mkpath(sourceDir) || !QDir().mkpath(packDir)) {
        return failure(QStringLiteral("无法创建项目目录：%1").arg(rootDir));
    }

    QStringList files;
    QDirIterator fileIterator(sourceRoot, QDir::Files | QDir::NoDotAndDotDot,
                              QDirIterator::Subdirectories);
    while (fileIterator.hasNext())
        files.append(fileIterator.next());

    int done = 0;
    const int total = files.size();
    for (const QString &sourceFile : files) {
        const QString relative =
            QDir::fromNativeSeparators(QDir(sourceRoot).relativeFilePath(sourceFile));
        const QString target = QDir(sourceDir).filePath(relative);
        if (!QDir().mkpath(QFileInfo(target).absolutePath())) {
            QDir(rootDir).removeRecursively();
            return failure(QStringLiteral("无法创建目录：%1").arg(QFileInfo(target).absolutePath()));
        }
        if (!QFile::copy(sourceFile, target)) {
            QDir(rootDir).removeRecursively();
            return failure(QStringLiteral("复制文件失败：%1").arg(sourceFile));
        }
        ++done;
        if (progress && (done % 20 == 0 || done == total))
            progress(done, total, QStringLiteral("正在复制资源包文件…"));
    }

    // Preserve empty directories too (e.g. prepared assets/<namespace>/ folders).
    QDirIterator dirIterator(sourceRoot, QDir::Dirs | QDir::NoDotAndDotDot,
                             QDirIterator::Subdirectories);
    while (dirIterator.hasNext()) {
        const QString sourceDirPath = dirIterator.next();
        const QString relative = QDir::fromNativeSeparators(
            QDir(sourceRoot).relativeFilePath(sourceDirPath));
        QDir().mkpath(QDir(sourceDir).filePath(relative));
    }

    const QJsonObject packMeta = readJson(packMetaPath);
    const QJsonObject packObject = packMeta.value(QStringLiteral("pack")).toObject();
    QVector<int> supportedFormats;
    const QJsonValue supported = packObject.value(QStringLiteral("supported_formats"));
    if (supported.isArray()) {
        for (const QJsonValue &value : supported.toArray()) {
            const int format = value.toInt();
            if (format > 0 && !supportedFormats.contains(format))
                supportedFormats.append(format);
        }
    } else if (supported.isObject()) {
        const QJsonObject range = supported.toObject();
        const int min = range.value(QStringLiteral("min_inclusive")).toInt();
        const int max = range.value(QStringLiteral("max_inclusive")).toInt(min);
        for (int format = min; format <= max && format - min < 256; ++format) {
            if (format > 0 && !supportedFormats.contains(format))
                supportedFormats.append(format);
        }
    }

    Project project;
    project.id = id;
    project.name = name.trimmed().isEmpty() ? QFileInfo(sourceRoot).fileName()
                                            : name.trimmed();
    project.rootDir = rootDir;
    project.sourceDir = sourceDir;
    project.packDir = packDir;
    project.origin = QStringLiteral("folder");
    project.originPath = sourceRoot;
    project.packFormat = packObject.value(QStringLiteral("pack_format")).toInt(15);
    project.supportedFormats = supportedFormats;
    project.description = packDescription(packMeta);
    project.createdAt = QDateTime::currentDateTimeUtc().toString(Qt::ISODate);
    project.updatedAt = project.createdAt;
    project.scan();

    QString error;
    if (!project.save(&error)) {
        QDir(rootDir).removeRecursively();
        return failure(error);
    }

    ProjectCreateResult result;
    result.ok = true;
    result.projectDir = rootDir;
    result.projectId = id;
    return result;
}

QVector<ProjectSummary> ProjectService::listProjects(const QString &projectsRoot)
{
    QVector<ProjectSummary> result;
    const QDir root(projectsRoot);
    if (!root.exists())
        return result;

    const QFileInfoList directories =
        root.entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name);
    for (const QFileInfo &directory : directories) {
        const QString projectFile =
            QDir(directory.absoluteFilePath()).filePath(QStringLiteral("project.json"));
        bool ok = false;
        const QJsonObject object = readJson(projectFile, &ok);
        if (!ok)
            continue;

        ProjectSummary summary;
        summary.projectDir = directory.absoluteFilePath();
        summary.id = object.value(QStringLiteral("id")).toString();
        if (summary.id.isEmpty())
            summary.id = directory.fileName();
        summary.name = object.value(QStringLiteral("name")).toString();
        if (summary.name.isEmpty())
            summary.name = summary.id;
        summary.origin = object.value(QStringLiteral("origin")).toString();
        if (summary.origin.isEmpty())
            summary.origin = QStringLiteral("zip");
        summary.originPath = object.value(QStringLiteral("originPath")).toString();
        summary.updatedAt = object.value(QStringLiteral("updatedAt")).toString();
        summary.packFormat = object.value(QStringLiteral("packFormat")).toInt(15);
        result.append(summary);
    }

    std::sort(result.begin(), result.end(),
              [](const ProjectSummary &a, const ProjectSummary &b) {
                  if (a.updatedAt != b.updatedAt)
                      return a.updatedAt > b.updatedAt;
                  return a.name.localeAwareCompare(b.name) < 0;
              });
    return result;
}

} // namespace mcstudio
