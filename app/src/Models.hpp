// Read-only list models published to QML (CONTRACT-ui-core §2–§3). Rows are rebuilt by AppController;
// statuses are derived from the project, never stored separately.
#pragma once

#include <QAbstractListModel>
#include <QStringList>

#include <vector>

namespace fkapp {

struct ScanRow {
    QString scanId, name, path, sizeText, depthText, iccText, dpiText, state, error, thumb;
    int itemCount = 0;
    int needReview = 0;
    bool current = false;
};

struct ItemRow {
    QString itemId, scanId, label, scanName, filmType, thumb, confidence; // confidence: high/medium/low
    QString detection;                                                    // auto/template/manual
    bool reviewed = false, excluded = false, exported = false, relinked = false, composited = false,
         finalExported = false, current = false, selected = false, scanCell = false;
    int edits = 0;
};

class ScanListModel : public QAbstractListModel {
    Q_OBJECT
    Q_PROPERTY(int count READ count NOTIFY countChanged)
  public:
    enum Roles {
        ScanIdRole = Qt::UserRole + 1,
        NameRole,
        PathRole,
        SizeRole,
        DepthRole,
        IccRole,
        DpiRole,
        StateRole,
        ErrorRole,
        ThumbRole,
        ItemCountRole,
        NeedReviewRole,
        CurrentRole
    };
    using QAbstractListModel::QAbstractListModel;
    int rowCount(const QModelIndex& = {}) const override { return static_cast<int>(rows_.size()); }
    QVariant data(const QModelIndex& index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;
    int count() const { return static_cast<int>(rows_.size()); }
    void setRows(std::vector<ScanRow> rows);
    Q_INVOKABLE QString scanIdAt(int row) const;
    Q_INVOKABLE int indexOf(const QString& scanId) const;

  Q_SIGNALS:
    void countChanged();

  private:
    std::vector<ScanRow> rows_;
};

class ItemListModel : public QAbstractListModel {
    Q_OBJECT
    Q_PROPERTY(int count READ count NOTIFY countChanged)
  public:
    enum Roles {
        ItemIdRole = Qt::UserRole + 1,
        ScanIdRole,
        LabelRole,
        ScanNameRole,
        FilmTypeRole,
        ThumbRole,
        ConfidenceRole,
        DetectionRole,
        ReviewedRole,
        ExcludedRole,
        ExportedRole,
        RelinkedRole,
        CompositedRole,
        FinalExportedRole,
        CurrentRole,
        SelectedRole,
        ScanCellRole,
        EditsRole
    };
    using QAbstractListModel::QAbstractListModel;
    int rowCount(const QModelIndex& = {}) const override { return static_cast<int>(rows_.size()); }
    QVariant data(const QModelIndex& index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;
    int count() const { return static_cast<int>(rows_.size()); }
    void setRows(std::vector<ItemRow> rows);
    Q_INVOKABLE int indexOf(const QString& itemId) const;

  Q_SIGNALS:
    void countChanged();

  private:
    std::vector<ItemRow> rows_;
};

} // namespace fkapp
