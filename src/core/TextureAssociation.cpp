#include "core/TextureAssociation.h"

#include "core/AssetNamespace.h"
#include "core/AssetStack.h"
#include "core/ModCatalog.h"
#include "core/Project.h"

#include <QFileInfo>
#include <QElapsedTimer>
#include <QHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QSet>
#include <algorithm>
#include <functional>
#include <memory>
#include <utility>

namespace mcstudio {
namespace {

struct BuildContext {
    const Project *project = nullptr;
    const AssetStack *assets = nullptr;
    const ModCatalog *catalog = nullptr;
    QStringList namespaces;
    QHash<QString, QJsonObject> jsonCache;
    QHash<QString, bool> textureExistsCache;
    qint64 readMs = 0;
    int jsonReads = 0;
};

struct ModelResolution {
    QStringList textures;        // textures present in the pack
    QStringList missingTextures; // referenced but absent from the pack
    bool usedPack = false;
    bool usedReference = false;
};

QString splitNamespace(const QString &id, QString *path,
                       const QString &defaultNamespace = QStringLiteral("minecraft"))
{
    QString namespaceId = defaultNamespace;
    QString localPath = id;
    const int colon = id.indexOf(QLatin1Char(':'));
    if (colon >= 0) {
        namespaceId = id.left(colon);
        localPath = id.mid(colon + 1);
    }
    if (path)
        *path = localPath;
    return namespaceId;
}

QString modelAssetPath(const QString &modelId)
{
    QString path;
    const QString namespaceId = splitNamespace(modelId, &path);
    return QStringLiteral("assets/%1/models/%2.json").arg(namespaceId, path);
}

QString textureAssetPath(const QString &textureId)
{
    QString path;
    const QString namespaceId = splitNamespace(textureId, &path);
    if (path.endsWith(QLatin1String(".png"), Qt::CaseInsensitive))
        path.chop(4);
    if (path.startsWith(QLatin1String("textures/")))
        path.remove(0, 9);
    return QStringLiteral("assets/%1/textures/%2.png").arg(namespaceId, path);
}

QByteArray stripBom(QByteArray data)
{
    static const char kUtf8Bom[] = "\xEF\xBB\xBF";
    if (data.startsWith(kUtf8Bom))
        data.remove(0, 3);
    return data;
}

QJsonObject parseJsonObject(const QByteArray &bytes)
{
    if (bytes.isEmpty())
        return {};
    QJsonParseError error;
    const QJsonDocument document = QJsonDocument::fromJson(stripBom(bytes), &error);
    if (error.error != QJsonParseError::NoError || !document.isObject())
        return {};
    return document.object();
}

QJsonObject readJson(BuildContext &ctx, const QString &relativePath)
{
    const auto cached = ctx.jsonCache.constFind(relativePath);
    if (cached != ctx.jsonCache.constEnd())
        return cached.value();

    QJsonObject object;
    if (ctx.assets) {
        QElapsedTimer timer;
        timer.start();
        const QByteArray bytes = ctx.assets->read(relativePath);
        if (!bytes.isEmpty())
            object = parseJsonObject(bytes);
        ctx.readMs += timer.elapsed();
        ++ctx.jsonReads;
    }
    ctx.jsonCache.insert(relativePath, object);
    return object;
}

bool packHasTexture(BuildContext &ctx, const QString &path)
{
    const auto cached = ctx.textureExistsCache.constFind(path);
    if (cached != ctx.textureExistsCache.constEnd())
        return cached.value();

    // Use the in-memory project index; Project::exists() would hit the disk
    // for every model texture and make large packs extremely slow.
    const bool exists = ctx.project && ctx.project->fileIndex().contains(path);
    ctx.textureExistsCache.insert(path, exists);
    return exists;
}

QString resolveTextureValue(QString value, const QHash<QString, QString> &variables)
{
    QSet<QString> seen;
    while (value.startsWith(QLatin1Char('#'))) {
        const QString key = value.mid(1);
        if (seen.contains(key) || !variables.contains(key))
            return {};
        seen.insert(key);
        value = variables.value(key);
    }
    if (value.isEmpty() || value.startsWith(QLatin1Char('#')))
        return {};
    return value;
}

void noteModelOrigin(BuildContext &ctx, const QString &path, ModelResolution &result)
{
    if (!ctx.assets)
        return;
    const AssetLocation location = ctx.assets->locate(path);
    if (!location.found())
        return;
    if (location.fromReference())
        result.usedReference = true;
    else
        result.usedPack = true;
}

ModelResolution resolveModelTextures(BuildContext &ctx, const QString &modelId,
                                     QSet<QString> &visiting, QStringList &missingModels)
{
    ModelResolution result;
    const QString normalizedId = modelId.contains(QLatin1Char(':'))
                                     ? modelId
                                     : QStringLiteral("minecraft:") + modelId;
    if (visiting.contains(normalizedId))
        return result;
    visiting.insert(normalizedId);

    QHash<QString, QString> variables;
    QSet<QString> mergeVisiting;

    const std::function<void(const QString &)> mergeModel = [&](const QString &id) {
        const QString normalized = id.contains(QLatin1Char(':'))
                                       ? id
                                       : QStringLiteral("minecraft:") + id;
        if (mergeVisiting.contains(normalized))
            return;
        mergeVisiting.insert(normalized);

        const QString path = modelAssetPath(normalized);
        const QJsonObject object = readJson(ctx, path);
        if (object.isEmpty()) {
            if (!missingModels.contains(normalized))
                missingModels.append(normalized);
            return;
        }
        noteModelOrigin(ctx, path, result);

        const QString parentId = object.value(QStringLiteral("parent")).toString();
        if (!parentId.isEmpty())
            mergeModel(parentId);

        const QJsonObject textures = object.value(QStringLiteral("textures")).toObject();
        for (auto it = textures.constBegin(); it != textures.constEnd(); ++it) {
            if (it.value().isString())
                variables.insert(it.key(), it.value().toString());
        }
    };
    mergeModel(normalizedId);
    visiting.remove(normalizedId);

    QSet<QString> seen;
    for (auto it = variables.constBegin(); it != variables.constEnd(); ++it) {
        const QString resolved = resolveTextureValue(it.value(), variables);
        if (resolved.isEmpty())
            continue;
        const QString path = textureAssetPath(resolved);
        if (!packHasTexture(ctx, path)) {
            if (!result.missingTextures.contains(path))
                result.missingTextures.append(path);
            continue;
        }
        if (seen.contains(path))
            continue;
        seen.insert(path);
        result.textures.append(path);
    }
    result.textures.sort(Qt::CaseInsensitive);
    return result;
}

void appendModelIds(const QJsonValue &value, QStringList &models)
{
    if (value.isObject()) {
        const QString model = value.toObject().value(QStringLiteral("model")).toString();
        if (!model.isEmpty())
            models.append(model);
    } else if (value.isArray()) {
        for (const QJsonValue &item : value.toArray())
            appendModelIds(item, models);
    }
}

QString groupDisplayName(const QString &kind, const QString &namespaceId,
                         const QString &name, const QHash<QString, QString> &zh,
                         const QHash<QString, QString> &en)
{
    // Item models frequently reuse the block translation key (doors, windows,
    // cogwheels, ...), so try the other kind before falling back to IDs.
    QStringList keys;
    keys.append(QStringLiteral("%1.%2.%3").arg(kind, namespaceId, name));
    keys.append(QStringLiteral("%1.%2.%3")
                    .arg(kind == QLatin1String("item") ? QStringLiteral("block")
                                                       : QStringLiteral("item"),
                         namespaceId, name));

    for (const QString &key : std::as_const(keys)) {
        if (zh.contains(key))
            return zh.value(key);
    }
    for (const QString &key : std::as_const(keys)) {
        if (en.contains(key))
            return en.value(key);
    }
    return QStringLiteral("%1:%2").arg(namespaceId, name);
}

void loadLanguage(BuildContext &ctx, const QString &locale, QHash<QString, QString> &target)
{
    QStringList namespaces = ctx.namespaces;
    if (!namespaces.contains(QStringLiteral("minecraft")))
        namespaces.append(QStringLiteral("minecraft"));

    for (const QString &namespaceId : std::as_const(namespaces)) {
        const QString path = QStringLiteral("assets/%1/lang/%2.json")
                                 .arg(namespaceId, locale);
        // Merge pack + reference lang files key by key; earlier layers win.
        for (const QByteArray &bytes : ctx.assets->readAll(path)) {
            const QJsonObject object = parseJsonObject(bytes);
            for (auto it = object.constBegin(); it != object.constEnd(); ++it) {
                if (!it.value().isString() || target.contains(it.key()))
                    continue;
                target.insert(it.key(), it.value().toString());
            }
        }
    }
}

void fillGroupMetadata(BuildContext &ctx, TextureGroup &group)
{
    group.textureSource = QStringLiteral("pack");
    if (ctx.catalog) {
        if (const NamespaceMod *mapping = ctx.catalog->entry(group.namespaceId)) {
            group.modId = mapping->modId;
            group.modName = mapping->label();
        }
        if (group.modName.isEmpty())
            group.modName = ctx.catalog->displayNameFor(group.namespaceId);
    }
    if (group.modName.isEmpty())
        group.modName = group.namespaceId;
}

QString resolveModelSource(bool statePack, bool stateReference, bool modelPack,
                           bool modelReference)
{
    const bool pack = statePack || modelPack;
    const bool reference = stateReference || modelReference;
    if (reference && pack)
        return QStringLiteral("mixed");
    if (reference)
        return QStringLiteral("reference");
    return QStringLiteral("pack");
}

} // namespace

AssociationResult TextureAssociation::build(const Project &project, const AssetStack &assets,
                                            const ModCatalog &catalog,
                                            const QStringList &namespaces,
                                            const Progress &progress,
                                            const std::atomic_bool *cancel)
{
    BuildContext ctx;
    ctx.project = &project;
    ctx.assets = &assets;
    ctx.catalog = &catalog;
    ctx.namespaces = namespaces;

    AssociationResult result;
    QElapsedTimer buildTimer;
    buildTimer.start();

    const auto isCancelled = [cancel]() {
        return cancel && cancel->load(std::memory_order_relaxed);
    };
    const int totalSteps = qMax(1, static_cast<int>(namespaces.size()) * 2);
    int step = 0;

    QHash<QString, QString> zh;
    QHash<QString, QString> en;
    loadLanguage(ctx, QStringLiteral("zh_cn"), zh);
    loadLanguage(ctx, QStringLiteral("en_us"), en);

    // Blockstate -> model -> texture, with per-file fallback across the stack.
    for (const QString &namespaceId : namespaces) {
        if (isCancelled())
            return result;

        const QString prefix =
            QStringLiteral("assets/%1/blockstates/").arg(namespaceId);
        for (const QString &path : assets.list(prefix)) {
            if (isCancelled())
                return result;
            if (!path.endsWith(QLatin1String(".json")))
                continue;
            const QString relative = path.mid(prefix.size());
            const QString name = relative.chopped(5);
            if (name.isEmpty())
                continue;

            const QJsonObject state = readJson(ctx, path);
            if (state.isEmpty())
                continue;

            QStringList modelIds;
            const QJsonObject variants = state.value(QStringLiteral("variants")).toObject();
            for (auto it = variants.constBegin(); it != variants.constEnd(); ++it)
                appendModelIds(it.value(), modelIds);

            const QJsonArray multipart = state.value(QStringLiteral("multipart")).toArray();
            for (const QJsonValue &caseValue : multipart) {
                const QJsonObject apply =
                    caseValue.toObject().value(QStringLiteral("apply")).toObject();
                appendModelIds(apply, modelIds);
            }

            QSet<QString> textures;
            QStringList missingTextures;
            bool modelPack = false;
            bool modelReference = false;
            for (const QString &modelId : modelIds) {
                QSet<QString> visiting;
                const ModelResolution resolved =
                    resolveModelTextures(ctx, modelId, visiting, result.missingModels);
                modelPack = modelPack || resolved.usedPack;
                modelReference = modelReference || resolved.usedReference;
                for (const QString &texture : resolved.textures)
                    textures.insert(texture);
                for (const QString &texture : resolved.missingTextures) {
                    if (!missingTextures.contains(texture))
                        missingTextures.append(texture);
                }
            }

            if (textures.isEmpty())
                continue; // no pack texture for this block: do not show it

            const AssetLocation stateLocation = assets.locate(path);
            const bool statePack = stateLocation.found()
                                   && stateLocation.origin == AssetOrigin::Pack;
            const bool stateReference = stateLocation.found()
                                        && stateLocation.fromReference();

            TextureGroup group;
            group.id = QStringLiteral("block:%1:%2").arg(namespaceId, name);
            group.kind = QStringLiteral("block");
            group.namespaceId = namespaceId;
            group.name = name;
            group.displayName =
                groupDisplayName(QStringLiteral("block"), namespaceId, name, zh, en);
            group.modelSource = resolveModelSource(statePack, stateReference, modelPack,
                                                   modelReference);
            group.textures = textures.values();
            group.textures.sort(Qt::CaseInsensitive);
            group.missingTextures = missingTextures;
            fillGroupMetadata(ctx, group);

            for (const QString &texture : missingTextures) {
                if (!result.missingTextures.contains(texture))
                    result.missingTextures.append(texture);
            }

            result.groups.append(group);
            ++result.blockGroups;
            if (group.modelSource != QLatin1String("pack"))
                ++result.referenceGroups;
        }

        ++step;
        if (progress)
            progress(step, totalSteps, QStringLiteral("解析方块关联：%1").arg(namespaceId));
    }

    // Item model -> texture (models/item may also come from the reference).
    for (const QString &namespaceId : namespaces) {
        if (isCancelled())
            return result;

        const QString prefix =
            QStringLiteral("assets/%1/models/item/").arg(namespaceId);
        for (const QString &path : assets.list(prefix)) {
            if (isCancelled())
                return result;
            if (!path.endsWith(QLatin1String(".json")))
                continue;
            const QString name = path.mid(prefix.size()).chopped(5);
            if (name.isEmpty())
                continue;

            QSet<QString> visiting;
            const ModelResolution resolved = resolveModelTextures(
                ctx, QStringLiteral("%1:item/%2").arg(namespaceId, name), visiting,
                result.missingModels);
            if (resolved.textures.isEmpty())
                continue;

            const AssetLocation modelLocation = assets.locate(path);
            const bool modelPack = modelLocation.found()
                                   && modelLocation.origin == AssetOrigin::Pack;
            const bool modelReference = modelLocation.found()
                                        && modelLocation.fromReference();

            TextureGroup group;
            group.id = QStringLiteral("item:%1:%2").arg(namespaceId, name);
            group.kind = QStringLiteral("item");
            group.namespaceId = namespaceId;
            group.name = name;
            group.displayName =
                groupDisplayName(QStringLiteral("item"), namespaceId, name, zh, en);
            group.modelSource = resolveModelSource(false, false, modelPack, modelReference);
            group.textures = resolved.textures;
            group.missingTextures = resolved.missingTextures;
            fillGroupMetadata(ctx, group);

            for (const QString &texture : resolved.missingTextures) {
                if (!result.missingTextures.contains(texture))
                    result.missingTextures.append(texture);
            }

            result.groups.append(group);
            ++result.itemGroups;
            if (group.modelSource != QLatin1String("pack"))
                ++result.referenceGroups;
        }

        ++step;
        if (progress)
            progress(step, totalSteps, QStringLiteral("解析物品关联：%1").arg(namespaceId));
    }

    result.missingModels.removeDuplicates();
    result.missingModels.sort(Qt::CaseInsensitive);
    result.missingTextures.removeDuplicates();
    result.missingTextures.sort(Qt::CaseInsensitive);

    std::sort(result.groups.begin(), result.groups.end(),
              [](const TextureGroup &a, const TextureGroup &b) {
                  if (a.kind != b.kind)
                      return a.kind < b.kind;
                  return a.displayName.localeAwareCompare(b.displayName) < 0;
              });

    if (progress)
        progress(totalSteps, totalSteps, QStringLiteral("关联解析完成"));

    result.readMs = ctx.readMs;
    result.jsonReads = ctx.jsonReads;
    result.buildMs = buildTimer.elapsed();
    return result;
}

AssociationResult TextureAssociation::build(const Project &project)
{
    auto stack = std::make_shared<AssetStack>();
    stack->addProvider(std::make_shared<ProjectAssetProvider>(project));
    ModCatalog catalog;
    return build(project, *stack, catalog, namespacesFromPaths(project.files()));
}

} // namespace mcstudio
