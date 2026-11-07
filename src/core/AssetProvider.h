#pragma once

#include "core/Project.h"

#include <QByteArray>
#include <QHash>
#include <QString>
#include <QStringList>
#include <memory>

namespace mcstudio {

enum class AssetOrigin {
    Missing,
    Pack,
    Reference,
    Vanilla,
    Conflict,
};

QString assetOriginLabel(AssetOrigin origin);

struct AssetLocation {
    AssetOrigin origin = AssetOrigin::Missing;
    QString label;  // human readable provider label
    QString detail; // extra path-level detail
    bool conflict = false;

    bool found() const { return origin != AssetOrigin::Missing; }
    bool fromReference() const
    {
        return origin == AssetOrigin::Reference || origin == AssetOrigin::Vanilla;
    }
};

// Layered read-only asset lookup. Providers are consulted in insertion order;
// the first hit wins. Providers never write, so reference assets can supply
// structure without leaking into the pack or the exported ZIP.
class AssetProvider
{
public:
    virtual ~AssetProvider() = default;

    virtual bool exists(const QString &path) const = 0;
    virtual QByteArray read(const QString &path) const = 0;
    virtual QStringList list(const QString &prefix) const = 0;
    virtual QString label() const = 0;
    virtual AssetOrigin origin() const = 0;
    virtual QString detail(const QString &path) const;
};

class ProjectAssetProvider : public AssetProvider
{
public:
    explicit ProjectAssetProvider(const Project &project);

    bool exists(const QString &path) const override;
    QByteArray read(const QString &path) const override;
    QStringList list(const QString &prefix) const override;
    QString label() const override;
    AssetOrigin origin() const override;
    QString detail(const QString &path) const override;

private:
    Project m_project;
    QStringList m_paths;
};

// Reads a jar through its central directory only. The archive is opened on
// first use, and entries are never extracted.
class JarAssetProvider : public AssetProvider
{
public:
    JarAssetProvider(const QString &jarPath, const QString &label,
                     AssetOrigin origin = AssetOrigin::Reference);
    ~JarAssetProvider() override;

    bool exists(const QString &path) const override;
    QByteArray read(const QString &path) const override;
    QStringList list(const QString &prefix) const override;
    QString label() const override;
    AssetOrigin origin() const override;
    QString detail(const QString &path) const override;
    QString jarPath() const { return m_jarPath; }

private:
    void ensureLoaded() const;

    QString m_jarPath;
    QString m_label;
    AssetOrigin m_origin = AssetOrigin::Reference;
    mutable bool m_loaded = false;
    mutable bool m_open = false;
    mutable QString m_error;
    mutable std::unique_ptr<class ZipArchive> m_archive;
    mutable QHash<QString, QString> m_entryLookup; // normalized path -> entry path
    mutable QStringList m_paths;
};

// Fallback for installs whose client jar has no assets: reads
// assets/indexes/<id>.json and fetches assets/objects/<hash>.
class AssetObjectsProvider : public AssetProvider
{
public:
    AssetObjectsProvider(const QString &rootPath, const QString &assetIndexId);
    ~AssetObjectsProvider() override;

    bool exists(const QString &path) const override;
    QByteArray read(const QString &path) const override;
    QStringList list(const QString &prefix) const override;
    QString label() const override;
    AssetOrigin origin() const override;
    QString detail(const QString &path) const override;

private:
    void ensureLoaded() const;

    QString m_rootPath;
    QString m_assetIndexId;
    mutable bool m_loaded = false;
    mutable QHash<QString, QString> m_objects; // "assets/<name>" -> hash
    mutable QStringList m_paths;
};

} // namespace mcstudio
