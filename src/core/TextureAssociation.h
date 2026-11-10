#pragma once

#include <QHash>
#include <QString>
#include <QStringList>
#include <QVector>
#include <atomic>
#include <functional>

namespace mcstudio {

class AssetStack;
class ModCatalog;
class Project;

struct TextureGroup {
    QString id; // block:minecraft:grass_block / item:minecraft:stone
    QString kind;
    QString namespaceId;
    QString modId;
    QString modName;
    QString name;
    QString displayName;
    QStringList textures; // pack textures only
    QStringList missingTextures;
    QString modelSource;   // pack / reference / mixed
    QString textureSource; // pack
};

struct AssociationResult {
    QVector<TextureGroup> groups;
    QStringList missingModels;
    QStringList missingTextures;
    // Every texture referenced by a blockstate/model (including reference-only
    // textures that the pack does not contain), mapped to its localized name
    // and namespace-qualified ID. Collection mode uses these for card labels
    // and tooltips even when a texture cannot be grouped into the pack.
    QHash<QString, QString> textureNames;
    QHash<QString, QString> textureIds;
    int blockGroups = 0;
    int itemGroups = 0;
    int referenceGroups = 0;
    // Diagnostics for the background build / association report.
    qint64 buildMs = 0;
    qint64 readMs = 0;
    int jsonReads = 0;
};

class TextureAssociation
{
public:
    using Progress = std::function<void(int done, int total, const QString &message)>;

    // Pack-only association (kept for existing callers and tests).
    static AssociationResult build(const Project &project);

    // Layered association: blockstates/models/lang may come from the
    // reference stack, but only textures that exist in the pack are emitted.
    static AssociationResult build(const Project &project, const AssetStack &assets,
                                   const ModCatalog &catalog, const QStringList &namespaces,
                                   const Progress &progress = {},
                                   const std::atomic_bool *cancel = nullptr);
};

} // namespace mcstudio
