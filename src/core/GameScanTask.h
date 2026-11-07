#pragma once

#include "core/GameEnvironment.h"

#include <QStringList>
#include <functional>
#include <memory>

namespace mcstudio {

// Runs GameEnvironment::scanRoots on a thread-pool worker. The UI owns a
// poll() timer: progress and the final snapshot are copied out under a mutex
// and dispatched on the UI thread, so the worker never touches widgets or the
// dialog lifetime.
class GameScanTask
{
public:
    using Progress = std::function<void(int done, int total, const QString &message)>;
    using Finished = std::function<void(const GameEnvironmentSnapshot &snapshot, bool ok,
                                        const QString &error)>;

    GameScanTask();
    ~GameScanTask();

    void start(const QStringList &roots, const GameScanOptions &options, Progress progress,
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
