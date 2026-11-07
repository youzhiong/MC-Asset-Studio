#pragma once

#include "core/TextureAssociation.h"

#include <QStringList>
#include <functional>
#include <memory>

namespace mcstudio {

class AssetStack;
class ModCatalog;
class Project;

// Background TextureAssociation build. Model graphs for large packs take
// seconds; the UI must stay responsive and show progress.
class AssociationTask
{
public:
    using Progress = TextureAssociation::Progress;
    using Finished =
        std::function<void(const AssociationResult &result, bool ok, const QString &error)>;

    AssociationTask();
    ~AssociationTask();

    void start(const Project &project, std::shared_ptr<const AssetStack> assets,
               std::shared_ptr<const ModCatalog> catalog, const QStringList &namespaces,
               Progress progress, Finished finished);
    void cancel();
    bool isRunning() const;
    void poll();

private:
    struct State;
    std::shared_ptr<State> m_state;
    bool m_delivered = true;
};

} // namespace mcstudio
