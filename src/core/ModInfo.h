#pragma once

#include <QJsonObject>
#include <QHash>
#include <QString>
#include <QStringList>
#include <QVector>
#include <QtGlobal>

namespace mcstudio {

// Metadata for one mod entry (a single jar may declare several mods, e.g. a
// library that ships extra compatibility modules).
struct ModInfo {
    QString jarPath;
    QString fileName;
    QString instanceKey;
    QString origin;      // "root" / "instance"
    QString modId;
    QString displayName;
    QString version;
    QString description;
    QString authors;
    QString logoFile;        // entry path inside the jar
    QString loader;          // javafml / fabric / quilt / legacy
    QString metadataSource;  // mods.toml / fabric.mod.json / mcmod.info / ...
    QString minecraftRange;  // dependencies.minecraft version range
    QStringList namespaces;  // assets/<ns>/ namespaces owned by the jar
    QString status = QStringLiteral("ok"); // ok / unknown / error
    QString error;

    QString displayLabel() const;
    QJsonObject toJson() const;
    static ModInfo fromJson(const QJsonObject &object);
};

// Result of opening one mod jar. Only metadata entries and the central
// directory are read; the jar is never extracted.
struct JarModScan {
    QString jarPath;
    QString fileName;
    QString instanceKey;
    QString origin;
    qint64 size = 0;
    qint64 mtime = 0;
    QStringList namespaces;
    QHash<QString, int> namespaceCounts; // assets/<ns>/ entry count, used to
                                         // reject incidental compat assets
    QVector<ModInfo> mods;
    QString status = QStringLiteral("ok"); // ok / unknown / error
    QString error;
    QString scannedAt;
    bool fromCache = false;

    bool ok() const { return status != QLatin1String("error"); }
    QString primaryModId() const;
    QString primaryDisplayName() const;
    QJsonObject toJson() const;
    static JarModScan fromJson(const QJsonObject &object);
};

} // namespace mcstudio
