#pragma once

#include "core/ModCatalog.h"

#include <QStringList>
#include <functional>
#include <memory>

namespace mcstudio {

// Background ModCatalog builder with the same poll-based UI bridge as
// GameScanTask: the worker never touches widgets.
class ModCatalogTask
{
public:
    using Progress = ModCatalog::Progress;
    using Finished =
        std::function<void(const ModCatalog &catalog, bool ok, const QString &error)>;

    ModCatalogTask();
    ~ModCatalogTask();

    void start(const InstanceInfo &reference, const QVector<InstanceInfo> &otherInstances,
               const QStringList &namespaces, bool allowCrossInstance, Progress progress,
               Finished finished);
    void cancel();
    bool isRunning() const;
    void poll();

private:
    struct State;
    std::shared_ptr<State> m_state;
    bool m_delivered = true;
};

} // namespace mcstudio
