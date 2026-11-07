#include "core/InstanceInfo.h"

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
    if (!value.isDouble())
        return fallback;
    return static_cast<qint64>(value.toDouble());
}

bool jsonBool(const QJsonObject &object, const QString &key, bool fallback = false)
{
    const QJsonValue value = object.value(key);
    return value.isBool() ? value.toBool() : fallback;
}

} // namespace

QJsonObject ModFileInfo::toJson() const
{
    QJsonObject object;
    object.insert(QStringLiteral("path"), path);
    object.insert(QStringLiteral("fileName"), fileName);
    object.insert(QStringLiteral("origin"), origin);
    object.insert(QStringLiteral("size"), static_cast<double>(size));
    object.insert(QStringLiteral("mtime"), static_cast<double>(mtime));
    object.insert(QStringLiteral("disabled"), disabled);
    return object;
}

ModFileInfo ModFileInfo::fromJson(const QJsonObject &object)
{
    ModFileInfo mod;
    mod.path = jsonString(object, QStringLiteral("path"));
    mod.fileName = jsonString(object, QStringLiteral("fileName"));
    mod.origin = jsonString(object, QStringLiteral("origin"));
    mod.size = jsonInt64(object, QStringLiteral("size"));
    mod.mtime = jsonInt64(object, QStringLiteral("mtime"));
    mod.disabled = jsonBool(object, QStringLiteral("disabled"));
    if (mod.fileName.isEmpty())
        mod.fileName = mod.path.section(QLatin1Char('/'), -1);
    if (mod.origin.isEmpty())
        mod.origin = QStringLiteral("instance");
    return mod;
}

QJsonObject ResourcePackFileInfo::toJson() const
{
    QJsonObject object;
    object.insert(QStringLiteral("path"), path);
    object.insert(QStringLiteral("fileName"), fileName);
    object.insert(QStringLiteral("size"), static_cast<double>(size));
    object.insert(QStringLiteral("mtime"), static_cast<double>(mtime));
    object.insert(QStringLiteral("isDirectory"), isDirectory);
    return object;
}

ResourcePackFileInfo ResourcePackFileInfo::fromJson(const QJsonObject &object)
{
    ResourcePackFileInfo pack;
    pack.path = jsonString(object, QStringLiteral("path"));
    pack.fileName = jsonString(object, QStringLiteral("fileName"));
    pack.size = jsonInt64(object, QStringLiteral("size"));
    pack.mtime = jsonInt64(object, QStringLiteral("mtime"));
    pack.isDirectory = jsonBool(object, QStringLiteral("isDirectory"));
    if (pack.fileName.isEmpty())
        pack.fileName = pack.path.section(QLatin1Char('/'), -1);
    return pack;
}

QJsonObject DirFingerprint::toJson() const
{
    QJsonObject object;
    object.insert(QStringLiteral("path"), path);
    object.insert(QStringLiteral("exists"), exists);
    object.insert(QStringLiteral("mtime"), static_cast<double>(mtime));
    object.insert(QStringLiteral("count"), count);
    return object;
}

DirFingerprint DirFingerprint::fromJson(const QJsonObject &object)
{
    DirFingerprint fingerprint;
    fingerprint.path = jsonString(object, QStringLiteral("path"));
    fingerprint.exists = jsonBool(object, QStringLiteral("exists"));
    fingerprint.mtime = jsonInt64(object, QStringLiteral("mtime"));
    fingerprint.count = static_cast<int>(jsonInt64(object, QStringLiteral("count")));
    return fingerprint;
}

int InstanceInfo::modCount() const
{
    int count = 0;
    for (const ModFileInfo &mod : mods) {
        if (!mod.disabled)
            ++count;
    }
    return count;
}

int InstanceInfo::disabledModCount() const
{
    int count = 0;
    for (const ModFileInfo &mod : mods) {
        if (mod.disabled)
            ++count;
    }
    return count;
}

int InstanceInfo::resourcePackCount() const
{
    return static_cast<int>(resourcePacks.size());
}

int InstanceInfo::rootModCount() const
{
    int count = 0;
    for (const ModFileInfo &mod : mods) {
        if (!mod.disabled && mod.origin == QLatin1String("root"))
            ++count;
    }
    return count;
}

int InstanceInfo::instanceModCount() const
{
    int count = 0;
    for (const ModFileInfo &mod : mods) {
        if (!mod.disabled && mod.origin == QLatin1String("instance"))
            ++count;
    }
    return count;
}

QString InstanceInfo::loaderLabel() const
{
    const QString display = loaderDisplayName(loader);
    if (loaderVersion.isEmpty())
        return display;
    return QStringLiteral("%1 %2").arg(display, loaderVersion);
}

QString InstanceInfo::versionLabel() const
{
    return clientVersion.isEmpty() ? QStringLiteral("未知") : clientVersion;
}

QString InstanceInfo::jarLabel() const
{
    if (!hasClientJar)
        return QStringLiteral("缺失");
    return clientJar.section(QLatin1Char('/'), -1);
}

QJsonObject InstanceInfo::toJson() const
{
    QJsonObject object;
    object.insert(QStringLiteral("key"), key);
    object.insert(QStringLiteral("name"), name);
    object.insert(QStringLiteral("rootPath"), rootPath);
    object.insert(QStringLiteral("instancePath"), instancePath);
    object.insert(QStringLiteral("versionJson"), versionJson);
    object.insert(QStringLiteral("clientJar"), clientJar);
    object.insert(QStringLiteral("clientVersion"), clientVersion);
    object.insert(QStringLiteral("loader"), loader);
    object.insert(QStringLiteral("loaderVersion"), loaderVersion);
    object.insert(QStringLiteral("assetIndex"), assetIndex);
    object.insert(QStringLiteral("hasClientJar"), hasClientJar);
    object.insert(QStringLiteral("hasAssetsInJar"), hasAssetsInJar);
    object.insert(QStringLiteral("isolated"), isolated);
    object.insert(QStringLiteral("versionJsonSize"), static_cast<double>(versionJsonSize));
    object.insert(QStringLiteral("versionJsonMtime"), static_cast<double>(versionJsonMtime));
    object.insert(QStringLiteral("instanceDir"), instanceDir.toJson());
    object.insert(QStringLiteral("modsDir"), modsDir.toJson());
    object.insert(QStringLiteral("resourcePacksDir"), resourcePacksDir.toJson());

    QJsonArray modArray;
    for (const ModFileInfo &mod : mods)
        modArray.append(mod.toJson());
    object.insert(QStringLiteral("mods"), modArray);

    QJsonArray packArray;
    for (const ResourcePackFileInfo &pack : resourcePacks)
        packArray.append(pack.toJson());
    object.insert(QStringLiteral("resourcePacks"), packArray);

    QJsonArray warningArray;
    for (const QString &warning : warnings)
        warningArray.append(warning);
    object.insert(QStringLiteral("warnings"), warningArray);
    return object;
}

InstanceInfo InstanceInfo::fromJson(const QJsonObject &object)
{
    InstanceInfo instance;
    instance.key = jsonString(object, QStringLiteral("key"));
    instance.name = jsonString(object, QStringLiteral("name"));
    instance.rootPath = jsonString(object, QStringLiteral("rootPath"));
    instance.instancePath = jsonString(object, QStringLiteral("instancePath"));
    instance.versionJson = jsonString(object, QStringLiteral("versionJson"));
    instance.clientJar = jsonString(object, QStringLiteral("clientJar"));
    instance.clientVersion = jsonString(object, QStringLiteral("clientVersion"));
    instance.loader = jsonString(object, QStringLiteral("loader"));
    if (instance.loader.isEmpty())
        instance.loader = QStringLiteral("vanilla");
    instance.loaderVersion = jsonString(object, QStringLiteral("loaderVersion"));
    instance.assetIndex = jsonString(object, QStringLiteral("assetIndex"));
    instance.hasClientJar = jsonBool(object, QStringLiteral("hasClientJar"));
    instance.hasAssetsInJar = jsonBool(object, QStringLiteral("hasAssetsInJar"));
    instance.isolated = jsonBool(object, QStringLiteral("isolated"));
    instance.versionJsonSize = jsonInt64(object, QStringLiteral("versionJsonSize"));
    instance.versionJsonMtime = jsonInt64(object, QStringLiteral("versionJsonMtime"));
    instance.instanceDir = DirFingerprint::fromJson(
        object.value(QStringLiteral("instanceDir")).toObject());
    instance.modsDir = DirFingerprint::fromJson(
        object.value(QStringLiteral("modsDir")).toObject());
    instance.resourcePacksDir = DirFingerprint::fromJson(
        object.value(QStringLiteral("resourcePacksDir")).toObject());

    for (const QJsonValue &value : object.value(QStringLiteral("mods")).toArray())
        instance.mods.append(ModFileInfo::fromJson(value.toObject()));
    for (const QJsonValue &value : object.value(QStringLiteral("resourcePacks")).toArray())
        instance.resourcePacks.append(ResourcePackFileInfo::fromJson(value.toObject()));
    for (const QJsonValue &value : object.value(QStringLiteral("warnings")).toArray())
        instance.warnings.append(value.toString());
    return instance;
}

int GameRootInfo::instanceCount() const
{
    return static_cast<int>(instances.size());
}

int GameRootInfo::totalMods() const
{
    int count = 0;
    for (const InstanceInfo &instance : instances)
        count += instance.modCount();
    return count;
}

int GameRootInfo::totalResourcePacks() const
{
    int count = 0;
    for (const InstanceInfo &instance : instances)
        count += instance.resourcePackCount();
    return count;
}

const InstanceInfo *GameRootInfo::findInstance(const QString &instanceKey) const
{
    for (const InstanceInfo &instance : instances) {
        if (instance.key == instanceKey)
            return &instance;
    }
    return nullptr;
}

const InstanceInfo *GameRootInfo::findInstanceByPath(const QString &instancePath) const
{
    for (const InstanceInfo &instance : instances) {
        if (instance.instancePath.compare(instancePath, Qt::CaseInsensitive) == 0)
            return &instance;
    }
    return nullptr;
}

int GameEnvironmentSnapshot::instanceCount() const
{
    int count = 0;
    for (const GameRootInfo &root : roots)
        count += root.instanceCount();
    return count;
}

int GameEnvironmentSnapshot::modCount() const
{
    int count = 0;
    for (const GameRootInfo &root : roots)
        count += root.totalMods();
    return count;
}

int GameEnvironmentSnapshot::resourcePackCount() const
{
    int count = 0;
    for (const GameRootInfo &root : roots)
        count += root.totalResourcePacks();
    return count;
}

const InstanceInfo *GameEnvironmentSnapshot::findInstance(const QString &instanceKey) const
{
    for (const GameRootInfo &root : roots) {
        if (const InstanceInfo *instance = root.findInstance(instanceKey))
            return instance;
    }
    return nullptr;
}

GameRootInfo *GameEnvironmentSnapshot::findRoot(const QString &rootPath)
{
    for (GameRootInfo &root : roots) {
        if (root.path.compare(rootPath, Qt::CaseInsensitive) == 0)
            return &root;
    }
    return nullptr;
}

const GameRootInfo *GameEnvironmentSnapshot::findRoot(const QString &rootPath) const
{
    for (const GameRootInfo &root : roots) {
        if (root.path.compare(rootPath, Qt::CaseInsensitive) == 0)
            return &root;
    }
    return nullptr;
}

const GameRootInfo *GameEnvironmentSnapshot::findRootOf(const QString &instanceKey) const
{
    for (const GameRootInfo &root : roots) {
        if (root.findInstance(instanceKey))
            return &root;
    }
    return nullptr;
}

QString loaderDisplayName(const QString &loader)
{
    if (loader == QLatin1String("forge"))
        return QStringLiteral("Forge");
    if (loader == QLatin1String("neoforge"))
        return QStringLiteral("NeoForge");
    if (loader == QLatin1String("fabric"))
        return QStringLiteral("Fabric");
    if (loader == QLatin1String("quilt"))
        return QStringLiteral("Quilt");
    return QStringLiteral("原版");
}

} // namespace mcstudio
