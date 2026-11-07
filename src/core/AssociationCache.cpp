#include "core/AssociationCache.h"

#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QSaveFile>

namespace mcstudio {
namespace {

QByteArray readBytes(const QString &path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return {};
    return file.readAll();
}

QJsonObject groupToJson(const TextureGroup &group)
{
    QJsonObject object;
    object.insert(QStringLiteral("id"), group.id);
    object.insert(QStringLiteral("kind"), group.kind);
    object.insert(QStringLiteral("namespace"), group.namespaceId);
    object.insert(QStringLiteral("modId"), group.modId);
    object.insert(QStringLiteral("modName"), group.modName);
    object.insert(QStringLiteral("name"), group.name);
    object.insert(QStringLiteral("displayName"), group.displayName);
    object.insert(QStringLiteral("modelSource"), group.modelSource);
    object.insert(QStringLiteral("textureSource"), group.textureSource);

    QJsonArray textures;
    for (const QString &texture : group.textures)
        textures.append(texture);
    object.insert(QStringLiteral("textures"), textures);

    QJsonArray missing;
    for (const QString &texture : group.missingTextures)
        missing.append(texture);
    object.insert(QStringLiteral("missingTextures"), missing);
    return object;
}

TextureGroup groupFromJson(const QJsonObject &object)
{
    TextureGroup group;
    group.id = object.value(QStringLiteral("id")).toString();
    group.kind = object.value(QStringLiteral("kind")).toString();
    group.namespaceId = object.value(QStringLiteral("namespace")).toString();
    group.modId = object.value(QStringLiteral("modId")).toString();
    group.modName = object.value(QStringLiteral("modName")).toString();
    group.name = object.value(QStringLiteral("name")).toString();
    group.displayName = object.value(QStringLiteral("displayName")).toString();
    group.modelSource = object.value(QStringLiteral("modelSource")).toString();
    group.textureSource = object.value(QStringLiteral("textureSource")).toString();
    for (const QJsonValue &value : object.value(QStringLiteral("textures")).toArray())
        group.textures.append(value.toString());
    for (const QJsonValue &value :
         object.value(QStringLiteral("missingTextures")).toArray()) {
        group.missingTextures.append(value.toString());
    }
    return group;
}

} // namespace

QString associationCacheFile(const Project &project)
{
    return QDir(project.rootDir).filePath(QStringLiteral("association.json"));
}

bool loadAssociationCache(const Project &project, const AssociationCacheKey &key,
                          AssociationResult *result)
{
    if (!result || (key.zipSha1.isEmpty() && key.contentFingerprint.isEmpty()))
        return false;

    const QByteArray bytes = readBytes(associationCacheFile(project));
    if (bytes.isEmpty())
        return false;

    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(bytes, &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject())
        return false;

    const QJsonObject root = document.object();
    if (root.value(QStringLiteral("schema")).toInt() != 2)
        return false;
    if (root.value(QStringLiteral("zipSha1")).toString() != key.zipSha1)
        return false;
    if (root.value(QStringLiteral("contentFingerprint")).toString()
        != key.contentFingerprint) {
        return false;
    }
    if (root.value(QStringLiteral("referenceInstanceKey")).toString()
        != key.referenceInstanceKey) {
        return false;
    }
    if (root.value(QStringLiteral("fallbackModels")).toBool() != key.fallbackModels)
        return false;
    if (root.value(QStringLiteral("fallbackLang")).toBool() != key.fallbackLang)
        return false;

    AssociationResult loaded;
    for (const QJsonValue &value : root.value(QStringLiteral("groups")).toArray())
        loaded.groups.append(groupFromJson(value.toObject()));
    for (const QJsonValue &value : root.value(QStringLiteral("missingModels")).toArray())
        loaded.missingModels.append(value.toString());
    for (const QJsonValue &value :
         root.value(QStringLiteral("missingTextures")).toArray()) {
        loaded.missingTextures.append(value.toString());
    }
    loaded.blockGroups = root.value(QStringLiteral("blockGroups")).toInt();
    loaded.itemGroups = root.value(QStringLiteral("itemGroups")).toInt();
    loaded.referenceGroups = root.value(QStringLiteral("referenceGroups")).toInt();
    loaded.jsonReads = root.value(QStringLiteral("jsonReads")).toInt();
    *result = std::move(loaded);
    return true;
}

bool saveAssociationCache(const Project &project, const AssociationCacheKey &key,
                          const AssociationResult &result)
{
    if (key.zipSha1.isEmpty() && key.contentFingerprint.isEmpty())
        return false;

    QJsonObject root;
    root.insert(QStringLiteral("schema"), 2);
    root.insert(QStringLiteral("zipSha1"), key.zipSha1);
    root.insert(QStringLiteral("contentFingerprint"), key.contentFingerprint);
    root.insert(QStringLiteral("referenceInstanceKey"), key.referenceInstanceKey);
    root.insert(QStringLiteral("fallbackModels"), key.fallbackModels);
    root.insert(QStringLiteral("fallbackLang"), key.fallbackLang);
    root.insert(QStringLiteral("packFormat"), project.packFormat);
    root.insert(QStringLiteral("blockGroups"), result.blockGroups);
    root.insert(QStringLiteral("itemGroups"), result.itemGroups);
    root.insert(QStringLiteral("referenceGroups"), result.referenceGroups);
    root.insert(QStringLiteral("jsonReads"), result.jsonReads);

    QJsonArray groups;
    for (const TextureGroup &group : result.groups)
        groups.append(groupToJson(group));
    root.insert(QStringLiteral("groups"), groups);

    QJsonArray missingModels;
    for (const QString &model : result.missingModels)
        missingModels.append(model);
    root.insert(QStringLiteral("missingModels"), missingModels);

    QJsonArray missingTextures;
    for (const QString &texture : result.missingTextures)
        missingTextures.append(texture);
    root.insert(QStringLiteral("missingTextures"), missingTextures);

    QSaveFile file(associationCacheFile(project));
    if (!file.open(QIODevice::WriteOnly))
        return false;
    file.write(QJsonDocument(root).toJson(QJsonDocument::Compact));
    return file.commit();
}

void removeAssociationCache(const Project &project)
{
    QFile::remove(associationCacheFile(project));
}

} // namespace mcstudio
