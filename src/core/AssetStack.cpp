#include "core/AssetStack.h"

#include <QSet>
#include <utility>

namespace mcstudio {

void AssetStack::addProvider(std::shared_ptr<AssetProvider> provider)
{
    if (provider)
        m_providers.append(std::move(provider));
}

void AssetStack::clear()
{
    m_providers.clear();
}

bool AssetStack::exists(const QString &path) const
{
    for (const std::shared_ptr<AssetProvider> &provider : m_providers) {
        if (provider->exists(path))
            return true;
    }
    return false;
}

QByteArray AssetStack::read(const QString &path) const
{
    for (const std::shared_ptr<AssetProvider> &provider : m_providers) {
        if (provider->exists(path))
            return provider->read(path);
    }
    return {};
}

QVector<QByteArray> AssetStack::readAll(const QString &path) const
{
    QVector<QByteArray> layers;
    for (const std::shared_ptr<AssetProvider> &provider : m_providers) {
        if (provider->exists(path))
            layers.append(provider->read(path));
    }
    return layers;
}

AssetLocation AssetStack::locate(const QString &path) const
{
    AssetLocation location;
    for (const std::shared_ptr<AssetProvider> &provider : m_providers) {
        if (!provider->exists(path))
            continue;
        location.origin = provider->origin();
        location.label = provider->label();
        location.detail = provider->detail(path);
        break;
    }
    return location;
}

QStringList AssetStack::list(const QString &prefix) const
{
    QStringList result;
    QSet<QString> seen;
    for (const std::shared_ptr<AssetProvider> &provider : m_providers) {
        for (const QString &path : provider->list(prefix)) {
            if (seen.contains(path))
                continue;
            seen.insert(path);
            result.append(path);
        }
    }
    result.sort(Qt::CaseInsensitive);
    return result;
}

QStringList AssetStack::providerLabels() const
{
    QStringList labels;
    for (const std::shared_ptr<AssetProvider> &provider : m_providers)
        labels.append(provider->label());
    return labels;
}

} // namespace mcstudio
