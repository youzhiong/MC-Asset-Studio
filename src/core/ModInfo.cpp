#include "core/ModInfo.h"

#include <QJsonArray>

namespace mcstudio {
namespace {

QString jsonString(const QJsonObject &object, const QString &key)
{
    return object.value(key).toString();
}

qint64 jsonInt64(const QJsonObject &object, const QString &key, qint64 fallback = 0)
{
    const QJsonValue value = object.value(key);
    return value.isDouble() ? static_cast<qint64>(value.toDouble()) : fallback;
}

} // namespace

QString ModInfo::displayLabel() const
{
    if (!displayName.isEmpty())
        return displayName;
    if (!modId.isEmpty())
        return modId;
    if (!fileName.isEmpty())
        return fileName;
    return QStringLiteral("未知模组");
}

QJsonObject ModInfo::toJson() const
{
    QJsonObject object;
    object.insert(QStringLiteral("jarPath"), jarPath);
    object.insert(QStringLiteral("fileName"), fileName);
    object.insert(QStringLiteral("instanceKey"), instanceKey);
    object.insert(QStringLiteral("origin"), origin);
    object.insert(QStringLiteral("modId"), modId);
    object.insert(QStringLiteral("displayName"), displayName);
    object.insert(QStringLiteral("version"), version);
    object.insert(QStringLiteral("description"), description);
    object.insert(QStringLiteral("authors"), authors);
    object.insert(QStringLiteral("logoFile"), logoFile);
    object.insert(QStringLiteral("loader"), loader);
    object.insert(QStringLiteral("metadataSource"), metadataSource);
    object.insert(QStringLiteral("minecraftRange"), minecraftRange);
    object.insert(QStringLiteral("status"), status);
    object.insert(QStringLiteral("error"), error);

    QJsonArray namespaceArray;
    for (const QString &ns : namespaces)
        namespaceArray.append(ns);
    object.insert(QStringLiteral("namespaces"), namespaceArray);
    return object;
}

ModInfo ModInfo::fromJson(const QJsonObject &object)
{
    ModInfo info;
    info.jarPath = jsonString(object, QStringLiteral("jarPath"));
    info.fileName = jsonString(object, QStringLiteral("fileName"));
    info.instanceKey = jsonString(object, QStringLiteral("instanceKey"));
    info.origin = jsonString(object, QStringLiteral("origin"));
    info.modId = jsonString(object, QStringLiteral("modId"));
    info.displayName = jsonString(object, QStringLiteral("displayName"));
    info.version = jsonString(object, QStringLiteral("version"));
    info.description = jsonString(object, QStringLiteral("description"));
    info.authors = jsonString(object, QStringLiteral("authors"));
    info.logoFile = jsonString(object, QStringLiteral("logoFile"));
    info.loader = jsonString(object, QStringLiteral("loader"));
    info.metadataSource = jsonString(object, QStringLiteral("metadataSource"));
    info.minecraftRange = jsonString(object, QStringLiteral("minecraftRange"));
    info.status = jsonString(object, QStringLiteral("status"));
    if (info.status.isEmpty())
        info.status = QStringLiteral("ok");
    info.error = jsonString(object, QStringLiteral("error"));
    for (const QJsonValue &value : object.value(QStringLiteral("namespaces")).toArray())
        info.namespaces.append(value.toString());
    return info;
}

QString JarModScan::primaryModId() const
{
    for (const ModInfo &mod : mods) {
        if (!mod.modId.isEmpty())
            return mod.modId;
    }
    return {};
}

QString JarModScan::primaryDisplayName() const
{
    for (const ModInfo &mod : mods) {
        if (!mod.displayName.isEmpty())
            return mod.displayName;
    }
    for (const ModInfo &mod : mods) {
        if (!mod.modId.isEmpty())
            return mod.modId;
    }
    return {};
}

QJsonObject JarModScan::toJson() const
{
    QJsonObject object;
    object.insert(QStringLiteral("schema"), 2);
    object.insert(QStringLiteral("jarPath"), jarPath);
    object.insert(QStringLiteral("fileName"), fileName);
    object.insert(QStringLiteral("instanceKey"), instanceKey);
    object.insert(QStringLiteral("origin"), origin);
    object.insert(QStringLiteral("size"), static_cast<double>(size));
    object.insert(QStringLiteral("mtime"), static_cast<double>(mtime));
    object.insert(QStringLiteral("status"), status);
    object.insert(QStringLiteral("error"), error);
    object.insert(QStringLiteral("scannedAt"), scannedAt);

    QJsonArray namespaceArray;
    for (const QString &ns : namespaces)
        namespaceArray.append(ns);
    object.insert(QStringLiteral("namespaces"), namespaceArray);

    QJsonObject countObject;
    for (auto it = namespaceCounts.constBegin(); it != namespaceCounts.constEnd(); ++it)
        countObject.insert(it.key(), it.value());
    object.insert(QStringLiteral("namespaceCounts"), countObject);

    QJsonArray modsArray;
    for (const ModInfo &mod : mods)
        modsArray.append(mod.toJson());
    object.insert(QStringLiteral("mods"), modsArray);
    return object;
}

JarModScan JarModScan::fromJson(const QJsonObject &object)
{
    JarModScan scan;
    scan.jarPath = jsonString(object, QStringLiteral("jarPath"));
    scan.fileName = jsonString(object, QStringLiteral("fileName"));
    scan.instanceKey = jsonString(object, QStringLiteral("instanceKey"));
    scan.origin = jsonString(object, QStringLiteral("origin"));
    scan.size = jsonInt64(object, QStringLiteral("size"));
    scan.mtime = jsonInt64(object, QStringLiteral("mtime"));
    scan.status = jsonString(object, QStringLiteral("status"));
    if (scan.status.isEmpty())
        scan.status = QStringLiteral("ok");
    scan.error = jsonString(object, QStringLiteral("error"));
    scan.scannedAt = jsonString(object, QStringLiteral("scannedAt"));

    for (const QJsonValue &value : object.value(QStringLiteral("namespaces")).toArray())
        scan.namespaces.append(value.toString());
    const QJsonObject counts = object.value(QStringLiteral("namespaceCounts")).toObject();
    for (auto it = counts.constBegin(); it != counts.constEnd(); ++it)
        scan.namespaceCounts.insert(it.key(), it.value().toInt());
    for (const QJsonValue &value : object.value(QStringLiteral("mods")).toArray())
        scan.mods.append(ModInfo::fromJson(value.toObject()));

    // Namespace ownership is stored once per jar; propagate it to the mods.
    for (ModInfo &mod : scan.mods)
        mod.namespaces = scan.namespaces;
    return scan;
}

} // namespace mcstudio
