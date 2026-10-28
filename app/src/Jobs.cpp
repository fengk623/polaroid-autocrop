#include "Jobs.hpp"

#include <QMetaObject>
#include <QPointer>
#include <QRunnable>

#include <atomic>
#include <chrono>
#include <mutex>

namespace fkapp {

struct JobRunner::Job {
    int id = 0;
    JobContext ctx;
    std::atomic<bool> finished{false};
    std::mutex m;
    std::chrono::steady_clock::time_point lastReport{};
};

JobRunner::JobRunner(QObject* parent) : QObject(parent), model_(new JobModel(this)) {
    // Memory budget (PERFORMANCE-BUDGET §2): one detection (streams the whole scan) and one export at a
    // time; light I/O (thumbnails, full-resolution view tiles) in parallel.
    detect_.setMaxThreadCount(1);
    export_.setMaxThreadCount(1);
    io_.setMaxThreadCount(2);
}

JobRunner::~JobRunner() {
    cancelAll();
    waitAll();
}

int JobRunner::start(Lane lane, const QString& title, std::function<void(JobContext&)> work,
                     std::function<void(bool)> done) {
    auto job = std::make_shared<Job>();
    job->id = nextId_++;
    jobs_.push_back(job);
    const bool visible = lane != Lane::io;
    if (visible) model_->add(job->id, title);
    const int id = job->id;
    QPointer<JobRunner> self(this);
    job->ctx.progress = [self, job, visible](double f, const QString& stage) {
        if (!visible) return;
        {
            std::lock_guard<std::mutex> lk(job->m);
            const auto now = std::chrono::steady_clock::now();
            if (f < 1.0 && now - job->lastReport < std::chrono::milliseconds(50))
                return; // throttle (RFC-0010)
            job->lastReport = now;
        }
        QMetaObject::invokeMethod(
            self.data(),
            [self, id = job->id, f, stage]() {
                if (self) self->model_->update(id, f, stage, QStringLiteral("running"));
            },
            Qt::QueuedConnection);
    };
    QThreadPool& pool = lane == Lane::detect ? detect_ : lane == Lane::exportJob ? export_ : io_;
    pool.start(QRunnable::create([self, job, work = std::move(work), done = std::move(done), visible,
                                  id]() mutable {
        if (!job->ctx.cancel.cancelled()) work(job->ctx);
        const bool cancelled = job->ctx.cancel.cancelled();
        job->finished = true;
        QMetaObject::invokeMethod(
            self.data(),
            [self, id, cancelled, visible, done = std::move(done)]() {
                if (!self) return;
                if (visible)
                    self->model_->update(id, 1.0, QString(),
                                         cancelled ? QStringLiteral("cancelled") : QStringLiteral("done"));
                if (done) done(cancelled);
                auto& v = self->jobs_;
                v.erase(std::remove_if(v.begin(), v.end(), [id](const auto& j) { return j->id == id; }),
                        v.end());
                Q_EMIT self->busyChanged();
            },
            Qt::QueuedConnection);
    }));
    Q_EMIT busyChanged();
    return id;
}

void JobRunner::cancel(int id) {
    for (auto& j : jobs_)
        if (j->id == id) j->ctx.cancel.cancel();
}

void JobRunner::cancelAll() {
    for (auto& j : jobs_)
        j->ctx.cancel.cancel();
}

bool JobRunner::busy() const {
    return !jobs_.empty();
}

void JobRunner::waitAll() {
    detect_.waitForDone();
    export_.waitForDone();
    io_.waitForDone();
}

// ---- model ------------------------------------------------------------------------------------------

int JobModel::indexOf(int id) const {
    for (size_t i = 0; i < rows_.size(); ++i)
        if (rows_[i].id == id) return static_cast<int>(i);
    return -1;
}

QVariant JobModel::data(const QModelIndex& index, int role) const {
    if (!index.isValid() || index.row() >= static_cast<int>(rows_.size())) return {};
    const Row& r = rows_[static_cast<size_t>(index.row())];
    switch (role) {
    case IdRole:
        return r.id;
    case TitleRole:
        return r.title;
    case ProgressRole:
        return r.progress;
    case StageRole:
        return r.stage;
    case StateRole:
        return r.state;
    case ElapsedRole:
        return static_cast<double>(r.finishedMs >= 0 ? r.finishedMs : r.timer.elapsed()) / 1000.0;
    default:
        return {};
    }
}

QHash<int, QByteArray> JobModel::roleNames() const {
    return {{IdRole, "jobId"},    {TitleRole, "title"}, {ProgressRole, "progress"},
            {StageRole, "stage"}, {StateRole, "state"}, {ElapsedRole, "elapsed"}};
}

void JobModel::add(int id, const QString& title) {
    beginInsertRows({}, 0, 0);
    Row r;
    r.id = id;
    r.title = title;
    r.state = QStringLiteral("running");
    r.timer.start();
    rows_.insert(rows_.begin(), r);
    endInsertRows();
    // keep the list short: drop old finished rows
    while (rows_.size() > 30) {
        const int last = static_cast<int>(rows_.size()) - 1;
        if (rows_.back().state == QLatin1String("running")) break;
        beginRemoveRows({}, last, last);
        rows_.pop_back();
        endRemoveRows();
    }
    Q_EMIT countChanged();
    Q_EMIT progressChanged();
}

void JobModel::update(int id, double progress, const QString& stage, const QString& state) {
    const int i = indexOf(id);
    if (i < 0) return;
    Row& r = rows_[static_cast<size_t>(i)];
    r.progress = progress;
    if (!stage.isEmpty()) r.stage = stage;
    r.state = state;
    if (state != QLatin1String("running") && r.finishedMs < 0) r.finishedMs = r.timer.elapsed();
    Q_EMIT dataChanged(index(i), index(i));
    Q_EMIT progressChanged();
}

double JobModel::overallProgress() const {
    double sum = 0;
    int n = 0;
    for (const auto& r : rows_)
        if (r.state == QLatin1String("running")) {
            sum += r.progress;
            ++n;
        }
    return n ? sum / n : 1.0;
}

QString JobModel::summary() const {
    int running = 0;
    QString title;
    for (const auto& r : rows_)
        if (r.state == QLatin1String("running")) {
            ++running;
            if (title.isEmpty()) title = r.title;
        }
    if (running == 0) return {};
    if (running == 1) return title;
    return tr("%1 (+%2 more)").arg(title).arg(running - 1);
}

void JobModel::clearFinished() {
    for (int i = static_cast<int>(rows_.size()) - 1; i >= 0; --i)
        if (rows_[static_cast<size_t>(i)].state != QLatin1String("running")) {
            beginRemoveRows({}, i, i);
            rows_.erase(rows_.begin() + i);
            endRemoveRows();
        }
    Q_EMIT countChanged();
}

} // namespace fkapp
