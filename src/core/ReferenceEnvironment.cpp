#include "core/ReferenceEnvironment.h"

#include "core/ModCatalog.h"
#include "core/PackFormat.h"

#include <QDir>
#include <QFileInfo>
#include <QSet>

namespace mcstudio {
namespace {

QString packFormatWarning(const InstanceInfo &instance, int packFormat,
                          const QVector<int> &supportedFormats)
{
    const int instanceFormat = packFormatForMcVersion(instance.clientVersion);
    if (packFormat <= 0 || instanceFormat <= 0)
        return {};
    if (instanceFormat == packFormat || supportedFormats.contains(instanceFormat))
        return {};
    return QStringLiteral("参考实例 %1（Java %2，pack_format %3）与资源包 pack_format %4 不匹配；"
                          "结构可能不一致。")
        .arg(instance.name, instance.versionLabel())
        .arg(instanceFormat)
        .arg(packFormat);
}

bool instanceIsCompatible(const InstanceInfo &instance, int packFormat,
                          const QVector<int> &supportedFormats)
{
    if (packFormat <= 0)
        return true;
    const int instanceFormat = packFormatForMcVersion(instance.clientVersion);
    if (instanceFormat <= 0)
        return true; // unknown version: do not cry wolf
    return instanceFormat == packFormat || supportedFormats.contains(instanceFormat);
}

const InstanceInfo *pickBest(const QVector<const InstanceInfo *> &candidates)
{
    const InstanceInfo *best = nullptr;
    for (const InstanceInfo *instance : candidates) {
        if (!best || instance->modCount() > best->modCount())
            best = instance;
    }
    return best;
}

} // namespace

ReferenceMatch matchReferenceInstance(const GameEnvironmentSnapshot &snapshot,
                                      const GameSettings &settings, int packFormat,
                                      const QVector<int> &supportedFormats,
                                      const QString &projectOverrideKey)
{
    ReferenceMatch match;
    match.mode = QStringLiteral("none");

    const auto makeMatch = [&](const InstanceInfo &instance, const QString &mode,
                               const QString &reason) {
        ReferenceMatch result;
        result.found = true;
        result.compatible = instanceIsCompatible(instance, packFormat, supportedFormats);
        result.instanceKey = instance.key;
        result.instanceName = instance.name;
        result.clientVersion = instance.clientVersion;
        result.loader = instance.loader;
        result.mode = mode;
        result.reason = reason;
        result.warning = packFormatWarning(instance, packFormat, supportedFormats);
        return result;
    };

    if (!projectOverrideKey.isEmpty()) {
        if (const InstanceInfo *instance = snapshot.findInstance(projectOverrideKey))
            return makeMatch(*instance, QStringLiteral("manual"), QStringLiteral("项目覆盖"));
        match.warning = QStringLiteral("项目指定的参考实例已不存在，已回退自动匹配。");
    }

    if (settings.referenceMode == QLatin1String("default")
        && !settings.defaultInstanceKey.isEmpty()) {
        if (const InstanceInfo *instance =
                snapshot.findInstance(settings.defaultInstanceKey)) {
            ReferenceMatch result =
                makeMatch(*instance, QStringLiteral("default"), QStringLiteral("首选项默认实例"));
            if (!match.warning.isEmpty())
                result.warning = match.warning;
            return result;
        }
        match.warning =
            QStringLiteral("首选项默认实例已不存在，已回退按 pack_format 自动匹配。");
    }

    const QString targetVersion = mcVersionForPackFormat(packFormat);
    const QString targetFamily = mcVersionFamily(targetVersion);

    QVector<const InstanceInfo *> exact;
    QVector<const InstanceInfo *> compatible;
    QVector<const InstanceInfo *> sameFamily;

    for (const GameRootInfo &root : snapshot.roots) {
        for (const InstanceInfo &instance : root.instances) {
            const int instanceFormat = packFormatForMcVersion(instance.clientVersion);
            if (packFormat > 0 && instanceFormat == packFormat)
                exact.append(&instance);
            if (instanceFormat > 0 && supportedFormats.contains(instanceFormat))
                compatible.append(&instance);
            if (!targetFamily.isEmpty()
                && mcVersionFamily(instance.clientVersion) == targetFamily) {
                sameFamily.append(&instance);
            }
        }
    }

    const InstanceInfo *picked = nullptr;
    QString reason;
    if (const InstanceInfo *exactCandidate = pickBest(exact)) {
        picked = exactCandidate;
        reason = QStringLiteral("pack_format %1 精确匹配").arg(packFormat);
    } else if (const InstanceInfo *compatibleCandidate = pickBest(compatible)) {
        picked = compatibleCandidate;
        reason = QStringLiteral("supported_formats 匹配");
    } else if (const InstanceInfo *familyCandidate = pickBest(sameFamily)) {
        picked = familyCandidate;
        reason = QStringLiteral("同小版本匹配");
    }

    if (!picked) {
        match.warning = packFormat > 0
                            ? QStringLiteral("没有与 pack_format %1 匹配的实例，请手动选择参考实例。")
                                  .arg(packFormat)
                            : QStringLiteral("资源包没有可识别的 pack_format，请手动选择参考实例。");
        return match;
    }

    ReferenceMatch result = makeMatch(*picked, QStringLiteral("auto"), reason);
    if (!match.warning.isEmpty())
        result.warning = match.warning;
    return result;
}

ReferenceStackResult buildReferenceAssetStack(const Project &project,
                                              const InstanceInfo *reference,
                                              const ModCatalog &catalog,
                                              const QStringList &namespaces,
                                              const ReferenceStackOptions &options)
{
    ReferenceStackResult result;
    result.stack = std::make_shared<AssetStack>();
    result.stack->addProvider(std::make_shared<ProjectAssetProvider>(project));

    if (!reference) {
        result.diagnostics.append(QStringLiteral("未选择参考实例，仅使用资源包自身内容。"));
        return result;
    }
    if (!options.fallbackModels && !options.fallbackLang) {
        result.diagnostics.append(QStringLiteral("参考资源回退已在首选项中关闭。"));
        return result;
    }

    QSet<QString> seenJars;
    for (const QString &namespaceId : namespaces) {
        const NamespaceMod *entry = catalog.entry(namespaceId);
        if (!entry || !entry->found || entry->jarPath.isEmpty())
            continue;
        // Cross-instance metadata must never pull assets from another instance.
        if (entry->source != QLatin1String("local"))
            continue;
        if (seenJars.contains(entry->jarPath))
            continue;
        seenJars.insert(entry->jarPath);

        result.stack->addProvider(std::make_shared<JarAssetProvider>(
            entry->jarPath, QStringLiteral("参考 · %1").arg(entry->label()),
            AssetOrigin::Reference));
        ++result.referenceJarCount;
    }

    if (reference->hasClientJar && !reference->clientJar.isEmpty()) {
        result.stack->addProvider(std::make_shared<JarAssetProvider>(
            reference->clientJar,
            QStringLiteral("客户端 jar · %1").arg(reference->name),
            AssetOrigin::Vanilla));
        result.hasVanilla = true;
    }

    if (!reference->assetIndex.isEmpty() && !reference->rootPath.isEmpty()) {
        result.stack->addProvider(std::make_shared<AssetObjectsProvider>(
            reference->rootPath, reference->assetIndex));
        result.hasVanilla = true;
    }

    result.diagnostics.append(
        QStringLiteral("参考实例：%1（%2 · %3）")
            .arg(reference->name, reference->versionLabel(), reference->loaderLabel()));
    result.diagnostics.append(
        QStringLiteral("参考 mod jar：%1 个；原版来源：%2")
            .arg(result.referenceJarCount)
            .arg(result.hasVanilla ? QStringLiteral("客户端 jar / assets 对象")
                                   : QStringLiteral("无")));
    return result;
}

} // namespace mcstudio
