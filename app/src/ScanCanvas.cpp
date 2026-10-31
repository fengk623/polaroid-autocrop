#include "ScanCanvas.hpp"

#include "AppController.hpp"

#include <QGuiApplication>
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
            handle_.reset();
            if (mode_ != QLatin1String("scan")) fit();
            update();
        });
        connect(app_, &AppController::compositePreviewReady, this, [this] {
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
    tool_ = t;
    handle_.reset();
    Q_EMIT toolChanged();
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
    scanId_ = scanId;
    rebuildPyramid();
    if (newScan) {
        detail_ = QImage();
        handle_.reset();
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
    status_ = cs.state == QLatin1String("missing")         ? tr("Scan not found — use Relocate…")
              : cs.state == QLatin1String("error")         ? tr("Cannot read scan: %1").arg(cs.error)
              : cs.state == QLatin1String("changed")       ? cs.error
              : cs.display.isNull() && !cs.state.isEmpty() ? tr("Loading…")
                                                           : QString();
    if (cs.display.isNull()) {
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
    }
    Q_EMIT badgeChanged();
}

const fk::Shape* ScanCanvas::currentShape(bool outer) const {
    if (!app_) return nullptr;
    const fk::ItemRecord* it = app_->item(app_->currentItemId().toStdString());
    if (!it) return nullptr;
    return outer ? &it->outer : &it->inner;
}

QRectF ScanCanvas::contentRect() const {
    const QRectF scan(0, 0, static_cast<double>(scanW_), static_cast<double>(scanH_));
    if (mode_ == QLatin1String("scan") || !app_) return scan;
    const fk::Shape* s = currentShape(mode_ != QLatin1String("image"));
    if (!s) return scan;
    const fk::RectD b = fk::boundsOf(*s);
    return QRectF(b.x0, b.y0, b.x1 - b.x0, b.y1 - b.y0).intersected(scan);
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
    const fk::Vec2 p = toScan(anchor);
    setView(newScale, QPointF(anchor.x() - p.x * newScale, anchor.y() - p.y * newScale));
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
    const fk::Vec2 a = toScan({0, 0}), b = toScan({width(), height()});
    QRectF v(QPointF((a.x - r.left()) / r.width(), (a.y - r.top()) / r.height()),
             QPointF((b.x - r.left()) / r.width(), (b.y - r.top()) / r.height()));
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
    if (scale_ * proxyScale_ <= 1.05) {
        // the proxy is enough; release the full-resolution image
        detail_ = QImage();
        updateBadge();
        return;
    }
    const fk::Vec2 a = toScan({0, 0}), b = toScan({width(), height()});
    const QRectF vis =
        QRectF(QPointF(a.x, a.y), QPointF(b.x, b.y))
            .intersected(QRectF(0, 0, static_cast<double>(scanW_), static_cast<double>(scanH_)));
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
    else if (scale_ * proxyScale_ <= 1.05 || pyramid_.empty())
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
    const double k = scale_ * proxyScale_;
    int L = 0;
    while (L + 1 < static_cast<int>(pyramid_.size()) && k * std::pow(2.0, L + 1) <= 1.5)
        ++L;
    const QImage& img = pyramid_[static_cast<size_t>(L)];
    const double lvl = proxyScale_ * std::pow(2.0, L); // scan px per level px
    const fk::Vec2 a = toScan({0, 0}), b = toScan({width(), height()});
    QRectF src(std::floor(std::max(0.0, a.x / lvl)) - 1, std::floor(std::max(0.0, a.y / lvl)) - 1, 0, 0);
    src.setRight(std::min<double>(img.width(), std::ceil(b.x / lvl) + 1));
    src.setBottom(std::min<double>(img.height(), std::ceil(b.y / lvl) + 1));
    src = src.intersected(QRectF(0, 0, img.width(), img.height()));
    p->setRenderHint(QPainter::SmoothPixmapTransform, !pixelExact);
    if (!src.isEmpty()) {
        const QRectF dst(toScreen({src.left() * lvl, src.top() * lvl}),
                         toScreen({src.right() * lvl, src.bottom() * lvl}));
        p->drawImage(dst, img, src);
    }
    if (!detail_.isNull()) {
        const QRectF dst(toScreen({detailRect_.left(), detailRect_.top()}),
                         toScreen({detailRect_.right(), detailRect_.bottom()}));
        p->drawImage(dst, detail_);
    }
    // Composite mode: graded image inside the real frame (split: Original left, Composite right)
    if (mode_ == QLatin1String("composite") && app_ && !showOriginal_) {
        const QImage comp = app_->compositePreview(app_->currentItemId().toStdString());
        const fk::Shape* outer = currentShape(true);
        if (!comp.isNull() && outer) {
            const fk::RectI bi = fk::boundingRect(*outer);
            const QRectF dst(toScreen({static_cast<double>(bi.x), static_cast<double>(bi.y)}),
                             toScreen({static_cast<double>(bi.right()), static_cast<double>(bi.bottom())}));
            p->save();
            if (compare_) {
                const double x = dst.left() + comparePos_ * dst.width();
                p->setClipRect(QRectF(QPointF(x, dst.top()), dst.bottomRight()));
            }
            p->setRenderHint(QPainter::SmoothPixmapTransform, true);
            p->drawImage(dst, comp);
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
            if (s->kind == fk::ShapeKind::quad)
                hole.addPolygon(quadPoly(s->quad, [this](fk::Vec2 v) { return toScreen(v); }));
            else
                hole.addEllipse(toScreen(s->ellipse.c), s->ellipse.rx * scale_, s->ellipse.ry * scale_);
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
            p->rotate(qRadiansToDegrees(e.angleRad));
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
        if (dragging_ && it->itemId == dragItem_) (handle_ && handle_->outer ? outer : inner) = dragShape_;
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
        const QPointF tl = toScreen(outer.quad.p[0]);
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
            const QPointF o = toScreen({ib.x1, ib.y0}) + QPointF(4, -18);
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
            auto isSel = [&](HandleKind k, int idx, bool o) {
                return handle_ && handle_->kind == k && handle_->index == idx && handle_->outer == o;
            };
            if (outerTool || innerTool) {
                const fk::Shape& s = outerTool ? outer : inner;
                const bool o = outerTool;
                if (s.kind == fk::ShapeKind::quad) {
                    for (int i = 0; i < 4; ++i) {
                        handleSq(toScreen(s.quad.p[i]), 9, isSel(HandleKind::corner, i, o));
                        handleSq(toScreen((s.quad.p[i] + s.quad.p[(i + 1) % 4]) * 0.5), 7,
                                 isSel(HandleKind::edge, i, o));
                    }
                } else {
                    const auto& e = s.ellipse;
                    handleSq(toScreen(e.c), 9, isSel(HandleKind::center, 0, o));
                    handleSq(toScreen(e.c + fk::Vec2{e.rx, 0}), 7, isSel(HandleKind::radiusX, 0, o));
                    handleSq(toScreen(e.c + fk::Vec2{0, e.ry}), 7, isSel(HandleKind::radiusY, 0, o));
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
            if (near(s->quad.p[i])) return Handle{HandleKind::corner, i, outerTool};
        for (int i = 0; i < 4; ++i)
            if (near((s->quad.p[i] + s->quad.p[(i + 1) % 4]) * 0.5))
                return Handle{HandleKind::edge, i, outerTool};
    } else {
        const auto& e = s->ellipse;
        if (near(e.c + fk::Vec2{e.rx, 0})) return Handle{HandleKind::radiusX, 0, outerTool};
        if (near(e.c + fk::Vec2{0, e.ry})) return Handle{HandleKind::radiusY, 0, outerTool};
        if (near(e.c)) return Handle{HandleKind::center, 0, outerTool};
    }
    // inside the shape with an edit tool: move the whole boundary
    if (tool_ != QLatin1String("select") && fk::contains(*s, toScan(pos)))
        return Handle{HandleKind::move, 0, outerTool};
    return std::nullopt;
}

void ScanCanvas::applyDrag(QPointF pos) {
    const fk::Shape* start = currentShape(handle_->outer);
    if (!start) return;
    const fk::Vec2 d = toScan(pos) - toScan(pressPos_);
    fk::Shape s = *start;
    switch (handle_->kind) {
    case HandleKind::corner:
        s.quad.p[handle_->index] = start->quad.p[handle_->index] + d;
        break;
    case HandleKind::edge: {
        const int i = handle_->index, j = (i + 1) % 4;
        const fk::Vec2 n = fk::perp(fk::normalized(start->quad.p[j] - start->quad.p[i]));
        const double m = fk::dot(d, n);
        s.quad.p[i] = start->quad.p[i] + n * m;
        s.quad.p[j] = start->quad.p[j] + n * m;
        break;
    }
    case HandleKind::move:
        if (s.kind == fk::ShapeKind::quad)
            for (auto& p : s.quad.p)
                p = p + d;
        else
            s.ellipse.c = s.ellipse.c + d;
        break;
    case HandleKind::center:
        s.ellipse.c = start->ellipse.c + d;
        break;
    case HandleKind::radiusX:
    case HandleKind::radiusY: {
        const double r = fk::norm(toScan(pos) - start->ellipse.c);
        const bool circle = std::abs(start->ellipse.rx - start->ellipse.ry) < 1e-6;
        if (handle_->kind == HandleKind::radiusX || circle) s.ellipse.rx = std::max(2.0, r);
        if (handle_->kind == HandleKind::radiusY || circle) s.ellipse.ry = std::max(2.0, r);
        break;
    }
    }
    if (s.kind == fk::ShapeKind::quad && !fk::isConvex(s.quad)) return; // keep the last valid shape
    dragShape_ = s;
    update();
}

void ScanCanvas::nudge(int dx, int dy) {
    if (!handle_ || !app_) return;
    const fk::Shape* start = currentShape(handle_->outer);
    if (!start) return;
    fk::Shape s = *start;
    const fk::Vec2 d{static_cast<double>(dx), static_cast<double>(dy)};
    switch (handle_->kind) {
    case HandleKind::corner:
        s.quad.p[handle_->index] = s.quad.p[handle_->index] + d;
        break;
    case HandleKind::edge: {
        const int i = handle_->index, j = (i + 1) % 4;
        const fk::Vec2 n = fk::perp(fk::normalized(s.quad.p[j] - s.quad.p[i]));
        const double m = fk::dot(d, n);
        s.quad.p[i] = s.quad.p[i] + n * m;
        s.quad.p[j] = s.quad.p[j] + n * m;
        break;
    }
    case HandleKind::move:
    case HandleKind::center:
        if (s.kind == fk::ShapeKind::quad)
            for (auto& p : s.quad.p)
                p = p + d;
        else
            s.ellipse.c = s.ellipse.c + d;
        break;
    case HandleKind::radiusX:
        s.ellipse.rx = std::max(2.0, s.ellipse.rx + dx);
        break;
    case HandleKind::radiusY:
        s.ellipse.ry = std::max(2.0, s.ellipse.ry + dy);
        break;
    }
    if (s.kind == fk::ShapeKind::quad && !fk::isConvex(s.quad)) return;
    app_->commitShape(app_->currentItemId().toStdString(), handle_->outer, s);
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
            const double x = toScreen({b.x + comparePos_ * b.w, 0.0}).x();
            if (std::abs(e->position().x() - x) < 12) {
                compareDrag_ = true;
                return;
            }
        }
    }
    if (auto h = hitHandle(e->position())) {
        handle_ = h;
        dragging_ = true;
        dragItem_ = app_->currentItemId().toStdString();
        dragShape_ = *currentShape(h->outer);
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
            handle_.reset();
            update();
            return;
        }
    handle_.reset();
    panning_ = !atFit_;
    if (panning_) setCursor(Qt::ClosedHandCursor);
    update();
}

void ScanCanvas::mouseMoveEvent(QMouseEvent* e) {
    const QPointF pos = e->position();
    if (dragging_) {
        applyDrag(pos);
    } else if (panning_) {
        setView(scale_, offset_ + (pos - lastPos_));
    } else if (compareDrag_) {
        if (const fk::Shape* o = currentShape(true)) {
            const fk::RectI b = fk::boundingRect(*o);
            const double x0 = toScreen({static_cast<double>(b.x), 0.0}).x();
            const double x1 = toScreen({static_cast<double>(b.right()), 0.0}).x();
            setComparePos((pos.x() - x0) / std::max(1.0, x1 - x0));
        }
    }
    lastPos_ = pos;
}

void ScanCanvas::mouseReleaseEvent(QMouseEvent*) {
    if (dragging_ && app_) {
        dragging_ = false;
        app_->commitShape(dragItem_, handle_->outer, dragShape_);
    }
    dragging_ = panning_ = compareDrag_ = false;
    unsetCursor();
    update();
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
        case HandleKind::move:
        case HandleKind::center:
            setCursor(Qt::SizeAllCursor);
            break;
        case HandleKind::edge:
        case HandleKind::radiusX:
        case HandleKind::radiusY:
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
