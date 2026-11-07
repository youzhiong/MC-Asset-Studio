#pragma once

#include "core/InstanceInfo.h"
#include "core/ModInfo.h"

#include <QHash>
#include <QString>
#include <QStringList>
#include <QVector>
#include <atomic>
#include <functional>

namespace mcstudio {

// One pack namespace resolved to a mod (or to a built-in / unknown source).
struct NamespaceMod {
    QString namespaceId;
    QString modId;
    QString displayName;
    QString version;
    QString jarPath;
    QString logoFile;
    QString sourceInstanceKey;
    QString sourceInstanceName;
    QString source; // builtin / local / cross / missing
    bool builtin = false;
    bool found = false;
    bool ambiguous = false;
    int assetCount = 0; // assets/<namespace>/ entries in the owning jar
    QStringList candidateJars;
    QString detail;

    QString label() const;
    QString sourceLabel() const;
    bool matchesMod(const QString &filter) const;
};

// Namespace -> mod mapping for one reference instance. Metadata for missing
// namespaces may be filled from other scanned instances, but their assets are
// never used as reference.
class ModCatalog
{
public:
    using Progress = std::function<void(int done, int total, const QString &message)>;

    static ModCatalog build(const InstanceInfo &reference,
                            const QVector<InstanceInfo> &otherInstances,
                            const QStringList &namespaces, bool allowCrossInstance,
                            bool forceRefreshMods = false, const Progress &progress = {},
                            const std::atomic_bool *cancel = nullptr);

    const QVector<NamespaceMod> &entries() const { return m_entries; }
    const NamespaceMod *entry(const QString &namespaceId) const;
    QString displayNameFor(const QString &namespaceId) const;
    QString modIdFor(const QString &namespaceId) const;
    QStringList namespaces() const;

    const QVector<JarModScan> &localScans() const { return m_localScans; }
    const QVector<ModInfo> &localMods() const { return m_localMods; }
    const QVector<QString> &errors() const { return m_errors; }

    int foundCount() const;
    int missingCount() const;
    int crossCount() const;
    int builtinCount() const;
    int jarScanCount() const { return m_jarScanCount; }
    int cachedJarCount() const { return m_cachedJarCount; }
    const QStringList &crossInstancesScanned() const { return m_crossInstances; }
    QString referenceInstanceKey() const { return m_referenceKey; }
    QString referenceInstanceName() const { return m_referenceName; }

private:
    QVector<NamespaceMod> m_entries;
    QHash<QString, int> m_entryIndex;
    QVector<JarModScan> m_localScans;
    QVector<ModInfo> m_localMods;
    QVector<QString> m_errors;
    QStringList m_crossInstances;
    QString m_referenceKey;
    QString m_referenceName;
    int m_jarScanCount = 0;
    int m_cachedJarCount = 0;
};

} // namespace mcstudio
