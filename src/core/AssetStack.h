#pragma once

#include "core/AssetProvider.h"

#include <QString>
#include <QStringList>
#include <QVector>
#include <memory>

namespace mcstudio {

// Ordered fallback chain. The project pack is always first; reference mod
// jars, the client jar and asset objects follow.
class AssetStack
{
public:
    void addProvider(std::shared_ptr<AssetProvider> provider);
    void clear();
    bool isEmpty() const { return m_providers.isEmpty(); }
    int providerCount() const { return static_cast<int>(m_providers.size()); }

    bool exists(const QString &path) const;
    QByteArray read(const QString &path) const;
    QVector<QByteArray> readAll(const QString &path) const;
    AssetLocation locate(const QString &path) const;
    QStringList list(const QString &prefix) const;
    QStringList providerLabels() const;

    const QVector<std::shared_ptr<AssetProvider>> &providers() const { return m_providers; }

private:
    QVector<std::shared_ptr<AssetProvider>> m_providers;
};

} // namespace mcstudio
