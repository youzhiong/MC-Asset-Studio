#pragma once

#include <QJsonObject>
#include <QString>
#include <QStringList>
#include <QVector>
#include <QtGlobal>

namespace mcstudio {

// A jar found in a mods directory. Phase 1 records the file identity only;
// mod metadata (mods.toml / fabric.mod.json) is parsed by ModScanner later.
struct ModFileInfo {
    QString path;      // absolute path with forward slashes
    QString fileName;  // file name including .jar
    QString origin;    // "root" (.minecraft/mods) or "instance" (versions/<id>/mods)
    qint64 size = 0;
    qint64 mtime = 0;
    bool disabled = false;

    QJsonObject toJson() const;
    static ModFileInfo fromJson(const QJsonObject &object);
};

struct ResourcePackFileInfo {
    QString path;
    QString fileName;
    qint64 size = 0;
    qint64 mtime = 0;
    bool isDirectory = false;

    QJsonObject toJson() const;
    static ResourcePackFileInfo fromJson(const QJsonObject &object);
};

// Cheap directory identity used to invalidate the environment cache without
// stat-ing every entry.
struct DirFingerprint {
    QString path;
    bool exists = false;
    qint64 mtime = 0;
    int count = 0;

    QJsonObject toJson() const;
    static DirFingerprint fromJson(const QJsonObject &object);
};

// One launchable Minecraft instance: a versions/<id> directory, possibly with
// its own mods/resourcepacks (PCL / HMCL version isolation).
struct InstanceInfo {
    QString key;           // "<rootKey>:versions/<id>"
    QString name;          // version directory name
    QString rootPath;      // owning .minecraft root, forward slashes
    QString instancePath;  // versions/<id> absolute path
    QString versionJson;   // <instance>/<id>.json
    QString clientJar;     // resolved client jar (own, jar field, or inherited)
    QString clientVersion; // 1.20.1 (never guessed from the folder name)
    QString loader;        // vanilla / forge / neoforge / fabric / quilt
    QString loaderVersion; // 47.4.16
    QString assetIndex;    // 5 / 1.12
    bool hasClientJar = false;
    bool hasAssetsInJar = false; // filled once a jar index is built (phase 2+)
    bool isolated = false;

    qint64 versionJsonSize = 0;
    qint64 versionJsonMtime = 0;
    DirFingerprint instanceDir;
    DirFingerprint modsDir;
    DirFingerprint resourcePacksDir;

    QVector<ModFileInfo> mods;            // root mods ∪ instance mods
    QVector<ResourcePackFileInfo> resourcePacks;
    QStringList warnings;

    int modCount() const;          // enabled jars only
    int disabledModCount() const;
    int resourcePackCount() const;
    int rootModCount() const;
    int instanceModCount() const;
    QString loaderLabel() const;
    QString versionLabel() const;
    QString jarLabel() const;

    QJsonObject toJson() const;
    static InstanceInfo fromJson(const QJsonObject &object);
};

struct GameRootInfo {
    QString key;  // sha1(canonical root path)
    QString path; // forward slashes, absolute
    QString name; // last directory component
    bool hasVersions = false;
    bool hasAssets = false;
    bool hasLibraries = false;
    bool hasRootMods = false;
    bool hasRootResourcePacks = false;
    DirFingerprint rootModsDir;
    DirFingerprint rootResourcePacksDir;
    QVector<InstanceInfo> instances;
    QStringList warnings;
    QString scannedAt;
    bool fromCache = false;

    int instanceCount() const;
    int totalMods() const;
    int totalResourcePacks() const;
    const InstanceInfo *findInstance(const QString &instanceKey) const;
    const InstanceInfo *findInstanceByPath(const QString &instancePath) const;
};

struct GameEnvironmentSnapshot {
    QVector<GameRootInfo> roots;
    QStringList warnings;
    int instanceCount() const;
    int modCount() const;
    int resourcePackCount() const;
    const InstanceInfo *findInstance(const QString &instanceKey) const;
    GameRootInfo *findRoot(const QString &rootPath);
    const GameRootInfo *findRoot(const QString &rootPath) const;
    const GameRootInfo *findRootOf(const QString &instanceKey) const;
};

QString loaderDisplayName(const QString &loader);

} // namespace mcstudio
