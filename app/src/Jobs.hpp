// Background jobs (RFC-0010): lanes with bounded concurrency, progress, cancellation, results delivered on
// the UI thread. Core calls never run on the UI thread (CONTRACT-ui-core §1).
// Task safety (RFC-0010 §Task safety, FK-006): every job ends in exactly one state — succeeded, failed,
// cancelled or outdated. A job reports failure through JobContext::fail() or by returning JobApply::failed
// from `done`; the `done` callback decides whether its result still matches the current state and returns
// JobApply::outdated when it does not, in which case nothing was written and the job list says so.
#pragma once

#include "fk/common.hpp"

#include <QAbstractListModel>
#include <QElapsedTimer>
#include <QObject>
#include <QThreadPool>

#include <functional>
#include <memory>
#include <mutex>
#include <vector>

namespace fkapp {

enum class Lane { detect, exportJob, io };

struct JobContext {
    fk::CancelToken cancel;
    std::function<void(double, const QString&)> progress; ///< thread-safe, throttled
    /// Marks the job as failed (worker thread). The first message is kept and shown in the job list.
    void fail(const QString& message) {
        std::lock_guard<std::mutex> lk(m_);
        if (!failed_) failMessage_ = message;
        failed_ = true;
    }
    bool failed() const {
        std::lock_guard<std::mutex> lk(m_);
        return failed_;
    }
    QString failMessage() const {
        std::lock_guard<std::mutex> lk(m_);
        return failMessage_;
    }

  private:
    mutable std::mutex m_;
    bool failed_ = false;
    QString failMessage_;
};

/// How a job ended, as seen by its `done` callback on the UI thread.
struct JobOutcome {
    bool cancelled = false;
    bool failed = false;
    QString message; ///< failure message
};

/// What `done` did with the result: applied it; found it outdated and discarded it unchanged; or found that
/// the work failed (the message is shown in the job list).
enum class JobApply { applied, outdated, failed };
struct JobDone {
    JobApply apply = JobApply::applied;
    QString message;
    JobDone(JobApply a = JobApply::applied, QString m = {}) : apply(a), message(std::move(m)) {}
};

class JobModel;

/// One background job. `work` runs on a worker thread; `done` runs on the UI thread afterwards.
class JobRunner : public QObject {
    Q_OBJECT
  public:
    explicit JobRunner(QObject* parent = nullptr);
    ~JobRunner() override;
    JobModel* model() const { return model_; }
    /// Returns the job id. `done` runs on the UI thread after `work`, also when the job was cancelled or
    /// failed. `estimateBytes` (FK-005): the job's expected working memory; see reservedBytes().
    int start(Lane lane, const QString& title, std::function<void(JobContext&)> work,
              std::function<JobDone(const JobOutcome&)> done = {}, uint64_t estimateBytes = 0);
    /// FK-005: memory the queued and running jobs may need at once — per lane, the largest estimates of
    /// its active jobs, as many as the lane runs in parallel. Reserved before a job starts (it is counted
    /// from the moment it is queued), released when its `done` has run.
    uint64_t reservedBytes() const;
    /// Called on the UI thread whenever reservedBytes() changes (the cache budget makes room for it).
    void setReservationHandler(std::function<void(uint64_t)> f) { onReservation_ = std::move(f); }
    Q_INVOKABLE void cancel(int id);
    void cancelAll();
    bool busy() const;
    /// Jobs queued or running (FK-001: replacing the project cancels them).
    int activeCount() const;
    /// Blocks until every job has finished (shutdown, self-test).
    void waitAll();
    /// Test hook (self-test only): each job's work starts this many milliseconds late, so tests can change
    /// state while a job is in flight. 0 in the product.
    void setTestDelayMs(int ms) { testDelayMs_ = ms; }
    /// Id of the most recently started job (tests).
    int lastJobId() const { return nextId_ - 1; }
    /// Final state of a finished job ("succeeded", "failed", "cancelled", "outdated"), empty while running.
    QString finalState(int id) const;
    /// Test hook: the job's work has ended and its result is waiting for the UI thread (`done` not run yet).
    bool workEnded(int id) const;

  Q_SIGNALS:
    void busyChanged();

  private:
    struct Job;
    QThreadPool detect_, export_, io_;
    JobModel* model_;
    std::vector<std::shared_ptr<Job>> jobs_;
    std::vector<std::pair<int, QString>> finished_; ///< id → final state (bounded, for tests and the UI)
    std::function<void(uint64_t)> onReservation_;
    uint64_t lastReserved_ = 0;
    void reservationChanged();
    int nextId_ = 1;
    int testDelayMs_ = 0;
    friend class JobModel;
};

class JobModel : public QAbstractListModel {
    Q_OBJECT
    Q_PROPERTY(int count READ count NOTIFY countChanged)
    Q_PROPERTY(double overallProgress READ overallProgress NOTIFY progressChanged)
    Q_PROPERTY(QString summary READ summary NOTIFY progressChanged)
  public:
    enum Roles {
        IdRole = Qt::UserRole + 1,
        TitleRole,
        ProgressRole,
        StageRole,
        StateRole,
        ElapsedRole,
        MessageRole
    };
    struct Row {
        int id = 0;
        QString title, stage, state; ///< queued / running / succeeded / failed / cancelled / outdated
        QString message;             ///< failure message
        double progress = 0;
        QElapsedTimer timer;
        qint64 finishedMs = -1;
    };
    explicit JobModel(QObject* parent = nullptr) : QAbstractListModel(parent) {}
    int rowCount(const QModelIndex& = {}) const override { return static_cast<int>(rows_.size()); }
    QVariant data(const QModelIndex& index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;
    int count() const { return static_cast<int>(rows_.size()); }
    int activeCount() const {
        int n = 0;
        for (const auto& r : rows_)
            if (r.state == QLatin1String("queued") || r.state == QLatin1String("running")) ++n;
        return n;
    }
    double overallProgress() const;
    QString summary() const;
    Q_INVOKABLE void clearFinished();

    void add(int id, const QString& title);
    void update(int id, double progress, const QString& stage, const QString& state,
                const QString& message = {});

  Q_SIGNALS:
    void countChanged();
    void progressChanged();

  private:
    std::vector<Row> rows_;
    int indexOf(int id) const;
};

} // namespace fkapp
