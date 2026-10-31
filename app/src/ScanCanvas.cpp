#include "ScanCanvas.hpp"

#include "AppController.hpp"

#include <QGuiApplication>
#include <QKeyEvent>
#include <QNativeGestureEvent>
#include <QPainter>
#include <QPainterPath>
#include <QQuickWindow>
#include <QtMath>

#include <algorithm>
#include <cmath>

namespace fkapp {

namespace {

const QColor kOuter(0x3D, 0x8B, 0xFF), kInner(0xF2, 0xC7, 0x44), kManual(0xFF, 0x8A, 0x3D),
    kLow(0xFF, 0x4D, 0x4D), kRelinked(0x38, 0xC1, 0x72);
constexpr double kZoomSteps[] = {2, 3, 5, 8, 12.5, 16.7, 25, 33.3, 50, 66.7, 100, 150, 200, 300, 400, 800};
constexpr double kHandleHit = 14.0; // half of the 28 px target
constexpr double kOverscroll = 48.0;
constexpr double kFitPadding = 16.0;

QPolygonF quadPoly(const fk::Quad& q, const std::function<QPointF(fk::Vec2)>& f) {
    QPolygonF poly;
    for (const auto& p : q.p)
        poly << f(p);
    return poly;
}

} // namespace

ScanCanvas::ScanCanvas(QQuickItem* parent) : QQuickPaintedItem(parent) {
    setAcceptedMouseButtons(Qt::LeftButton | Qt::MiddleButton | Qt::RightButton);
    setAcceptHoverEvents(true);
    setAntialiasing(true);
    setOpaquePainting(true);
    setFlag(ItemAcceptsInputMethod, false);
    setActiveFocusOnTab(true);
    detailTimer_.setSingleShot(true);
    detailTimer_.setInterval(150); // RFC-0010 idle debounce
    connect(&detailTimer_, &QTimer::timeout, this, &ScanCanvas::requestDetail);
}

QObject* ScanCanvas::app() const {
    return app_;
}

void ScanCanvas::setApp(QObject* a) {
    auto* c = qobject_cast<AppController*>(a);
    if (c == app_) return;
    if (app_) disconnect(app_, nullptr, this, nullptr);
    app_ = c;
    if (app_) {
        connect(app_, &AppController::scanImageChanged, this, &ScanCanvas::onScanImage);
        connect(app_, &AppController::geometryChanged, this, &ScanCanvas::onGeometry);
        connect(app_, &AppController::currentChanged, this, [this] { update(); });
        connect(app_, &AppController::selectionChanged, this, [this] {
            cancelDrag();
            setHandle(std::nullopt);
            if (mode_ != QLatin1String("scan")) fit();
            // composite mode: another print, or a preview the memory budget gave back (FK-005) — compute it
            if (mode_ == QLatin1String("composite") && !app_->currentItemId().isEmpty() &&
                app_->compositePreview(app_->currentItemId().toStdString()).isNull())
                app_->requestCompositePreview(app_->currentItemId());
            update();
        });
        connect(app_, &AppController::compositePreviewReady, this, [this] {
            updateBadge();
            update();
        });
        // FK-011: another display profile — the full-resolution patch and the composite preview were
        // converted for the old one: dropped and asked for again (the proxy comes back through
        // scanImageChanged)
        connect(app_, &AppController::displayProfileChanged, this, [this] {
            detail_ = QImage();
            reportBytes();
            ++generation_;
            scheduleDetail();
            if (mode_ == QLatin1String("composite") && !app_->currentItemId().isEmpty())
                app_->requestCompositePreview(app_->currentItemId());
            updateBadge();
            update();
        });
        onScanImage(app_->currentScanId());
    }
    Q_EMIT appChanged();
}

void ScanCanvas::setMode(const QString& m) {
    if (m == mode_) return;
    mode_ = m;
    if (mode_ == QLatin1String("composite") && app_ && !app_->currentItemId().isEmpty() &&
        app_->compositePreview(app_->currentItemId().toStdString()).isNull())
        app_->requestCompositePreview(app_->currentItemId());
    fit();
    Q_EMIT modeChanged();
}

void ScanCanvas::setTool(const QString& t) {
    if (t == tool_) return;
    cancelDrag();
    tool_ = t;
    setHandle(std::nullopt);
    Q_EMIT toolChanged();
    update();
}

QString ScanCanvas::editTarget() const {
    if (tool_ == QLatin1String("inner")) return QStringLiteral("inner");
    if (tool_ == QLatin1String("outer") || tool_ == QLatin1String("select")) return QStringLiteral("outer");
    return {};
}

bool ScanCanvas::switchBoundary() {
    if (dragging_) return false;
    setTool(tool_ == QLatin1String("inner") ? QStringLiteral("outer") : QStringLiteral("inner"));
    return true;
}

void ScanCanvas::setHandle(std::optional<Handle> h) {
    const bool had = handle_.has_value();
    handle_ = h;
    if (had != handle_.has_value()) Q_EMIT handleChanged();
}

void ScanCanvas::cancelDrag() {
    if (!dragging_) return;
    dragging_ = false;
    update();
}

void ScanCanvas::setShowOverlays(bool v) {
    if (v == overlays_) return;
    overlays_ = v;
    Q_EMIT showOverlaysChanged();
    update();
}

void ScanCanvas::setCompare(bool v) {
    if (v == compare_) return;
    compare_ = v;
    Q_EMIT compareChanged();
    update();
}

void ScanCanvas::setShowOriginal(bool v) {
    if (v == showOriginal_) return;
    showOriginal_ = v;
    Q_EMIT compareChanged();
    updateBadge();
    update();
}

void ScanCanvas::setComparePos(double v) {
    v = std::clamp(v, 0.0, 1.0);
    if (v == comparePos_) return;
    comparePos_ = v;
    Q_EMIT compareChanged();
    update();
}

void ScanCanvas::setSpaceHeld(bool v) {
    if (v == space_) return;
    space_ = v;
    Q_EMIT toolChanged();
}

void ScanCanvas::setSurroundColor(const QColor& c) {
    surround_ = c;
    update();
}

double ScanCanvas::dpr() const {
    return window() ? window()->effectiveDevicePixelRatio() : 1.0;
}

double ScanCanvas::zoomPercent() const {
    return scale_ * dpr() * 100.0;
}

// ---- content -----------------------------------------------------------------------------------------

void ScanCanvas::onScanImage(const QString& scanId) {
    if (!app_ || scanId != app_->currentScanId()) return;
    const bool newScan = scanId != scanId_;
    const bool had = hasImage();
    const int rotationBefore = rotation_;
    scanId_ = scanId;
    rebuildPyramid();
    if (had != hasImage()) Q_EMIT badgeChanged();
    if (newScan || rotation_ != rotationBefore) {
        // another scan, or the same scan turned (UX-04): the content changed shape
        detail_ = QImage();
        reportBytes();
        cancelDrag();
        if (newScan) setHandle(std::nullopt);
        fit();
    } else if (atFit_) {
        fit(); // e.g. the proxy arrived after the scan was selected: fit the real size now
    } else {
        clampView();
    }
    updateBadge();
    update();
}

void ScanCanvas::onGeometry(const QString&) {
    if (mode_ == QLatin1String("composite") && app_ && !app_->currentItemId().isEmpty())
        app_->requestCompositePreview(app_->currentItemId());
    update();
}

void ScanCanvas::rebuildPyramid() {
    pyramid_.clear();
    if (!app_) return;
    const auto cs = app_->canvasScan();
    proxyScale_ = std::max(1, cs.proxyScale);
    scanW_ = cs.width;
    scanH_ = cs.height;
    rotation_ = cs.rotation;
    updateDisplayMapping();
    status_ = cs.state == QLatin1String("missing")         ? tr("Scan not found — use Relocate…")
              : cs.state == QLatin1String("error")         ? tr("Cannot read scan: %1").arg(cs.error)
              : cs.state == QLatin1String("changed")       ? cs.error
              : cs.display.isNull() && !cs.state.isEmpty() ? tr("Loading…")
                                                           : QString();
    if (cs.display.isNull()) {
        reportBytes();
        Q_EMIT badgeChanged();
        return;
    }
    pyramid_.push_back(cs.display);
    while (pyramid_.back().width() > 384 && pyramid_.back().height() > 384) {
        const QImage& b = pyramid_.back();
        pyramid_.push_back(
            b.scaled(b.width() / 2, b.height() / 2, Qt::IgnoreAspectRatio, Qt::SmoothTransformation));
    }
    if (scanW_ <= 0) {
        scanW_ = static_cast<int64_t>(cs.display.width()) * proxyScale_;
        scanH_ = static_cast<int64_t>(cs.display.height()) * proxyScale_;
        updateDisplayMapping();
    }
    reportBytes();
    Q_EMIT badgeChanged();
}

void ScanCanvas::reportBytes() {
    // FK-005: the canvas's own images count in the cache budget (level 0 is the controller's proxy, shared)
    if (!app_) return;
    uint64_t b = detail_.isNull() ? 0 : static_cast<uint64_t>(detail_.sizeInBytes());
    for (size_t i = 1; i < pyramid_.size(); ++i)
        b += static_cast<uint64_t>(pyramid_[i].sizeInBytes());
    app_->setCanvasBytes(b);
}

const fk::Shape* ScanCanvas::currentShape(bool outer) const {
    if (!app_) return nullptr;
    const fk::ItemRecord* it = app_->item(app_->currentItemId().toStdString());
    if (!it) return nullptr;
    return outer ? &it->outer : &it->inner;
}

void ScanCanvas::updateDisplayMapping() {
    disp_ = fk::quarterTurnMatrix(rotation_, static_cast<double>(scanW_), static_cast<double>(scanH_));
    dispInv_ = fk::quarterTurnMatrix(4 - rotation_, dispW(), dispH());
}

QTransform ScanCanvas::viewTransform() const {
    const auto& m = disp_.m;
    return QTransform(m[0] * scale_, m[3] * scale_, m[1] * scale_, m[4] * scale_, m[2] * scale_ + offset_.x(),
                      m[5] * scale_ + offset_.y());
}

QRectF ScanCanvas::scanRectOf(const QRectF& screen) const {
    const fk::Vec2 a = toScan(screen.topLeft()), b = toScan(screen.bottomRight());
    return QRectF(QPointF(std::min(a.x, b.x), std::min(a.y, b.y)),
                  QPointF(std::max(a.x, b.x), std::max(a.y, b.y)))
        .intersected(QRectF(0, 0, static_cast<double>(scanW_), static_cast<double>(scanH_)));
}

QRectF ScanCanvas::contentRect() const {
    // display coordinates (the turned scan)
    const QRectF all(0, 0, dispW(), dispH());
    if (mode_ == QLatin1String("scan") || !app_) return all;
    const fk::Shape* s = currentShape(mode_ != QLatin1String("image"));
    if (!s) return all;
    const fk::RectD b = fk::boundsOf(*s);
    const fk::Vec2 a = disp_.apply({b.x0, b.y0}), c = disp_.apply({b.x1, b.y1});
    return QRectF(QPointF(std::min(a.x, c.x), std::min(a.y, c.y)),
                  QPointF(std::max(a.x, c.x), std::max(a.y, c.y)))
        .intersected(all);
}

// ---- view -------------------------------------------------------------------------------------------------

void ScanCanvas::fit() {
    const QRectF r = contentRect();
    if (r.isEmpty() || width() <= 0 || height() <= 0) {
        atFit_ = true;
        Q_EMIT viewChanged();
        return;
    }
    const double aw = std::max(1.0, width() - 2 * kFitPadding),
                 ah = std::max(1.0, height() - 2 * kFitPadding);
    const double s = std::min(aw / r.width(), ah / r.height());
    const QPointF off(width() / 2 - (r.center().x()) * s, height() / 2 - (r.center().y()) * s);
    setView(s, off, true);
}

void ScanCanvas::setView(double scale, QPointF offset, bool fromFit) {
    scale_ = scale;
    offset_ = offset;
    atFit_ = fromFit;
    clampView();
    ++generation_;
    Q_EMIT viewChanged();
    updateBadge();
    scheduleDetail();
    update();
}

void ScanCanvas::clampView() {
    const QRectF r = contentRect();
    if (r.isEmpty()) return;
    const double fitS =
        std::min((width() - 2 * kFitPadding) / r.width(), (height() - 2 * kFitPadding) / r.height());
    const double minS = std::min(fitS, 0.02 / dpr()), maxS = 8.0 / dpr();
    scale_ = std::clamp(scale_, minS, maxS);
    // no panning at or below fit; above fit at most 48 px overscroll past the content edges
    auto axis = [&](double lo, double hi, double view, double& off) {
        const double len = (hi - lo) * scale_;
        if (len <= view - 2 * kFitPadding + 0.5) {
            off = view / 2 - (lo + hi) / 2 * scale_;
        } else {
            off = std::clamp(off, view - kOverscroll - hi * scale_, kOverscroll - lo * scale_);
        }
    };
    double ox = offset_.x(), oy = offset_.y();
    axis(r.left(), r.right(), width(), ox);
    axis(r.top(), r.bottom(), height(), oy);
    // 100 %: snap the origin to device pixels
    if (std::abs(scale_ * dpr() - 1.0) < 1e-9) {
        ox = std::round(ox * dpr()) / dpr();
        oy = std::round(oy * dpr()) / dpr();
    }
    offset_ = {ox, oy};
}

void ScanCanvas::zoomAround(double newScale, QPointF anchor) {
    const QPointF p = toDisplay(anchor);
    setView(newScale, QPointF(anchor.x() - p.x() * newScale, anchor.y() - p.y() * newScale));
}

void ScanCanvas::zoomTo(double percent) {
    zoomAround(percent / 100.0 / dpr(), QPointF(width() / 2, height() / 2));
}

void ScanCanvas::zoomStep(int dir) {
    const double cur = zoomPercent();
    double target = cur;
    if (dir > 0) {
        for (double s : kZoomSteps)
            if (s > cur * 1.001) {
                target = s;
                break;
            }
    } else {
        for (auto it = std::rbegin(kZoomSteps); it != std::rend(kZoomSteps); ++it)
            if (*it < cur * 0.999) {
                target = *it;
                break;
            }
    }
    zoomTo(target);
}

void ScanCanvas::toggleFit() {
    if (atFit_)
        zoomTo(100);
    else
        fit();
}

void ScanCanvas::centerOnRelative(double rx, double ry) {
    const QRectF r = contentRect();
    const QPointF target(r.left() + rx * r.width(), r.top() + ry * r.height());
    setView(scale_, QPointF(width() / 2 - target.x() * scale_, height() / 2 - target.y() * scale_));
}

QRectF ScanCanvas::navigatorView() const {
    const QRectF r = contentRect();
    if (r.isEmpty()) return {0, 0, 1, 1};
    const QPointF a = toDisplay({0, 0}), b = toDisplay({width(), height()});
    QRectF v(QPointF((a.x() - r.left()) / r.width(), (a.y() - r.top()) / r.height()),
             QPointF((b.x() - r.left()) / r.width(), (b.y() - r.top()) / r.height()));
    return v.intersected(QRectF(0, 0, 1, 1));
}

void ScanCanvas::geometryChange(const QRectF& n, const QRectF& o) {
    QQuickPaintedItem::geometryChange(n, o);
    if (n.size() == o.size()) return;
    if (atFit_)
        fit();
    else {
        clampView();
        scheduleDetail();
    }
}

// ---- full resolution
// ---------------------------------------------------------------------------------------

void ScanCanvas::scheduleDetail() {
    detailTimer_.start();
}

void ScanCanvas::requestDetail() {
    if (!app_ || pyramid_.empty()) return;
    // device pixels per scan pixel decide (UX-09 HiDPI run: logical scale alone showed the proxy at 100 % on
    // a 2× display)
    if (scale_ * dpr() * proxyScale_ <= 1.05) {
        // the proxy is enough; release the full-resolution image
        if (!detail_.isNull()) {
            detail_ = QImage();
            reportBytes();
        }
        updateBadge();
        return;
    }
    const QRectF vis = scanRectOf(QRectF(0, 0, width(), height()));
    if (vis.isEmpty()) return;
    const QRectF r(std::floor(vis.left()), std::floor(vis.top()),
                   std::ceil(vis.right()) - std::floor(vis.left()),
                   std::ceil(vis.bottom()) - std::floor(vis.top()));
    const int ow = std::max(1, static_cast<int>(std::ceil(r.width() * scale_ * dpr())));
    const int oh = std::max(1, static_cast<int>(std::ceil(r.height() * scale_ * dpr())));
    const quint64 gen = generation_;
    const QString scan = scanId_;
    detailPending_ = true;
    updateBadge();
    app_->requestDetail(r, ow, oh, gen, [this, scan](QImage img, QRectF rect, quint64 g) {
        if (scan != scanId_) return; // superseded: another scan is shown
        detail_ = std::move(img);
        detailProfile_ = currentDisplayProfile().id; // delivered only when made for the current profile
        reportBytes();
        detailRect_ = rect;
        detailGen_ = g;
        if (g == generation_) detailPending_ = false;
        updateBadge();
        update();
    });
}

void ScanCanvas::updateBadge() {
    QString b;
    if (mode_ == QLatin1String("composite") && app_ && !showOriginal_ &&
        !app_->compositePreview(app_->currentItemId().toStdString()).isNull())
        b = QStringLiteral("Preview");
    else if (scale_ * dpr() * proxyScale_ <= 1.05 || pyramid_.empty())
        b = QStringLiteral("Preview");
    else if (!detail_.isNull() && detailGen_ == generation_)
        b = QStringLiteral("Full");
    else
        b = QStringLiteral("Full…");
    if (b != badge_) {
        badge_ = b;
        Q_EMIT badgeChanged();
    }
}

// ---- painting ---------------------------------------------------------------------------------------------

void ScanCanvas::paint(QPainter* p) {
    p->fillRect(QRectF(0, 0, width(), height()), surround_);
    if (pyramid_.empty()) return;
    const bool pixelExact = scale_ * dpr() >= 0.999;
    // proxy level: the smallest image that still has ≥ 1 source pixel per ~1.5 screen pixels
    const double k = scale_ * dpr() * proxyScale_; // device px per proxy px
    int L = 0;
    while (L + 1 < static_cast<int>(pyramid_.size()) && k * std::pow(2.0, L + 1) <= 1.5)
        ++L;
    const QImage& img = pyramid_[static_cast<size_t>(L)];
    const double lvl = proxyScale_ * std::pow(2.0, L); // scan px per level px
    // images are drawn in scan coordinates under the view transform (UX-04: turned by quarter turns)
    const QRectF vis = scanRectOf(QRectF(0, 0, width(), height()));
    QRectF src(std::floor(vis.left() / lvl) - 1, std::floor(vis.top() / lvl) - 1, 0, 0);
    src.setRight(std::ceil(vis.right() / lvl) + 1);
    src.setBottom(std::ceil(vis.bottom() / lvl) + 1);
    src = src.intersected(QRectF(0, 0, img.width(), img.height()));
    p->setRenderHint(QPainter::SmoothPixmapTransform, !pixelExact);
    p->save();
    p->setTransform(viewTransform(), true);
    if (!src.isEmpty())
        p->drawImage(QRectF(src.left() * lvl, src.top() * lvl, src.width() * lvl, src.height() * lvl), img,
                     src);
    if (!detail_.isNull()) p->drawImage(detailRect_, detail_);
    p->restore();
    // Composite mode: graded image inside the real frame (split: Original left, Composite right — on screen)
    if (mode_ == QLatin1String("composite") && app_ && !showOriginal_) {
        const QImage comp = app_->compositePreview(app_->currentItemId().toStdString());
        const fk::Shape* outer = currentShape(true);
        if (!comp.isNull() && outer) {
            const fk::RectI bi = fk::boundingRect(*outer);
            const QRectF box(static_cast<double>(bi.x), static_cast<double>(bi.y), static_cast<double>(bi.w),
                             static_cast<double>(bi.h));
            const QRectF dst = screenRect(box);
            p->save();
            if (compare_) {
                const double x = dst.left() + comparePos_ * dst.width();
                p->setClipRect(QRectF(QPointF(x, dst.top()), dst.bottomRight()));
            }
            p->setRenderHint(QPainter::SmoothPixmapTransform, true);
            p->setTransform(viewTransform(), true);
            p->drawImage(box, comp);
            p->restore();
            if (compare_) {
                const double x = dst.left() + comparePos_ * dst.width();
                p->setPen(QPen(QColor(0, 0, 0, 153), 3));
                p->drawLine(QPointF(x, dst.top()), QPointF(x, dst.bottom()));
                p->setPen(QPen(QColor(0xF0, 0xF1, 0xF3), 1));
                p->drawLine(QPointF(x, dst.top()), QPointF(x, dst.bottom()));
                const QPointF c(x, dst.center().y());
                p->setBrush(QColor(0x2A, 0x2B, 0x2E, 178));
                p->setPen(QPen(QColor(0x38, 0x39, 0x3D), 1));
                p->drawEllipse(c, 14, 14);
                p->setPen(QPen(QColor(0xF0, 0xF1, 0xF3), 1.5));
                p->drawLine(c + QPointF(-3, -5), c + QPointF(-7, 0));
                p->drawLine(c + QPointF(-7, 0), c + QPointF(-3, 5));
                p->drawLine(c + QPointF(3, -5), c + QPointF(7, 0));
                p->drawLine(c + QPointF(7, 0), c + QPointF(3, 5));
            }
        }
    }
    // Full Print / Image Area modes: dim everything outside the boundary being looked at
    if ((mode_ == QLatin1String("print") || mode_ == QLatin1String("image")) && app_) {
        const fk::Shape* s = currentShape(mode_ == QLatin1String("print"));
        if (s) {
            QPainterPath all;
            all.addRect(QRectF(0, 0, width(), height()));
            QPainterPath hole;
            if (s->kind == fk::ShapeKind::quad) {
                hole.addPolygon(quadPoly(s->quad, [this](fk::Vec2 v) { return toScreen(v); }));
            } else {
                QPainterPath e;
                e.addEllipse(QPointF(0, 0), s->ellipse.rx, s->ellipse.ry);
                QTransform t;
                t.translate(s->ellipse.c.x, s->ellipse.c.y);
                t.rotate(qRadiansToDegrees(s->ellipse.angleRad));
                hole = viewTransform().map(t.map(e));
            }
            hole.closeSubpath();
            p->fillPath(all.subtracted(hole), QColor(0, 0, 0, 128));
        }
    }
    if (overlays_) drawOverlays(p);
}

void ScanCanvas::drawShape(QPainter* p, const fk::Shape& s, const QPen& pen, bool ticks) const {
    QPen halo(QColor(0, 0, 0, 153), pen.widthF() + 2.0);
    halo.setCapStyle(Qt::FlatCap);
    halo.setJoinStyle(Qt::MiterJoin);
    auto draw = [&](const QPen& pn) {
        p->setPen(pn);
        p->setBrush(Qt::NoBrush);
        if (s.kind == fk::ShapeKind::quad) {
            p->drawPolygon(quadPoly(s.quad, [this](fk::Vec2 v) { return toScreen(v); }));
            if (ticks) {
                // inward corner ticks: Inner differs from Outer by shape, not only by colour (CONTRACT §4)
                const fk::Vec2 c = fk::centroid(s);
                for (const auto& q : s.quad.p) {
                    const QPointF a = toScreen(q);
                    const QPointF d = toScreen(c) - a;
                    const double len = std::hypot(d.x(), d.y());
                    if (len > 1) p->drawLine(a, a + d / len * 8.0);
                }
            }
        } else {
            const auto& e = s.ellipse;
            p->save();
            p->translate(toScreen(e.c));
            p->rotate(qRadiansToDegrees(e.angleRad) + 90.0 * rotation_);
            p->drawEllipse(QPointF(0, 0), e.rx * scale_, e.ry * scale_);
            if (ticks) {
                for (int k = 0; k < 4; ++k) {
                    const double t = k * M_PI / 2;
                    const QPointF on(std::cos(t) * e.rx * scale_, std::sin(t) * e.ry * scale_);
                    const double len = std::hypot(on.x(), on.y());
                    if (len > 1) p->drawLine(on, on - on / len * 8.0);
                }
            }
            p->restore();
        }
    };
    draw(halo);
    draw(pen);
}

void ScanCanvas::drawOverlays(QPainter* p) {
    if (!app_) return;
    const auto cs = app_->canvasScan();
    const std::string cur = app_->currentItemId().toStdString();
    p->setRenderHint(QPainter::Antialiasing, true);
    QFont f = p->font();
    f.setPixelSize(11);
    f.setWeight(QFont::DemiBold);
    p->setFont(f);
    for (const fk::ItemRecord* it : cs.items) {
        const bool isCur = it->itemId == cur;
        const bool low = !it->reviewed && it->confidence < fk::kLowConfidence;
        const bool relinked = std::any_of(it->edits.begin(), it->edits.end(),
                                          [](const fk::EditVersion& e) { return !e.relinkedPath.empty(); });
        fk::Shape outer = it->outer, inner = it->inner;
        if (dragging_ && it->itemId == dragItem_) (dragOuter_ ? outer : inner) = dragShape_;
        // outer
        QPen po(low                                             ? kLow
                : it->outerSource == fk::BoundarySource::manual ? kManual
                                                                : kOuter,
                isCur ? 3.0 : 2.0);
        po.setColor(QColor(po.color().red(), po.color().green(), po.color().blue(), 230));
        if (low)
            po.setDashPattern({8, 3, 2, 3});
        else if (it->outerSource == fk::BoundarySource::manual)
            po.setDashPattern({6, 4});
        if (it->excluded) po.setColor(QColor(160, 160, 160, 160));
        drawShape(p, outer, po, false);
        // inner
        QPen pi(low                                             ? kLow
                : it->innerSource == fk::BoundarySource::manual ? kManual
                : relinked                                      ? kRelinked
                                                                : kInner,
                isCur ? 2.5 : 1.5);
        pi.setColor(QColor(pi.color().red(), pi.color().green(), pi.color().blue(), 230));
        if (low)
            pi.setDashPattern({8, 3, 2, 3});
        else if (it->innerSource == fk::BoundarySource::manual)
            pi.setDashPattern({6, 4});
        if (it->excluded) pi.setColor(QColor(160, 160, 160, 160));
        drawShape(p, inner, pi, true);
        // label capsule + state marks
        // the label sits at the corner that is top-left on screen (the view may be turned, UX-04)
        QPointF tl = toScreen(outer.quad.p[0]);
        for (const auto& c : outer.quad.p)
            if (const QPointF q = toScreen(c); q.x() + q.y() < tl.x() + tl.y()) tl = q;
        const QString label = QString::fromStdString(it->label);
        const QRectF lab(tl + QPointF(6, 6), QSizeF(30, 18));
        p->setPen(Qt::NoPen);
        p->setBrush(QColor(0x2A, 0x2B, 0x2E, 217));
        p->drawRoundedRect(lab, 9, 9);
        p->setPen(QColor(0xF0, 0xF1, 0xF3));
        p->drawText(lab, Qt::AlignCenter, label);
        if (low) {
            QPolygonF tri;
            const QPointF o = tl + QPointF(-4, -20);
            tri << o + QPointF(7, 0) << o + QPointF(14, 13) << o + QPointF(0, 13);
            p->setPen(QPen(QColor(0, 0, 0, 153), 1));
            p->setBrush(kLow);
            p->drawPolygon(tri);
            p->setPen(QPen(Qt::white, 1.5));
            p->drawLine(o + QPointF(7, 4), o + QPointF(7, 8.5));
            p->drawPoint(o + QPointF(7, 11));
        }
        if (relinked) {
            const fk::RectD ib = fk::boundsOf(inner);
            const QRectF sr = screenRect(QRectF(QPointF(ib.x0, ib.y0), QPointF(ib.x1, ib.y1)));
            const QPointF o = sr.topRight() + QPointF(4, -18);
            p->setBrush(Qt::NoBrush);
            p->setPen(QPen(kRelinked, 2));
            p->drawEllipse(QRectF(o, QSizeF(8, 8)));
            p->drawEllipse(QRectF(o + QPointF(6, 6), QSizeF(8, 8)));
        }
        // handles of the current item
        if (isCur && !it->excluded) {
            const bool outerTool = tool_ == QLatin1String("select") || tool_ == QLatin1String("outer");
            const bool innerTool = tool_ == QLatin1String("inner");
            auto handleSq = [&](QPointF c, double s, bool sel) {
                p->setPen(QPen(Qt::black, 1));
                p->setBrush(sel ? QColor(0x35, 0xB5, 0xA8) : QColor(Qt::white));
                p->drawRect(QRectF(c.x() - s / 2, c.y() - s / 2, s, s));
            };
            auto isSel = [&](fk::EditHandle k, int idx, bool o) {
                return handle_ && handle_->kind == k && handle_->index == idx && handle_->outer == o;
            };
            if (outerTool || innerTool) {
                const fk::Shape& s = outerTool ? outer : inner;
                const bool o = outerTool;
                if (s.kind == fk::ShapeKind::quad) {
                    for (int i = 0; i < 4; ++i) {
                        handleSq(toScreen(s.quad.p[i]), 9, isSel(fk::EditHandle::corner, i, o));
                        handleSq(toScreen((s.quad.p[i] + s.quad.p[(i + 1) % 4]) * 0.5), 7,
                                 isSel(fk::EditHandle::edge, i, o));
                    }
                } else {
                    const auto& e = s.ellipse;
                    handleSq(toScreen(e.c), 9, isSel(fk::EditHandle::center, 0, o));
                    // on the rotated axes (UX-03): the handles sit on the outline whatever its angle
                    handleSq(toScreen(fk::ellipseHandle(e, true)), 7, isSel(fk::EditHandle::radiusX, 0, o));
                    handleSq(toScreen(fk::ellipseHandle(e, false)), 7, isSel(fk::EditHandle::radiusY, 0, o));
                }
            }
        }
    }
}

QImage ScanCanvas::grab() {
    const double d = dpr();
    QImage img(static_cast<int>(width() * d), static_cast<int>(height() * d), QImage::Format_RGB32);
    img.setDevicePixelRatio(d);
    QPainter p(&img);
    paint(&p);
    return img;
}

// ---- interaction ------------------------------------------------------------------------------------------

std::optional<ScanCanvas::Handle> ScanCanvas::hitHandle(QPointF pos) const {
    const bool outerTool = tool_ == QLatin1String("select") || tool_ == QLatin1String("outer");
    const bool innerTool = tool_ == QLatin1String("inner");
    if (!outerTool && !innerTool) return std::nullopt;
    const fk::Shape* s = currentShape(outerTool);
    if (!s) return std::nullopt;
    auto near = [&](fk::Vec2 v) {
        const QPointF q = toScreen(v);
        return std::abs(q.x() - pos.x()) <= kHandleHit && std::abs(q.y() - pos.y()) <= kHandleHit;
    };
    if (s->kind == fk::ShapeKind::quad) {
        for (int i = 0; i < 4; ++i)
            if (near(s->quad.p[i])) return Handle{fk::EditHandle::corner, i, outerTool};
        for (int i = 0; i < 4; ++i)
            if (near((s->quad.p[i] + s->quad.p[(i + 1) % 4]) * 0.5))
                return Handle{fk::EditHandle::edge, i, outerTool};
    } else {
        const auto& e = s->ellipse;
        if (near(fk::ellipseHandle(e, true))) return Handle{fk::EditHandle::radiusX, 0, outerTool};
        if (near(fk::ellipseHandle(e, false))) return Handle{fk::EditHandle::radiusY, 0, outerTool};
        if (near(e.c)) return Handle{fk::EditHandle::center, 0, outerTool};
    }
    // inside the shape with an edit tool: move the whole boundary
    if (tool_ != QLatin1String("select") && fk::contains(*s, toScan(pos)))
        return Handle{fk::EditHandle::move, 0, outerTool};
    return std::nullopt;
}

void ScanCanvas::applyDrag(QPointF pos) {
    if (!handle_) return;
    // one geometry for the canvas and the stored boundary: fk::editShape (RFC-0002 §Editing). Shift held:
    // corners keep the rectangle (aspect, right angles, rotation), moves lock to an axis, radii scale
    // together.
    const fk::Vec2 d = toScan(pos) - toScan(pressPos_);
    if (auto s = fk::editShape(dragStart_, {handle_->kind, handle_->index, d, constrain_}))
        dragShape_ = *s; // otherwise keep the last valid shape
    update();
}

void ScanCanvas::nudge(int dx, int dy) {
    if (!handle_ || !app_ || dragging_) return;
    const fk::Shape* start = currentShape(handle_->outer);
    if (!start) return;
    // arrows move the selected handle freely (Shift = 10 px steps, never the constraint), in screen
    // directions: with a turned view (UX-04) → is still "right" on screen
    const fk::Vec2 d =
        dispInv_.apply({static_cast<double>(dx), static_cast<double>(dy)}) - dispInv_.apply({0, 0});
    const auto s = fk::editShape(*start, {handle_->kind, handle_->index, d, false});
    if (s) app_->commitShape(app_->currentItemId().toStdString(), handle_->outer, *s);
}

void ScanCanvas::mousePressEvent(QMouseEvent* e) {
    forceActiveFocus(Qt::MouseFocusReason);
    pressPos_ = lastPos_ = e->position();
    if (e->button() == Qt::MiddleButton || tool_ == QLatin1String("hand") || space_) {
        panning_ = true;
        setCursor(Qt::ClosedHandCursor);
        return;
    }
    if (e->button() != Qt::LeftButton || !app_) return;
    if (compare_ && mode_ == QLatin1String("composite")) {
        if (const fk::Shape* o = currentShape(true)) {
            const fk::RectI b = fk::boundingRect(*o);
            const QRectF sr = screenRect(QRectF(static_cast<double>(b.x), static_cast<double>(b.y),
                                                static_cast<double>(b.w), static_cast<double>(b.h)));
            const double x = sr.left() + comparePos_ * sr.width();
            if (std::abs(e->position().x() - x) < 12) {
                compareDrag_ = true;
                return;
            }
        }
    }
    if (auto h = hitHandle(e->position())) {
        setHandle(h);
        dragging_ = true;
        dragItem_ = app_->currentItemId().toStdString();
        dragOuter_ = h->outer;
        dragStart_ = dragShape_ = *currentShape(h->outer);
        constrain_ = e->modifiers() & Qt::ShiftModifier;
        update();
        return;
    }
    // click on a print selects it
    const fk::Vec2 q = toScan(e->position());
    const auto cs = app_->canvasScan();
    for (auto it = cs.items.rbegin(); it != cs.items.rend(); ++it)
        if (fk::contains((*it)->outer, q)) {
            const bool toggle = e->modifiers() & (Qt::ControlModifier | Qt::MetaModifier);
            app_->selectItem(QString::fromStdString((*it)->itemId), e->modifiers() & Qt::ShiftModifier,
                             toggle);
            Q_EMIT itemClicked(QString::fromStdString((*it)->itemId));
            setHandle(std::nullopt);
            update();
            return;
        }
    setHandle(std::nullopt);
    panning_ = !atFit_;
    if (panning_) setCursor(Qt::ClosedHandCursor);
    update();
}

void ScanCanvas::mouseMoveEvent(QMouseEvent* e) {
    const QPointF pos = e->position();
    if (dragging_) {
        constrain_ = e->modifiers() & Qt::ShiftModifier;
        applyDrag(pos);
    } else if (panning_) {
        setView(scale_, offset_ + (pos - lastPos_));
    } else if (compareDrag_) {
        if (const fk::Shape* o = currentShape(true)) {
            const fk::RectI b = fk::boundingRect(*o);
            const QRectF sr = screenRect(QRectF(static_cast<double>(b.x), static_cast<double>(b.y),
                                                static_cast<double>(b.w), static_cast<double>(b.h)));
            setComparePos((pos.x() - sr.left()) / std::max(1.0, sr.width()));
        }
    }
    lastPos_ = pos;
}

void ScanCanvas::mouseReleaseEvent(QMouseEvent* e) {
    if (dragging_ && app_) {
        constrain_ = e->modifiers() & Qt::ShiftModifier;
        applyDrag(e->position());
        dragging_ = false;
        // one complete drag = one undo step (a drag back to the start adds none)
        app_->commitShape(dragItem_, dragOuter_, dragShape_);
    }
    dragging_ = panning_ = compareDrag_ = false;
    unsetCursor();
    update();
}

void ScanCanvas::mouseUngrabEvent() {
    // the grab was taken away (window lost focus, a popup opened): nothing was completed
    cancelDrag();
    panning_ = compareDrag_ = false;
    unsetCursor();
}

void ScanCanvas::focusOutEvent(QFocusEvent* e) {
    cancelDrag();
    QQuickPaintedItem::focusOutEvent(e);
}

void ScanCanvas::keyPressEvent(QKeyEvent* e) {
    // pressing or releasing Shift mid-drag switches the constraint at once, without moving the mouse
    if (dragging_ && e->key() == Qt::Key_Shift) {
        constrain_ = true;
        applyDrag(lastPos_);
        e->accept();
        return;
    }
    if (dragging_ && e->key() == Qt::Key_Escape) {
        cancelDrag();
        e->accept();
        return;
    }
    QQuickPaintedItem::keyPressEvent(e);
}

void ScanCanvas::keyReleaseEvent(QKeyEvent* e) {
    if (dragging_ && e->key() == Qt::Key_Shift) {
        constrain_ = false;
        applyDrag(lastPos_);
        e->accept();
        return;
    }
    QQuickPaintedItem::keyReleaseEvent(e);
}

void ScanCanvas::mouseDoubleClickEvent(QMouseEvent* e) {
    if (hitHandle(e->position())) return;
    if (atFit_)
        zoomAround(1.0 / dpr(), e->position());
    else
        fit();
}

void ScanCanvas::wheelEvent(QWheelEvent* e) {
    const QPoint pd = e->pixelDelta();
    const QPoint ad = e->angleDelta();
    const bool zoomMod = e->modifiers() & (Qt::ControlModifier | Qt::MetaModifier);
#if defined(Q_OS_MACOS)
    const bool zoom = zoomMod; // two-finger scroll pans; ⌘-scroll zooms
#else
    const bool zoom = zoomMod || (pd.isNull() && !(e->modifiers() & Qt::ShiftModifier)); // wheel zooms (F15)
#endif
    if (zoom) {
        const double steps = (ad.y() != 0 ? ad.y() : pd.y()) / 120.0;
        zoomAround(scale_ * std::pow(1.25, steps), e->position());
    } else if (!atFit_) {
        QPointF delta = !pd.isNull() ? QPointF(pd) : QPointF(ad) / 120.0 * 48.0;
        if (e->modifiers() & Qt::ShiftModifier && pd.isNull()) delta = QPointF(delta.y(), 0);
        setView(scale_, offset_ + delta);
    }
    e->accept();
}

void ScanCanvas::hoverMoveEvent(QHoverEvent* e) {
    if (tool_ == QLatin1String("hand") || space_) {
        setCursor(Qt::OpenHandCursor);
        return;
    }
    if (auto h = hitHandle(e->position())) {
        switch (h->kind) {
        case fk::EditHandle::move:
        case fk::EditHandle::center:
            setCursor(Qt::SizeAllCursor);
            break;
        case fk::EditHandle::edge:
        case fk::EditHandle::radiusX:
        case fk::EditHandle::radiusY:
            setCursor(Qt::SizeHorCursor);
            break;
        default:
            setCursor(Qt::CrossCursor);
        }
    } else {
        unsetCursor();
    }
}

bool ScanCanvas::event(QEvent* e) {
    if (e->type() == QEvent::NativeGesture) {
        auto* g = static_cast<QNativeGestureEvent*>(e);
        if (g->gestureType() == Qt::ZoomNativeGesture) {
            zoomAround(scale_ * (1.0 + g->value()), g->position());
            return true;
        }
        if (g->gestureType() == Qt::SmartZoomNativeGesture) {
            toggleFit();
            return true;
        }
    }
    return QQuickPaintedItem::event(e);
}

} // namespace fkapp
