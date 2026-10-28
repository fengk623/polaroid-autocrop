#include "Models.hpp"

namespace fkapp {

QVariant ScanListModel::data(const QModelIndex& index, int role) const {
    if (!index.isValid() || index.row() >= static_cast<int>(rows_.size())) return {};
    const ScanRow& r = rows_[static_cast<size_t>(index.row())];
    switch (role) {
    case ScanIdRole:
        return r.scanId;
    case NameRole:
        return r.name;
    case PathRole:
        return r.path;
    case SizeRole:
        return r.sizeText;
    case DepthRole:
        return r.depthText;
    case IccRole:
        return r.iccText;
    case DpiRole:
        return r.dpiText;
    case StateRole:
        return r.state;
    case ErrorRole:
        return r.error;
    case ThumbRole:
        return r.thumb;
    case ItemCountRole:
        return r.itemCount;
    case NeedReviewRole:
        return r.needReview;
    case CurrentRole:
        return r.current;
    default:
        return {};
    }
}

QHash<int, QByteArray> ScanListModel::roleNames() const {
    return {{ScanIdRole, "scanId"},  {NameRole, "name"},           {PathRole, "path"},
            {SizeRole, "sizeText"},  {DepthRole, "depthText"},     {IccRole, "iccText"},
            {DpiRole, "dpiText"},    {StateRole, "state"},         {ErrorRole, "error"},
            {ThumbRole, "thumb"},    {ItemCountRole, "itemCount"}, {NeedReviewRole, "needReview"},
            {CurrentRole, "current"}};
}

void ScanListModel::setRows(std::vector<ScanRow> rows) {
    const bool sameShape = rows.size() == rows_.size() && [&] {
        for (size_t i = 0; i < rows.size(); ++i)
            if (rows[i].scanId != rows_[i].scanId) return false;
        return true;
    }();
    if (sameShape) {
        rows_ = std::move(rows);
        if (!rows_.empty()) Q_EMIT dataChanged(index(0), index(static_cast<int>(rows_.size()) - 1));
        return;
    }
    beginResetModel();
    rows_ = std::move(rows);
    endResetModel();
    Q_EMIT countChanged();
}

QString ScanListModel::scanIdAt(int row) const {
    if (row < 0 || row >= static_cast<int>(rows_.size())) return {};
    return rows_[static_cast<size_t>(row)].scanId;
}

int ScanListModel::indexOf(const QString& scanId) const {
    for (size_t i = 0; i < rows_.size(); ++i)
        if (rows_[i].scanId == scanId) return static_cast<int>(i);
    return -1;
}

QVariant ItemListModel::data(const QModelIndex& index, int role) const {
    if (!index.isValid() || index.row() >= static_cast<int>(rows_.size())) return {};
    const ItemRow& r = rows_[static_cast<size_t>(index.row())];
    switch (role) {
    case ItemIdRole:
        return r.itemId;
    case ScanIdRole:
        return r.scanId;
    case LabelRole:
        return r.label;
    case ScanNameRole:
        return r.scanName;
    case FilmTypeRole:
        return r.filmType;
    case ThumbRole:
        return r.thumb;
    case ConfidenceRole:
        return r.confidence;
    case DetectionRole:
        return r.detection;
    case ReviewedRole:
        return r.reviewed;
    case ExcludedRole:
        return r.excluded;
    case ExportedRole:
        return r.exported;
    case RelinkedRole:
        return r.relinked;
    case CompositedRole:
        return r.composited;
    case FinalExportedRole:
        return r.finalExported;
    case CurrentRole:
        return r.current;
    case SelectedRole:
        return r.selected;
    case ScanCellRole:
        return r.scanCell;
    case EditsRole:
        return r.edits;
    default:
        return {};
    }
}

QHash<int, QByteArray> ItemListModel::roleNames() const {
    return {{ItemIdRole, "itemId"},         {ScanIdRole, "scanId"},
            {LabelRole, "label"},           {ScanNameRole, "scanName"},
            {FilmTypeRole, "filmType"},     {ThumbRole, "thumb"},
            {ConfidenceRole, "confidence"}, {DetectionRole, "detection"},
            {ReviewedRole, "reviewed"},     {ExcludedRole, "excluded"},
            {ExportedRole, "exported"},     {RelinkedRole, "relinked"},
            {CompositedRole, "composited"}, {FinalExportedRole, "finalExported"},
            {CurrentRole, "current"},       {SelectedRole, "selected"},
            {ScanCellRole, "scanCell"},     {EditsRole, "edits"}};
}

void ItemListModel::setRows(std::vector<ItemRow> rows) {
    const bool sameShape = rows.size() == rows_.size() && [&] {
        for (size_t i = 0; i < rows.size(); ++i)
            if (rows[i].itemId != rows_[i].itemId || rows[i].scanCell != rows_[i].scanCell) return false;
        return true;
    }();
    if (sameShape) {
        rows_ = std::move(rows);
        if (!rows_.empty()) Q_EMIT dataChanged(index(0), index(static_cast<int>(rows_.size()) - 1));
        return;
    }
    beginResetModel();
    rows_ = std::move(rows);
    endResetModel();
    Q_EMIT countChanged();
}

int ItemListModel::indexOf(const QString& itemId) const {
    for (size_t i = 0; i < rows_.size(); ++i)
        if (rows_[i].itemId == itemId && !rows_[i].scanCell) return static_cast<int>(i);
    return -1;
}

} // namespace fkapp
