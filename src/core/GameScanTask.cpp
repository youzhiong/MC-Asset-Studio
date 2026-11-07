#include "core/GameScanTask.h"

#include <QMutex>
#include <QMutexLocker>
#include <QRunnable>
#include <QThreadPool>

#include <exception>
#include <utility>

namespace mcstudio {

struct GameScanTask::State {
    std::atomic_bool cancelled{false};
    std::atomic_bool running{false};

    QMutex mutex;
    bool progressPending = false;
    int done = 0;
    int total = 0;
    QString message;

    bool finishedPending = false;
    bool ok = false;
    QString error;
    GameEnvironmentSnapshot snapshot;

    Progress progressCallback;
    Finished finishedCallback;
};

namespace {

class ScanRunnable : public QRunnable
{
public:
    explicit ScanRunnable(std::function<void()> work)
        : m_work(std::move(work))
    {
    }

    void run() override
    {
        if (m_work)
            m_work();
    }

private:
    std::function<void()> m_work;
};

} // namespace

GameScanTask::GameScanTask() = default;

GameScanTask::~GameScanTask()
{
    cancel();
}

void GameScanTask::start(const QStringList &roots, const GameScanOptions &options,
                         Progress progress, Finished finished)
{
    // Cancel a previous run; its worker keeps its own state alive through the
    // shared pointer, so results are simply dropped.
    cancel();

    auto state = std::make_shared<State>();
    state->progressCallback = std::move(progress);
    state->finishedCallback = std::move(finished);
    state->running.store(true);
    m_state = state;
    m_delivered = false;

    const QStringList rootsCopy = roots;
    const GameScanOptions optionsCopy = options;

    QThreadPool::globalInstance()->start(new ScanRunnable([state, rootsCopy, optionsCopy]() {
        GameEnvironmentSnapshot snapshot;
        QString error;
        bool ok = true;

        try {
            snapshot = GameEnvironment::scanRoots(
                rootsCopy, optionsCopy,
                [state](int done, int total, const QString &message) {
                    QMutexLocker locker(&state->mutex);
                    state->progressPending = true;
                    state->done = done;
                    state->total = total;
                    state->message = message;
                },
                &state->cancelled);
        } catch (const std::exception &exception) {
            ok = false;
            error = QString::fromLocal8Bit(exception.what());
        } catch (...) {
            ok = false;
            error = QStringLiteral("环境扫描发生未知错误");
        }

        QMutexLocker locker(&state->mutex);
        state->snapshot = std::move(snapshot);
        state->ok = ok;
        state->error = error;
        state->finishedPending = true;
        state->running.store(false);
    }));
}

void GameScanTask::cancel()
{
    if (m_state)
        m_state->cancelled.store(true);
}

bool GameScanTask::isRunning() const
{
    return m_state && m_state->running.load();
}

void GameScanTask::poll()
{
    if (!m_state || m_delivered)
        return;

    Progress progressCallback;
    bool deliverProgress = false;
    int done = 0;
    int total = 0;
    QString message;

    Finished finishedCallback;
    bool deliverFinished = false;
    bool ok = false;
    QString error;
    GameEnvironmentSnapshot snapshot;

    {
        QMutexLocker locker(&m_state->mutex);
        if (m_state->progressPending) {
            deliverProgress = true;
            progressCallback = m_state->progressCallback;
            done = m_state->done;
            total = m_state->total;
            message = m_state->message;
            m_state->progressPending = false;
        }
        if (m_state->finishedPending) {
            deliverFinished = true;
            finishedCallback = m_state->finishedCallback;
            ok = m_state->ok;
            error = m_state->error;
            snapshot = m_state->snapshot;
            m_state->finishedPending = false;
            m_state->progressCallback = {};
            m_state->finishedCallback = {};
        }
    }

    if (deliverProgress && progressCallback)
        progressCallback(done, total, message);

    if (deliverFinished) {
        m_delivered = true;
        m_state.reset();
        if (finishedCallback)
            finishedCallback(snapshot, ok, error);
    }
}

} // namespace mcstudio
