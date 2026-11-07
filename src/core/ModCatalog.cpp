#include "core/ModCatalog.h"

#include "core/ModScanner.h"

#include <QSet>
#include <algorithm>
#include <utility>

namespace mcstudio {
namespace {

const QHash<QString, QString> &builtinNamespaces()
{
    static const QHash<QString, QString> map = {
        {QStringLiteral("minecraft"), QStringLiteral("Minecraft")},
        {QStringLiteral("forge"), QStringLiteral("Forge")},
        {QStringLiteral("neoforge"), QStringLiteral("NeoForge")},
        {QStringLiteral("fabric"), QStringLiteral("Fabric")},
        {QStringLiteral("quilt"), QStringLiteral("Quilt")},
        {QStringLiteral("c"), QStringLiteral("公共库 (c)")},
        {QStringLiteral("common"), QStringLiteral("公共库 (common)")},
    };
    return map;
}

void fillEntryFromScan(NamespaceMod &entry, const JarModScan &scan, const QString &namespaceId)
{
    entry.jarPath = scan.jarPath;
    entry.assetCount = scan.namespaceCounts.value(namespaceId, 0);

    const ModInfo *match = nullptr;
    for (const ModInfo &mod : scan.mods) {
        if (mod.modId == namespaceId) {
            match = &mod;
            break;
        }
    }
    if (!match && !scan.mods.isEmpty())
        match = &scan.mods.first();

    if (match) {
        entry.modId = match->modId;
        entry.displayName = match->displayLabel();
        entry.version = match->version;
        entry.logoFile = match->logoFile;
    }
    if (entry.modId.isEmpty())
        entry.modId = namespaceId;
    if (entry.displayName.isEmpty())
        entry.displayName = namespaceId;
}

// A mod jar may legitimately ship a handful of assets for another mod's
// namespace (compat models/textures). Only treat the namespace as owned when
// it is the jar's dominant namespace, has a substantial asset count, or the
// jar carries no metadata at all.
bool jarOwnsNamespace(const JarModScan &scan, const QString &namespaceId)
{
    const int count = scan.namespaceCounts.value(namespaceId, 0);
    if (count <= 0)
        return false;

    bool hasModInfo = false;
    for (const ModInfo &mod : scan.mods) {
        if (!mod.modId.isEmpty()) {
            hasModInfo = true;
            break;
        }
    }
    if (!hasModInfo)
        return true;

    int maxCount = 0;
    for (auto it = scan.namespaceCounts.constBegin();
         it != scan.namespaceCounts.constEnd(); ++it) {
        maxCount = qMax(maxCount, it.value());
    }
    if (count == maxCount)
        return true;

    // A real secondary namespace is normally a meaningful share of the jar.
    // Compat bundles sit at a few percent of the owning namespace.
    const int threshold = qMax(8, maxCount / 5);
    return count >= threshold;
}

QVector<ModFileInfo> enabledJars(const InstanceInfo &instance)
{
    QVector<ModFileInfo> jars;
    for (const ModFileInfo &mod : instance.mods) {
        if (!mod.disabled)
            jars.append(mod);
    }
    return jars;
}

} // namespace

QString NamespaceMod::label() const
{
    return displayName.isEmpty() ? namespaceId : displayName;
}

QString NamespaceMod::sourceLabel() const
{
    if (source == QLatin1String("builtin"))
        return QStringLiteral("内置");
    if (source == QLatin1String("local"))
        return QStringLiteral("当前实例");
    if (source == QLatin1String("cross"))
        return QStringLiteral("其他实例 · %1").arg(sourceInstanceName);
    return QStringLiteral("未找到模组");
}

bool NamespaceMod::matchesMod(const QString &filter) const
{
    if (filter.isEmpty())
        return true;
    return namespaceId.compare(filter, Qt::CaseInsensitive) == 0
           || modId.compare(filter, Qt::CaseInsensitive) == 0
           || displayName.compare(filter, Qt::CaseInsensitive) == 0;
}

ModCatalog ModCatalog::build(const InstanceInfo &reference,
                             const QVector<InstanceInfo> &otherInstances,
                             const QStringList &namespaces, bool allowCrossInstance,
                             bool forceRefreshMods, const Progress &progress,
                             const std::atomic_bool *cancel)
{
    ModCatalog catalog;
    catalog.m_referenceKey = reference.key;
    catalog.m_referenceName = reference.name;

    const auto isCancelled = [cancel]() {
        return cancel && cancel->load(std::memory_order_relaxed);
    };

    const QVector<ModFileInfo> localJars = enabledJars(reference);
    int otherJarTotal = 0;
    if (allowCrossInstance) {
        for (const InstanceInfo &instance : otherInstances) {
            if (instance.key == reference.key)
                continue;
            for (const ModFileInfo &mod : instance.mods) {
                if (!mod.disabled)
                    ++otherJarTotal;
            }
        }
    }
    const int total = qMax(1, static_cast<int>(localJars.size()) + otherJarTotal);
    int done = 0;

    // Local instance: index namespaces and mod ids straight from the jar
    // central directories.
    QHash<QString, QVector<int>> scanIndicesByNamespace;
    QHash<QString, QVector<int>> scanIndicesByModId;

    for (const ModFileInfo &jar : localJars) {
        if (isCancelled())
            break;

        JarModScan scan = ModScanner::scanJar(jar, reference.key, reference.loader,
                                              forceRefreshMods, true);
        ++catalog.m_jarScanCount;
        if (scan.fromCache)
            ++catalog.m_cachedJarCount;

        const int scanIndex = static_cast<int>(catalog.m_localScans.size());
        catalog.m_localScans.append(scan);
        for (const QString &ns : scan.namespaces)
            scanIndicesByNamespace[ns].append(scanIndex);
        for (const ModInfo &mod : scan.mods) {
            if (!mod.modId.isEmpty())
                scanIndicesByModId[mod.modId].append(scanIndex);
            catalog.m_localMods.append(mod);
        }
        if (!scan.ok()) {
            catalog.m_errors.append(scan.error.isEmpty()
                                        ? QStringLiteral("%1：无法读取").arg(scan.fileName)
                                        : QStringLiteral("%1：%2").arg(scan.fileName, scan.error));
        }

        ++done;
        if (progress && (done % 5 == 0 || done == localJars.size())) {
            progress(done, total,
                     QStringLiteral("解析模组元数据 %1/%2").arg(done).arg(localJars.size()));
        }
    }

    QVector<int> unresolved;
    for (const QString &rawNamespace : namespaces) {
        const QString ns = rawNamespace.trimmed();
        if (ns.isEmpty())
            continue;

        NamespaceMod entry;
        entry.namespaceId = ns;
        bool resolved = false;

        const auto builtin = builtinNamespaces().constFind(ns);
        if (builtin != builtinNamespaces().constEnd()) {
            entry.displayName = builtin.value();
            entry.modId = ns;
            entry.source = QStringLiteral("builtin");
            entry.builtin = true;
            entry.found = true;
            entry.detail = QStringLiteral("Minecraft / 加载器内置命名空间");
            resolved = true;
        }

        if (!resolved) {
            const auto byModId = scanIndicesByModId.constFind(ns);
            if (byModId != scanIndicesByModId.constEnd() && !byModId->isEmpty()) {
                const int scanIndex = byModId->first();
                fillEntryFromScan(entry, catalog.m_localScans.at(scanIndex), ns);
                entry.modId = ns;
                entry.source = QStringLiteral("local");
                entry.found = true;
                entry.ambiguous = byModId->size() > 1;
                for (int index : *byModId)
                    entry.candidateJars.append(catalog.m_localScans.at(index).jarPath);
                entry.detail = entry.ambiguous
                                   ? QStringLiteral("modId 匹配（存在多版本冲突）")
                                   : QStringLiteral("modId 匹配");
                resolved = true;
            }
        }

        if (!resolved) {
            const auto byAssets = scanIndicesByNamespace.constFind(ns);
            if (byAssets != scanIndicesByNamespace.constEnd()) {
                QVector<int> owned;
                int bestIndex = -1;
                int bestCount = -1;
                for (int index : *byAssets) {
                    const JarModScan &scan = catalog.m_localScans.at(index);
                    if (!jarOwnsNamespace(scan, ns))
                        continue;
                    owned.append(index);
                    const int count = scan.namespaceCounts.value(ns);
                    if (count > bestCount) {
                        bestCount = count;
                        bestIndex = index;
                    }
                }
                if (bestIndex >= 0) {
                    fillEntryFromScan(entry, catalog.m_localScans.at(bestIndex), ns);
                    entry.source = QStringLiteral("local");
                    entry.found = true;
                    entry.ambiguous = owned.size() > 1;
                    for (int index : owned)
                        entry.candidateJars.append(catalog.m_localScans.at(index).jarPath);
                    entry.detail = entry.ambiguous
                                       ? QStringLiteral("jar 内 assets 反查（多候选）")
                                       : QStringLiteral("jar 内 assets 反查");
                    resolved = true;
                }
            }
        }

        if (!resolved) {
            entry.modId = ns;
            entry.displayName = ns;
            entry.source = QStringLiteral("missing");
            entry.found = false;
            entry.detail = QStringLiteral("未找到模组");
            unresolved.append(static_cast<int>(catalog.m_entries.size()));
        }

        catalog.m_entryIndex.insert(ns, static_cast<int>(catalog.m_entries.size()));
        catalog.m_entries.append(entry);
    }

    // Cross-instance fill: metadata only, never assets. Strong modId matches
    // win over assets reverse lookups; a compat jar never owns another mod's
    // namespace.
    if (allowCrossInstance && !unresolved.isEmpty()) {
        struct CrossCandidate {
            bool strong = false;
            int assetCount = 0;
            JarModScan scan;
            QString instanceKey;
            QString instanceName;
        };
        QHash<int, CrossCandidate> bestCandidates;
        bool allStrong = false;

        for (const InstanceInfo &instance : otherInstances) {
            if (instance.key == reference.key)
                continue;
            if (unresolved.isEmpty() || isCancelled() || allStrong)
                break;

            catalog.m_crossInstances.append(instance.name);
            const QVector<ModFileInfo> jars = enabledJars(instance);
            for (const ModFileInfo &jar : jars) {
                if (unresolved.isEmpty() || isCancelled())
                    break;

                JarModScan scan = ModScanner::scanJar(jar, instance.key, instance.loader,
                                                      forceRefreshMods, true);
                ++catalog.m_jarScanCount;
                if (scan.fromCache)
                    ++catalog.m_cachedJarCount;
                ++done;

                for (int entryIndex : std::as_const(unresolved)) {
                    const QString ns = catalog.m_entries.at(entryIndex).namespaceId;

                    bool strong = false;
                    for (const ModInfo &mod : scan.mods) {
                        if (mod.modId == ns) {
                            strong = true;
                            break;
                        }
                    }
                    if (!strong && !jarOwnsNamespace(scan, ns))
                        continue;

                    const int assetCount = scan.namespaceCounts.value(ns);
                    const auto existing = bestCandidates.constFind(entryIndex);
                    if (existing != bestCandidates.constEnd()) {
                        const bool better = (strong && !existing->strong)
                                            || (strong == existing->strong
                                                && assetCount > existing->assetCount);
                        if (!better)
                            continue;
                    }

                    CrossCandidate candidate;
                    candidate.strong = strong;
                    candidate.assetCount = assetCount;
                    candidate.scan = scan;
                    candidate.instanceKey = instance.key;
                    candidate.instanceName = instance.name;
                    bestCandidates.insert(entryIndex, candidate);
                }

                // Every unresolved namespace already has a strong modId match;
                // later jars cannot improve on that.
                allStrong = true;
                for (int entryIndex : std::as_const(unresolved)) {
                    const auto it = bestCandidates.constFind(entryIndex);
                    if (it == bestCandidates.constEnd() || !it->strong) {
                        allStrong = false;
                        break;
                    }
                }
                if (allStrong)
                    break;

                if (progress && (done % 5 == 0)) {
                    progress(done, total,
                             QStringLiteral("补全模组元数据 %1").arg(instance.name));
                }
            }
        }

        for (int entryIndex : std::as_const(unresolved)) {
            const auto candidate = bestCandidates.constFind(entryIndex);
            if (candidate == bestCandidates.constEnd())
                continue;

            NamespaceMod &entry = catalog.m_entries[entryIndex];
            fillEntryFromScan(entry, candidate->scan, entry.namespaceId);
            entry.found = true;
            entry.source = QStringLiteral("cross");
            entry.sourceInstanceKey = candidate->instanceKey;
            entry.sourceInstanceName = candidate->instanceName;
            entry.detail = candidate->strong
                               ? QStringLiteral("其他实例 modId 匹配；仅补全元数据")
                               : QStringLiteral("其他实例 assets 反查；仅补全元数据");
        }
    }

    std::sort(catalog.m_entries.begin(), catalog.m_entries.end(),
              [](const NamespaceMod &a, const NamespaceMod &b) {
                  const int byLabel = a.label().compare(b.label(), Qt::CaseInsensitive);
                  if (byLabel != 0)
                      return byLabel < 0;
                  return a.namespaceId.compare(b.namespaceId, Qt::CaseInsensitive) < 0;
              });

    catalog.m_entryIndex.clear();
    for (int index = 0; index < static_cast<int>(catalog.m_entries.size()); ++index)
        catalog.m_entryIndex.insert(catalog.m_entries.at(index).namespaceId, index);

    if (progress)
        progress(total, total, QStringLiteral("模组元数据解析完成"));

    return catalog;
}

const NamespaceMod *ModCatalog::entry(const QString &namespaceId) const
{
    const auto index = m_entryIndex.constFind(namespaceId);
    if (index == m_entryIndex.constEnd())
        return nullptr;
    return &m_entries.at(index.value());
}

QString ModCatalog::displayNameFor(const QString &namespaceId) const
{
    if (const NamespaceMod *found = entry(namespaceId))
        return found->label();
    return namespaceId;
}

QString ModCatalog::modIdFor(const QString &namespaceId) const
{
    if (const NamespaceMod *found = entry(namespaceId))
        return found->modId;
    return namespaceId;
}

QStringList ModCatalog::namespaces() const
{
    QStringList result;
    for (const NamespaceMod &entry : m_entries)
        result.append(entry.namespaceId);
    return result;
}

int ModCatalog::foundCount() const
{
    int count = 0;
    for (const NamespaceMod &entry : m_entries) {
        if (entry.found)
            ++count;
    }
    return count;
}

int ModCatalog::missingCount() const
{
    return static_cast<int>(m_entries.size()) - foundCount();
}

int ModCatalog::crossCount() const
{
    int count = 0;
    for (const NamespaceMod &entry : m_entries) {
        if (entry.source == QLatin1String("cross"))
            ++count;
    }
    return count;
}

int ModCatalog::builtinCount() const
{
    int count = 0;
    for (const NamespaceMod &entry : m_entries) {
        if (entry.builtin)
            ++count;
    }
    return count;
}

} // namespace mcstudio
