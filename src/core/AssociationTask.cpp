#include "core/AssociationTask.h"

#include "core/AssetStack.h"
#include "core/ModCatalog.h"
#include "core/Project.h"

#include <QMutex>
#include <QMutexLocker>
#include <QRunnable>
#include <QThreadPool>

#include <exception>
#include <utility>

namespace mcstudio {

struct AssociationTask::State {
    std::atomic_bool cancelled{false};
    std::atomic_bool running{false};

    Project project;
    std::shared_ptr<const AssetStack> assets;
    std::shared_ptr<const ModCatalog> catalog;
    QStringList namespaces;

    QMutex mutex;
    bool progressPending = false;
    int done = 0;
    int total = 0;
    QString message;

    bool finishedPending = false;
    bool ok = false;
    QString error;
    AssociationResult result;

    Progress progressCallback;
    Finished finishedCallback;
};

namespace {

class AssociationRunnable : public QRunnable
{
public:
    explicit AssociationRunnable(std::function<void()> work)
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

AssociationTask::AssociationTask() = default;

AssociationTask::~AssociationTask()
{
    cancel();
}

void AssociationTask::start(const Project &project, std::shared_ptr<const AssetStack> assets,
                            std::shared_ptr<const ModCatalog> catalog,
                            const QStringList &namespaces, Progress progress,
                            Finished finished)
{
    cancel();

    auto state = std::make_shared<State>();
    state->project = project;
    state->assets = std::move(assets);
    state->catalog = std::move(catalog);
    state->namespaces = namespaces;
    state->progressCallback = std::move(progress);
    state->finishedCallback = std::move(finished);
    state->running.store(true);
    m_state = state;
    m_delivered = false;

    QThreadPool::globalInstance()->start(new AssociationRunnable([state]() {
        AssociationResult result;
        QString error;
        bool ok = true;

        try {
            if (state->assets) {
                const ModCatalog emptyCatalog;
                const ModCatalog &catalog =
                    state->catalog ? *state->catalog : emptyCatalog;
                result = TextureAssociation::build(
                    state->project, *state->assets, catalog, state->namespaces,
                    [state](int done, int total, const QString &message) {
                        QMutexLocker locker(&state->mutex);
                        state->progressPending = true;
                        state->done = done;
                        state->total = total;
                        state->message = message;
                    },
                    &state->cancelled);
            } else {
                result = TextureAssociation::build(state->project);
            }
        } catch (const std::exception &exception) {
            ok = false;
            error = QString::fromLocal8Bit(exception.what());
        } catch (...) {
            ok = false;
            error = QStringLiteral("模型关联解析发生未知错误");
        }

        QMutexLocker locker(&state->mutex);
        state->result = std::move(result);
        state->ok = ok;
        state->error = error;
        state->finishedPending = true;
        state->running.store(false);
    }));
}

void AssociationTask::cancel()
{
    if (m_state)
        m_state->cancelled.store(true);
}

bool AssociationTask::isRunning() const
{
    return m_state && m_state->running.load();
}

void AssociationTask::poll()
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
    AssociationResult result;

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
            result = m_state->result;
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
            finishedCallback(result, ok, error);
    }
}

} // namespace mcstudio
