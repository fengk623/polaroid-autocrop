// Background jobs (RFC-0010): lanes with bounded concurrency, progress, cancellation, results delivered on
// the UI thread. Core calls never run on the UI thread (CONTRACT-ui-core §1).
#pragma once

#include "fk/common.hpp"

#include <QAbstractListModel>
#include <QElapsedTimer>
#include <QObject>
#include <QThreadPool>

#include <functional>
#include <memory>
#include <vector>

namespace fkapp {

enum class Lane { detect, exportJob, io };

struct JobContext {
    fk::CancelToken cancel;
    std::function<void(double, const QString&)> progress; ///< thread-safe, throttled
};

class JobModel;

/// One background job. `work` runs on a worker thread; `done` runs on the UI thread afterwards.
class JobRunner : public QObject {
    Q_OBJECT
  public:
    explicit JobRunner(QObject* parent = nullptr);
    ~JobRunner() override;
    JobModel* model() const { return model_; }
    /// Returns the job id.
    int start(Lane lane, const QString& title, std::function<void(JobContext&)> work,
              std::function<void(bool cancelled)> done = {});
    Q_INVOKABLE void cancel(int id);
    void cancelAll();
    bool busy() const;
    /// Blocks until every job has finished (shutdown, self-test).
    void waitAll();

  Q_SIGNALS:
    void busyChanged();

  private:
    struct Job;
    QThreadPool detect_, export_, io_;
    JobModel* model_;
    std::vector<std::shared_ptr<Job>> jobs_;
    int nextId_ = 1;
    friend class JobModel;
};

class JobModel : public QAbstractListModel {
    Q_OBJECT
    Q_PROPERTY(int count READ count NOTIFY countChanged)
    Q_PROPERTY(double overallProgress READ overallProgress NOTIFY progressChanged)
    Q_PROPERTY(QString summary READ summary NOTIFY progressChanged)
  public:
    enum Roles { IdRole = Qt::UserRole + 1, TitleRole, ProgressRole, StageRole, StateRole, ElapsedRole };
    struct Row {
        int id = 0;
        QString title, stage, state; ///< state: queued / running / done / cancelled
        double progress = 0;
        QElapsedTimer timer;
        qint64 finishedMs = -1;
    };
    explicit JobModel(QObject* parent = nullptr) : QAbstractListModel(parent) {}
    int rowCount(const QModelIndex& = {}) const override { return static_cast<int>(rows_.size()); }
    QVariant data(const QModelIndex& index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;
    int count() const { return static_cast<int>(rows_.size()); }
    double overallProgress() const;
    QString summary() const;
    Q_INVOKABLE void clearFinished();

    void add(int id, const QString& title);
    void update(int id, double progress, const QString& stage, const QString& state);

  Q_SIGNALS:
    void countChanged();
    void progressChanged();

  private:
    std::vector<Row> rows_;
    int indexOf(int id) const;
};

} // namespace fkapp
