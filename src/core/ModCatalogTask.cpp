#include "core/ModCatalogTask.h"

#include <QMutex>
#include <QMutexLocker>
#include <QRunnable>
#include <QThreadPool>

#include <exception>
#include <utility>

namespace mcstudio {

struct ModCatalogTask::State {
    std::atomic_bool cancelled{false};
    std::atomic_bool running{false};

    InstanceInfo reference;
    QVector<InstanceInfo> otherInstances;
    QStringList namespaces;
    bool allowCrossInstance = true;

    QMutex mutex;
    bool progressPending = false;
    int done = 0;
    int total = 0;
    QString message;

    bool finishedPending = false;
    bool ok = false;
    QString error;
    ModCatalog catalog;

    Progress progressCallback;
    Finished finishedCallback;
};

namespace {

class CatalogRunnable : public QRunnable
{
public:
    explicit CatalogRunnable(std::function<void()> work)
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

ModCatalogTask::ModCatalogTask() = default;

ModCatalogTask::~ModCatalogTask()
{
    cancel();
}

void ModCatalogTask::start(const InstanceInfo &reference,
                           const QVector<InstanceInfo> &otherInstances,
                           const QStringList &namespaces, bool allowCrossInstance,
                           Progress progress, Finished finished)
{
    cancel();

    auto state = std::make_shared<State>();
    state->reference = reference;
    state->otherInstances = otherInstances;
    state->namespaces = namespaces;
    state->allowCrossInstance = allowCrossInstance;
    state->progressCallback = std::move(progress);
    state->finishedCallback = std::move(finished);
    state->running.store(true);
    m_state = state;
    m_delivered = false;

    QThreadPool::globalInstance()->start(new CatalogRunnable([state]() {
        ModCatalog catalog;
        QString error;
        bool ok = true;

        try {
            catalog = ModCatalog::build(
                state->reference, state->otherInstances, state->namespaces,
                state->allowCrossInstance, false,
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
            error = QStringLiteral("模组元数据解析发生未知错误");
        }

        QMutexLocker locker(&state->mutex);
        state->catalog = std::move(catalog);
        state->ok = ok;
        state->error = error;
        state->finishedPending = true;
        state->running.store(false);
    }));
}

void ModCatalogTask::cancel()
{
    if (m_state)
        m_state->cancelled.store(true);
}

bool ModCatalogTask::isRunning() const
{
    return m_state && m_state->running.load();
}

void ModCatalogTask::poll()
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
    ModCatalog catalog;

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
            catalog = m_state->catalog;
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
            finishedCallback(catalog, ok, error);
    }
}

} // namespace mcstudio
