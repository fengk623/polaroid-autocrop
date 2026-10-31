// Scan canvas (RFC-0011, CONTRACT-ui-core §4): proxy pyramid + on-demand full-resolution view, overlays
// drawn from core shapes, boundary editing. 100 % = one scan pixel per physical device pixel.
#pragma once

#include "fk/geometry.hpp"

#include <QElapsedTimer>
#include <QImage>
#include <QPointF>
#include <QQuickPaintedItem>
#include <QTimer>
#include <QTransform>

#include <optional>

namespace fkapp {

class AppController;

class ScanCanvas : public QQuickPaintedItem {
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(QObject* app READ app WRITE setApp NOTIFY appChanged)
    Q_PROPERTY(QString mode READ mode WRITE setMode NOTIFY modeChanged) // scan, print, image, composite
    Q_PROPERTY(QString tool READ tool WRITE setTool NOTIFY toolChanged) // select, hand, outer, inner
    /// The boundary whose handles the tool edits: "outer", "inner" or "" (hand) — shown on the canvas
    /// (UX-03).
    Q_PROPERTY(QString editTarget READ editTarget NOTIFY toolChanged)
    /// A handle is selected: arrow keys nudge it instead of stepping through prints and scans.
    Q_PROPERTY(bool handleActive READ handleActive NOTIFY handleChanged)
    Q_PROPERTY(bool showOverlays READ showOverlays WRITE setShowOverlays NOTIFY showOverlaysChanged)
    Q_PROPERTY(bool compare READ compare WRITE setCompare NOTIFY compareChanged)
    Q_PROPERTY(bool showOriginal READ showOriginal WRITE setShowOriginal NOTIFY compareChanged)
    Q_PROPERTY(double comparePos READ comparePos WRITE setComparePos NOTIFY compareChanged)
    Q_PROPERTY(double zoomPercent READ zoomPercent NOTIFY viewChanged)
    Q_PROPERTY(bool atFit READ atFit NOTIFY viewChanged)
    Q_PROPERTY(QString badge READ badge NOTIFY badgeChanged)
    Q_PROPERTY(QString status READ status NOTIFY badgeChanged)
    Q_PROPERTY(bool hasImage READ hasImage NOTIFY badgeChanged) ///< a scan is shown
    Q_PROPERTY(bool spaceHeld READ spaceHeld WRITE setSpaceHeld NOTIFY toolChanged)
    Q_PROPERTY(QRectF navigatorView READ navigatorView NOTIFY viewChanged)
    Q_PROPERTY(QColor surroundColor READ surroundColor WRITE setSurroundColor NOTIFY appChanged)
    /// FK-011 (self-test): the display profile the full-resolution patch on screen was converted for.
    Q_PROPERTY(QString detailProfile READ detailProfile NOTIFY badgeChanged)

  public:
    explicit ScanCanvas(QQuickItem* parent = nullptr);

    QObject* app() const;
    void setApp(QObject* a);
    QString mode() const { return mode_; }
    void setMode(const QString& m);
    QString tool() const { return tool_; }
    void setTool(const QString& t);
    QString editTarget() const;
    bool handleActive() const { return handle_.has_value(); }
    bool showOverlays() const { return overlays_; }
    void setShowOverlays(bool v);
    bool compare() const { return compare_; }
    void setCompare(bool v);
    bool showOriginal() const { return showOriginal_; }
    void setShowOriginal(bool v);
    double comparePos() const { return comparePos_; }
    void setComparePos(double v);
    double zoomPercent() const;
    bool atFit() const { return atFit_; }
    QString badge() const { return badge_; }
    bool hasImage() const { return !pyramid_.empty(); }
    QString status() const { return status_; }
    bool spaceHeld() const { return space_; }
    void setSpaceHeld(bool v);
    QRectF navigatorView() const; ///< visible part of the content, relative 0..1
    QColor surroundColor() const { return surround_; }
    void setSurroundColor(const QColor& c);

    Q_INVOKABLE void fit();
    Q_INVOKABLE void zoomTo(double percent);
    Q_INVOKABLE void zoomStep(int dir);
    Q_INVOKABLE void toggleFit();
    Q_INVOKABLE void centerOnRelative(double rx, double ry);
    Q_INVOKABLE void nudge(int dx, int dy);
    /// UX-03: switches the edited boundary (Outer ↔ Inner; from Select or Hand to Inner). Refused while a
    /// drag is in progress.
    Q_INVOKABLE bool switchBoundary();
    /// Scan coordinates → this item's coordinates (self-test input at handle positions).
    Q_INVOKABLE QPointF mapFromScan(double x, double y) const { return toScreen({x, y}); }
    /// Renders the current view into an image (self-test snapshots).
    Q_INVOKABLE QImage grab();

    void paint(QPainter* p) override;

  Q_SIGNALS:
    void appChanged();
    void modeChanged();
    void toolChanged();
    void showOverlaysChanged();
    void compareChanged();
    void viewChanged();
    void badgeChanged();
    void handleChanged();
    void itemClicked(const QString& itemId);

  protected:
    void mousePressEvent(QMouseEvent* e) override;
    void mouseMoveEvent(QMouseEvent* e) override;
    void mouseReleaseEvent(QMouseEvent* e) override;
    void mouseDoubleClickEvent(QMouseEvent* e) override;
    void mouseUngrabEvent() override;
    void keyPressEvent(QKeyEvent* e) override;
    void keyReleaseEvent(QKeyEvent* e) override;
    void focusOutEvent(QFocusEvent* e) override;
    void wheelEvent(QWheelEvent* e) override;
    void hoverMoveEvent(QHoverEvent* e) override;
    bool event(QEvent* e) override;
    void geometryChange(const QRectF& n, const QRectF& o) override;

  private:
    struct Handle {
        fk::EditHandle kind;
        int index = 0;
        bool outer = true;
    };

    void onScanImage(const QString& scanId);
    void onGeometry(const QString& itemId);
    void rebuildPyramid();
    void reportBytes();         ///< FK-005: pyramid levels and the full-resolution patch → the cache budget
    QRectF contentRect() const; ///< scan coordinates of what "fit" shows
    void setView(double scale, QPointF offset, bool fromFit = false);
    void clampView();
    // UX-04: the view shows the scan turned by `rotation_` quarter turns. Boundaries stay in scan
    // coordinates; the display mapping D (scan → turned scan) is applied once, here, then scale and offset.
    QPointF toScreen(fk::Vec2 p) const {
        const fk::Vec2 d = disp_.apply(p);
        return {d.x * scale_ + offset_.x(), d.y * scale_ + offset_.y()};
    }
    fk::Vec2 toScan(QPointF p) const {
        return dispInv_.apply({(p.x() - offset_.x()) / scale_, (p.y() - offset_.y()) / scale_});
    }
    /// screen → display (turned scan) coordinates
    QPointF toDisplay(QPointF p) const { return (p - offset_) / scale_; }
    /// scan → screen as a painter transform (images are drawn in scan coordinates under it)
    QTransform viewTransform() const;
    /// screen rectangle covered by a scan rectangle (axis-aligned: the view turns by quarter turns only)
    QRectF screenRect(const QRectF& scanRect) const { return viewTransform().mapRect(scanRect); }
    /// scan rectangle seen through a screen rectangle, clamped to the scan
    QRectF scanRectOf(const QRectF& screen) const;
    double dispW() const { return static_cast<double>((rotation_ & 1) ? scanH_ : scanW_); }
    double dispH() const { return static_cast<double>((rotation_ & 1) ? scanW_ : scanH_); }
    void updateDisplayMapping();
    double dpr() const;
    void scheduleDetail();
    void requestDetail();
    void updateBadge();
    std::optional<Handle> hitHandle(QPointF pos) const;
    void applyDrag(QPointF pos);
    void setHandle(std::optional<Handle> h);
    void cancelDrag(); ///< an interrupted drag changes nothing
    const fk::Shape* currentShape(bool outer) const;
    void drawOverlays(QPainter* p);
    void drawShape(QPainter* p, const fk::Shape& s, const QPen& pen, bool ticks) const;
    void zoomAround(double newScale, QPointF anchor);

    AppController* app_ = nullptr;
    QString mode_ = QStringLiteral("scan"), tool_ = QStringLiteral("select");
    bool overlays_ = true, compare_ = false, showOriginal_ = false, space_ = false, atFit_ = true;
    double comparePos_ = 0.5;
    QColor surround_{0x5B, 0x5C, 0x5F};

    // content
    QString scanId_;
    std::vector<QImage> pyramid_; ///< [0] = proxy, then halves
    int proxyScale_ = 1;
    int64_t scanW_ = 0, scanH_ = 0;
    int rotation_ = 0;        ///< UX-04: whole-scan view, quarter turns clockwise
    fk::Mat3 disp_, dispInv_; ///< scan → display and back

    // view
    double scale_ = 0.1;
    QPointF offset_;
    quint64 generation_ = 0;
    QString detailProfile() const { return detail_.isNull() ? QString() : detailProfile_; }
    QImage detail_;
    QString detailProfile_;
    QRectF detailRect_;
    quint64 detailGen_ = 0;
    bool detailPending_ = false;
    QTimer detailTimer_;
    QString badge_ = QStringLiteral("Preview"), status_;

    // interaction
    std::optional<Handle> handle_;
    bool dragging_ = false, panning_ = false, compareDrag_ = false;
    QPointF pressPos_, lastPos_;
    fk::Shape dragShape_{}, dragStart_{};
    std::string dragItem_;
    bool dragOuter_ = true;
    bool constrain_ = false; ///< Shift held during the drag (UX-03)
};

} // namespace fkapp
