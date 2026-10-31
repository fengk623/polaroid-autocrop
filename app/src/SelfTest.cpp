#include "SelfTest.hpp"

#include "AppController.hpp"
#include "Display.hpp"
#include "Jobs.hpp"
#include "util.hpp"

#include "fk/hash.hpp"
#include "fk/image_io.hpp"
#include "fk/util.hpp"

#include <QCoreApplication>
#include <QDir>
#include <QDirIterator>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QGuiApplication>
#include <QJsonArray>
#include <QJsonDocument>
#include <QQuickItem>
#include <QQuickWindow>
#include <QRectF>
#include <QThread>
#include <QTransform>
#include <QUrl>
#include <QWheelEvent>

#include <algorithm>
#include <cstring>
#include <functional>
#include <set>

#if defined(__GLIBC__)
#include <malloc.h>
#endif

// Exported by Qt GUI for input injection (declared, like this, by Qt Test's qtestmouse.h).
Q_GUI_EXPORT void qt_handleMouseEvent(QWindow* window, const QPointF& local, const QPointF& global,
                                      Qt::MouseButtons state, Qt::MouseButton button, QEvent::Type type,
                                      Qt::KeyboardModifiers mods, int timestamp);

Q_GUI_EXPORT void qt_handleKeyEvent(QWindow* window, QEvent::Type t, int k, Qt::KeyboardModifiers mods,
                                    const QString& text = QString(), bool autorep = false, ushort count = 1);

namespace fkapp {

SelfTest::SelfTest(AppController* app, QQuickWindow* window, QString outDir, QString scan, QObject* parent)
    : QObject(parent), app_(app), window_(window), dir_(std::move(outDir)), scan_(std::move(scan)) {}

void SelfTest::check(const QString& name, bool ok, const QString& detail) {
    checks_.append(QJsonObject{{"name", name}, {"ok", ok}, {"detail", detail}});
    allOk_ = allOk_ && ok;
    qInfo().noquote() << (ok ? "PASS" : "FAIL") << name << detail;
}

void SelfTest::skip(const QString& name, const QString& why) {
    checks_.append(QJsonObject{{"name", name}, {"ok", true}, {"skipped", true}, {"detail", why}});
    qInfo().noquote() << "SKIP" << name << why;
}

void SelfTest::snapshot(const QString& name) {
    QCoreApplication::processEvents();
    const QImage img = window_->grabWindow();
    const QString path = QDir(dir_).filePath(QStringLiteral("snapshot-%1.png").arg(name));
    const bool ok = !img.isNull() && img.save(path);
    check(QStringLiteral("snapshot %1").arg(name), ok,
          QStringLiteral("%1×%2").arg(img.width()).arg(img.height()));
}

QQuickItem* SelfTest::findItem(const QString& objectName) const {
    // visual tree search (QML items are not always QObject children of their visual parent)
    std::function<QQuickItem*(QQuickItem*)> find = [&](QQuickItem* it) -> QQuickItem* {
        if (!it) return nullptr;
        if (it->objectName() == objectName) return it;
        for (QQuickItem* c : it->childItems())
            if (QQuickItem* f = find(c)) return f;
        return nullptr;
    };
    return find(window_->contentItem());
}

bool SelfTest::click(QQuickItem* item, QPointF local) {
    if (!item || !item->isVisible()) return false;
    if (local.x() < 0) local = QPointF(item->width() / 2, item->height() / 2);
    const QPointF at = item->mapToScene(local);
    if (!QRectF(0, 0, window_->width(), window_->height()).contains(at)) return false;
    const QPointF global = item->mapToGlobal(local);
    // the same injection Qt Test uses: through the window system interface, so the click is delivered,
    // grabbed and released exactly like a user's (a QMouseEvent sent to the window is not grabbed)
    inputTs_ += 1000; // apart enough never to be a double click
    qt_handleMouseEvent(window_, at, global, Qt::LeftButton, Qt::LeftButton, QEvent::MouseButtonPress,
                        Qt::NoModifier, inputTs_);
    qt_handleMouseEvent(window_, at, global, Qt::NoButton, Qt::LeftButton, QEvent::MouseButtonRelease,
                        Qt::NoModifier, inputTs_ + 50);
    QCoreApplication::processEvents();
    return true;
}

void SelfTest::mouse(QQuickItem* item, QPointF local, QEvent::Type type, Qt::KeyboardModifiers mods) {
    const QPointF at = item->mapToScene(local);
    const QPointF global = item->mapToGlobal(local);
    inputTs_ += type == QEvent::MouseButtonPress ? 1000 : 40;
    const Qt::MouseButtons state = type == QEvent::MouseButtonRelease ? Qt::NoButton : Qt::LeftButton;
    const Qt::MouseButton button = type == QEvent::MouseMove ? Qt::NoButton : Qt::LeftButton;
    qt_handleMouseEvent(window_, at, global, state, button, type, mods, inputTs_);
    QCoreApplication::processEvents();
}

void SelfTest::key(int k, Qt::KeyboardModifiers mods, bool press, const QString& text) {
    qt_handleKeyEvent(window_, press ? QEvent::KeyPress : QEvent::KeyRelease, k, mods, text);
    QCoreApplication::processEvents();
}

void SelfTest::doubleClick(QQuickItem* item, QPointF local) {
    if (local.x() < 0) local = QPointF(item->width() / 2, item->height() / 2);
    const QPointF at = item->mapToScene(local), global = item->mapToGlobal(local);
    inputTs_ += 1000;
    // two clicks 60 ms apart at the same place: Qt makes the second press a double click
    qt_handleMouseEvent(window_, at, global, Qt::LeftButton, Qt::LeftButton, QEvent::MouseButtonPress,
                        Qt::NoModifier, inputTs_);
    qt_handleMouseEvent(window_, at, global, Qt::NoButton, Qt::LeftButton, QEvent::MouseButtonRelease,
                        Qt::NoModifier, inputTs_ + 20);
    qt_handleMouseEvent(window_, at, global, Qt::LeftButton, Qt::LeftButton, QEvent::MouseButtonPress,
                        Qt::NoModifier, inputTs_ + 60);
    qt_handleMouseEvent(window_, at, global, Qt::NoButton, Qt::LeftButton, QEvent::MouseButtonRelease,
                        Qt::NoModifier, inputTs_ + 80);
    inputTs_ += 80;
    QCoreApplication::processEvents();
}

QQuickItem* SelfTest::findItemIf(const std::function<bool(QQuickItem*)>& pred) const {
    std::function<QQuickItem*(QQuickItem*)> find = [&](QQuickItem* it) -> QQuickItem* {
        if (!it) return nullptr;
        if (pred(it)) return it;
        for (QQuickItem* c : it->childItems())
            if (QQuickItem* f = find(c)) return f;
        return nullptr;
    };
    return find(window_->contentItem());
}

QObject* SelfTest::findCanvas() const {
    // visual tree search (QML items are not always QObject children of their visual parent)
    std::function<QQuickItem*(QQuickItem*)> find = [&](QQuickItem* it) -> QQuickItem* {
        if (!it) return nullptr;
        if (it->objectName() == QLatin1String("scanCanvas")) return it;
        for (QQuickItem* c : it->childItems())
            if (QQuickItem* f = find(c)) return f;
        return nullptr;
    };
    return find(window_->contentItem());
}

namespace {
bool waitFor(const std::function<bool()>& cond, int ms) {
    QElapsedTimer t;
    t.start();
    while (!cond()) {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
        if (t.elapsed() > ms) return false;
    }
    return true;
}
bool samePixels(const QString& a, const QString& b, QString& why) {
    auto ia = fk::readImageFile(toPath(a)), ib = fk::readImageFile(toPath(b));
    if (!ia || !ib) {
        why = QStringLiteral("cannot read");
        return false;
    }
    if (ia->size() != ib->size() || ia->channels() != ib->channels() || ia->bits() != ib->bits()) {
        why = QStringLiteral("size/layout differ");
        return false;
    }
    for (int64_t y = 0; y < ia->height(); ++y)
        if (std::memcmp(ia->row(y), ib->row(y), ia->rowBytes()) != 0) {
            why = QStringLiteral("row %1 differs").arg(y);
            return false;
        }
    return true;
}
/// Writes `src` with per-channel gains and offsets (a colour grade: v × gain + offset, offset in 0…1),
/// keeping its profile and its XMP (UX-08 edge tests).
bool rewriteGraded(const QString& src, const QString& dst, std::array<double, 3> gain,
                   std::array<double, 3> offset = {0, 0, 0}) {
    fk::ImageInfo info;
    auto img = fk::readImageFile(toPath(src), &info);
    if (!img) return false;
    const auto xmp = fk::readXmpPacket(toPath(src));
    const int ch = img->channels();
    for (int64_t y = 0; y < img->height(); ++y)
        for (int64_t x = 0; x < img->width(); ++x)
            for (int c = 0; c < ch; ++c) {
                const double g = gain[static_cast<size_t>(std::min(c, 2))];
                const double o = offset[static_cast<size_t>(std::min(c, 2))];
                if (img->bits() == 16) {
                    uint16_t v;
                    std::memcpy(&v, img->row(y) + (x * ch + c) * 2, 2);
                    v = static_cast<uint16_t>(std::clamp(std::lround(v * g + o * 65535), 0L, 65535L));
                    std::memcpy(img->row(y) + (x * ch + c) * 2, &v, 2);
                } else {
                    uint8_t& v = img->row(y)[x * ch + c];
                    v = static_cast<uint8_t>(std::clamp(std::lround(v * g + o * 255), 0L, 255L));
                }
            }
    fk::WriteOptions opt;
    opt.bits = img->bits();
    opt.icc = info.icc;
    opt.dpiX = info.dpiX;
    opt.dpiY = info.dpiY;
    if (xmp) opt.xmp = *xmp;
    const QString tmp = dst + QStringLiteral(".tmp.tif");
    QFile::remove(tmp);
    auto sink = fk::createSink(toPath(tmp), opt);
    if (!sink || !(*sink)->begin({img->width(), img->height()}, ch, img->bits())) return false;
    for (int64_t y = 0; y < img->height(); ++y)
        if (!(*sink)->writeRows(img->row(y), 1, img->rowBytes())) return false;
    if (!(*sink)->finish()) return false;
    QFile::remove(dst);
    return QFile::rename(tmp, dst);
}
/// `b` is `a` turned by `k` quarter turns clockwise, pixel for pixel (UX-04: lossless export orientation).
bool sameTurned(const QString& a, const QString& b, int k, QString& why) {
    auto ia = fk::readImageFile(toPath(a)), ib = fk::readImageFile(toPath(b));
    if (!ia || !ib) {
        why = QStringLiteral("cannot read");
        return false;
    }
    const int64_t w = ia->width(), h = ia->height();
    const bool odd = k & 1;
    if ((odd ? h : w) != ib->width() || (odd ? w : h) != ib->height() || ia->channels() != ib->channels() ||
        ia->bits() != ib->bits()) {
        why = QStringLiteral("size/layout differ: %1×%2 vs %3×%4")
                  .arg(w)
                  .arg(h)
                  .arg(ib->width())
                  .arg(ib->height());
        return false;
    }
    for (int64_t y = 0; y < h; ++y)
        for (int64_t x = 0; x < w; ++x) {
            int64_t bx = x, by = y;
            switch (k & 3) {
            case 1:
                bx = h - 1 - y, by = x;
                break;
            case 2:
                bx = w - 1 - x, by = h - 1 - y;
                break;
            case 3:
                bx = y, by = w - 1 - x;
                break;
            default:
                break;
            }
            for (int c = 0; c < ia->channels(); ++c)
                if (ia->sample16(x, y, c) != ib->sample16(bx, by, c)) {
                    why = QStringLiteral("pixel %1,%2 differs").arg(x).arg(y);
                    return false;
                }
        }
    return true;
}
/// A scanner bed with the first print of `src` four times, turned 0°, 90°, 180° and 270° (UX-04 test scan),
/// at about 300 dpi on the scan's own background.
bool writeFourOrientations(const QString& src, const fk::Shape& outer, const QString& dst,
                           uint32_t seed = 11) {
    fk::ImageInfo info;
    auto img = fk::readImageFile(toPath(src), &info);
    if (!img) return false;
    const double dpi = info.dpiX > 0 ? info.dpiX : 300.0;
    const int64_t k = std::max<int64_t>(1, std::llround(dpi / 300.0));
    const int ch = img->channels();
    std::vector<uint16_t> bg(static_cast<size_t>(ch));
    const int64_t band = std::max<int64_t>(4, std::min(img->width(), img->height()) / 50);
    for (int c = 0; c < ch; ++c) {
        std::vector<uint16_t> v;
        for (int64_t y = 0; y < img->height(); y += 3)
            for (int64_t x = 0; x < img->width(); x += 3)
                if (x < band || y < band || x >= img->width() - band || y >= img->height() - band)
                    v.push_back(img->sample16(x, y, c));
        std::nth_element(v.begin(), v.begin() + static_cast<std::ptrdiff_t>(v.size() / 2), v.end());
        bg[static_cast<size_t>(c)] = v[v.size() / 2];
    }
    // the print with 1.5 mm around it, at 300 dpi (every k-th pixel)
    const fk::RectI b =
        fk::boundingRect(fk::expandShape(outer, 1.5 / 25.4 * dpi), fk::SizeI{img->width(), img->height()});
    // a scan filled by its print has no backing to copy (its border is the print's frame): use a dark lid, as
    // a user would for white frames, so the bed is a valid detection input
    if (static_cast<double>(b.w) * static_cast<double>(b.h) >=
        0.6 * static_cast<double>(img->width()) * static_cast<double>(img->height()))
        std::fill(bg.begin(), bg.end(), static_cast<uint16_t>(0.12 * 65535));
    const int64_t cw = b.w / k, chh = b.h / k;
    const int64_t gap = 236; // 20 mm between prints and to the bed's edges
    const int64_t cell = std::max(cw, chh) + gap;
    const int64_t ow = 2 * cell + gap + cell / 2, oh = 2 * cell + gap; // not square: a turn changes the shape
    auto out = fk::ImageBuffer::create(ow, oh, ch, img->bits());
    if (!out) return false;
    uint32_t rng = seed; // another seed: the same scene with other scanner noise (a different file)
    auto put = [&](int64_t x, int64_t y, int c, int v) {
        if (img->bits() == 8) {
            out->row(y)[x * ch + c] = static_cast<uint8_t>(v / 257);
        } else {
            const uint16_t u = static_cast<uint16_t>(v);
            std::memcpy(out->row(y) + (x * ch + c) * 2, &u, 2);
        }
    };
    for (int64_t y = 0; y < oh; ++y)
        for (int64_t x = 0; x < ow; ++x) {
            rng = rng * 1664525u + 1013904223u;
            const int n = static_cast<int>((rng >> 16) % 3) - 1;
            for (int c = 0; c < ch; ++c)
                put(x, y, c,
                    std::clamp(bg[static_cast<size_t>(c)] + n * (img->bits() == 8 ? 257 : 64), 0, 65535));
        }
    for (int q = 0; q < 4; ++q) {
        const bool odd = q & 1;
        const int64_t tw = odd ? chh : cw, th = odd ? cw : chh;
        const int64_t ox = gap + (q % 2) * cell + (cell - gap - tw) / 2,
                      oy = gap + (q / 2) * cell + (cell - gap - th) / 2;
        for (int64_t y = 0; y < th; ++y)
            for (int64_t x = 0; x < tw; ++x) {
                // (x, y) in the turned crop → (sx, sy) in the crop
                int64_t sx = x, sy = y;
                switch (q) {
                case 1:
                    sx = y, sy = chh - 1 - x;
                    break;
                case 2:
                    sx = cw - 1 - x, sy = chh - 1 - y;
                    break;
                case 3:
                    sx = cw - 1 - y, sy = x;
                    break;
                default:
                    break;
                }
                for (int c = 0; c < ch; ++c)
                    put(ox + x, oy + y, c, img->sample16(b.x + sx * k, b.y + sy * k, c));
            }
    }
    fk::WriteOptions opt;
    opt.bits = img->bits();
    opt.icc = info.icc;
    opt.dpiX = opt.dpiY = dpi / static_cast<double>(k);
    QFile::remove(dst);
    auto sink = fk::createSink(toPath(dst), opt);
    if (!sink || !(*sink)->begin({ow, oh}, ch, img->bits())) return false;
    for (int64_t y = 0; y < oh; ++y)
        if (!(*sink)->writeRows(out->row(y), 1, out->rowBytes())) return false;
    return static_cast<bool>((*sink)->finish());
}
/// Writes `src`'s pixels brightened by `delta` (16-bit units; 8-bit scaled), keeping its profile — a "grade
/// again".
bool rewriteBrighter(const QString& src, const QString& dst, int delta) {
    fk::ImageInfo info;
    auto img = fk::readImageFile(toPath(src), &info);
    if (!img) return false;
    for (int64_t y = 0; y < img->height(); ++y)
        for (int64_t x = 0; x < img->width() * img->channels(); ++x) {
            if (img->bits() == 16) {
                uint16_t v;
                std::memcpy(&v, img->row(y) + x * 2, 2);
                v = static_cast<uint16_t>(std::min(65535, v + delta));
                std::memcpy(img->row(y) + x * 2, &v, 2);
            } else {
                uint8_t& v = img->row(y)[x];
                v = static_cast<uint8_t>(std::min(255, v + delta / 257 + 1));
            }
        }
    fk::WriteOptions opt;
    opt.bits = img->bits();
    opt.icc = info.icc;
    const QString tmp = dst + QStringLiteral(".tmp.tif");
    QFile::remove(tmp);
    auto sink = fk::createSink(toPath(tmp), opt);
    if (!sink) return false;
    if (!(*sink)->begin({img->width(), img->height()}, img->channels(), img->bits())) return false;
    for (int64_t y = 0; y < img->height(); ++y)
        if (!(*sink)->writeRows(img->row(y), 1, img->rowBytes())) return false;
    if (!(*sink)->finish()) return false;
    QFile::remove(dst);
    return QFile::rename(tmp, dst);
}
/// Mean absolute difference of two images of the same layout, 0…1 (−1 if they cannot be compared).
double meanDifference(const QString& a, const QString& b) {
    auto ia = fk::readImageFile(toPath(a)), ib = fk::readImageFile(toPath(b));
    if (!ia || !ib || ia->size() != ib->size() || ia->channels() != ib->channels()) return -1;
    double sum = 0;
    for (int64_t y = 0; y < ia->height(); ++y)
        for (int64_t x = 0; x < ia->width(); ++x)
            for (int c = 0; c < ia->channels(); ++c)
                sum += std::abs(static_cast<double>(ia->sample16(x, y, c)) - ib->sample16(x, y, c));
    return sum / (static_cast<double>(ia->width()) * ia->height() * ia->channels() * 65535.0);
}
/// UX-01: a single-print scan derived from the test scan (the other prints painted over with the scan's
/// background colour, reduced to about 300 dpi) and a blank scan of that background colour.
bool writeCountScans(const QString& src, const std::vector<fk::Shape>& outers, const QString& single,
                     const QString& blank) {
    fk::ImageInfo info;
    auto img = fk::readImageFile(toPath(src), &info);
    if (!img || outers.empty()) return false;
    const double dpi = info.dpiX > 0 ? info.dpiX : 300.0;
    const int64_t k = std::max<int64_t>(1, std::llround(dpi / 300.0));
    const int64_t w = img->width() / k, h = img->height() / k;
    const int ch = img->channels();
    // background: per-channel median of a 2 % border band
    std::vector<uint16_t> bg(static_cast<size_t>(ch));
    const int64_t band = std::max<int64_t>(4, std::min(img->width(), img->height()) / 50);
    for (int c = 0; c < ch; ++c) {
        std::vector<uint16_t> v;
        for (int64_t y = 0; y < img->height(); y += 3)
            for (int64_t x = 0; x < img->width(); x += 3)
                if (x < band || y < band || x >= img->width() - band || y >= img->height() - band)
                    v.push_back(img->sample16(x, y, c));
        std::nth_element(v.begin(), v.begin() + static_cast<std::ptrdiff_t>(v.size() / 2), v.end());
        bg[static_cast<size_t>(c)] = v[v.size() / 2];
    }
    const double margin = 2.0 / 25.4 * dpi; // 2 mm around each painted-over print
    uint32_t rng = 7;
    // keepFirst: the test scan with only its first print; otherwise a blank A4 scanner bed at 300 dpi (A4,
    // not the test scan's size: a scan cropped to one print would turn into a card-sized "print" of frame
    // colour)
    auto write = [&](const QString& dst, bool keepFirst) {
        const int64_t ow = keepFirst ? w : 2551, oh = keepFirst ? h : 3508;
        auto out = fk::ImageBuffer::create(ow, oh, ch, img->bits());
        if (!out) return false;
        for (int64_t y = 0; y < oh; ++y)
            for (int64_t x = 0; x < ow; ++x) {
                const fk::Vec2 p{(x + 0.5) * k, (y + 0.5) * k};
                bool paint = !keepFirst;
                if (keepFirst && fk::signedDistance(outers.front(), p) < -1.0)
                    for (size_t i = 1; i < outers.size() && !paint; ++i)
                        paint = fk::signedDistance(outers[i], p) > -margin;
                rng = rng * 1664525u + 1013904223u;
                const int n = static_cast<int>((rng >> 16) % 3) - 1; // ±1 level of scanner noise
                for (int c = 0; c < ch; ++c) {
                    int v = paint ? bg[static_cast<size_t>(c)] : img->sample16(x * k, y * k, c);
                    if (paint) v = std::clamp(v + n * (img->bits() == 8 ? 257 : 64), 0, 65535);
                    if (img->bits() == 8) {
                        out->row(y)[x * ch + c] = static_cast<uint8_t>(v / 257);
                    } else {
                        const uint16_t u = static_cast<uint16_t>(v);
                        std::memcpy(out->row(y) + (x * ch + c) * 2, &u, 2);
                    }
                }
            }
        fk::WriteOptions opt;
        opt.bits = img->bits();
        opt.icc = info.icc;
        opt.dpiX = opt.dpiY = keepFirst ? dpi / static_cast<double>(k) : 300.0;
        QFile::remove(dst);
        auto sink = fk::createSink(toPath(dst), opt);
        if (!sink || !(*sink)->begin({ow, oh}, ch, img->bits())) return false;
        for (int64_t y = 0; y < oh; ++y)
            if (!(*sink)->writeRows(out->row(y), 1, out->rowBytes())) return false;
        return static_cast<bool>((*sink)->finish());
    };
    return write(single, true) && write(blank, false);
}
QStringList filesMatching(const QString& dir, const QString& pattern) {
    QStringList out;
    QDirIterator it(dir, {pattern}, QDir::Files);
    while (it.hasNext())
        out << it.next();
    out.sort();
    return out;
}
/// Writes `src` again without any metadata (no XMP, no sidecar): what an external editor typically produces.
bool rewriteWithoutMetadata(const QString& src, const QString& dst, fk::FileFormat fmt, bool keepIcc = true) {
    fk::ImageInfo info;
    auto img = fk::readImageFile(toPath(src), &info);
    if (!img) return false;
    fk::WriteOptions opt;
    opt.format = fmt;
    opt.bits = fmt == fk::FileFormat::jpeg ? 8 : img->bits();
    if (fmt == fk::FileFormat::jpeg && img->bits() != 8) return false;
    if (keepIcc) opt.icc = info.icc;
    opt.jpegQuality = 92;
    QFile::remove(dst);
    auto sink = fk::createSink(toPath(dst), opt);
    if (!sink) return false;
    if (!(*sink)->begin({img->width(), img->height()}, img->channels(), img->bits())) return false;
    for (int64_t y = 0; y < img->height(); ++y)
        if (!(*sink)->writeRows(img->row(y), 1, img->rowBytes())) return false;
    return static_cast<bool>((*sink)->finish());
}
/// Writes the pixels of `r` (scan px) of `src` without metadata: a print's picture that was never exported.
bool writeCrop(const QString& src, const fk::RectI& r, const QString& dst) {
    fk::ImageInfo info;
    auto img = fk::readImageFile(toPath(src), &info);
    if (!img) return false;
    const int64_t x0 = std::clamp<int64_t>(r.x, 0, img->width()),
                  y0 = std::clamp<int64_t>(r.y, 0, img->height());
    const int64_t w = std::clamp<int64_t>(r.w, 0, img->width() - x0),
                  h = std::clamp<int64_t>(r.h, 0, img->height() - y0);
    if (w <= 0 || h <= 0) return false;
    const size_t bpp = static_cast<size_t>(img->channels()) * static_cast<size_t>(img->bits() / 8);
    fk::WriteOptions opt;
    opt.bits = img->bits();
    opt.icc = info.icc;
    QFile::remove(dst);
    auto sink = fk::createSink(toPath(dst), opt);
    if (!sink || !(*sink)->begin({w, h}, img->channels(), img->bits())) return false;
    for (int64_t y = 0; y < h; ++y)
        if (!(*sink)->writeRows(img->row(y0 + y) + static_cast<size_t>(x0) * bpp, 1,
                                static_cast<size_t>(w) * bpp))
            return false;
    return static_cast<bool>((*sink)->finish());
}
/// Writes `src` rotated by 180° (lossless) without metadata.
bool rewriteRotated180(const QString& src, const QString& dst) {
    fk::ImageInfo info;
    auto img = fk::readImageFile(toPath(src), &info);
    if (!img) return false;
    fk::WriteOptions opt;
    opt.bits = img->bits();
    opt.icc = info.icc;
    QFile::remove(dst);
    auto sink = fk::createSink(toPath(dst), opt);
    if (!sink || !(*sink)->begin({img->width(), img->height()}, img->channels(), img->bits())) return false;
    const size_t bpp = img->rowBytes() / static_cast<size_t>(img->width());
    std::vector<uint8_t> row(img->rowBytes());
    for (int64_t y = img->height() - 1; y >= 0; --y) {
        const uint8_t* r = img->row(y);
        for (int64_t x = 0; x < img->width(); ++x)
            std::memcpy(row.data() + static_cast<size_t>(x) * bpp,
                        r + static_cast<size_t>(img->width() - 1 - x) * bpp, bpp);
        if (!(*sink)->writeRows(row.data(), 1, row.size())) return false;
    }
    return static_cast<bool>((*sink)->finish());
}
} // namespace

void SelfTest::known(const QString& ticket, const QString& name, bool reproduced, const QString& detail) {
    known_.append(QJsonObject{{"ticket", ticket},
                              {"name", name},
                              {"status", reproduced ? "reproduced" : "NOT_REPRODUCED"},
                              {"detail", detail}});
    if (!reproduced) allOk_ = false;
    qInfo().noquote() << (reproduced ? "KNOWN" : "FAIL(known issue no longer reproduces)") << ticket << name
                      << detail;
}

void SelfTest::knownNotApplicable(const QString& ticket, const QString& name, const QString& why) {
    known_.append(
        QJsonObject{{"ticket", ticket}, {"name", name}, {"status", "not_applicable"}, {"detail", why}});
    qInfo().noquote() << "N/A(known issue, precondition not met)" << ticket << name << why;
}

void SelfTest::runTaskSafety(const QString& exportDir, const QString& projectPath) {
    // RFC-0010 §Task safety (FK-006, U002): a result computed from state that changed meanwhile is never
    // applied; every job ends as succeeded, failed, cancelled or outdated; cancelled exports leave no temp
    // files.
    JobRunner* jobs = app_->jobRunner();
    const std::string item = app_->currentItemId().toStdString();
    const QString scanId = app_->currentScanId();
    const QVariantMap mask{{"maskMode", app_->settings().value("maskMode")},
                           {"featherPx", app_->settings().value("featherPx")}};

    // TS1: the mask changes while a composite preview is computed → the preview is discarded
    {
        jobs->setTestDelayMs(500);
        app_->requestCompositePreview(QString::fromStdString(item));
        const int id = jobs->lastJobId();
        app_->setSetting(QStringLiteral("maskMode"), QStringLiteral("feather"));
        app_->setSetting(QStringLiteral("featherPx"), 3.0);
        app_->waitIdle(120000);
        jobs->setTestDelayMs(0);
        const QString state = jobs->finalState(id);
        check(QStringLiteral("TS1 preview computed before a mask change is not applied"),
              state == QLatin1String("outdated") && app_->compositePreview(item).isNull(),
              QStringLiteral("job %1, preview stored %2")
                  .arg(state)
                  .arg(!app_->compositePreview(item).isNull()));
        app_->requestCompositePreview(QString::fromStdString(item));
        app_->waitIdle(120000);
        check(QStringLiteral("TS1 a fresh preview matches the current print"),
              !app_->compositePreview(item).isNull() &&
                  app_->compositePreviewGeneration(item) == app_->itemGeneration(item),
              QStringLiteral("preview gen %1, item gen %2")
                  .arg(app_->compositePreviewGeneration(item))
                  .arg(app_->itemGeneration(item)));
        app_->setSetting(QStringLiteral("maskMode"), mask.value("maskMode"));
        app_->setSetting(QStringLiteral("featherPx"), mask.value("featherPx"));
    }

    // TS2: the user edits a print while the scan is detected again → the detection result is not applied
    {
        const QVariantMap before = app_->currentItem();
        jobs->setTestDelayMs(500);
        app_->detectScan(scanId, QString());
        const int id = jobs->lastJobId();
        const QVariantList pts = before.value("outer").toMap().value("points").toList();
        const double x0 = pts.isEmpty() ? 0 : pts.first().toList().value(0).toDouble();
        const double y0 = pts.isEmpty() ? 0 : pts.first().toList().value(1).toDouble();
        app_->setOuterNumeric(QString::fromStdString(item), 0, x0 + 3, y0);
        app_->waitIdle(300000);
        jobs->setTestDelayMs(0);
        const QVariantList after = app_->currentItem().value("outer").toMap().value("points").toList();
        const double x1 = after.isEmpty() ? -1 : after.first().toList().value(0).toDouble();
        check(QStringLiteral("TS2 detection finishing after an edit keeps the edit"),
              jobs->finalState(id) == QLatin1String("outdated") && std::abs(x1 - (x0 + 3)) < 1e-6 &&
                  app_->currentItemId().toStdString() == item,
              QStringLiteral("job %1, corner x %2 (edited to %3)")
                  .arg(jobs->finalState(id))
                  .arg(x1)
                  .arg(x0 + 3));
        app_->undo();
    }

    // TS3: the project is replaced while an export runs → cancelled, no temporary files, nothing recorded
    {
        jobs->setTestDelayMs(300);
        app_->exportImageAreas({});
        const int id = jobs->lastJobId();
        check(QStringLiteral("TS3 open project"), app_->openProject(projectPath));
        app_->waitIdle(300000);
        jobs->setTestDelayMs(0);
        const QStringList temps = filesMatching(exportDir, QStringLiteral("*.fk-tmp-*"));
        check(QStringLiteral("TS3 export interrupted by opening a project: cancelled, no temp files"),
              jobs->finalState(id) == QLatin1String("cancelled") && temps.isEmpty(),
              QStringLiteral("job %1, temp files %2").arg(jobs->finalState(id)).arg(temps.size()));
    }

    // TS4: a job that fails is shown as failed, not done
    {
        const QString bad = QDir(dir_).filePath(QStringLiteral("not-a-tiff.tif"));
        QFile f(bad);
        if (f.open(QIODevice::WriteOnly)) f.write(QByteArray(4096, 'x'));
        f.close();
        app_->importScans({bad});
        const int id = jobs->lastJobId();
        app_->waitIdle(120000);
        check(QStringLiteral("TS4 a failing job ends as failed"),
              jobs->finalState(id) == QLatin1String("failed"), jobs->finalState(id));
        app_->openProject(projectPath); // back to the saved state for the following checks
        app_->waitIdle(120000);
    }
}

void SelfTest::runKnownIssues(const QString& exportDir) {
    // Each check reproduces a confirmed defect from the v1.0 requirements (docs/v1/TRACEABILITY.md). When a
    // unit fixes one, the check stops reproducing, the run fails, and the unit replaces it with a normal
    // check.
    const QString kiDir = QDir(dir_).filePath(QStringLiteral("known-issues"));
    QDir(kiDir).removeRecursively();
    QDir().mkpath(kiDir);
    auto relinkRows = [&](const QString& file, const QString& target = QString()) {
        QVariantList rows;
        auto c = connect(app_, &AppController::relinkResults, this, [&](const QVariantList& r) { rows = r; });
        app_->relinkFiles({file}, target);
        app_->waitIdle(300000);
        disconnect(c);
        QMetaObject::invokeMethod(window_, "closeDialogs");
        return rows;
    };

    const QStringList edits = filesMatching(exportDir, QStringLiteral("*_EDIT_v001.tif"));
    if (edits.isEmpty()) {
        known(QStringLiteral("UX-07"), QStringLiteral("setup: exported image areas"), false,
              QStringLiteral("none"));
        return;
    }
    const QString f = edits.first();

    // UX-07 (U004, was a known issue): a renamed graded file without our metadata is offered by its picture
    // but not pre-selected; choosing its print relinks it. An editor's "_1" copy is matched by name and
    // picture.
    const QVariantList rowsF = relinkRows(f);
    const QVariantMap rf = rowsF.isEmpty() ? QVariantMap() : rowsF.first().toMap();
    const QString itemId = rf.value("itemId").toString();
    const QString label = rf.value("label").toString();
    check(QStringLiteral("unchanged export decided by its file hash"),
          rf.value("decision").toString() == QLatin1String("unique") &&
              rf.value("source").toString().contains(QLatin1String("file_hash")) &&
              rf.value("accept").toBool(),
          rf.value("decision").toString() + QLatin1Char(' ') + rf.value("source").toString());
    {
        auto brief = [](const QVariantMap& r) {
            return QStringLiteral("%1 %2 accept=%3 %4")
                .arg(r.value("decision").toString(), r.value("source").toString())
                .arg(r.value("accept").toBool())
                .arg(r.value("message").toString());
        };
        const QString r2 = QDir(kiDir).filePath(QStringLiteral("graded_in_editor.tif"));
        const bool written = rewriteWithoutMetadata(f, r2, fk::FileFormat::tiff);
        const QVariantList rows = relinkRows(r2);
        const QVariantMap a = rows.isEmpty() ? QVariantMap() : rows.first().toMap();
        const QVariantList opts = a.value("options").toList();
        check(QStringLiteral("renamed file without metadata: offered by its picture, not pre-selected"),
              written && a.value("decision").toString() == QLatin1String("ambiguous") && !opts.isEmpty() &&
                  opts.first().toMap().value("itemId").toString() == itemId && !a.value("accept").toBool(),
              brief(a));
        const QVariantList rowsT = relinkRows(r2, itemId);
        const QVariantMap t = rowsT.isEmpty() ? QVariantMap() : rowsT.first().toMap();
        check(QStringLiteral("renamed file relinks once its print is chosen"),
              t.value("decision").toString() == QLatin1String("unique") && t.value("accept").toBool() &&
                  t.value("itemId").toString() == itemId && !t.value("newAlignment").toBool(),
              brief(t));
        const QString r4 = QDir(kiDir).filePath(QFileInfo(f).completeBaseName() + QStringLiteral("_1.tif"));
        rewriteWithoutMetadata(f, r4, fk::FileFormat::tiff);
        const QVariantList rows4 = relinkRows(r4);
        const QVariantMap c4 = rows4.isEmpty() ? QVariantMap() : rows4.first().toMap();
        check(QStringLiteral("editor suffix _1 matched by name and picture"),
              c4.value("decision").toString() == QLatin1String("unique") && c4.value("accept").toBool() &&
                  c4.value("itemId").toString() == itemId &&
                  c4.value("source").toString().contains(QLatin1String("file_name")),
              brief(c4));
    }

    // FK-010 (U010, was a known issue): prompts follow the core's plan of the real output; reducing 16-bit
    // data needs an explicit yes
    {
        const QVariant bits = app_->settings().value("exportBits");
        app_->setSetting(QStringLiteral("exportBits"), 8);
        QCoreApplication::processEvents();
        const bool sixteen = app_->currentScan().value("bits").toInt() == 16;
        const QVariantMap item = app_->currentItem();
        if (sixteen) {
            check(QStringLiteral("FK-010 16→8-bit export is not announced as bit-identical"),
                  !item.value("frameBitExact").toBool() &&
                      item.value("exportPlan").toMap().value("reducesDepth").toBool() &&
                      (item.value("notExactReason").toString().contains(QLatin1String("8 bits")) ||
                       item.value("notExactReason").toString().contains(QStringLiteral("降为 8 位"))),
                  item.value("notExactReason").toString());
            const QString id = app_->currentItemId();
            const QStringList before = filesMatching(exportDir, QStringLiteral("*_EDIT_v*.tif"));
            app_->exportImageAreas({id}); // no consent
            app_->waitIdle(300000);
            const qsizetype refused =
                filesMatching(exportDir, QStringLiteral("*_EDIT_v*.tif")).size() - before.size();
            app_->exportImageAreas({id}, true); // the user confirmed
            app_->waitIdle(300000);
            const qsizetype written =
                filesMatching(exportDir, QStringLiteral("*_EDIT_v*.tif")).size() - before.size() - refused;
            check(QStringLiteral("FK-010 8-bit export only with explicit consent"),
                  refused == 0 && written == 1,
                  QStringLiteral("without consent %1, with consent %2").arg(refused).arg(written));
        } else {
            knownNotApplicable(QStringLiteral("FK-010"), QStringLiteral("16→8-bit export prompt"),
                               QStringLiteral("scan bits %1").arg(app_->currentScan().value("bits").toInt()));
        }
        app_->setSetting(QStringLiteral("exportBits"), bits);
    }

    // FK-002 (U005, was a known issue): a same-size graded file turned by 180° — the picture finds the turn,
    // the user confirms it, the turn is stored with the relink and the output turns the file back; after
    // saving and reopening the project the stored turn is used again.
    {
        const QString itemFile = QFileInfo(f).fileName();
        const QString rot = QDir(kiDir).filePath(QStringLiteral("rot180"));
        QDir().mkpath(rot);
        const QString r =
            QDir(rot).filePath(itemFile); // export name in another folder → matched by file name
        const bool written = rewriteRotated180(f, r);
        QVariantList rows = relinkRows(r);
        QVariantMap row = rows.isEmpty() ? QVariantMap() : rows.first().toMap();
        check(QStringLiteral("180° graded file: turn found by the picture, confirmation required"),
              written && row.value("decision").toString() == QLatin1String("unique") &&
                  row.value("rotate").toInt() == 180 && row.value("rotationReliable").toBool() &&
                  row.value("actions").toStringList().contains(QStringLiteral("rotate")) &&
                  !row.value("accept").toBool(),
              QStringLiteral("rotate %1 reliable %2 accept %3 actions %4")
                  .arg(row.value("rotate").toInt())
                  .arg(row.value("rotationReliable").toBool())
                  .arg(row.value("accept").toBool())
                  .arg(row.value("actions").toStringList().join(',')));
        row["accept"] = true; // the user confirms
        app_->applyRelink({row});
        const QString itemId = row.value("itemId").toString();
        auto storedTurn = [&]() -> int {
            const fk::ItemRecord* it = app_->item(itemId.toStdString());
            if (!it) return -1;
            for (auto e = it->edits.rbegin(); e != it->edits.rend(); ++e)
                if (!e->relinkedPath.empty()) return e->relinkTransform.set ? e->relinkTransform.rotate : -2;
            return -3;
        };
        check(QStringLiteral("180° turn stored with the relink"), storedTurn() == 2,
              QString::number(storedTurn()));
        app_->setSetting(QStringLiteral("finalMode"), QStringLiteral("real"));
        const QString stem = QFileInfo(f).completeBaseName().section(QStringLiteral("_EDIT_"), 0, 0);
        const QStringList fulls = filesMatching(exportDir, stem + QStringLiteral("_FULL_v001.tif"));
        auto finalEqualsFullPrint = [&](QString& why) {
            const QStringList before = filesMatching(exportDir, QStringLiteral("*_FINAL_v*.tif"));
            app_->exportFinal({itemId}, {});
            app_->waitIdle(300000);
            QStringList after = filesMatching(exportDir, QStringLiteral("*_FINAL_v*.tif"));
            for (const auto& b : before)
                after.removeAll(b);
            if (after.size() != 1 || fulls.size() != 1) {
                why = QStringLiteral("final files %1, full prints %2").arg(after.size()).arg(fulls.size());
                return false;
            }
            return samePixels(after.first(), fulls.first(), why);
        };
        QString why;
        check(QStringLiteral("final output turns the graded file back (bit-exact with the full print)"),
              finalEqualsFullPrint(why), why);
        // persisted: save, reopen, compose again
        const QString proj = QDir(kiDir).filePath(QStringLiteral("fk002.fkproj"));
        QFile::remove(proj);
        app_->saveProjectAs(proj);
        app_->openProject(proj);
        app_->waitIdle(300000);
        check(QStringLiteral("reopened project keeps the confirmed turn"), storedTurn() == 2,
              QString::number(storedTurn()));
        QString why2;
        check(QStringLiteral("reopened project: final output still bit-exact"), finalEqualsFullPrint(why2),
              why2);
    }

    // FK-003 (U006): the export was converted to Adobe RGB, the editor dropped the profile. The export's
    // profile is offered (identified by its hash), the user confirms it, the choice is stored with the
    // project and the output converts once — colours as in the scan. An untagged scan cannot be converted
    // (the export is refused until a profile is assigned): the precondition is missing, not the behaviour.
    if (app_->project().scans.empty() || app_->project().scans.front().info.iccDescription.empty() &&
                                             app_->project().scans.front().iccSha256.empty()) {
        for (const char* n : {"graded file without a profile: the export's profile is offered, confirmation "
                              "required",
                              "graded file without a profile is not relinked before the choice",
                              "chosen profile stored with the relink and the project",
                              "output converts the chosen profile once: colours as in the scan"})
            skip(QString::fromUtf8(n), QStringLiteral("untagged scan: converting the export to Adobe RGB is "
                                                      "refused until a profile is assigned"));
    } else {
        const QVariant policy = app_->settings().value("colorPolicy");
        app_->setSetting(QStringLiteral("colorPolicy"), QStringLiteral("convert_adobe"));
        const QStringList before = filesMatching(exportDir, QStringLiteral("*_EDIT_v*.tif"));
        app_->exportImageAreas({itemId});
        app_->waitIdle(300000);
        app_->setSetting(QStringLiteral("colorPolicy"), policy);
        QStringList made = filesMatching(exportDir, QStringLiteral("*_EDIT_v*.tif"));
        for (const auto& b : before)
            made.removeAll(b);
        const QString dir = QDir(kiDir).filePath(QStringLiteral("fk003"));
        QDir().mkpath(dir);
        const QString g =
            made.size() == 1 ? QDir(dir).filePath(QFileInfo(made.first()).fileName()) : QString();
        const bool written =
            !g.isEmpty() && rewriteWithoutMetadata(made.first(), g, fk::FileFormat::tiff, /*keepIcc=*/false);
        const QVariantList rows = relinkRows(g);
        QVariantMap row = rows.isEmpty() ? QVariantMap() : rows.first().toMap();
        const QVariantList choices = row.value("iccChoices").toList();
        const QString first =
            choices.isEmpty() ? QString() : choices.first().toMap().value("label").toString();
        check(QStringLiteral(
                  "graded file without a profile: the export's profile is offered, confirmation required"),
              written && row.value("decision").toString() == QLatin1String("unique") &&
                  row.value("actions").toStringList().contains(QStringLiteral("choose_icc")) &&
                  row.value("iccChoice").toString() == QLatin1String("export") &&
                  first.contains(QLatin1String("Adobe")) && !row.value("accept").toBool(),
              QStringLiteral("exports %1, decision %2, choice %3, first %4, accept %5")
                  .arg(made.size())
                  .arg(row.value("decision").toString(), row.value("iccChoice").toString(), first)
                  .arg(row.value("accept").toBool()));
        // without a choice the file is not relinked
        QVariantMap unchosen = row;
        unchosen["iccSha256"] = QString();
        unchosen["accept"] = true;
        const int relinkedBefore = app_->counts().value("relinked").toInt();
        app_->applyRelink({unchosen});
        auto relinkedTo = [&]() -> const fk::EditVersion* {
            const fk::ItemRecord* it = app_->item(itemId.toStdString());
            if (!it) return nullptr;
            for (const auto& e : it->edits)
                if (QString::fromStdString(e.relinkedPath) == g) return &e;
            return nullptr;
        };
        check(QStringLiteral("graded file without a profile is not relinked before the choice"),
              relinkedTo() == nullptr,
              QString::number(app_->counts().value("relinked").toInt() - relinkedBefore));
        row["accept"] = true; // the user confirms the offered profile
        app_->applyRelink({row});
        const fk::EditVersion* ev = relinkedTo();
        check(QStringLiteral("chosen profile stored with the relink and the project"),
              ev && ev->relinkTransform.iccSource == "export" &&
                  app_->project().iccProfiles.count(ev->relinkTransform.iccSha256) == 1,
              ev ? QString::fromStdString(ev->relinkTransform.iccSource) : QStringLiteral("not relinked"));
        const QStringList beforeFinal = filesMatching(exportDir, QStringLiteral("*_FINAL_v*.tif"));
        app_->setSetting(QStringLiteral("finalMode"), QStringLiteral("real"));
        app_->exportFinal({itemId}, {});
        app_->waitIdle(300000);
        QStringList finals = filesMatching(exportDir, QStringLiteral("*_FINAL_v*.tif"));
        for (const auto& b : beforeFinal)
            finals.removeAll(b);
        const QString stem = QFileInfo(f).completeBaseName().section(QStringLiteral("_EDIT_"), 0, 0);
        const QStringList fulls = filesMatching(exportDir, stem + QStringLiteral("_FULL_v001.tif"));
        const double diff =
            finals.size() == 1 && fulls.size() == 1 ? meanDifference(finals.first(), fulls.first()) : -1;
        check(QStringLiteral("output converts the chosen profile once: colours as in the scan"),
              diff >= 0 && diff < 0.002, QStringLiteral("mean difference %1").arg(diff, 0, 'f', 5));
    }

    // UX-07 (U004, was a known issue): after a restart without saving, the export has no record in the new
    // project; choosing its print offers a new alignment, which relinks and composites.
    {
        const QString scan = scan_;
        app_->newProject();
        app_->importScans({scan});
        app_->waitIdle(300000);
        const QVariantList rows = relinkRows(f);
        const QVariantMap n = rows.isEmpty() ? QVariantMap() : rows.first().toMap();
        check(QStringLiteral("new session: no export record, the user chooses the print"),
              n.value("decision").toString() == QLatin1String("none") && !n.value("accept").toBool(),
              n.value("decision").toString() + QLatin1Char(' ') + n.value("message").toString());
        QString target;
        for (const auto& t : app_->relinkTargets())
            if (t.toMap().value("label").toString() == label) target = t.toMap().value("id").toString();
        const QVariantList rowsT = relinkRows(f, target);
        QVariantMap row = rowsT.isEmpty() ? QVariantMap() : rowsT.first().toMap();
        const bool offered = row.value("decision").toString() == QLatin1String("unique") &&
                             row.value("newAlignment").toBool() && row.value("ok").toBool() &&
                             !row.value("accept").toBool(); // a new alignment is never pre-accepted
        QStringList ev;
        for (const auto& o : row.value("options").toList())
            for (const auto& e : o.toMap().value("evidence").toList())
                ev << e.toMap().value("text").toString();
        check(QStringLiteral("new session: new alignment offered for the chosen print"), offered,
              QStringLiteral("target %1: %2 %3 ok=%4 %5 [%6]")
                  .arg(target, row.value("decision").toString(), row.value("source").toString())
                  .arg(row.value("ok").toBool())
                  .arg(row.value("message").toString(), ev.join(QStringLiteral("; "))));
        row["accept"] = true;
        app_->applyRelink({row});
        check(QStringLiteral("new session: new alignment relinked"),
              app_->counts().value("relinked").toInt() == 1 && app_->exportRecordCount() == 1,
              QStringLiteral("relinked %1, records %2")
                  .arg(app_->counts().value("relinked").toInt())
                  .arg(app_->exportRecordCount()));
        const QStringList before = filesMatching(exportDir, QStringLiteral("*_FINAL_v*.tif"));
        app_->setSetting(QStringLiteral("finalMode"), QStringLiteral("real"));
        app_->exportFinal({target}, {{"scale", true}, {"rotate", true}});
        app_->waitIdle(300000);
        QStringList after = filesMatching(exportDir, QStringLiteral("*_FINAL_v*.tif"));
        for (const auto& b : before)
            after.removeAll(b);
        check(QStringLiteral("new session: composite from the new alignment"), after.size() == 1,
              QString::number(after.size()));
        // the unchanged export placed by a new alignment of the same shape composites bit for bit
        const QString stem = QFileInfo(f).completeBaseName().section(QStringLiteral("_EDIT_"), 0, 0);
        const QStringList fulls = filesMatching(exportDir, stem + QStringLiteral("_FULL_v001.tif"));
        QString why;
        const bool exact =
            after.size() == 1 && fulls.size() == 1 && samePixels(after.first(), fulls.first(), why);
        check(QStringLiteral("new session: unchanged export through a new alignment is bit-exact"),
              exact || row.value("alignmentMode").toString() != QLatin1String("pixel_crop"),
              row.value("alignmentMode").toString() + QLatin1Char(' ') + why);
    }

    // FK-007 (U007, was a known issue): sources are verified before output. Works on a copy of the scan; the
    // original is never modified.
    {
        const QString d = QDir(kiDir).filePath(QStringLiteral("fk007"));
        QDir(d).removeRecursively();
        QDir().mkpath(d);
        const QString scanCopy = QDir(d).filePath(QStringLiteral("scan.tif"));
        const bool copied = QFile::copy(scan_, scanCopy);
        QStringList notes; // what the user is told: the refusals must say why
        auto nc = connect(app_, &AppController::notification, this,
                          [&](const QString& text, const QString&) { notes << text; });
        // the message in English or in Chinese (the self-test also runs with the Chinese interface)
        auto told = [&](const QString& what, const QString& zh) {
            return std::any_of(notes.begin(), notes.end(),
                               [&](const QString& n) { return n.contains(what) || n.contains(zh); });
        };
        app_->newProject();
        app_->importScans({scanCopy});
        app_->waitIdle(300000);
        const fk::ScanRecord* sr = app_->project().scans.empty() ? nullptr : &app_->project().scans.front();
        check(QStringLiteral("import hashes the whole scan"),
              copied && sr && !sr->fingerprint.fullSha256.empty(),
              sr ? QString::number(sr->fingerprint.size) : QStringLiteral("no scan"));
        const QString exp = QDir(d).filePath(QStringLiteral("export"));
        const QString prevFolder = app_->settings().value("exportFolder").toString();
        app_->setExportFolder(QUrl::fromLocalFile(exp));
        const QVariantList targets = app_->relinkTargets();
        const QString id = targets.isEmpty() ? QString() : targets.first().toMap().value("id").toString();
        app_->exportImageAreas({id});
        app_->waitIdle(300000);
        const QStringList ex = filesMatching(exp, QStringLiteral("*_EDIT_v001.tif"));
        QVariantList rows = ex.isEmpty() ? QVariantList() : relinkRows(ex.first());
        QVariantMap row = rows.isEmpty() ? QVariantMap() : rows.first().toMap();
        row["accept"] = true;
        app_->applyRelink({row});
        app_->setSetting(QStringLiteral("finalMode"), QStringLiteral("real"));
        auto finals = [&]() { return filesMatching(exp, QStringLiteral("*_FINAL_v*.tif")).size(); };
        auto relinkedSha = [&]() -> QString {
            const fk::ItemRecord* it = app_->item(id.toStdString());
            if (!it) return {};
            for (auto e = it->edits.rbegin(); e != it->edits.rend(); ++e)
                if (!e->relinkedPath.empty()) return QString::fromStdString(e->relinkedSha256);
            return {};
        };
        // graded again in the editor and saved over: same picture, size and profile → used, new hash recorded
        const bool regraded = !ex.isEmpty() && rewriteBrighter(ex.first(), ex.first(), 600);
        const qsizetype f0 = finals();
        app_->exportFinal({id}, {});
        app_->waitIdle(300000);
        const auto newSha = fk::sha256File(toPath(ex.isEmpty() ? QString() : ex.first()));
        check(QStringLiteral("graded file graded again (same picture): output made, new hash recorded"),
              regraded && finals() == f0 + 1 && newSha && relinkedSha() == QString::fromStdString(*newSha),
              QStringLiteral("finals +%1").arg(finals() - f0));
        // replaced by a different picture of the same size → refused
        const bool replaced =
            !ex.isEmpty() && rewriteRotated180(ex.first(), ex.first() + QStringLiteral(".r.tif")) &&
            QFile::remove(ex.first()) && QFile::rename(ex.first() + QStringLiteral(".r.tif"), ex.first());
        const qsizetype f1 = finals();
        app_->exportFinal({id}, {});
        app_->waitIdle(300000);
        check(QStringLiteral("graded file replaced by another picture: no output, the user is told why"),
              replaced && finals() == f1 &&
                  told(QStringLiteral("replaced by a different picture"), QStringLiteral("换成了另一张画面")),
              QStringLiteral("finals +%1; %2")
                  .arg(finals() - f1)
                  .arg(notes.isEmpty() ? QString() : notes.last()));
        // the scan replaced at the same path (same size, one byte changed in the middle) → refused
        const QString proj = QDir(d).filePath(QStringLiteral("fk007.fkproj"));
        app_->saveProjectAs(proj);
        bool tampered = false;
        {
            QFile f(scanCopy);
            if (f.open(QIODevice::ReadWrite)) {
                const qint64 mid = f.size() / 2;
                f.seek(mid);
                char c = 0;
                f.getChar(&c);
                f.seek(mid);
                tampered = f.putChar(static_cast<char>(c ^ 0x5a));
            }
        }
        const QStringList before = filesMatching(exp, QStringLiteral("*_EDIT_v*.tif"));
        app_->exportImageAreas({id});
        app_->waitIdle(300000);
        check(QStringLiteral("scan changed in the middle: export refused, the user is told why"),
              tampered && filesMatching(exp, QStringLiteral("*_EDIT_v*.tif")).size() == before.size() &&
                  told(QStringLiteral("changed since it was imported"), QStringLiteral("导入后已被改动")),
              QStringLiteral("exports +%1; %2")
                  .arg(filesMatching(exp, QStringLiteral("*_EDIT_v*.tif")).size() - before.size())
                  .arg(notes.isEmpty() ? QString() : notes.last()));
        app_->openProject(proj);
        app_->waitIdle(300000);
        check(QStringLiteral("scan changed: shown as changed after reopening, not loaded"),
              app_->currentScan().value("state").toString() == QLatin1String("changed"),
              app_->currentScan().value("state").toString() + QLatin1Char(' ') +
                  app_->currentScan().value("error").toString());
        app_->setExportFolder(QUrl::fromLocalFile(prevFolder));
        disconnect(nc);
    }
}

void SelfTest::runDataProtection() {
    const QString d = QDir(dir_).filePath(QStringLiteral("data-protection"));
    QDir(d).removeRecursively();
    QDir().mkpath(d);
    QObject* dialog = window_ ? window_->findChild<QObject*>(QStringLiteral("replaceConfirm")) : nullptr;
    auto dialogOpen = [&] { return dialog && dialog->property("visible").toBool(); };
    auto ask = [&](const QString& action, const QString& url) {
        QMetaObject::invokeMethod(window_, "replaceProject", Q_ARG(QVariant, action), Q_ARG(QVariant, url));
        QCoreApplication::processEvents();
    };
    auto answer = [&](const QString& choice) {
        QMetaObject::invokeMethod(window_, "finishReplace", Q_ARG(QVariant, choice));
        QCoreApplication::processEvents();
        app_->waitIdle(300000);
    };
    auto scans = [&] { return app_->counts().value("scans").toInt(); };

    // a project with unsaved work, saved once to a file
    const QString saved = QDir(d).filePath(QStringLiteral("saved.fkproj"));
    app_->newProject();
    app_->importScans({scan_});
    app_->waitIdle(300000);
    app_->saveProjectAs(saved);
    app_->saveProject(); // second save: saved.fkproj.bak exists
    app_->setSetting(QStringLiteral("expectedCount"),
                     app_->settings().value("expectedCount").toInt() + 1); // unsaved
    check(QStringLiteral("FK-001 setup: unsaved changes"), app_->dirty() && scans() == 1,
          QStringLiteral("dirty %1, scans %2").arg(app_->dirty()).arg(scans()));

    // FK-001: "New" asks; Cancel keeps everything
    ask(QStringLiteral("new"), QString());
    check(QStringLiteral("FK-001 new project with unsaved changes asks first"), dialogOpen() && scans() == 1,
          QStringLiteral("dialog %1, scans %2").arg(dialogOpen()).arg(scans()));
    answer(QStringLiteral("cancel"));
    check(QStringLiteral("FK-001 cancel keeps the project and its unsaved changes"),
          !dialogOpen() && scans() == 1 && app_->dirty());
    // dropping a project file goes through the same question
    app_->dropFiles({QUrl::fromLocalFile(saved)});
    QCoreApplication::processEvents();
    check(QStringLiteral("FK-001 dropping a project file asks first"), dialogOpen() && app_->dirty(),
          QStringLiteral("dialog %1").arg(dialogOpen()));
    answer(QStringLiteral("cancel"));

    // a failed Save As keeps path, name and unsaved state
    const QString pathBefore = app_->projectPath();
    const bool savedOk = app_->saveProjectAs(QDir(d).filePath(QStringLiteral("no-such-dir/x.fkproj")));
    check(QStringLiteral("FK-001 failed save keeps the project path and unsaved changes"),
          !savedOk && app_->projectPath() == pathBefore && app_->dirty(),
          QStringLiteral("path %1").arg(app_->projectPath()));

    // FK-008 recovery snapshot: written while unsaved, offered after a crash, recovered unsaved. Only this
    // project's snapshot counts: on a slow scan the 3 s timer also wrote snapshots of earlier phases'
    // projects, which programmatic replacements leave behind like a crash would.
    app_->writeRecovery();
    const QString ownId = toQ(app_->project().projectId);
    auto ownSnapshot = [&] {
        for (const QVariant& v : app_->recoverySnapshots())
            if (QFileInfo(v.toMap().value("file").toString()).completeBaseName() == ownId)
                return v.toMap().value("file").toString();
        return QString();
    };
    const bool hiddenWhileOpen = ownSnapshot().isEmpty();
    app_->newProject(); // what a crash looks like to the next session: the snapshot is still there
    const QString snap = ownSnapshot();
    const bool recovered = !snap.isEmpty() && app_->recoverSnapshot(snap);
    app_->waitIdle(300000);
    check(QStringLiteral("FK-008 unsaved work is recovered from the snapshot (unsaved, same path)"),
          hiddenWhileOpen && recovered && scans() == 1 && app_->dirty() && app_->projectPath() == saved,
          QStringLiteral("hidden while open %1, recovered %2, scans %3, path %4")
              .arg(hiddenWhileOpen)
              .arg(recovered)
              .arg(scans())
              .arg(app_->projectPath()));
    // "Don't save" drops the snapshot
    ask(QStringLiteral("new"), QString());
    answer(QStringLiteral("dontsave"));
    check(QStringLiteral("FK-008 \"Don't save\" discards the snapshot"),
          scans() == 0 && ownSnapshot().isEmpty(),
          QStringLiteral("scans %1, own snapshot %2").arg(scans()).arg(ownSnapshot()));

    // output parameters travel with the project
    app_->openProject(saved);
    app_->waitIdle(300000);
    const QVariant finalMode = app_->settings().value("finalMode");
    app_->setSetting(QStringLiteral("finalMode"), QStringLiteral("synthetic"));
    app_->saveProject();
    app_->setSetting(QStringLiteral("finalMode"), QStringLiteral("real"));
    app_->openProject(saved);
    app_->waitIdle(300000);
    check(QStringLiteral("FK-008 output parameters are saved with the project"),
          app_->settings().value("finalMode").toString() == QLatin1String("synthetic"),
          app_->settings().value("finalMode").toString());
    app_->setSetting(QStringLiteral("finalMode"), finalMode);
    app_->saveProject();
    app_->saveProject(); // the backup is a good file again

    // FK-008: a damaged project offers its backup; restoring keeps the damaged file
    QString backupFor;
    auto bc = connect(app_, &AppController::backupAvailable, this,
                      [&](const QString& path, const QString&, const QString&) { backupFor = path; });
    app_->newProject();
    {
        QFile f(saved);
        if (f.open(QIODevice::WriteOnly | QIODevice::Truncate)) f.write("{ damaged");
    }
    const bool opened = app_->openProject(saved);
    disconnect(bc);
    check(QStringLiteral("FK-008 a damaged project offers its backup"), !opened && backupFor == saved,
          backupFor);
    const bool restored = app_->restoreBackup(saved);
    app_->waitIdle(300000);
    const QStringList aside = QDir(d).entryList({QStringLiteral("saved.fkproj.damaged-*")}, QDir::Files);
    QString asideText;
    if (!aside.isEmpty()) {
        QFile f(QDir(d).filePath(aside.first()));
        if (f.open(QIODevice::ReadOnly)) asideText = QString::fromUtf8(f.readAll());
    }
    check(QStringLiteral("FK-008 backup restored unsaved; the damaged file is kept"),
          restored && scans() == 1 && app_->dirty() && aside.size() == 1 &&
              asideText == QLatin1String("{ damaged"),
          QStringLiteral("restored %1, scans %2, kept %3").arg(restored).arg(scans()).arg(aside.join(',')));
    const bool savedAgain = app_->saveProject();
    check(QStringLiteral("FK-008 the restored project saves to its own path"),
          savedAgain && app_->openProject(saved) && scans() == 1);
    app_->waitIdle(300000);
    app_->newProject();
}

void SelfTest::runPrivacy() {
    const QString d = QDir(dir_).filePath(QStringLiteral("privacy"));
    QDir(d).removeRecursively();
    QDir().mkpath(d);
    // a copy of the scan carrying identity: TIFF Artist and HostComputer, XMP dc:creator
    const QString scanCopy = QDir(d).filePath(QStringLiteral("identity.tif"));
    bool written = false;
    {
        fk::ImageInfo info;
        auto img = fk::readImageFile(toPath(scan_), &info);
        if (img) {
            fk::WriteOptions o;
            o.bits = img->bits();
            o.icc = info.icc;
            o.dpiX = info.dpiX;
            o.dpiY = info.dpiY;
            o.meta.make = "EPSON";
            o.meta.artist = "Ann Example";
            o.meta.hostComputer = "ann-host-1234";
            o.xmp =
                "<x:xmpmeta xmlns:x=\"adobe:ns:meta/\"><rdf:RDF><rdf:Description><dc:creator><rdf:Seq>"
                "<rdf:li>Ann Example</rdf:li></rdf:Seq></dc:creator></rdf:Description></rdf:RDF></x:xmpmeta>";
            if (auto sink = fk::createSink(toPath(scanCopy), o)) {
                written = (*sink)->begin({img->width(), img->height()}, img->channels(), img->bits()).ok() &&
                          (*sink)->writeRows(img->data(), img->height(), img->rowBytes()).ok() &&
                          (*sink)->finish().ok();
            }
        }
    }
    app_->newProject();
    app_->importScans({scanCopy});
    app_->waitIdle(300000);
    const QVariant privacy = app_->settings().value("privacy"),
                   format = app_->settings().value("exportFormat"),
                   bits = app_->settings().value("exportBits"),
                   folder = app_->settings().value("exportFolder");
    auto listsIdentity = [&] {
        const QVariantList l = app_->privacyPreview();
        return std::any_of(l.begin(), l.end(),
                           [](const QVariant& v) { return v.toMap().value("identity").toBool(); });
    };
    app_->setSetting(QStringLiteral("privacy"), QStringLiteral("keep_all_safe"));
    app_->setSetting(QStringLiteral("exportFormat"), QStringLiteral("tif"));
    const bool keepLists = listsIdentity();
    app_->setSetting(QStringLiteral("privacy"), QStringLiteral("remove_identity"));
    const bool removeLists = listsIdentity();
    check(QStringLiteral("FK-004 the privacy panel states the written fields (identity only when kept)"),
          written && keepLists && !removeLists && !app_->privacyPreview().isEmpty(),
          QStringLiteral("keep lists identity %1, remove lists identity %2, fields %3")
              .arg(keepLists)
              .arg(removeLists)
              .arg(app_->privacyPreview().size()));
    // export one print in every format with "remove identity" and read the files back byte for byte
    const QString exp = QDir(d).filePath(QStringLiteral("export"));
    app_->setExportFolder(QUrl::fromLocalFile(exp));
    const QVariantList targets = app_->relinkTargets();
    const QString id = targets.isEmpty() ? QString() : targets.first().toMap().value("id").toString();
    QStringList leaks;
    int files = 0;
    for (const QString fmt : {QStringLiteral("tif"), QStringLiteral("png"), QStringLiteral("jpg")}) {
        app_->setSetting(QStringLiteral("exportFormat"), fmt);
        app_->setSetting(QStringLiteral("exportBits"), fmt == QLatin1String("jpg") ? 8 : 0);
        app_->exportImageAreas({id}, true);
        app_->waitIdle(300000);
        for (const QString& f : filesMatching(exp, QStringLiteral("*.") + fmt)) {
            QFile file(f);
            if (!file.open(QIODevice::ReadOnly)) continue;
            const QByteArray bytes = file.readAll();
            ++files;
            if (bytes.contains("Ann Example") || bytes.contains("ann-host-1234"))
                leaks << QFileInfo(f).fileName();
        }
    }
    check(QStringLiteral("FK-004 \"remove identity\": TIFF, PNG and JPEG outputs carry no identity"),
          files == 3 && leaks.isEmpty(),
          QStringLiteral("files %1, leaking: %2").arg(files).arg(leaks.join(',')));
    app_->setSetting(QStringLiteral("privacy"), privacy);
    app_->setSetting(QStringLiteral("exportFormat"), format);
    app_->setSetting(QStringLiteral("exportBits"), bits);
    app_->setSetting(QStringLiteral("exportFolder"), folder);
    app_->newProject();
}

void SelfTest::runCountSetting() {
    const QString d = QDir(dir_).filePath(QStringLiteral("count"));
    QDir(d).removeRecursively();
    QDir().mkpath(d);
    auto hasWarning = [](const QVariantMap& scan, const QString& prefix) {
        const QStringList w = scan.value("warnings").toStringList();
        return std::any_of(w.begin(), w.end(), [&](const QString& x) { return x.startsWith(prefix); });
    };
    auto scanText = [](const QVariantMap& scan) {
        return QStringLiteral("auto %1, expected %2, found %3, warnings [%4]")
            .arg(scan.value("autoCount").toBool())
            .arg(scan.value("expectedCount").toInt())
            .arg(scan.value("itemCount").toInt())
            .arg(scan.value("warnings").toStringList().join(QStringLiteral("; ")));
    };
    // a modal dialog (also while its closing transition runs) would take the mouse input
    QMetaObject::invokeMethod(window_, "closeDialogs");
    waitFor([] { return false; }, 600);
    snapshot(QStringLiteral("U011-before"));
    QQuickItem* autoCheck = findItem(QStringLiteral("autoCountCheck"));
    QQuickItem* spin = findItem(QStringLiteral("countSpin"));
    auto uiText = [&] {
        return QStringLiteral("check %1, number %2 (%3)")
            .arg(autoCheck ? autoCheck->property("checked").toBool() : false)
            .arg(spin ? spin->property("value").toInt() : -1)
            .arg(spin && spin->isEnabled() ? QStringLiteral("enabled") : QStringLiteral("greyed out"));
    };
    check(QStringLiteral("UX-01 left rail has the count switch and number"), autoCheck && spin);
    if (!autoCheck || !spin) return;
    const bool defaultAuto = app_->settings().value("autoCount").toBool();
    const int defaultCount = app_->settings().value("expectedCount").toInt();

    // the default: new scans are counted automatically; no warning made from the default count of 4
    app_->newProject();
    QCoreApplication::processEvents();
    QObject* canvas = findCanvas();
    check(QStringLiteral("new project: the canvas no longer shows the previous scan"),
          canvas && !canvas->property("hasImage").toBool());
    app_->importScans({scan_});
    app_->waitIdle(300000);
    QCoreApplication::processEvents();
    const QString multiId = app_->currentScanId();
    const int found = app_->currentScan().value("itemCount").toInt();
    check(QStringLiteral("UX-01 new scans are counted automatically by default"),
          defaultAuto && app_->currentScan().value("autoCount").toBool() &&
              !hasWarning(app_->currentScan(), QStringLiteral("count_mismatch")),
          scanText(app_->currentScan()));
    check(QStringLiteral("UX-01 automatic: number greyed out, showing the prints found"),
          autoCheck->property("checked").toBool() && !spin->isEnabled() &&
              spin->property("value").toInt() == found,
          uiText());

    // a mixed batch: the same scan with one print, and a blank scan
    std::vector<fk::Shape> outers;
    for (const auto& sc : app_->project().scans)
        for (const auto& it : sc.items)
            outers.push_back(it.outer);
    const QString single = QDir(d).filePath(QStringLiteral("single.tif"));
    const QString blank = QDir(d).filePath(QStringLiteral("blank.tif"));
    const bool made = writeCountScans(scan_, outers, single, blank);
    check(QStringLiteral("UX-01 setup: single-print and blank scans"), made);
    if (!made) return;
    app_->importScans({single, blank});
    app_->waitIdle(300000);
    QString singleId, blankId;
    for (const auto& sc : app_->project().scans) {
        if (QFileInfo(toQ(sc.path)).fileName() == QLatin1String("single.tif")) singleId = toQ(sc.scanId);
        if (QFileInfo(toQ(sc.path)).fileName() == QLatin1String("blank.tif")) blankId = toQ(sc.scanId);
    }
    auto scanOf = [&](const QString& id) {
        app_->selectScan(id);
        QCoreApplication::processEvents();
        return app_->currentScan();
    };
    const QVariantMap s1 = scanOf(singleId), s0 = scanOf(blankId);
    check(QStringLiteral("UX-01 automatic: a single print, no count warning"),
          s1.value("itemCount").toInt() == 1 && !hasWarning(s1, QStringLiteral("count_mismatch")) &&
              !hasWarning(s1, QStringLiteral("possible_missed")),
          scanText(s1));
    check(QStringLiteral("UX-01 automatic: a blank scan says no prints were found, no count warning"),
          s0.value("itemCount").toInt() == 0 && hasWarning(s0, QStringLiteral("no_prints_found")) &&
              !hasWarning(s0, QStringLiteral("count_mismatch")),
          scanText(s0));
    check(QStringLiteral("UX-01 blank scan: number shows 0 found"),
          !spin->isEnabled() && spin->property("value").toInt() == 0, uiText());

    // per-scan override with the mouse: the blank scan gets the kept manual count, the warning appears at
    // once and only on that scan
    const bool clicked = click(autoCheck);
    const QVariantMap s0m = app_->currentScan();
    check(QStringLiteral("UX-01 switch off (mouse): manual count, warning at once without detecting"),
          clicked && !s0m.value("autoCount").toBool() && s0m.value("expectedCount").toInt() == defaultCount &&
              hasWarning(s0m, QStringLiteral("count_mismatch")) && spin->isEnabled() &&
              spin->property("value").toInt() == defaultCount && app_->counts().value("scans").toInt() == 3,
          scanText(s0m) + QStringLiteral(" · ") + uiText());
    check(QStringLiteral("UX-01 the other scans keep their own setting"),
          scanOf(multiId).value("autoCount").toBool() && scanOf(singleId).value("autoCount").toBool() &&
              !hasWarning(scanOf(multiId), QStringLiteral("count_mismatch")),
          scanText(scanOf(multiId)));

    // the multi-print scan: manual, then the number corrected with the − / + buttons
    scanOf(multiId);
    click(autoCheck);
    const bool mismatchAtDefault = hasWarning(app_->currentScan(), QStringLiteral("count_mismatch"));
    check(QStringLiteral("UX-01 manual count differing from the prints found is flagged"),
          mismatchAtDefault == (found != defaultCount), scanText(app_->currentScan()));
    auto indicator = [&](const char* which) -> QQuickItem* {
        QObject* button = spin->property(which).value<QObject*>();
        return button ? button->property("indicator").value<QQuickItem*>() : nullptr;
    };
    QQuickItem* down = indicator("down");
    QQuickItem* up = indicator("up");
    for (int i = 0; i < 12 && spin->property("value").toInt() != found; ++i)
        click(spin->property("value").toInt() > found ? down : up);
    QCoreApplication::processEvents();
    check(QStringLiteral("UX-01 corrected manual count (mouse): the warning goes"),
          app_->currentScan().value("expectedCount").toInt() == found &&
              !hasWarning(app_->currentScan(), QStringLiteral("count_mismatch")),
          scanText(app_->currentScan()) + QStringLiteral(" · ") + uiText());
    click(up); // one more than found: flagged again
    check(QStringLiteral("UX-01 manual count changed again: the warning follows"),
          hasWarning(app_->currentScan(),
                     QStringLiteral("count_mismatch: found %1, expected %2").arg(found).arg(found + 1)),
          scanText(app_->currentScan()));
    click(autoCheck); // automatic again
    check(QStringLiteral("UX-01 switch on (mouse): no count warning, the manual value is kept"),
          app_->currentScan().value("autoCount").toBool() &&
              !hasWarning(app_->currentScan(), QStringLiteral("count_mismatch")) &&
              app_->currentScan().value("expectedCount").toInt() == found + 1 && !spin->isEnabled() &&
              spin->property("value").toInt() == found,
          scanText(app_->currentScan()) + QStringLiteral(" · ") + uiText());
    click(autoCheck); // manual again: the kept value comes back
    check(QStringLiteral("UX-01 switch off again: the last manual value is back"),
          spin->property("value").toInt() == found + 1 &&
              hasWarning(app_->currentScan(), QStringLiteral("count_mismatch")),
          uiText());
    snapshot(QStringLiteral("U011-manual-count"));

    // saved and reopened: every scan keeps its own setting and warning
    const QString proj = QDir(d).filePath(QStringLiteral("count.fkproj"));
    const bool saved = app_->saveProjectAs(proj);
    app_->newProject();
    const bool opened = app_->openProject(QUrl::fromLocalFile(proj));
    app_->waitIdle(300000);
    const QVariantMap m = scanOf(multiId), one = scanOf(singleId), none = scanOf(blankId);
    check(QStringLiteral("UX-01 saved and reopened: each scan keeps its count setting"),
          saved && opened && !m.value("autoCount").toBool() &&
              m.value("expectedCount").toInt() == found + 1 &&
              hasWarning(m, QStringLiteral("count_mismatch")) && one.value("autoCount").toBool() &&
              !hasWarning(one, QStringLiteral("count_mismatch")) && !none.value("autoCount").toBool() &&
              none.value("expectedCount").toInt() == defaultCount &&
              hasWarning(none, QStringLiteral("count_mismatch")),
          scanText(m) + QStringLiteral(" | ") + scanText(one) + QStringLiteral(" | ") + scanText(none));
    scanOf(blankId);
    check(QStringLiteral("UX-01 reopened: the controls show the scan's setting"),
          !autoCheck->property("checked").toBool() && spin->isEnabled() &&
              spin->property("value").toInt() == defaultCount,
          uiText());

    // the global default switched off: new scans get the default manual count
    app_->setSetting(QStringLiteral("autoCount"), false);
    app_->setSetting(QStringLiteral("expectedCount"), 2);
    const QString single2 = QDir(d).filePath(QStringLiteral("single-2.tif"));
    QFile::copy(single, single2);
    app_->importScans({single2});
    app_->waitIdle(300000);
    QVariantMap s2;
    for (const auto& sc : app_->project().scans)
        if (QFileInfo(toQ(sc.path)).fileName() == QLatin1String("single-2.tif")) s2 = scanOf(toQ(sc.scanId));
    check(QStringLiteral("UX-01 default off: new scans use the default manual count"),
          !s2.value("autoCount").toBool() && s2.value("expectedCount").toInt() == 2 &&
              hasWarning(s2, QStringLiteral("count_mismatch: found 1, expected 2")),
          scanText(s2));
    app_->setSetting(QStringLiteral("autoCount"), defaultAuto);
    app_->setSetting(QStringLiteral("expectedCount"), defaultCount);
    app_->newProject();
}

namespace {
double rightAngleError(const fk::Quad& q) {
    double worst = 0;
    for (int i = 0; i < 4; ++i)
        worst = std::max(worst, std::abs(fk::dot(fk::normalized(q.p[(i + 1) % 4] - q.p[i]),
                                                 fk::normalized(q.p[(i + 3) % 4] - q.p[i]))));
    return worst;
}
double sideRatio(const fk::Quad& q) {
    const auto s = fk::quadSideLengths(q);
    return s[0] / s[1];
}
fk::Quad tiltedRect(fk::Vec2 c, double w, double h, double angle) {
    const fk::Mat3 m = fk::Mat3::translate(c.x, c.y) * fk::Mat3::rotate(angle);
    return fk::normalizeQuad({m.apply({-w / 2, -h / 2}), m.apply({w / 2, -h / 2}), m.apply({w / 2, h / 2}),
                              m.apply({-w / 2, h / 2})});
}
QString quadText(const fk::Quad& q) {
    return QStringLiteral("right-angle error %1, ratio %2, angle %3°")
        .arg(rightAngleError(q), 0, 'g', 3)
        .arg(sideRatio(q), 0, 'f', 6)
        .arg(fk::frameAngle(q) * 180 / M_PI, 0, 'f', 4);
}
} // namespace

void SelfTest::runShapeEditing() {
    QMetaObject::invokeMethod(window_, "closeDialogs");
    waitFor([] { return false; }, 600);
    const QString d = QDir(dir_).filePath(QStringLiteral("shapes"));
    QDir(d).removeRecursively();
    QDir().mkpath(d);
    const bool shiftDefault = app_->settings().value("shiftSwitchesBoundary").toBool();
    check(QStringLiteral("UX-03 Shift switching is on by default"), shiftDefault);
    app_->setSetting(QStringLiteral("shiftSwitchesBoundary"), true);
    app_->newProject();
    app_->importScans({scan_});
    app_->waitIdle(300000);
    QCoreApplication::processEvents();
    auto* canvas = qobject_cast<QQuickItem*>(findCanvas());
    const QString itemId = app_->currentItemId();
    check(QStringLiteral("UX-03 setup: a print to edit"), canvas && !itemId.isEmpty());
    if (!canvas || itemId.isEmpty()) return;
    const std::string sid = itemId.toStdString();
    auto rec = [&]() -> const fk::ItemRecord* {
        for (const auto& sc : app_->project().scans)
            for (const auto& it : sc.items)
                if (it.itemId == sid) return &it;
        return nullptr;
    };
    fk::SizeI scanSize{};
    for (const auto& sc : app_->project().scans)
        for (const auto& it : sc.items)
            if (it.itemId == sid) scanSize = {sc.info.width, sc.info.height};
    canvas->setProperty("mode", QStringLiteral("scan"));
    canvas->setProperty("tool", QStringLiteral("select"));
    QMetaObject::invokeMethod(canvas, "fit");
    window_->requestActivate();
    waitFor([&] { return window_->isActive(); }, 2000);
    canvas->forceActiveFocus();
    QCoreApplication::processEvents();

    auto target = [&] { return canvas->property("editTarget").toString(); };
    QQuickItem* badge = findItem(QStringLiteral("editTargetBadge"));
    auto badgeText = [&] {
        return badge && badge->isVisible() ? badge->property("text").toString()
                                           : QStringLiteral("(not shown)");
    };
    auto state = [&] {
        return QStringLiteral("tool %1, editing %2, canvas says \"%3\"")
            .arg(canvas->property("tool").toString(), target(), badgeText());
    };
    auto tapShift = [&] {
        key(Qt::Key_Shift, Qt::ShiftModifier, true);
        key(Qt::Key_Shift, Qt::NoModifier, false);
    };
    auto at = [&](fk::Vec2 p) {
        QPointF r;
        QMetaObject::invokeMethod(canvas, "mapFromScan", Q_RETURN_ARG(QPointF, r), Q_ARG(double, p.x),
                                  Q_ARG(double, p.y));
        return r;
    };
    // a drag in screen steps: press at `from`, move by `by` in 4 steps, release (mods per phase)
    auto drag = [&](QPointF from, QPointF by, Qt::KeyboardModifiers mods) {
        mouse(canvas, from, QEvent::MouseButtonPress, mods);
        for (int k = 1; k <= 4; ++k)
            mouse(canvas, from + by * (k / 4.0), QEvent::MouseMove, mods);
        mouse(canvas, from + by, QEvent::MouseButtonRelease, mods);
    };
    const QPointF empty(4, 4); // canvas corner: outside the fitted scan

    // ---- the current mode is always stated; a lone Shift tap switches it
    const QString outerText = badgeText();
    check(QStringLiteral("UX-03 the canvas states which boundary is edited"),
          target() == QLatin1String("outer") && badge && badge->isVisible(), state());
    tapShift();
    const QString innerText = badgeText();
    check(QStringLiteral("UX-03 Shift tap (real key events): Outer → Inner, shown on the canvas"),
          target() == QLatin1String("inner") && innerText != outerText && badge->isVisible(), state());
    tapShift();
    check(QStringLiteral("UX-03 Shift tap again: Inner → Outer"),
          target() == QLatin1String("outer") && badgeText() == outerText, state());

    // ---- Shift used with something else never switches
    key(Qt::Key_Shift, Qt::ShiftModifier, true);
    mouse(canvas, empty, QEvent::MouseButtonPress, Qt::ShiftModifier);
    mouse(canvas, empty, QEvent::MouseButtonRelease, Qt::ShiftModifier);
    key(Qt::Key_Shift, Qt::NoModifier, false);
    check(QStringLiteral("UX-03 Shift+click does not switch"), target() == QLatin1String("outer"), state());
    key(Qt::Key_Shift, Qt::ShiftModifier, true);
    key(Qt::Key_A, Qt::ShiftModifier, true, QStringLiteral("A"));
    key(Qt::Key_A, Qt::ShiftModifier, false, QStringLiteral("A"));
    key(Qt::Key_Shift, Qt::NoModifier, false);
    check(QStringLiteral("UX-03 Shift+letter does not switch"), target() == QLatin1String("outer"), state());
    key(Qt::Key_Shift, Qt::ShiftModifier, true);
    key(Qt::Key_Right, Qt::ShiftModifier, true); // no handle selected: the "next scan" shortcut
    key(Qt::Key_Right, Qt::ShiftModifier, false);
    key(Qt::Key_Shift, Qt::NoModifier, false);
    check(QStringLiteral("UX-03 Shift+→ (next scan shortcut) does not switch"),
          target() == QLatin1String("outer"), state());
    key(Qt::Key_Shift, Qt::ShiftModifier, true);
    {
        const QPointF local = canvas->mapToScene(QPointF(canvas->width() / 2, canvas->height() / 2));
        QWheelEvent w(local, canvas->mapToGlobal(QPointF(canvas->width() / 2, canvas->height() / 2)),
                      QPoint(), QPoint(0, 120), Qt::NoButton, Qt::ShiftModifier, Qt::NoScrollPhase, false);
        QCoreApplication::sendEvent(window_, &w);
    }
    key(Qt::Key_Shift, Qt::NoModifier, false);
    check(QStringLiteral("UX-03 Shift+wheel does not switch"), target() == QLatin1String("outer"), state());
    QMetaObject::invokeMethod(canvas, "fit");
    key(Qt::Key_Shift, Qt::ShiftModifier, true);
    waitFor([] { return false; }, 1200);
    key(Qt::Key_Shift, Qt::NoModifier, false);
    check(QStringLiteral("UX-03 Shift held over a second is not a tap"), target() == QLatin1String("outer"),
          state());
    key(Qt::Key_Shift, Qt::ShiftModifier, true);
    {
        QFocusEvent out(QEvent::FocusOut, Qt::ActiveWindowFocusReason);
        QCoreApplication::sendEvent(window_, &out);
        QFocusEvent in(QEvent::FocusIn, Qt::ActiveWindowFocusReason);
        QCoreApplication::sendEvent(window_, &in);
    }
    key(Qt::Key_Shift, Qt::NoModifier, false);
    check(QStringLiteral("UX-03 window losing focus while Shift is down: no switch"),
          target() == QLatin1String("outer"), state());
    canvas->forceActiveFocus();
    // typing: a text field has the focus
    QQuickItem* field = findItemIf([](QQuickItem* it) {
        return it->isVisible() && it->isEnabled() &&
               it->metaObject()->indexOfProperty("cursorPosition") >= 0 &&
               it->metaObject()->indexOfProperty("readOnly") >= 0 && !it->property("readOnly").toBool();
    });
    if (field) {
        field->forceActiveFocus();
        QCoreApplication::processEvents();
        const bool focused = window_->activeFocusItem() == field;
        tapShift();
        check(QStringLiteral("UX-03 Shift tap while typing in a text field: no switch"),
              focused && target() == QLatin1String("outer"),
              state() +
                  QStringLiteral(", focus in %1").arg(QString::fromLatin1(field->metaObject()->className())));
        canvas->forceActiveFocus();
    } else {
        skip(QStringLiteral("UX-03 Shift tap while typing in a text field: no switch"),
             QStringLiteral("no editable text field visible in this layout"));
    }
    // a dialog is open
    QMetaObject::invokeMethod(window_, "showSettings");
    const bool dlg = waitFor(
        [&] {
            QVariant r;
            QMetaObject::invokeMethod(window_, "dialogOpen", Q_RETURN_ARG(QVariant, r));
            return r.toBool();
        },
        3000);
    tapShift();
    check(QStringLiteral("UX-03 Shift tap with a dialog open: no switch"),
          dlg && target() == QLatin1String("outer"), state());
    QMetaObject::invokeMethod(window_, "closeDialogs");
    waitFor([] { return false; }, 600);
    canvas->forceActiveFocus();
    // switched off in Settings
    app_->setSetting(QStringLiteral("shiftSwitchesBoundary"), false);
    QCoreApplication::processEvents();
    tapShift();
    check(QStringLiteral("UX-03 Shift switching turned off: a tap does nothing"),
          target() == QLatin1String("outer"), state());
    app_->setSetting(QStringLiteral("shiftSwitchesBoundary"), true);
    canvas->setProperty("tool", QStringLiteral("outer"));

    // ---- rectangle: Shift-drag a corner keeps the rectangle about the opposite corner, one undo step
    const fk::RectD b = fk::boundsOf(rec()->outer);
    const fk::Quad rect = fk::rectQuad(b.x0, b.y0, b.x1, b.y1);
    app_->commitShape(sid, true, fk::Shape::fromQuad(rect));
    key(Qt::Key_Shift, Qt::ShiftModifier, true);
    drag(at(rect.p[2]), QPointF(40, 25), Qt::ShiftModifier);
    key(Qt::Key_Shift, Qt::NoModifier, false);
    const fk::Quad r1 = rec()->outer.quad;
    check(QStringLiteral("UX-03 Shift-drag does not switch the boundary"), target() == QLatin1String("outer"),
          state());
    check(QStringLiteral(
              "UX-03 rectangle, Shift-drag corner: right angles, same proportions, opposite corner fixed"),
          rightAngleError(r1) < 1e-9 && std::abs(sideRatio(r1) / sideRatio(rect) - 1) < 1e-9 &&
              r1.p[0] == rect.p[0] && std::abs(fk::frameAngle(r1)) < 1e-9 &&
              fk::norm(r1.p[2] - rect.p[2]) > 1,
          quadText(r1));
    app_->undo();
    const bool undone = rec()->outer.quad == rect;
    app_->redo();
    check(QStringLiteral("UX-03 rectangle: one drag = one undo step; redo"),
          undone && rec()->outer.quad == r1);

    // ---- tilted rectangle: rotation kept
    const auto sides = fk::quadSideLengths(rect);
    const double tilt = 3.0 * M_PI / 180;
    const fk::Quad tilted = tiltedRect(fk::centroid(rec()->outer), sides[0] * 0.9, sides[1] * 0.9, tilt);
    app_->commitShape(sid, true, fk::Shape::fromQuad(tilted));
    key(Qt::Key_Shift, Qt::ShiftModifier, true);
    drag(at(tilted.p[1]), QPointF(-30, 20), Qt::ShiftModifier);
    key(Qt::Key_Shift, Qt::NoModifier, false);
    const fk::Quad t1 = rec()->outer.quad;
    check(QStringLiteral(
              "UX-03 tilted rectangle, Shift-drag corner: rotation, right angles and proportions kept"),
          rightAngleError(t1) < 1e-9 && std::abs(sideRatio(t1) / sideRatio(tilted) - 1) < 1e-9 &&
              std::abs(fk::frameAngle(t1) - fk::frameAngle(tilted)) < 1e-9 && t1.p[3] == tilted.p[3] &&
              fk::norm(t1.p[1] - tilted.p[1]) > 1,
          quadText(t1));
    app_->undo();
    const bool tUndone = rec()->outer.quad == tilted;
    app_->redo();
    check(QStringLiteral("UX-03 tilted rectangle: undo / redo"), tUndone && rec()->outer.quad == t1);
    // free corner editing is kept: without Shift only the dragged corner moves
    drag(at(t1.p[0]), QPointF(15, 10), Qt::NoModifier);
    const fk::Quad t2 = rec()->outer.quad;
    check(QStringLiteral("UX-03 drag without Shift: free corner (the other three unchanged)"),
          t2.p[1] == t1.p[1] && t2.p[2] == t1.p[2] && t2.p[3] == t1.p[3] && fk::norm(t2.p[0] - t1.p[0]) > 1);
    // Shift released during the drag: free from that moment, still no switch
    key(Qt::Key_Shift, Qt::ShiftModifier, true);
    const QPointF c3 = at(t2.p[3]);
    mouse(canvas, c3, QEvent::MouseButtonPress, Qt::ShiftModifier);
    mouse(canvas, c3 + QPointF(10, 5), QEvent::MouseMove, Qt::ShiftModifier);
    key(Qt::Key_Shift, Qt::NoModifier, false);
    mouse(canvas, c3 + QPointF(20, 10), QEvent::MouseMove, Qt::NoModifier);
    mouse(canvas, c3 + QPointF(20, 10), QEvent::MouseButtonRelease, Qt::NoModifier);
    const fk::Quad t3 = rec()->outer.quad;
    check(QStringLiteral("UX-03 Shift released mid-drag: free corner from then on, no switch"),
          t3.p[0] == t2.p[0] && t3.p[1] == t2.p[1] && t3.p[2] == t2.p[2] && fk::norm(t3.p[3] - t2.p[3]) > 1 &&
              target() == QLatin1String("outer"),
          state());
    // Shift+→ with the handle selected nudges it by 10 px (not the next scan, not a switch)
    key(Qt::Key_Shift, Qt::ShiftModifier, true);
    key(Qt::Key_Right, Qt::ShiftModifier, true);
    key(Qt::Key_Right, Qt::ShiftModifier, false);
    key(Qt::Key_Shift, Qt::NoModifier, false);
    const fk::Quad t4 = rec()->outer.quad;
    check(QStringLiteral("UX-03 Shift+→ on a selected corner: 10 px nudge, no switch"),
          std::abs(t4.p[3].x - t3.p[3].x - 10) < 1e-9 && t4.p[3].y == t3.p[3].y && t4.p[0] == t3.p[0] &&
              target() == QLatin1String("outer"),
          QStringLiteral("moved %1, %2 · ").arg(t4.p[3].x - t3.p[3].x).arg(t4.p[3].y - t3.p[3].y) + state());
    // Shift pressed only during the drag: the constraint applies at once; releasing Shift afterwards is no
    // tap
    const QPointF c2 = at(t4.p[2]);
    mouse(canvas, c2, QEvent::MouseButtonPress, Qt::NoModifier);
    mouse(canvas, c2 + QPointF(10, 6), QEvent::MouseMove, Qt::NoModifier);
    key(Qt::Key_Shift, Qt::ShiftModifier, true);
    mouse(canvas, c2 + QPointF(30, 20), QEvent::MouseMove, Qt::ShiftModifier);
    mouse(canvas, c2 + QPointF(30, 20), QEvent::MouseButtonRelease, Qt::ShiftModifier);
    key(Qt::Key_Shift, Qt::NoModifier, false);
    const fk::Quad t5 = rec()->outer.quad;
    check(QStringLiteral(
              "UX-03 Shift pressed during a drag: rectangle kept; releasing it after the drag: no switch"),
          rightAngleError(t5) < 1e-9 && t5.p[0] == t4.p[0] && target() == QLatin1String("outer"),
          quadText(t5) + QStringLiteral(" · ") + state());
    // the window loses focus mid-drag: the drag is dropped, nothing changes
    mouse(canvas, at(t5.p[2]), QEvent::MouseButtonPress, Qt::NoModifier);
    mouse(canvas, at(t5.p[2]) + QPointF(25, 15), QEvent::MouseMove, Qt::NoModifier);
    {
        QFocusEvent out(QEvent::FocusOut, Qt::ActiveWindowFocusReason);
        QCoreApplication::sendEvent(window_, &out);
    }
    mouse(canvas, at(t5.p[2]) + QPointF(25, 15), QEvent::MouseButtonRelease, Qt::NoModifier);
    {
        QFocusEvent in(QEvent::FocusIn, Qt::ActiveWindowFocusReason);
        QCoreApplication::sendEvent(window_, &in);
    }
    canvas->forceActiveFocus();
    app_->undo();
    const bool lastStepWasTheDrag = rec()->outer.quad == t4;
    app_->redo();
    check(QStringLiteral("UX-03 focus lost mid-drag: the drag is dropped (no change, no undo step)"),
          rec()->outer.quad == t5 && lastStepWasTheDrag);

    // ---- circle: switched with the tool button, stays round whatever is dragged or nudged
    app_->setInnerKind(itemId, QStringLiteral("ellipse"));
    const fk::Ellipse e0 = rec()->inner.ellipse;
    const bool toolBtn = click(findItem(QStringLiteral("toolInner")));
    check(QStringLiteral("UX-03 tool button (mouse): Edit Inner"),
          toolBtn && target() == QLatin1String("inner") && rec()->inner.kind == fk::ShapeKind::ellipse &&
              fk::isCircle(e0),
          state());
    auto circleText = [&] {
        const fk::Ellipse& e = rec()->inner.ellipse;
        return QStringLiteral("rx %1, ry %2, centre %3, %4")
            .arg(e.rx, 0, 'f', 4)
            .arg(e.ry, 0, 'f', 4)
            .arg(e.c.x)
            .arg(e.c.y);
    };
    canvas->forceActiveFocus();
    drag(at(fk::ellipseHandle(e0, true)), QPointF(30, 45), Qt::NoModifier);
    const fk::Ellipse e1 = rec()->inner.ellipse;
    check(QStringLiteral("UX-03 circle: radius drag (diagonal pointer) keeps rx = ry, centre fixed"),
          e1.rx == e1.ry && e1.rx != e0.rx && e1.c == e0.c, circleText());
    key(Qt::Key_Shift, Qt::ShiftModifier, true);
    drag(at(fk::ellipseHandle(e1, false)), QPointF(-20, 10), Qt::ShiftModifier);
    key(Qt::Key_Shift, Qt::NoModifier, false);
    const fk::Ellipse e2 = rec()->inner.ellipse;
    check(QStringLiteral("UX-03 circle: Shift-drag keeps rx = ry, no switch"),
          e2.rx == e2.ry && e2.rx != e1.rx && target() == QLatin1String("inner"), circleText());
    key(Qt::Key_Up, Qt::NoModifier, true);
    key(Qt::Key_Up, Qt::NoModifier, false);
    key(Qt::Key_Right, Qt::NoModifier, true); // across the ry axis: nothing changes
    key(Qt::Key_Right, Qt::NoModifier, false);
    const fk::Ellipse e3 = rec()->inner.ellipse;
    check(QStringLiteral("UX-03 circle: arrow nudges keep rx = ry"),
          e3.rx == e3.ry && std::abs(e3.rx - (e2.rx - 1)) < 1e-9, circleText());
    app_->undo(); // the Up nudge (the Right nudge changed nothing and made no step)
    const bool cu1 = rec()->inner.ellipse == e2;
    app_->undo();
    const bool cu2 = rec()->inner.ellipse == e1;
    app_->redo();
    app_->redo();
    check(QStringLiteral("UX-03 circle: undo / redo step by step, always round"),
          cu1 && cu2 && rec()->inner.ellipse == e3 && fk::isCircle(rec()->inner.ellipse), circleText());
    key(Qt::Key_Shift, Qt::ShiftModifier, true);
    drag(at(e3.c), QPointF(30, 8), Qt::ShiftModifier);
    key(Qt::Key_Shift, Qt::NoModifier, false);
    const fk::Ellipse e4 = rec()->inner.ellipse;
    check(QStringLiteral("UX-03 Shift-drag of the centre moves along one axis"),
          e4.c.y == e3.c.y && e4.c.x > e3.c.x && e4.rx == e3.rx && e4.ry == e3.ry, circleText());

    // ---- the menu does the same (Windows/Linux in-window menu bar; the macOS menu bar is native)
    if (window_->property("isMac").toBool()) {
        skip(QStringLiteral("UX-03 menu: Print ▸ Switch Outer ↔ Inner (mouse)"),
             QStringLiteral("native macOS menu bar: not reachable offscreen"));
    } else {
        QQuickItem* printMenu = findItemIf([](QQuickItem* it) {
            QObject* m = it->property("menu").value<QObject*>();
            return m && m->objectName() == QLatin1String("menuPrint");
        });
        click(printMenu);
        waitFor(
            [&] {
                QQuickItem* i = findItem(QStringLiteral("menuSwitchBoundary"));
                return i && i->isVisible();
            },
            2000);
        QQuickItem* item = findItem(QStringLiteral("menuSwitchBoundary"));
        const bool clicked = item && item->isVisible() && click(item);
        waitFor([] { return false; }, 300);
        check(QStringLiteral("UX-03 menu: Print ▸ Switch Outer ↔ Inner (mouse)"),
              clicked && target() == QLatin1String("outer"), state());
    }

    // ---- saved and reopened: the same geometry; the export uses it
    const fk::Shape outerSaved = rec()->outer, innerSaved = rec()->inner;
    const QString proj = QDir(d).filePath(QStringLiteral("shapes.fkproj"));
    const bool saved = app_->saveProjectAs(proj);
    app_->newProject();
    const bool opened = app_->openProject(QUrl::fromLocalFile(proj));
    app_->waitIdle(300000);
    const fk::ItemRecord* back = rec();
    check(QStringLiteral("UX-03 saved and reopened: outer and round inner unchanged, still a circle"),
          saved && opened && back && back->outer == outerSaved && back->inner == innerSaved &&
              back->inner.ellipse.rx == back->inner.ellipse.ry);
    if (back) {
        const QString exportDir = app_->settings().value("exportFolder").toString();
        const QStringList before = filesMatching(exportDir, QStringLiteral("*_EDIT_v*.tif"));
        app_->setSetting(QStringLiteral("geometryMode"), QStringLiteral("crop"));
        app_->exportImageAreas({itemId}, false);
        app_->waitIdle(300000);
        QStringList after = filesMatching(exportDir, QStringLiteral("*_EDIT_v*.tif"));
        for (const auto& f : before)
            after.removeAll(f);
        fk::ImageInfo info;
        bool read = false;
        if (after.size() == 1) {
            auto img = fk::readImageFile(toPath(after.first()), &info);
            read = static_cast<bool>(img);
        }
        const fk::RectI want = fk::boundingRect(back->inner, scanSize);
        check(
            QStringLiteral("UX-03 the export uses the edited circle (pixel crop = its bounding box)"),
            read && info.width == want.w && info.height == want.h,
            QStringLiteral("%1×%2, expected %3×%4").arg(info.width).arg(info.height).arg(want.w).arg(want.h));
    }
    snapshot(QStringLiteral("U013-shapes"));
    app_->setSetting(QStringLiteral("shiftSwitchesBoundary"), shiftDefault);
    app_->newProject();
}

void SelfTest::runScanRotation() {
    QMetaObject::invokeMethod(window_, "closeDialogs");
    waitFor([] { return false; }, 600);
    const QString d = QDir(dir_).filePath(QStringLiteral("rotation"));
    QDir(d).removeRecursively();
    QDir().mkpath(d);
    const QVariantMap saved = app_->settings();
    const QString exportDir = QDir(d).filePath(QStringLiteral("export"));
    app_->setSetting(QStringLiteral("exportFolder"), exportDir);
    app_->setSetting(QStringLiteral("exportFormat"), QStringLiteral("tif"));
    app_->setSetting(QStringLiteral("geometryMode"), QStringLiteral("crop"));
    app_->setSetting(QStringLiteral("finalMode"), QStringLiteral("real"));
    app_->setSetting(QStringLiteral("finalFormat"), QStringLiteral("tif"));
    app_->setSetting(QStringLiteral("autoCount"), true);
    auto restoreSettings = [&] {
        for (const char* k : {"exportFolder", "exportFormat", "geometryMode", "finalMode", "finalFormat",
                              "autoCount", "applyUpright"})
            app_->setSetting(QString::fromLatin1(k), saved.value(QString::fromLatin1(k)));
    };

    // the test scan: the first print of this scan four times, turned 0°, 90°, 180°, 270°
    app_->newProject();
    app_->importScans({scan_});
    app_->waitIdle(300000);
    fk::Shape first;
    if (!app_->project().scans.empty() && !app_->project().scans.front().items.empty())
        first = app_->project().scans.front().items.front().outer;
    const QString four = QDir(d).filePath(QStringLiteral("four-orientations.tif"));
    const bool made = !app_->project().scans.empty() && writeFourOrientations(scan_, first, four);
    check(QStringLiteral("UX-04 setup: a scan with four prints turned 0/90/180/270°"), made);
    if (!made) {
        restoreSettings();
        return;
    }
    const auto sourceSha = fk::sha256File(toPath(four));
    app_->newProject();
    app_->importScans({four});
    app_->waitIdle(300000);
    QCoreApplication::processEvents();
    const QString scanId = app_->currentScanId();
    auto scan = [&]() -> const fk::ScanRecord* {
        for (const auto& sc : app_->project().scans)
            if (toQ(sc.scanId) == scanId) return &sc;
        return nullptr;
    };
    auto shapes = [&] {
        std::vector<std::pair<fk::Shape, fk::Shape>> v;
        if (const auto* sc = scan())
            for (const auto& it : sc->items)
                v.emplace_back(it.outer, it.inner);
        return v;
    };
    const auto start = shapes();
    std::set<int> uprights;
    if (const auto* sc = scan())
        for (const auto& it : sc->items)
            uprights.insert(it.upright);
    check(QStringLiteral("UX-04 four prints found, in four orientations"),
          start.size() == 4 && uprights.size() == 4,
          QStringLiteral("%1 prints, %2 different uprights").arg(start.size()).arg(uprights.size()));
    if (start.size() != 4) {
        restoreSettings();
        return;
    }
    auto* canvas = qobject_cast<QQuickItem*>(findCanvas());
    canvas->setProperty("mode", QStringLiteral("scan"));
    canvas->setProperty("tool", QStringLiteral("select"));
    QMetaObject::invokeMethod(canvas, "fit");
    QCoreApplication::processEvents();
    const QString thumb0 = QStringLiteral("scan/") + scanId;
    const QImage t0 = app_->thumbnail(thumb0);
    const int64_t W = scan()->info.width, H = scan()->info.height;

    // where a scan point must appear on screen with the view turned k times, computed here independently of
    // the canvas: turn the scan, fit it with 16 px padding, centre it
    auto expected = [&](fk::Vec2 p, int k) {
        const double dw = (k & 1) ? static_cast<double>(H) : static_cast<double>(W);
        const double dh = (k & 1) ? static_cast<double>(W) : static_cast<double>(H);
        fk::Vec2 q = p;
        for (int i = 0; i < k; ++i) // one turn clockwise of a w×h image: (x, y) → (h − y, x)
            q = {((i & 1) ? static_cast<double>(W) : static_cast<double>(H)) - q.y, q.x};
        const double sc = std::min((canvas->width() - 32) / dw, (canvas->height() - 32) / dh);
        return QPointF(canvas->width() / 2 + (q.x - dw / 2) * sc, canvas->height() / 2 + (q.y - dh / 2) * sc);
    };
    // every print is where it must be: a click there selects exactly that print
    auto clickEach = [&](int k) {
        QStringList wrong;
        if (const auto* sc = scan())
            for (const auto& it : sc->items) {
                const QPointF at = expected(fk::centroid(it.inner), k);
                click(canvas, at);
                if (app_->currentItemId() != toQ(it.itemId))
                    wrong << QStringLiteral("%1 → %2").arg(toQ(it.label), app_->currentItemId());
            }
        return wrong;
    };
    const QStringList wrong0 = clickEach(0);
    check(QStringLiteral("UX-04 before turning: a click on each print selects it"), wrong0.isEmpty(),
          wrong0.join(QStringLiteral(", ")));
    QImage before; // after the clicks: the same print is current when the view comes back
    QMetaObject::invokeMethod(canvas, "grab", Q_RETURN_ARG(QImage, before));

    // turn clockwise with the left-rail button (mouse)
    const bool clicked = click(findItem(QStringLiteral("scanRotateRight")));
    QMetaObject::invokeMethod(canvas, "fit");
    QCoreApplication::processEvents();
    const QStringList wrong1 = clickEach(1);
    check(QStringLiteral(
              "UX-04 whole scan turned 90° (left-rail button): every print follows, count and boundaries "
              "unchanged"),
          clicked && scan()->rotation == 1 && shapes() == start && wrong1.isEmpty() &&
              app_->counts().value("prints").toInt() == 4,
          QStringLiteral("rotation %1°, wrong: %2")
              .arg(scan()->rotation * 90)
              .arg(wrong1.join(QStringLiteral(", "))));
    const QImage t1 = app_->thumbnail(thumb0);
    check(QStringLiteral("UX-04 the scan thumbnail turns with it (lossless)"),
          t0.width() != t0.height() && t1 == t0.transformed(QTransform().rotate(90)),
          QStringLiteral("%1×%2 → %3×%4").arg(t0.width()).arg(t0.height()).arg(t1.width()).arg(t1.height()));
    snapshot(QStringLiteral("U014-turned-90"));
    // turned and zoomed to 100 %: the full-resolution patch arrives for the turned view, the navigator shows
    // a part
    QMetaObject::invokeMethod(canvas, "zoomTo", Q_ARG(double, 100.0));
    const bool full =
        waitFor([&] { return canvas->property("badge").toString() == QLatin1String("Full"); }, 20000);
    const QRectF nav = canvas->property("navigatorView").toRectF();
    check(QStringLiteral("UX-04 turned view at 100 %: full resolution, navigator shows the visible part"),
          full && nav.width() < 0.999 && nav.height() < 0.999 && nav.width() > 0 && nav.height() > 0,
          QStringLiteral("badge %1, navigator %2 × %3")
              .arg(canvas->property("badge").toString())
              .arg(nav.width(), 0, 'f', 3)
              .arg(nav.height(), 0, 'f', 3));
    snapshot(QStringLiteral("U014-turned-100pct"));
    QMetaObject::invokeMethod(canvas, "fit");
    // three more quarter turns (menu on Windows/Linux, else the controller): back to the start, pixel for
    // pixel
    QQuickItem* printMenu = findItemIf([](QQuickItem* it) {
        QObject* m = it->property("menu").value<QObject*>();
        return m && m->objectName() == QLatin1String("menuPrint");
    });
    int viaMenu = 0;
    for (int i = 0; i < 3; ++i) {
        bool done = false;
        if (printMenu && !window_->property("isMac").toBool()) {
            click(printMenu);
            waitFor(
                [&] {
                    QQuickItem* m = findItem(QStringLiteral("menuRotateScanRight"));
                    return m && m->isVisible();
                },
                2000);
            QQuickItem* m = findItem(QStringLiteral("menuRotateScanRight"));
            const int r0 = scan()->rotation;
            if (m && m->isVisible() && click(m)) done = scan()->rotation == (r0 + 1) % 4;
            waitFor([] { return false; }, 300);
        }
        if (done)
            ++viaMenu;
        else
            app_->rotateScan(scanId, 1);
    }
    QMetaObject::invokeMethod(canvas, "fit");
    QCoreApplication::processEvents();
    QImage after4;
    QMetaObject::invokeMethod(canvas, "grab", Q_RETURN_ARG(QImage, after4));
    check(QStringLiteral("UX-04 four turns: back to the start (view pixel-identical, boundaries unchanged)"),
          scan()->rotation == 0 && shapes() == start && after4 == before,
          QStringLiteral("%1 of 3 turns through the Print menu").arg(viaMenu));

    // one undo restores the whole scan; it interleaves with a print's own edits
    app_->rotateScan(scanId, 1);
    canvas->forceActiveFocus();
    const bool mac = window_->property("isMac").toBool();
    if (mac) {
        app_->undo(); // macOS: Undo's shortcut belongs to the native menu bar, which offscreen does not have
    } else {
        key(Qt::Key_Z, Qt::ControlModifier, true);
        key(Qt::Key_Z, Qt::ControlModifier, false);
    }
    check(QStringLiteral("UX-04 one undo restores the whole scan"),
          scan()->rotation == 0 && shapes() == start,
          QStringLiteral("rotation %1° (%2)")
              .arg(scan()->rotation * 90)
              .arg(mac ? QStringLiteral("App.undo: native menu shortcut not available offscreen")
                       : QStringLiteral("Ctrl+Z")));
    const std::string firstId = scan()->items.front().itemId;
    app_->selectItem(toQ(firstId));
    fk::Shape moved = scan()->items.front().outer;
    for (auto& q : moved.quad.p)
        q = q + fk::Vec2{3, 0};
    app_->commitShape(firstId, true, moved);
    app_->rotateScan(scanId, -1);
    app_->undo();
    const bool u1 = scan()->rotation == 0 && scan()->items.front().outer == moved;
    app_->undo();
    const bool u2 = scan()->items.front().outer == start.front().first;
    app_->redo();
    app_->redo();
    check(QStringLiteral("UX-04 undo/redo interleave a print's edit and a whole-scan turn in order"),
          u1 && u2 && scan()->rotation == 3 && scan()->items.front().outer == moved);
    app_->undo();
    app_->undo();

    // exports before the turn, relinked after it: matched, and the final composite equals the full print bit
    // for bit
    app_->setSetting(QStringLiteral("applyUpright"), false);
    app_->exportImageAreas({});
    app_->waitIdle(300000);
    const QStringList edits = filesMatching(exportDir, QStringLiteral("*_EDIT_v001.tif"));
    app_->rotateScan(scanId, 1);
    QVariantList rows;
    auto c = connect(app_, &AppController::relinkResults, this, [&](const QVariantList& r) { rows = r; });
    QVariantList urls;
    for (const auto& e : edits)
        urls << e;
    app_->relinkFiles(urls);
    app_->waitIdle(300000);
    disconnect(c);
    int matched = 0;
    for (const auto& r : rows)
        if (r.toMap().value("decision").toString() == QLatin1String("unique") &&
            r.toMap().value("accept").toBool())
            ++matched;
    app_->applyRelink(rows);
    QMetaObject::invokeMethod(window_, "closeDialogs");
    app_->exportFinal({}, {});
    app_->waitIdle(300000);
    app_->exportFullPrints({});
    app_->waitIdle(300000);
    const QStringList finals = filesMatching(exportDir, QStringLiteral("*_FINAL_v001.tif"));
    const QStringList fulls = filesMatching(exportDir, QStringLiteral("*_FULL_v001.tif"));
    bool exact = edits.size() == 4 && finals.size() == 4 && fulls.size() == 4;
    QString why;
    for (int i = 0; exact && i < finals.size(); ++i)
        exact = samePixels(finals[i], fulls[i], why);
    check(QStringLiteral(
              "UX-04 exported before the turn, relinked after it: 4/4 matched, final = full print bit for "
              "bit"),
          matched == 4 && exact,
          QStringLiteral("edits %1, matched %2, finals %3, fulls %4 %5")
              .arg(edits.size())
              .arg(matched)
              .arg(finals.size())
              .arg(fulls.size())
              .arg(why));
    // the other canvas views follow the turn too (visual evidence; the checks above are the measurements)
    canvas->setProperty("mode", QStringLiteral("composite"));
    waitFor([&] { return !app_->compositePreview(app_->currentItemId().toStdString()).isNull(); }, 60000);
    canvas->setProperty("compare", true);
    snapshot(QStringLiteral("U014-composite-turned-90"));
    canvas->setProperty("compare", false);
    canvas->setProperty("mode", QStringLiteral("image"));
    snapshot(QStringLiteral("U014-image-turned-90"));
    canvas->setProperty("mode", QStringLiteral("scan"));
    // uprights off: a new export looks like the turned canvas — the old one turned 90°, pixel for pixel
    app_->exportImageAreas({});
    app_->waitIdle(300000);
    const QStringList edits2 = filesMatching(exportDir, QStringLiteral("*_EDIT_v002.tif"));
    bool turned = edits2.size() == 4;
    for (int i = 0; turned && i < 4; ++i)
        turned = sameTurned(edits[i], edits2[i], 1, why);
    check(QStringLiteral("UX-04 uprights off: exports follow the turned view (lossless quarter turn)"),
          turned, why);
    // uprights on: the turn is never added to a print's own upright
    app_->setSetting(QStringLiteral("applyUpright"), true);
    app_->exportImageAreas({});
    app_->waitIdle(300000);
    app_->rotateScan(scanId, -1);
    app_->exportImageAreas({});
    app_->waitIdle(300000);
    const QStringList up3 = filesMatching(exportDir, QStringLiteral("*_EDIT_v003.tif"));
    const QStringList up4 = filesMatching(exportDir, QStringLiteral("*_EDIT_v004.tif"));
    bool same = up3.size() == 4 && up4.size() == 4;
    for (int i = 0; same && i < 4; ++i)
        same = samePixels(up3[i], up4[i], why);
    check(QStringLiteral(
              "UX-04 uprights on: the same export whether the scan is turned or not (never doubled)"),
          same, why);

    // saved and reopened; the scan file itself never changed
    app_->rotateScan(scanId, -1); // 270°
    const QString proj = QDir(d).filePath(QStringLiteral("rotation.fkproj"));
    const bool savedOk = app_->saveProjectAs(proj);
    app_->newProject();
    const bool opened = app_->openProject(QUrl::fromLocalFile(proj));
    app_->waitIdle(300000);
    QCoreApplication::processEvents();
    check(QStringLiteral("UX-04 saved and reopened: the turn, the prints and their boundaries are kept"),
          savedOk && opened && scan() && scan()->rotation == 3 && shapes() == start);
    const auto shaAfter = fk::sha256File(toPath(four));
    check(QStringLiteral("UX-04 the scan file is unchanged (SHA-256)"),
          sourceSha && shaAfter && *sourceSha == *shaAfter,
          sourceSha ? QString::fromStdString(sourceSha->substr(0, 16)) : QString());
    snapshot(QStringLiteral("U014-reopened-270"));
    restoreSettings();
    app_->newProject();
}

void SelfTest::runMaskDefaults() {
    QMetaObject::invokeMethod(window_, "closeDialogs");
    waitFor([] { return false; }, 600);
    const QString d = QDir(dir_).filePath(QStringLiteral("mask"));
    QDir(d).removeRecursively();
    QDir().mkpath(d);
    const QVariantMap saved = app_->settings();
    const QString exportDir = QDir(d).filePath(QStringLiteral("export"));
    for (const auto& [k, v] : std::vector<std::pair<QString, QVariant>>{{"exportFolder", exportDir},
                                                                        {"exportFormat", "tif"},
                                                                        {"geometryMode", "crop"},
                                                                        {"finalMode", "real"},
                                                                        {"finalFormat", "tif"},
                                                                        {"applyUpright", false},
                                                                        {"rightTab", 2}})
        app_->setSetting(k, v);
    auto restoreSettings = [&] {
        for (const char* k : {"exportFolder", "exportFormat", "geometryMode", "finalMode", "finalFormat",
                              "applyUpright", "rightTab", "newPrintMask"})
            app_->setSetting(QString::fromLatin1(k), saved.value(QString::fromLatin1(k)));
    };
    check(QStringLiteral("UX-08 new prints get the natural blend by default"),
          saved.value("newPrintMask").toString() == QLatin1String("feather"),
          saved.value("newPrintMask").toString());
    app_->newProject();
    app_->importScans({scan_});
    app_->waitIdle(300000);
    QCoreApplication::processEvents();
    std::vector<std::string> ids;
    double dpi = 0;
    bool reliable = false;
    if (!app_->project().scans.empty()) {
        const auto& sc = app_->project().scans.front();
        for (const auto& it : sc.items)
            ids.push_back(it.itemId);
        dpi = sc.dpiUsed > 0 ? sc.dpiUsed : sc.info.dpiX;
        reliable =
            dpi > 0 && std::find(sc.warnings.begin(), sc.warnings.end(), "dpi_unknown") == sc.warnings.end();
    }
    check(QStringLiteral("UX-08 setup: prints to composite"), !ids.empty(), QString::number(ids.size()));
    if (ids.empty()) {
        restoreSettings();
        return;
    }
    auto rec = [&](const std::string& id) -> const fk::ItemRecord* { return app_->item(id); };
    auto maskOf = [&](const std::string& id) {
        app_->selectItem(toQ(id));
        QCoreApplication::processEvents();
        return app_->currentItem().value("mask").toMap();
    };
    auto maskText = [](const QVariantMap& m) {
        return QStringLiteral("%1 %2 %3 (= %4 px), own %5")
            .arg(m.value("mode").toString())
            .arg(m.value("width").toDouble())
            .arg(m.value("unit").toString())
            .arg(m.value("px").toDouble(), 0, 'f', 3)
            .arg(m.value("own").toBool());
    };
    const fk::MaskChoice natural = fk::naturalBlend(fk::MaskMode::feather, reliable);
    bool allNatural = true;
    for (const auto& id : ids)
        allNatural = allNatural && rec(id)->composite.maskSet && rec(id)->composite.mask == natural;
    const QVariantMap m0 = maskOf(ids.front());
    check(QStringLiteral("UX-08 every new print: feather %1 %2, converted with the scan's DPI")
              .arg(natural.width)
              .arg(natural.mm ? QStringLiteral("mm") : QStringLiteral("px")),
          allNatural && std::abs(m0.value("px").toDouble() -
                                 (natural.mm ? natural.width * dpi / 25.4 : natural.width)) < 1e-9,
          maskText(m0) + QStringLiteral(" at %1 dpi").arg(dpi));
    QQuickItem* combo = findItem(QStringLiteral("maskModeCombo"));
    QQuickItem* field = findItem(QStringLiteral("maskWidthField"));
    check(QStringLiteral("UX-08 the right rail shows the print's mask"),
          combo && field && combo->isVisible() && field->isVisible() &&
              combo->property("value").toString() == QLatin1String("feather") &&
              std::abs(field->property("value").toDouble() - natural.width) < 1e-9,
          combo && field ? QStringLiteral("%1 / %2").arg(combo->property("value").toString(),
                                                         field->property("text").toString())
                         : QStringLiteral("controls not found"));
    // the width typed into the field (real key events), then undone
    if (field && field->isVisible()) {
        doubleClick(field); // UX-09: keyboard input opens with a double-click (the text is selected)
        const QString typed = natural.mm ? QStringLiteral("0.06") : QStringLiteral("2.5");
        for (const QChar c : typed) {
            const int k = c == QLatin1Char('.') ? Qt::Key_Period : Qt::Key_0 + (c.unicode() - '0');
            key(k, Qt::NoModifier, true, QString(c));
            key(k, Qt::NoModifier, false, QString(c));
        }
        key(Qt::Key_Return, Qt::NoModifier, true);
        key(Qt::Key_Return, Qt::NoModifier, false);
        const fk::MaskChoice afterTyping = rec(ids.front())->composite.mask;
        app_->undo();
        check(QStringLiteral("UX-08 width typed in the right rail is saved with the print; undo restores it"),
              afterTyping.mode == fk::MaskMode::feather &&
                  std::abs(afterTyping.width - typed.toDouble()) < 1e-9 &&
                  rec(ids.front())->composite.mask == natural,
              QStringLiteral("typed %1 → %2").arg(typed).arg(afterTyping.width));
    }
    // explicit batch: two prints selected, the current one's mask applied with the button (mouse)
    if (ids.size() >= 2) {
        app_->selectItem(toQ(ids[0]));
        app_->selectItem(toQ(ids[1]), true);
        QCoreApplication::processEvents();
        app_->setItemMaskMode(app_->currentItemId(), QStringLiteral("preserve_edge"));
        const std::string cur = toStd(app_->currentItemId());
        const std::string other = cur == ids[0] ? ids[1] : ids[0];
        const bool before = rec(other)->composite.mask == natural;
        const bool clicked = click(findItem(QStringLiteral("maskApplyButton")));
        const bool appliedToOther =
            rec(other)->composite.mask == fk::naturalBlend(fk::MaskMode::preserve_edge, reliable);
        const bool restUnchanged = ids.size() < 3 || rec(ids[2])->composite.mask == natural;
        check(QStringLiteral("UX-08 applied to the selected prints only, with the button (mouse)"),
              before && clicked && appliedToOther && restUnchanged);
    } else {
        skip(QStringLiteral("UX-08 applied to the selected prints only, with the button (mouse)"),
             QStringLiteral("one print on this scan"));
    }

    // preview and final output use the same mask; graded files: a normal grade and a strong one
    app_->selectItem(toQ(ids.front()));
    app_->exportImageAreas({});
    app_->waitIdle(300000);
    const QStringList edits = filesMatching(exportDir, QStringLiteral("*_EDIT_v001.tif"));
    // edits are named by label: P01 is ids[0], … (labels follow the detection order)
    const bool graded =
        edits.size() == ids.size() && rewriteGraded(edits[0], edits[0], {1.03, 1.03, 1.03}) &&
        (ids.size() < 2 || rewriteGraded(edits.back(), edits.back(), {1.2, 1.0, 0.7}, {0.12, 0, 0}));
    QVariantList rows;
    auto c = connect(app_, &AppController::relinkResults, this, [&](const QVariantList& r) { rows = r; });
    QVariantList urls;
    for (const auto& e : edits)
        urls << e;
    app_->relinkFiles(urls);
    app_->waitIdle(300000);
    disconnect(c);
    app_->applyRelink(rows);
    QMetaObject::invokeMethod(window_, "closeDialogs");
    waitFor([] { return false; }, 600);
    auto previewOf = [&](const std::string& id) {
        app_->selectItem(toQ(id));
        app_->requestCompositePreview(toQ(id));
        app_->waitIdle(120000);
        QCoreApplication::processEvents();
        return app_->currentItem().value("mask").toMap();
    };
    const QVariantMap normal = previewOf(ids.front());
    app_->exportFinal({toQ(ids.front())}, {});
    app_->waitIdle(300000);
    const auto used = app_->masksUsed(ids.front());
    const fk::MaskParams want = fk::resolveMask(rec(ids.front())->composite.mask, dpi);
    auto same = [](const fk::MaskParams& a, const fk::MaskParams& b) {
        return a.mode == b.mode && a.featherPx == b.featherPx && a.preserveEdgePx == b.preserveEdgePx;
    };
    check(QStringLiteral("UX-08 the composite preview and the final output use the print's mask"),
          graded && same(used.first, want) && same(used.second, want) &&
              !filesMatching(exportDir, QStringLiteral("*_FINAL_v001.tif")).isEmpty(),
          QStringLiteral("preview %1/%2 px, final %3/%4 px, print %5/%6 px")
              .arg(used.first.featherPx)
              .arg(used.first.preserveEdgePx)
              .arg(used.second.featherPx)
              .arg(used.second.preserveEdgePx)
              .arg(want.featherPx)
              .arg(want.preserveEdgePx));
    check(QStringLiteral("UX-08 a normal grade (+3 %) is not flagged at the edge"),
          normal.value("strongEdgeChange").toBool() == false && normal.value("edgeChange").toDouble() > 0,
          QStringLiteral("edge change ΔE00 %1").arg(normal.value("edgeChange").toDouble(), 0, 'f', 2));
    if (ids.size() >= 2) {
        const QVariantMap strong = previewOf(ids.back());
        QQuickItem* hint = findItem(QStringLiteral("strongEdgeHint"));
        check(QStringLiteral("UX-08 a strong colour change at the edge asks for a check (right rail)"),
              strong.value("strongEdgeChange").toBool() && hint && hint->isVisible(),
              QStringLiteral("edge change ΔE00 %1").arg(strong.value("edgeChange").toDouble(), 0, 'f', 2));
    } else {
        skip(QStringLiteral("UX-08 a strong colour change at the edge asks for a check (right rail)"),
             QStringLiteral("one print on this scan (its graded file carries the normal grade)"));
    }
    snapshot(QStringLiteral("U015-mask"));

    // saved and reopened: each print keeps its own mask
    std::map<std::string, fk::MaskChoice> masks;
    for (const auto& id : ids)
        masks[id] = rec(id)->composite.mask;
    const QString proj = QDir(d).filePath(QStringLiteral("mask.fkproj"));
    const bool savedOk = app_->saveProjectAs(proj);
    app_->newProject();
    const bool opened = app_->openProject(QUrl::fromLocalFile(proj));
    app_->waitIdle(300000);
    bool kept = savedOk && opened;
    for (const auto& id : ids)
        kept = kept && rec(id) && rec(id)->composite.maskSet && rec(id)->composite.mask == masks[id];
    check(QStringLiteral("UX-08 saved and reopened: every print keeps its own mask"), kept);
    // a project from before 0.7.0-alpha.5 (no per-print mask): its prints keep the project's setting
    {
        QFile f(proj);
        f.open(QIODevice::ReadOnly);
        QJsonDocument doc = QJsonDocument::fromJson(f.readAll());
        f.close();
        QJsonObject root = doc.object();
        QJsonArray scans = root.value("scans").toArray();
        for (int i = 0; i < scans.size(); ++i) {
            QJsonObject sc = scans[i].toObject();
            QJsonArray items = sc.value("items").toArray();
            for (int j = 0; j < items.size(); ++j) {
                QJsonObject it = items[j].toObject();
                QJsonObject comp = it.value("composite").toObject();
                comp.remove(QStringLiteral("mask"));
                it.insert(QStringLiteral("composite"), comp);
                items[j] = it;
            }
            sc.insert(QStringLiteral("items"), items);
            scans[i] = sc;
        }
        root.insert(QStringLiteral("scans"), scans);
        const QString old = QDir(d).filePath(QStringLiteral("before-0.7.0-alpha.5.fkproj"));
        QFile o(old);
        o.open(QIODevice::WriteOnly);
        o.write(QJsonDocument(root).toJson());
        o.close();
        app_->newProject();
        app_->openProject(QUrl::fromLocalFile(old));
        app_->waitIdle(300000);
        const QVariantMap m = maskOf(ids.front());
        check(QStringLiteral("UX-08 older projects: prints keep compositing with the project's setting"),
              rec(ids.front()) && !rec(ids.front())->composite.maskSet && !m.value("own").toBool() &&
                  m.value("unit").toString() == QLatin1String("px") &&
                  m.value("mode").toString() == app_->settings().value("maskMode").toString(),
              maskText(m));
    }
    restoreSettings();
    app_->newProject();
}

void SelfTest::runScrubFields() {
    QMetaObject::invokeMethod(window_, "closeDialogs");
    waitFor([] { return false; }, 600);
    const QString d = QDir(dir_).filePath(QStringLiteral("scrub"));
    QDir(d).removeRecursively();
    QDir().mkpath(d);
    const QVariantMap saved = app_->settings();
    const QString exportDir = QDir(d).filePath(QStringLiteral("export"));
    for (const auto& [k, v] : std::vector<std::pair<QString, QVariant>>{
             {"exportFolder", exportDir}, {"exportFormat", "tif"}, {"geometryMode", "crop"}, {"rightTab", 2}})
        app_->setSetting(k, v);
    auto restoreSettings = [&] {
        for (const char* k : {"exportFolder", "exportFormat", "geometryMode", "rightTab"})
            app_->setSetting(QString::fromLatin1(k), saved.value(QString::fromLatin1(k)));
    };
    app_->newProject();
    app_->importScans({scan_});
    app_->waitIdle(300000);
    QCoreApplication::processEvents();
    const QString id = app_->currentItemId();
    const fk::ItemRecord* it0 = app_->item(toStd(id));
    QQuickItem* field = findItem(QStringLiteral("maskWidthField"));
    check(QStringLiteral("UX-09 setup: the mask width field of a print"),
          it0 && field && field->isVisible() && it0->composite.mask.mode == fk::MaskMode::feather);
    if (!it0 || !field || !field->isVisible()) {
        restoreSettings();
        return;
    }
    const fk::MaskChoice start = it0->composite.mask;
    const bool mm = start.mm;
    const double step = field->property("step").toDouble(), upper = field->property("to").toDouble();
    auto width = [&] { return app_->item(toStd(id))->composite.mask.width; };
    auto near = [](double a, double b) { return std::abs(a - b) < 1e-9; };
    auto expected = [&](double from, double dxPx) { // 2 logical px per step, clamped to the field's range
        return std::clamp(from + std::round(dxPx / 2.0) * step, 0.0, upper);
    };
    const QPointF c(field->width() / 2, field->height() / 2);
    // a drag in `n` moves of `dx/n` px, optionally waiting between moves (a realistic speed)
    auto scrubBy = [&](double dx, int n, int waitMs, bool release = true) {
        mouse(field, c, QEvent::MouseButtonPress);
        for (int k = 1; k <= n; ++k) {
            mouse(field, c + QPointF(dx * k / n, 0), QEvent::MouseMove);
            if (waitMs > 0) waitFor([] { return false; }, waitMs);
        }
        if (release) mouse(field, c + QPointF(dx, 0), QEvent::MouseButtonRelease);
        QCoreApplication::processEvents();
    };
    // 1. drag right over about 0.25 s (live updates happen): the value follows, one undo step for the whole
    // drag
    scrubBy(60, 12, 20);
    const double w1 = width();
    app_->undo();
    const double undone = width();
    const bool moreSteps = app_->canUndo(); // a fresh print: the drag must be its only step
    app_->redo();
    check(QStringLiteral("UX-09 drag right (mouse): the value follows, one undo step for the whole drag"),
          near(w1, expected(start.width, 60)) && near(undone, start.width) && !moreSteps && near(width(), w1),
          QStringLiteral("%1 → %2 %3, undo → %4").arg(start.width).arg(w1).arg(mm ? "mm" : "px").arg(undone));
    // 2. a click is not a drag and does not open keyboard input or select text
    mouse(field, c, QEvent::MouseButtonPress);
    mouse(field, c, QEvent::MouseButtonRelease);
    check(QStringLiteral("UX-09 a click changes nothing, no keyboard input, no selected text"),
          near(width(), w1) && field->property("readOnly").toBool() &&
              field->property("selectedText").toString().isEmpty());
    // 3. under the 4 px threshold
    scrubBy(3, 3, 0);
    check(QStringLiteral("UX-09 a 3 px move is not a drag"), near(width(), w1));
    // 4. / 5. negative drag to the lower bound, then past the upper bound
    scrubBy(-400, 20, 0);
    const double lo = width();
    scrubBy(400, 20, 0);
    check(QStringLiteral("UX-09 drag left to the lower bound, right to the upper bound (clamped)"),
          near(lo, 0.0) && near(width(), upper), QStringLiteral("%1 … %2").arg(lo).arg(width()));
    // 6. Esc during a drag: back to the value before it, no undo step
    const double beforeEsc = width();
    scrubBy(-40, 8, 20, false); // long enough for live updates: Esc must undo them
    const double during = field->property("liveValue").toDouble();
    key(Qt::Key_Escape, Qt::NoModifier, true);
    key(Qt::Key_Escape, Qt::NoModifier, false);
    mouse(field, c + QPointF(-40, 0), QEvent::MouseButtonRelease);
    app_->undo();
    const double afterUndo = width();
    app_->redo();
    check(QStringLiteral("UX-09 Esc during a drag restores the value (no undo step)"),
          !near(during, beforeEsc) && near(width(), beforeEsc) && near(afterUndo, 0.0),
          QStringLiteral("live %1, after Esc %2, undo → %3").arg(during).arg(width()).arg(afterUndo));
    // 7. double-click: keyboard input; letters refused; Enter commits
    doubleClick(field);
    const bool editing = !field->property("readOnly").toBool() &&
                         field->property("selectedText").toString() == field->property("text").toString();
    for (const QChar ch : QStringLiteral("ab")) {
        key(Qt::Key_A + (ch.unicode() - 'a'), Qt::NoModifier, true, QString(ch));
        key(Qt::Key_A + (ch.unicode() - 'a'), Qt::NoModifier, false, QString(ch));
    }
    const QString afterLetters = field->property("text").toString();
    auto type = [&](const QString& t) {
        for (const QChar ch : t) {
            const int k = ch == QLatin1Char('.') ? Qt::Key_Period : Qt::Key_0 + (ch.unicode() - '0');
            key(k, Qt::NoModifier, true, QString(ch));
            key(k, Qt::NoModifier, false, QString(ch));
        }
    };
    const QString v12 = mm ? QStringLiteral("0.12") : QStringLiteral("3.5");
    type(v12);
    key(Qt::Key_Return, Qt::NoModifier, true);
    key(Qt::Key_Return, Qt::NoModifier, false);
    check(QStringLiteral("UX-09 double-click opens keyboard input; letters are refused; Enter commits"),
          editing && !afterLetters.contains(QLatin1Char('a')) && near(width(), v12.toDouble()) &&
              field->property("readOnly").toBool(),
          QStringLiteral("after letters \"%1\", value %2").arg(afterLetters).arg(width()));
    // 8. Esc cancels typing
    doubleClick(field);
    type(mm ? QStringLiteral("0.5") : QStringLiteral("9"));
    key(Qt::Key_Escape, Qt::NoModifier, true);
    key(Qt::Key_Escape, Qt::NoModifier, false);
    check(QStringLiteral("UX-09 Esc cancels keyboard input"),
          near(width(), v12.toDouble()) && field->property("readOnly").toBool());
    // 9. focus loss commits
    doubleClick(field);
    const QString v07 = mm ? QStringLiteral("0.07") : QStringLiteral("2");
    type(v07);
    if (auto* canvas = qobject_cast<QQuickItem*>(findCanvas())) click(canvas, QPointF(4, 4));
    check(QStringLiteral("UX-09 leaving the field commits the typed value"), near(width(), v07.toDouble()),
          QString::number(width()));
    // 10. keyboard steps kept
    click(field);
    key(Qt::Key_Up, Qt::NoModifier, true);
    key(Qt::Key_Up, Qt::NoModifier, false);
    const double up1 = width();
    key(Qt::Key_Up, Qt::ShiftModifier, true);
    key(Qt::Key_Up, Qt::ShiftModifier, false);
    check(QStringLiteral("UX-09 arrow keys still step (Shift ×10)"),
          near(up1, v07.toDouble() + step) && near(width(), v07.toDouble() + 11 * step),
          QStringLiteral("%1, %2").arg(up1).arg(width()));
    // 11. no backlog of composite previews while dragging, and the last preview is the last value
    app_->exportImageAreas({id});
    app_->waitIdle(300000);
    QVariantList rows;
    auto conn = connect(app_, &AppController::relinkResults, this, [&](const QVariantList& r) { rows = r; });
    app_->relinkFiles({filesMatching(exportDir, QStringLiteral("*_EDIT_v001.tif")).value(0)});
    app_->waitIdle(300000);
    disconnect(conn);
    app_->applyRelink(rows);
    QMetaObject::invokeMethod(window_, "closeDialogs");
    waitFor([] { return false; }, 600);
    app_->selectItem(id);
    QObject* canvas = findCanvas();
    canvas->setProperty("mode", QStringLiteral("composite"));
    app_->requestCompositePreview(id);
    app_->waitIdle(120000);
    QCoreApplication::processEvents();
    const int firstJob = app_->jobRunner()->lastJobId();
    int maxActive = 0;
    const double before11 = width();
    mouse(field, c, QEvent::MouseButtonPress);
    for (int k = 1; k <= 30; ++k) { // 30 moves over about 0.6 s
        mouse(field, c + QPointF(2.0 * k, 0), QEvent::MouseMove);
        waitFor([] { return false; }, 20);
        maxActive = std::max(maxActive, app_->jobRunner()->activeCount());
    }
    mouse(field, c + QPointF(60, 0), QEvent::MouseButtonRelease);
    app_->waitIdle(120000);
    QCoreApplication::processEvents();
    const int started = app_->jobRunner()->lastJobId() - firstJob;
    const auto used = app_->masksUsed(toStd(id));
    const fk::ItemRecord* it1 = app_->item(toStd(id));
    double scanDpi = 0;
    for (const auto& sc : app_->project().scans)
        for (const auto& i : sc.items)
            if (i.itemId == toStd(id)) scanDpi = sc.dpiUsed > 0 ? sc.dpiUsed : sc.info.dpiX;
    const fk::MaskParams want = fk::resolveMask(it1->composite.mask, scanDpi);
    check(QStringLiteral(
              "UX-09 a 0.6 s drag in composite view: few previews, never more than 2 at once, the last "
              "preview is the last value"),
          near(it1->composite.mask.width, expected(before11, 60)) && started <= 10 && maxActive <= 2 &&
              used.first.featherPx == want.featherPx && used.first.preserveEdgePx == want.preserveEdgePx &&
              app_->compositePreviewGeneration(toStd(id)) == app_->itemGeneration(toStd(id)),
          QStringLiteral("%1 preview jobs for 30 moves, at most %2 at once, preview mask %3 px, value %4")
              .arg(started)
              .arg(maxActive)
              .arg(used.first.featherPx + used.first.preserveEdgePx)
              .arg(it1->composite.mask.width));
    canvas->setProperty("mode", QStringLiteral("scan"));
    snapshot(QStringLiteral("U016-scrub"));
    restoreSettings();
    app_->newProject();
}

void SelfTest::runMemoryBudget() {
    QMetaObject::invokeMethod(window_, "closeDialogs");
    waitFor([] { return false; }, 600);
    const QString d = QDir(dir_).filePath(QStringLiteral("memory"));
    QDir(d).removeRecursively();
    QDir().mkpath(d);
    const QVariantMap saved = app_->settings();
    const QString exportDir = QDir(d).filePath(QStringLiteral("export"));
    for (const auto& [k, v] : std::vector<std::pair<QString, QVariant>>{{"exportFolder", exportDir},
                                                                        {"exportFormat", "tif"},
                                                                        {"geometryMode", "crop"},
                                                                        {"applyUpright", false},
                                                                        {"autoCount", true}})
        app_->setSetting(k, v);
    QObject* canvas = findCanvas();
    auto restore = [&] {
        for (const char* k : {"exportFolder", "exportFormat", "geometryMode", "applyUpright", "autoCount"})
            app_->setSetting(QString::fromLatin1(k), saved.value(QString::fromLatin1(k)));
        app_->setCacheBudget(0);
        app_->jobRunner()->setTestDelayMs(0);
        if (canvas) {
            canvas->setProperty("mode", QStringLiteral("scan"));
            QMetaObject::invokeMethod(canvas, "fit");
        }
    };
    const fk::CacheLedger& L = app_->cacheLedger();
    auto rep = [&] { return app_->memoryReport(); };
    // the ledger's rule: what can be dropped fits next to the jobs' reservation
    auto within = [&] {
        const uint64_t room = L.budget() > L.reserved() ? L.budget() - L.reserved() : 0;
        return L.bytes() <= std::max(room, L.fixedBytes());
    };
    // every image the controller holds is in the ledger (nothing kept behind its back)
    auto counted = [&] {
        const QVariantMap r = rep();
        return r.value("displaysHeld").toInt() == r.value("displayCount").toInt() &&
               r.value("sourcesHeld").toInt() == r.value("sourceCount").toInt() &&
               r.value("compositesHeld").toInt() == r.value("compositeCount").toInt();
    };
    auto mb = [](uint64_t b) { return QString::number(static_cast<double>(b) / 1048576.0, 'f', 1); };
    auto state = [&] {
        const QVariantMap r = rep();
        return QStringLiteral("cache %1 / budget %2 MB (fixed %3, reserved %4) · proxies %5 · sources %6 · "
                              "previews %7 · RSS %8 MB")
            .arg(mb(L.bytes()), mb(L.budget()), mb(L.fixedBytes()), mb(L.reserved()))
            .arg(r.value("displaysHeld").toInt())
            .arg(r.value("sourcesHeld").toInt())
            .arg(r.value("compositesHeld").toInt())
            .arg(r.value("residentMB").toDouble(), 0, 'f', 0);
    };

    // setup: the first print of the test scan four times on a bed, six scans with different scanner noise
    app_->newProject();
    app_->importScans({scan_});
    app_->waitIdle(300000);
    fk::Shape first;
    if (!app_->project().scans.empty() && !app_->project().scans.front().items.empty())
        first = app_->project().scans.front().items.front().outer;
    QVariantList beds;
    bool made = !app_->project().scans.empty();
    for (uint32_t i = 0; made && i < 6; ++i) {
        const QString f = QDir(d).filePath(QStringLiteral("bed-%1.tif").arg(i + 1));
        made = writeFourOrientations(scan_, first, f, 101 + i);
        beds << f;
    }
    check(QStringLiteral("FK-005 setup: six scans with four prints each"), made);
    if (!made) {
        restore();
        return;
    }
    // the reservation of the job that reads a proxy again (what switching scans runs), measured while it runs
    uint64_t jobReserve = 0;
    auto waitAndWatch = [&](int ms) {
        waitFor(
            [&] {
                jobReserve = std::max(jobReserve, L.reserved());
                return !app_->busy();
            },
            ms);
        app_->waitIdle(ms);
        QCoreApplication::processEvents();
    };
    app_->newProject();
    app_->importScans(beds.mid(0, 2));
    waitAndWatch(300000);
    std::vector<QString> scanIds;
    for (const auto& sc : app_->project().scans)
        scanIds.push_back(toQ(sc.scanId));
    const uint64_t proxy = scanIds.size() == 2 ? L.bytes("display:" + toStd(scanIds[0])) : 0; // one proxy
    if (proxy > 0) {
        // the second scan's proxy goes: reading it again shows that job's reservation (detection reserved for
        // a scan of unknown size, i.e. a large one: not what switching needs)
        app_->selectScan(scanIds[0]);
        app_->onMemoryPressure();
        jobReserve = 0;
        app_->selectScan(scanIds[1]);
        waitAndWatch(300000);
    }
    if (proxy == 0 || jobReserve == 0) {
        check(QStringLiteral("FK-005 setup: a scan proxy counted, jobs reserve memory"), false, state());
        restore();
        return;
    }
    // a budget that leaves room for about two more proxies while a proxy is read (detection reserves at least
    // as much: a scan of unknown size counts as a 1600 dpi A4 scan)
    const uint64_t budget = jobReserve + L.fixedBytes() + 5 * proxy / 2;
    app_->setCacheBudget(budget);

    // 1. batch detection of the other four: proxies given back as they arrive, least recently used first
    app_->importScans(beds.mid(2));
    app_->waitIdle(600000);
    QCoreApplication::processEvents();
    int prints = 0;
    scanIds.clear();
    for (const auto& sc : app_->project().scans) {
        prints += static_cast<int>(sc.items.size());
        scanIds.push_back(toQ(sc.scanId));
    }
    const int heldAfterBatch = rep().value("displaysHeld").toInt();
    check(QStringLiteral("FK-005 batch detection of 6 scans: proxies beyond the budget are given back"),
          scanIds.size() == 6 && prints == 24 && heldAfterBatch < 6 && L.evictions() > 0 && within() &&
              counted(),
          QStringLiteral("%1 prints · job reservation %2 MB, proxy %3 MB · %4")
              .arg(prints)
              .arg(mb(jobReserve), mb(proxy), state()));

    // 2. switching through every scan twice: each one is shown (its proxy read again when it was given back),
    // the cache stays within the budget, nothing grows with the number of scans visited
    bool allShown = true, allWithin = true, allCounted = true;
    int maxHeld = 0;
    double rssFirst = 0, rssSecond = 0;
    for (int round = 0; round < 2; ++round) {
        for (const QString& id : scanIds) {
            app_->selectScan(id);
            app_->waitIdle(120000);
            QCoreApplication::processEvents();
            allShown = allShown && app_->canvasScan().ready &&
                       !(canvas && canvas->property("badge").toString().isEmpty());
            allWithin = allWithin && within();
            allCounted = allCounted && counted();
            maxHeld = std::max(maxHeld, rep().value("displaysHeld").toInt());
        }
        (round == 0 ? rssFirst : rssSecond) = rep().value("residentMB").toDouble();
    }
    check(QStringLiteral("FK-005 switching through 6 scans twice: every scan shown, cache within the budget"),
          allShown && allWithin && allCounted && maxHeld <= 3,
          QStringLiteral("at most %1 proxies held · RSS after round 1 %2 MB, round 2 %3 MB · %4")
              .arg(maxHeld)
              .arg(rssFirst, 0, 'f', 0)
              .arg(rssSecond, 0, 'f', 0)
              .arg(state()));

    // 3. full-resolution view on three scans: decoded strips are counted and given back like the proxies
    bool fullShown = canvas != nullptr;
    for (int i = 0; canvas && i < 3; ++i) {
        app_->selectScan(scanIds[static_cast<size_t>(i)]);
        app_->waitIdle(120000);
        QMetaObject::invokeMethod(canvas, "zoomTo", Q_ARG(double, 100.0));
        fullShown =
            fullShown &&
            waitFor([&] { return canvas->property("badge").toString() == QLatin1String("Full"); }, 20000);
        app_->waitIdle(60000);
        QMetaObject::invokeMethod(canvas, "fit");
    }
    QCoreApplication::processEvents();
    check(QStringLiteral("FK-005 full-resolution view on 3 scans: decoded strips counted, within the budget"),
          fullShown && L.count("source:") >= 1 && L.bytes("canvas") > 0 && within() && counted(), state());

    // 4. composite previews: four prints of the current scan, relinked unchanged; with room for about one
    // more preview besides the one on screen, the others are given back and computed again when shown
    const QString cur = app_->currentScanId();
    QStringList items;
    for (const auto& sc : app_->project().scans)
        if (toQ(sc.scanId) == cur)
            for (const auto& it : sc.items)
                items << toQ(it.itemId);
    app_->exportImageAreas(items);
    app_->waitIdle(300000);
    const QStringList edits = filesMatching(exportDir, QStringLiteral("*_EDIT_v001.tif"));
    QVariantList rows;
    auto conn = connect(app_, &AppController::relinkResults, this, [&](const QVariantList& r) { rows = r; });
    QVariantList urls;
    for (const auto& e : edits)
        urls << e;
    app_->relinkFiles(urls);
    app_->waitIdle(300000);
    disconnect(conn);
    app_->applyRelink(rows);
    QMetaObject::invokeMethod(window_, "closeDialogs");
    QCoreApplication::processEvents();
    std::vector<QImage> firstPreviews;
    bool previewsShown = canvas != nullptr && items.size() == 4 && edits.size() == 4;
    bool previewsWithin = true;
    int heldAll = 0, heldShrunk = 0;
    bool firstGivenBack = false, sameAgain = false;
    if (previewsShown) {
        // within the budget, every preview computed stays
        canvas->setProperty("mode", QStringLiteral("composite"));
        for (const QString& id : items) {
            app_->selectItem(id); // the canvas asks for the preview (composite mode)
            app_->waitIdle(120000);
            QCoreApplication::processEvents();
            const QImage p = app_->compositePreview(toStd(id));
            previewsShown = previewsShown && !p.isNull();
            firstPreviews.push_back(p);
            previewsWithin = previewsWithin && within() && counted();
        }
        heldAll = rep().value("compositesHeld").toInt();
        // a smaller budget: room for about one preview besides what is on screen; the oldest go first
        app_->setCacheBudget(L.fixedBytes() + L.bytes("composite:" + toStd(items.back())) * 3 / 2);
        heldShrunk = rep().value("compositesHeld").toInt();
        firstGivenBack = app_->compositePreview(toStd(items.front())).isNull() &&
                         !app_->compositePreview(toStd(items.back())).isNull();
        previewsWithin = previewsWithin && within() && counted();
        // shown again: computed again, identical
        app_->selectItem(items.front());
        app_->waitIdle(120000);
        QCoreApplication::processEvents();
        sameAgain = app_->compositePreview(toStd(items.front())) == firstPreviews.front();
    }
    check(
        QStringLiteral("FK-005 composite previews of 4 prints: kept within the budget, the oldest given back "
                       "beyond it, computed again identically when shown"),
        previewsShown && heldAll == 4 && heldShrunk <= 2 && firstGivenBack && sameAgain && previewsWithin,
        QStringLiteral("previews held %1 → %2 after the budget shrank · %3")
            .arg(heldAll)
            .arg(heldShrunk)
            .arg(state()));
    if (canvas) canvas->setProperty("mode", QStringLiteral("scan"));
    app_->setCacheBudget(budget);

    // 5. a job's working memory is reserved before it starts: the caches make room at once
    for (int i = 3; i < 6; ++i) { // three scans visited: proxies held besides the current one
        app_->selectScan(scanIds[static_cast<size_t>(i)]);
        app_->waitIdle(120000);
    }
    QCoreApplication::processEvents();
    const int heldBefore = rep().value("displaysHeld").toInt();
    app_->setCacheBudget(L.bytes());         // exactly full: any reservation has to take room from the cache
    app_->jobRunner()->setTestDelayMs(1500); // the export's work has not begun when we look
    app_->exportFullPrints(items);
    const uint64_t reservedAtStart = L.reserved();
    const int heldAtStart = rep().value("displaysHeld").toInt();
    const bool roomAtStart = within();
    app_->waitIdle(600000);
    app_->jobRunner()->setTestDelayMs(0);
    QCoreApplication::processEvents();
    check(QStringLiteral("FK-005 export: working memory reserved before the job starts, caches make room, "
                         "released at the end"),
          reservedAtStart > 0 && heldBefore > heldAtStart && heldAtStart >= 1 && roomAtStart &&
              L.reserved() == 0,
          QStringLiteral("reserved %1 MB · proxies %2 → %3 · after: reserved %4 MB")
              .arg(mb(reservedAtStart))
              .arg(heldBefore)
              .arg(heldAtStart)
              .arg(mb(L.reserved())));
    app_->setCacheBudget(budget);

    // 6. memory pressure: everything that can be rebuilt goes (also the current scan's decoded strips); the
    // scan on screen stays shown and an output made afterwards is identical
    for (int i = 0; i < 3; ++i) {
        app_->selectScan(scanIds[static_cast<size_t>(i)]);
        app_->waitIdle(120000);
    }
    if (canvas) {
        QMetaObject::invokeMethod(canvas, "zoomTo", Q_ARG(double, 100.0));
        waitFor([&] { return canvas->property("badge").toString() == QLatin1String("Full"); }, 20000);
        app_->waitIdle(60000);
    }
    QCoreApplication::processEvents();
    const QString pressedScan = app_->currentScanId();
    const QStringList fullsBefore = filesMatching(exportDir, QStringLiteral("*_FULL_v001.tif"));
    const int heldBeforePressure = rep().value("displaysHeld").toInt(),
              sourcesBeforePressure = rep().value("sourcesHeld").toInt(),
              pressuresBefore = rep().value("pressureEvents").toInt();
    app_->onMemoryPressure();
    const QVariantMap afterPressure = rep();
    if (canvas) QMetaObject::invokeMethod(canvas, "fit");
    app_->exportFullPrints(items);
    app_->waitIdle(600000);
    const QStringList fullsAfter = filesMatching(exportDir, QStringLiteral("*_FULL_v002.tif"));
    bool sameOutput = !fullsBefore.isEmpty() && fullsBefore.size() == fullsAfter.size();
    QString why;
    for (int i = 0; sameOutput && i < fullsBefore.size(); ++i)
        sameOutput = samePixels(fullsBefore[i], fullsAfter[i], why);
    check(
        QStringLiteral("FK-005 memory pressure: rebuildable caches dropped, the scan on screen stays, output "
                       "identical"),
        heldBeforePressure > 1 && sourcesBeforePressure >= 1 &&
            afterPressure.value("displaysHeld").toInt() == 1 &&
            afterPressure.value("sourcesHeld").toInt() == 0 &&
            afterPressure.value("pressureEvents").toInt() == pressuresBefore + 1 &&
            app_->currentScanId() == pressedScan && app_->canvasScan().ready && sameOutput,
        QStringLiteral("proxies %1 → %2, sources %3 → %4 · %5 outputs compared %6")
            .arg(heldBeforePressure)
            .arg(afterPressure.value("displaysHeld").toInt())
            .arg(sourcesBeforePressure)
            .arg(afterPressure.value("sourcesHeld").toInt())
            .arg(fullsAfter.size())
            .arg(why));

    // 7. cancelled detection: nothing reserved afterwards, prints kept, cache within the budget
    app_->jobRunner()->setTestDelayMs(300);
    for (const QString& id : scanIds)
        app_->detectScan(id);
    waitFor([] { return false; }, 400);
    const uint64_t reservedRunning = L.reserved();
    app_->jobRunner()->cancelAll();
    app_->waitIdle(300000);
    app_->jobRunner()->setTestDelayMs(0);
    QCoreApplication::processEvents();
    int printsAfter = 0;
    for (const auto& sc : app_->project().scans)
        printsAfter += static_cast<int>(sc.items.size());
    check(
        QStringLiteral("FK-005 cancelled detection of 6 scans: reservation released, prints kept, within the "
                       "budget"),
        reservedRunning > 0 && L.reserved() == 0 && printsAfter == prints && within() && counted(),
        QStringLiteral("reserved while queued %1 MB (one detection at a time) · %2")
            .arg(mb(reservedRunning))
            .arg(state()));
    observe(QStringLiteral("FK-005 steady state"), QJsonObject::fromVariantMap(rep()));

    // 8. a relink check never depends on what the cache still holds. A picture never exported (no export
    // record): only a new alignment on the chosen print can take it, and its picture check compares the file
    // with the print's current image area, sampled from the scan's proxy — on screen, then given back.
    app_->newProject();
    app_->importScans({beds.value(0), beds.value(1)});
    app_->waitIdle(300000);
    const fk::ItemRecord* target = nullptr;
    std::string targetScan, otherScan;
    for (const auto& sc : app_->project().scans) {
        if (!target && !sc.items.empty()) {
            target = &sc.items.front();
            targetScan = sc.scanId;
        } else {
            otherScan = sc.scanId;
        }
    }
    const QString picture = QDir(d).filePath(QStringLiteral("never-exported.tif"));
    const fk::ScanRecord* tScan = target ? app_->scanOf(target->itemId) : nullptr;
    const bool written = target && tScan && !otherScan.empty() &&
                         writeCrop(toQ(tScan->path), fk::boundingRect(target->inner), picture);
    if (written) {
        const QString targetId = toQ(target->itemId);
        auto assess = [&] {
            QVariantList r;
            auto c =
                connect(app_, &AppController::relinkResults, this, [&](const QVariantList& x) { r = x; });
            app_->relinkFiles({picture}, targetId);
            app_->waitIdle(300000);
            disconnect(c);
            QMetaObject::invokeMethod(window_, "closeDialogs");
            // a new alignment gets a fresh record id each time: compare everything else
            QVariantMap row = r.value(0).toMap();
            row.remove(QStringLiteral("editVersionId"));
            QVariantList options;
            for (const auto& o : row.value("options").toList()) {
                QVariantMap om = o.toMap();
                om.remove(QStringLiteral("editVersionId"));
                options << om;
            }
            row[QStringLiteral("options")] = options;
            return row;
        };
        auto pictureChecked = [](const QVariantMap& row) {
            for (const auto& o : row.value("options").toList())
                if (o.toMap().value("newAlignment").toBool() &&
                    o.toMap().value("contentScore").toDouble() > 0)
                    return true;
            return false;
        };
        app_->selectItem(targetId);
        app_->waitIdle(120000);
        const QVariantMap shown = assess();
        app_->selectScan(toQ(otherScan));
        app_->waitIdle(120000);
        app_->onMemoryPressure();
        const bool givenBack = !L.contains("display:" + targetScan);
        const QVariantMap reread = assess();
        QStringList differs; // what differs, for the report
        for (auto it = shown.begin(); it != shown.end(); ++it)
            if (reread.value(it.key()) != it.value() && it.key() != QLatin1String("options"))
                differs << it.key();
        for (int i = 0; i < shown.value("options").toList().size(); ++i) {
            const QVariantMap a = shown.value("options").toList().value(i).toMap(),
                              b = reread.value("options").toList().value(i).toMap();
            for (auto it = a.begin(); it != a.end(); ++it)
                if (b.value(it.key()) != it.value())
                    differs << QStringLiteral("option %1 %2: %3 / %4")
                                   .arg(i)
                                   .arg(it.key(), it.value().toString(), b.value(it.key()).toString());
        }
        check(QStringLiteral("FK-005 relink check (new alignment, picture compared with the print): the same "
                             "assessment whether the target's proxy is held or given back"),
              givenBack && pictureChecked(shown) && shown == reread,
              shown.value("decision").toString() + QStringLiteral(" / ") +
                  reread.value("decision").toString() +
                  (pictureChecked(reread) ? QString()
                                          : QStringLiteral(" (picture not compared the 2nd time)")) +
                  (givenBack ? QString() : QStringLiteral(" (proxy still held)")) +
                  (pictureChecked(shown) ? QString() : QStringLiteral(" (picture not compared)")) +
                  (differs.isEmpty() ? QString()
                                     : QStringLiteral(" · differs: ") + differs.join(QStringLiteral("; "))));
    } else {
        check(QStringLiteral("FK-005 relink check (new alignment, picture compared with the print): the same "
                             "assessment whether the target's proxy is held or given back"),
              false, QStringLiteral("setup: a picture of a print that was never exported"));
    }
    restore();
}

void SelfTest::runDisplayProfile() {
    QMetaObject::invokeMethod(window_, "closeDialogs");
    waitFor([] { return false; }, 600);
    const QString d = QDir(dir_).filePath(QStringLiteral("display"));
    QDir(d).removeRecursively();
    QDir().mkpath(d);
    const QVariantMap saved = app_->settings();
    const QString exportDir = QDir(d).filePath(QStringLiteral("export"));
    for (const auto& [k, v] : std::vector<std::pair<QString, QVariant>>{
             {"exportFolder", exportDir}, {"exportFormat", "tif"}, {"geometryMode", "crop"}})
        app_->setSetting(k, v);
    QObject* canvas = findCanvas();
    auto restore = [&] {
        for (const char* k : {"exportFolder", "exportFormat", "geometryMode"})
            app_->setSetting(QString::fromLatin1(k), saved.value(QString::fromLatin1(k)));
        app_->setDisplayProfileOverride(std::nullopt);
        app_->refreshDisplayProfile(window_->screen());
        app_->jobRunner()->setTestDelayMs(0);
        if (canvas) QMetaObject::invokeMethod(canvas, "fit");
    };
    // two "monitors": sRGB and a wide-gamut one (Adobe RGB as its profile)
    DisplayProfileInfo srgb;
    srgb.source = QStringLiteral("self-test: sRGB monitor");
    DisplayProfileInfo wide;
    wide.profile = fk::Profile::adobeRGB1998();
    wide.id = QString::fromStdString(wide.profile.sha256());
    wide.description = QString::fromStdString(wide.profile.description());
    wide.source = QStringLiteral("self-test: wide-gamut monitor");
    // what the canvas must show for a profile: the same proxy pipeline, computed here independently
    auto proxyOf = [&](const QString& path, const DisplayProfileInfo& p) {
        auto src = fk::openTiff(toPath(path));
        if (!src) return QImage();
        fk::CancelToken cancel;
        auto proxy = fk::buildProxy(**src, 3000, cancel, [](double, std::string_view) {});
        DisplayConverter conv;
        if (!proxy || !conv.init((*src)->info(), &p)) return QImage();
        return conv.convertProxy(*proxy);
    };
    auto proxyFor = [&](const DisplayProfileInfo& p) { return proxyOf(scan_, p); };
    auto meanDiff = [](const QImage& a, const QImage& b) {
        if (a.isNull() || b.isNull() || a.size() != b.size()) return 1e9;
        double sum = 0;
        for (int y = 0; y < a.height(); ++y)
            for (int x = 0; x < a.width(); ++x) {
                const QRgb p = a.pixel(x, y), q = b.pixel(x, y);
                sum += std::abs(qRed(p) - qRed(q)) + std::abs(qGreen(p) - qGreen(q)) +
                       std::abs(qBlue(p) - qBlue(q));
            }
        return sum / (3.0 * a.width() * a.height());
    };
    // waits, without running the UI thread, until a job has converted and its result is queued (large scans
    // take longer than any fixed pause)
    auto untilWorkEnded = [&](int job) {
        QElapsedTimer t;
        t.start();
        while (!app_->jobRunner()->workEnded(job) && t.elapsed() < 300000)
            QThread::msleep(20);
        return app_->jobRunner()->workEnded(job);
    };
    app_->setDisplayProfileOverride(srgb);
    app_->newProject();
    app_->importScans({scan_});
    app_->waitIdle(300000);
    QCoreApplication::processEvents();
    const QString scanId = app_->currentScanId();
    // a second scan, not on screen when the window moves: its thumbnail is converted, its proxy read again
    // later
    const QString other = QDir(d).filePath(QStringLiteral("other.tif"));
    QString otherId;
    if (const fk::ItemRecord* it = app_->item(toStd(app_->currentItemId()));
        it && writeFourOrientations(scan_, it->outer, other)) {
        app_->importScans({other});
        app_->waitIdle(300000);
        for (const auto& sc : app_->project().scans)
            if (toQ(sc.scanId) != scanId) otherId = toQ(sc.scanId);
        app_->selectScan(scanId);
        app_->waitIdle(120000);
        QCoreApplication::processEvents();
    }
    const QImage srgbExpected = proxyFor(srgb), wideExpected = proxyFor(wide);
    const fk::ItemRecord* first = app_->item(toStd(app_->currentItemId()));
    check(QStringLiteral("FK-011 setup: the scan shown for an sRGB monitor"),
          first && !srgbExpected.isNull() && app_->canvasScan().display == srgbExpected &&
              meanDiff(srgbExpected, wideExpected) > 0.5,
          QStringLiteral("sRGB vs wide-gamut conversion differ by %1 levels")
              .arg(meanDiff(srgbExpected, wideExpected)));
    if (!first || srgbExpected.isNull()) {
        restore();
        return;
    }
    const std::string itemId = first->itemId;
    // the Full patch of the print's image area, reduced to the proxy's scale, against the proxy
    auto fullVsPreview = [&] {
        const fk::RectI b = fk::boundingRect(first->inner);
        const int k = std::max(1, app_->canvasScan().proxyScale);
        const QRectF r(b.x, b.y, (b.w / k) * k, (b.h / k) * k);
        QImage patch;
        app_->requestDetail(r, static_cast<int>(r.width()) / k, static_cast<int>(r.height()) / k, 0,
                            [&](QImage img, QRectF, quint64) { patch = img; });
        app_->waitIdle(120000);
        QCoreApplication::processEvents();
        const QImage proxy = app_->canvasScan().display.copy(
            QRect(b.x / k, b.y / k, static_cast<int>(r.width()) / k, static_cast<int>(r.height()) / k));
        return patch.size() == proxy.size() ? meanDiff(patch.convertToFormat(QImage::Format_RGB32), proxy)
                                            : 1e9;
    };
    const double agreeSrgb = fullVsPreview();
    // an output made now, to compare with one made for the other monitor
    app_->exportImageAreas({toQ(itemId)});
    app_->waitIdle(300000);
    const QImage thumbBefore = app_->thumbnail(QStringLiteral("scan/") + scanId);
    const QImage otherBefore = app_->thumbnail(QStringLiteral("scan/") + otherId);

    // 1. the window moves to the wide-gamut monitor
    const quint64 genBefore = app_->displayGeneration();
    app_->setDisplayProfileOverride(wide);
    app_->waitIdle(120000);
    QCoreApplication::processEvents();
    const bool proxyConverted = app_->canvasScan().display == wideExpected;
    const double agreeWide = fullVsPreview();
    const QImage thumbAfter = app_->thumbnail(QStringLiteral("scan/") + scanId);
    const double thumbVsProxy = meanDiff(thumbAfter, boxDownscale(wideExpected, 320));
    // the other scan's thumbnail: converted from the old display profile (not read again): close to what the
    // scan converted for the new profile gives
    const QImage otherAfter = app_->thumbnail(QStringLiteral("scan/") + otherId);
    // (thumbnails: the proxy reduced to 320 px on the long side, as the controller makes them)
    const double otherVsExpected = meanDiff(otherAfter, boxDownscale(proxyOf(other, wide), 320));
    check(QStringLiteral(
              "FK-011 another monitor's profile: the proxy is converted again, Preview and Full agree, "
              "thumbnails follow"),
          app_->displayGeneration() > genBefore && proxyConverted && agreeSrgb < 3 && agreeWide < 3 &&
              meanDiff(thumbBefore, thumbAfter) > 0.3 && thumbVsProxy < 3 && !otherId.isEmpty() &&
              meanDiff(otherBefore, otherAfter) > 0.3 && otherVsExpected < 3,
          QStringLiteral("Full vs Preview %1 (sRGB) / %2 (wide) levels · thumbnail vs proxy %3 · other "
                         "scan's thumbnail vs expected %4 · profile: %5")
              .arg(agreeSrgb, 0, 'f', 2)
              .arg(agreeWide, 0, 'f', 2)
              .arg(thumbVsProxy, 0, 'f', 2)
              .arg(otherVsExpected, 0, 'f', 2)
              .arg(app_->displayProfileName()));
    if (canvas) {
        // at 100 % on the sRGB monitor, then the window moves: the patch on screen is replaced by one made
        // for the new profile
        auto fullFor = [&](const QString& id) {
            return waitFor(
                [&] {
                    return canvas->property("badge").toString() == QLatin1String("Full") &&
                           canvas->property("detailProfile").toString() == id;
                },
                20000);
        };
        app_->setDisplayProfileOverride(srgb);
        app_->waitIdle(120000);
        QMetaObject::invokeMethod(canvas, "zoomTo", Q_ARG(double, 100.0));
        const bool before = fullFor(srgb.id);
        app_->setDisplayProfileOverride(wide);
        // at once: the patch made for the previous profile is no longer drawn (no colour jump while the new
        // one is being made)
        const QString meanwhile = canvas->property("detailProfile").toString();
        app_->waitIdle(120000);
        const bool after = fullFor(wide.id);
        const QString badge = canvas->property("badge").toString() + QStringLiteral(" · ") +
                              canvas->property("detailProfile").toString().left(12);
        snapshot(QStringLiteral("U025-wide-gamut-100pct"));
        QMetaObject::invokeMethod(canvas, "fit");
        check(QStringLiteral("FK-011 full resolution at 100 %: the patch on screen is made again for the new "
                             "profile"),
              before && meanwhile != srgb.id && after, badge);
    }

    // 2. outputs never depend on the monitor
    app_->exportImageAreas({toQ(itemId)});
    app_->waitIdle(300000);
    const QStringList edits = filesMatching(exportDir, QStringLiteral("*_EDIT_v00*.tif"));
    QString why;
    check(QStringLiteral("FK-011 outputs are the same whatever the display profile"),
          edits.size() == 2 && samePixels(edits[0], edits[1], why), why);

    // 3. back to the sRGB monitor: exactly as before
    app_->setDisplayProfileOverride(srgb);
    app_->waitIdle(120000);
    QCoreApplication::processEvents();
    check(QStringLiteral("FK-011 back on the first monitor: the same picture as before"),
          app_->canvasScan().display == srgbExpected);

    // 4. a proxy converted for the old profile while it was being read is never shown: the window moves again
    // after the job converted (the UI thread does not run in between) and before its result arrives
    app_->setDisplayProfileOverride(wide); // reads the scan again (a job)
    const int job = app_->jobRunner()->lastJobId();
    const bool converted = untilWorkEnded(job); // converted for "wide", result not delivered yet
    app_->setDisplayProfileOverride(srgb);
    app_->waitIdle(120000);
    QCoreApplication::processEvents();
    check(QStringLiteral(
              "FK-011 a proxy converted for the previous monitor is not shown (outdated, read again)"),
          converted && app_->jobRunner()->finalState(job) == QLatin1String("outdated") &&
              app_->canvasScan().display == srgbExpected,
          app_->jobRunner()->finalState(job));

    // 5. the same for a full-resolution patch and a composite preview converted for the previous monitor
    {
        const fk::RectI b = fk::boundingRect(first->inner);
        bool delivered = false;
        app_->requestDetail(QRectF(b.x, b.y, b.w, b.h), b.w / 4, b.h / 4, 0,
                            [&](QImage, QRectF, quint64) { delivered = true; });
        const bool patchConverted = untilWorkEnded(app_->jobRunner()->lastJobId()); // converted for sRGB
        app_->setDisplayProfileOverride(wide);
        app_->waitIdle(120000);
        QCoreApplication::processEvents();
        check(
            QStringLiteral("FK-011 a full-resolution patch converted for the previous monitor is not shown"),
            patchConverted && !delivered);
        // composite: the unchanged second export relinked to the print
        QVariantList rows;
        auto c = connect(app_, &AppController::relinkResults, this, [&](const QVariantList& r) { rows = r; });
        app_->relinkFiles({edits.value(1)});
        app_->waitIdle(300000);
        disconnect(c);
        app_->applyRelink(rows);
        QMetaObject::invokeMethod(window_, "closeDialogs");
        app_->selectItem(toQ(itemId));
        app_->requestCompositePreview(toQ(itemId));
        const int previewJob = app_->jobRunner()->lastJobId();
        const bool previewConverted = untilWorkEnded(previewJob); // converted for "wide"
        app_->setDisplayProfileOverride(srgb);
        app_->waitIdle(120000);
        QCoreApplication::processEvents();
        const QString state = app_->jobRunner()->finalState(previewJob);
        const bool dropped = app_->compositePreview(itemId).isNull();
        app_->requestCompositePreview(toQ(itemId));
        app_->waitIdle(120000);
        check(QStringLiteral(
                  "FK-011 a composite preview converted for the previous monitor is not shown; asked "
                  "again, it comes for the current one"),
              previewConverted && state == QLatin1String("outdated") && dropped &&
                  !app_->compositePreview(itemId).isNull(),
              QStringLiteral("%1 · %2").arg(state,
                                            dropped ? QStringLiteral("not shown") : QStringLiteral("shown")));
    }
    restore();
}

void SelfTest::runFormats() {
    QMetaObject::invokeMethod(window_, "closeDialogs");
    waitFor([] { return false; }, 600);
    const QString d = QDir(dir_).filePath(QStringLiteral("formats"));
    QDir(d).removeRecursively();
    QDir().mkpath(d);
    const QVariantMap saved = app_->settings();
    const QString exportDir = QDir(d).filePath(QStringLiteral("export"));
    for (const auto& [k, v] : std::vector<std::pair<QString, QVariant>>{
             {"exportFolder", exportDir}, {"exportFormat", "dng"}, {"geometryMode", "crop"}})
        app_->setSetting(k, v);
    auto restore = [&] {
        for (const char* k : {"exportFolder", "exportFormat", "geometryMode"})
            app_->setSetting(QString::fromLatin1(k), saved.value(QString::fromLatin1(k)));
        QMetaObject::invokeMethod(window_, "closeDialogs");
    };
    app_->newProject();
    app_->importScans({scan_});
    app_->waitIdle(300000);
    const fk::ScanRecord* sc = app_->project().scans.empty() ? nullptr : &app_->project().scans.front();
    const QString name =
        QStringLiteral("FK-012 a Linear DNG given back (relink, dropped) is refused with what to do "
                       "instead, never imported as a scan");
    if (!sc || sc->items.empty() || sc->iccSha256.empty()) {
        skip(name, QStringLiteral("Linear DNG export needs a scan with an ICC profile (this one has none)"));
        restore();
        return;
    }
    app_->exportImageAreas({app_->currentItemId()});
    app_->waitIdle(300000);
    const QStringList dngs = filesMatching(exportDir, QStringLiteral("*.dng"));
    auto messageOf = [&](const QVariantList& rows) {
        return rows.value(0).toMap().value("message").toString();
    };
    auto guidance = [](const QString& m) {
        return (m.contains(QLatin1String("Linear DNG")) && m.contains(QLatin1String("16-bit TIFF"))) ||
               (m.contains(QStringLiteral("线性 DNG")) && m.contains(QStringLiteral("16 位 TIFF")));
    };
    QVariantList rows;
    auto c = connect(app_, &AppController::relinkResults, this, [&](const QVariantList& r) { rows = r; });
    app_->relinkFiles({dngs.value(0)});
    app_->waitIdle(300000);
    const QString relinkMessage = messageOf(rows);
    rows.clear();
    const int scansBefore = static_cast<int>(app_->project().scans.size());
    app_->dropFiles({QUrl::fromLocalFile(dngs.value(0))});
    app_->waitIdle(300000);
    disconnect(c);
    const QString dropMessage = messageOf(rows);
    check(name,
          dngs.size() == 1 && guidance(relinkMessage) && guidance(dropMessage) &&
              static_cast<int>(app_->project().scans.size()) == scansBefore,
          relinkMessage);
    restore();
}

int SelfTest::scenarioMinIsWhite() {
    // FK-011 (fixed in U025): rows from the TIFF reader are already in normal polarity; the display converter
    // inverted MinIsWhite again, so Preview (proxy) and Full (rows) disagreed (15.8 vs 239.2 before the fix).
    app_->importScans({scan_});
    app_->waitIdle(300000);
    auto src = fk::openTiff(toPath(scan_));
    if (!src) return 1;
    const fk::ImageInfo info = (*src)->info();
    const bool miw = info.photometric == fk::Photometric::min_is_white;
    DisplayConverter conv;
    conv.init(info);
    const int64_t w = std::min<int64_t>(info.width, 512), y0 = info.height / 2;
    std::vector<uint8_t> rows(static_cast<size_t>(info.width) * info.channels * (info.bitsPerSample / 8));
    double fullMean = 0;
    std::vector<uint32_t> out(static_cast<size_t>(info.width));
    if ((*src)->readRows(y0, 1, 0, info.width, rows.data(), rows.size())) {
        conv.convertRow(rows.data(), info.width, out.data());
        for (int64_t x = 0; x < w; ++x)
            fullMean += qGray(out[static_cast<size_t>(x)]);
        fullMean /= static_cast<double>(w);
    }
    const auto cs = app_->canvasScan();
    double proxyMean = 0;
    if (!cs.display.isNull()) {
        const int py = static_cast<int>(y0 / std::max(1, cs.proxyScale));
        const int pw = static_cast<int>(w / std::max(1, cs.proxyScale));
        for (int x = 0; x < std::max(1, pw); ++x)
            proxyMean += qGray(cs.display.pixel(x, std::min(py, cs.display.height() - 1)));
        proxyMean /= std::max(1, pw);
    }
    observe(QStringLiteral("M1 MinIsWhite preview vs full"),
            {{"minIsWhite", miw}, {"fullMean", fullMean}, {"proxyMean", proxyMean}});
    check(QStringLiteral("FK-011 MinIsWhite %1-bit: Preview and Full show the same brightness")
              .arg(info.bitsPerSample),
          miw && std::abs(fullMean - proxyMean) < 6,
          QStringLiteral("full %1 vs preview %2").arg(fullMean, 0, 'f', 1).arg(proxyMean, 0, 'f', 1));
    return allOk_ ? 0 : 1;
}

void SelfTest::runLayout() {
    QMetaObject::invokeMethod(window_, "closeDialogs");
    const QVariantMap saved = app_->settings();
    const QStringList keys{QStringLiteral("showLeftRail"), QStringLiteral("showRightRail"),
                           QStringLiteral("showFilmstrip"), QStringLiteral("leftRailWidth"),
                           QStringLiteral("rightRailWidth")};
    auto settle = [] { waitFor([] { return false; }, 250); };
    auto restore = [&] {
        for (const auto& k : keys)
            app_->setSetting(k, saved.value(k));
        window_->resize(1440, 900);
        settle();
    };
    for (const auto& k :
         {QStringLiteral("showLeftRail"), QStringLiteral("showRightRail"), QStringLiteral("showFilmstrip")})
        app_->setSetting(k, true);
    app_->setSetting(QStringLiteral("leftRailWidth"), 254);
    app_->setSetting(QStringLiteral("rightRailWidth"), 300);
    app_->newProject();
    app_->importScans({scan_});
    app_->waitIdle(300000);
    window_->resize(1440, 900);
    settle();

    QQuickItem* left = findItem(QStringLiteral("leftRail"));
    QQuickItem* right = findItem(QStringLiteral("rightRail"));
    QQuickItem* film = findItem(QStringLiteral("filmstrip"));
    QQuickItem* top = findItem(QStringLiteral("topBar"));
    QQuickItem* eL = findItem(QStringLiteral("edgeLeft"));
    QQuickItem* eR = findItem(QStringLiteral("edgeRight"));
    QQuickItem* eB = findItem(QStringLiteral("edgeBottom"));
    auto* canvas = qobject_cast<QQuickItem*>(findCanvas());
    auto* area = window_->property("contentItem").value<QQuickItem*>(); // below an in-window menu bar
    const bool present = left && right && film && top && eL && eR && eB && canvas && area;
    check(QStringLiteral("UX-10 setup: rails, filmstrip, top bar, edge strips and canvas"), present);
    if (!present) {
        restore();
        return;
    }
    canvas->setProperty("mode", QStringLiteral("scan"));
    canvas->setProperty("tool", QStringLiteral("select"));
    auto rect = [](QQuickItem* it) { return it->mapRectToScene(QRectF(0, 0, it->width(), it->height())); };
    auto near = [](double a, double b) { return std::abs(a - b) < 0.75; };
    auto r2s = [](const QRectF& r) {
        return QStringLiteral("%1,%2 %3×%4").arg(r.x()).arg(r.y()).arg(r.width()).arg(r.height());
    };
    // the whole scan inside the canvas, touching it (less the fit padding) on one axis
    auto wholeAtFit = [&](QString* why = nullptr) {
        const QVariantMap sc = app_->currentScan();
        QPointF a, b;
        QMetaObject::invokeMethod(canvas, "mapFromScan", Q_RETURN_ARG(QPointF, a), Q_ARG(double, 0.0),
                                  Q_ARG(double, 0.0));
        QMetaObject::invokeMethod(canvas, "mapFromScan", Q_RETURN_ARG(QPointF, b),
                                  Q_ARG(double, sc.value("width").toDouble()),
                                  Q_ARG(double, sc.value("height").toDouble()));
        const QRectF s = QRectF(a, b).normalized();
        const double w = canvas->width(), h = canvas->height();
        const bool inside =
            s.left() >= -0.5 && s.top() >= -0.5 && s.right() <= w + 0.5 && s.bottom() <= h + 0.5;
        const bool fills = s.width() >= w - 2 * 16 - 1 || s.height() >= h - 2 * 16 - 1;
        if (why)
            *why = QStringLiteral("scan at %1 in a %2×%3 canvas, atFit %4")
                       .arg(r2s(s))
                       .arg(w)
                       .arg(h)
                       .arg(canvas->property("atFit").toBool());
        return canvas->property("atFit").toBool() && inside && fills;
    };
    // a print other than the current one: clicking its middle must select it
    auto clickOtherPrint = [&](QString* why) {
        const QString cur = app_->currentItemId();
        const QString scanId = app_->currentScanId();
        for (const auto& sc : app_->project().scans) {
            if (toQ(sc.scanId) != scanId) continue;
            for (const auto& it : sc.items) {
                if (toQ(it.itemId) == cur) continue;
                const fk::Vec2 c = fk::centroid(it.outer);
                QPointF at;
                QMetaObject::invokeMethod(canvas, "mapFromScan", Q_RETURN_ARG(QPointF, at),
                                          Q_ARG(double, c.x), Q_ARG(double, c.y));
                click(canvas, at);
                settle();
                *why = QStringLiteral("clicked %1 at %2,%3; current %4")
                           .arg(toQ(it.itemId))
                           .arg(at.x())
                           .arg(at.y())
                           .arg(app_->currentItemId());
                return app_->currentItemId() == toQ(it.itemId);
            }
        }
        *why = QStringLiteral("only one print on this scan");
        return false;
    };
    const int prints = app_->currentScan().value("itemCount").toInt();

    // ---- structure: full-width filmstrip under rails and canvas; strips on the window's edges
    auto structure = [&](QString* why) {
        const QRectF a = rect(area), f = rect(film), l = rect(left), r = rect(right), c = rect(canvas);
        const QRectF sl = rect(eL), sr = rect(eR), sb = rect(eB);
        const double floor = film->isVisible() ? f.top() : sb.top();
        const bool fullWidth = !film->isVisible() || (near(f.left(), a.left()) && near(f.right(), a.right()));
        const bool above = (!left->isVisible() || l.bottom() <= floor + 0.5) &&
                           (!right->isVisible() || r.bottom() <= floor + 0.5) && c.bottom() <= floor + 0.5;
        const bool edges = near(sl.left(), a.left()) && near(sr.right(), a.right()) &&
                           near(sb.bottom(), a.bottom()) && near(sb.left(), a.left()) &&
                           near(sb.right(), a.right()) && sl.bottom() <= sb.top() + 0.5 &&
                           sr.bottom() <= sb.top() + 0.5;
        *why = QStringLiteral(
                   "window %1 · filmstrip %2 · left %3 · right %4 · canvas %5 · strips L %6 R %7 B %8")
                   .arg(r2s(a), r2s(f), r2s(l), r2s(r), r2s(c), r2s(sl), r2s(sr), r2s(sb));
        return fullWidth && above && edges;
    };
    QString why;
    check(QStringLiteral("UX-10 the filmstrip spans the whole window; the rails and the canvas end above it; "
                         "edge strips on the left, right and bottom window edges"),
          structure(&why), why);
    snapshot(QStringLiteral("U022-layout"));
    check(QStringLiteral(
              "UX-10 the top bar no longer carries fold buttons (edge strips, View menu, shortcuts)"),
          findItemIf([](QQuickItem* it) {
              const QString n = it->property("iconName").toString();
              return it->isVisible() && (n == QLatin1String("foldLeft") || n == QLatin1String("foldRight"));
          }) == nullptr);

    // ---- each panel folds and unfolds with its strip (mouse); the strip stays put; the canvas refits
    struct Panel {
        QString name, key;
        QQuickItem* strip;
        QQuickItem* panel;
        bool vertical; // a side rail (the canvas grows in width)
    };
    const std::vector<Panel> panels{
        {QStringLiteral("left rail"), QStringLiteral("showLeftRail"), eL, left, true},
        {QStringLiteral("right rail"), QStringLiteral("showRightRail"), eR, right, true},
        {QStringLiteral("filmstrip"), QStringLiteral("showFilmstrip"), eB, film, false}};
    for (const auto& p : panels) {
        QMetaObject::invokeMethod(canvas, "fit");
        settle();
        const QSizeF before = canvas->size();
        const QRectF stripBefore = rect(p.strip);
        const QString tipOpen = p.strip->property("tip").toString();
        click(p.strip);
        settle();
        const QSizeF folded = canvas->size();
        const QString tipFolded = p.strip->property("tip").toString();
        QString fitWhy;
        const bool foldOk =
            !p.panel->isVisible() && !app_->settings().value(p.key).toBool() && p.strip->isVisible() &&
            rect(p.strip) == stripBefore &&
            (p.vertical ? folded.width() > before.width() + 100 : folded.height() > before.height() + 100) &&
            tipFolded != tipOpen && wholeAtFit(&fitWhy);
        click(p.strip);
        settle();
        const bool unfoldOk = p.panel->isVisible() && app_->settings().value(p.key).toBool() &&
                              canvas->size() == before && rect(p.strip) == stripBefore && wholeAtFit();
        check(QStringLiteral(
                  "UX-10 the %1 folds and unfolds with its edge strip (mouse); the strip stays on the "
                  "edge; the canvas grows and refits the whole scan")
                  .arg(p.name),
              foldOk && unfoldOk,
              QStringLiteral("canvas %1×%2 → %3×%4 → %5×%6; tip \"%7\" → \"%8\"; %9")
                  .arg(before.width())
                  .arg(before.height())
                  .arg(folded.width())
                  .arg(folded.height())
                  .arg(canvas->width())
                  .arg(canvas->height())
                  .arg(tipOpen, tipFolded, fitWhy));
    }

    // ---- the hit area reaches past the drawn strip where the neighbour has an empty inset
    {
        const int reachL = eL->property("hitWidth").toInt(), reachB = eB->property("hitWidth").toInt();
        click(eL, QPointF(reachL - 2, eL->height() / 2)); // over the left rail's inset
        const bool l = !left->isVisible();
        click(eL);
        click(eB, QPointF(eB->width() / 2, eB->height() - reachB + 2)); // over the filmstrip's margin
        const bool b = !film->isVisible();
        click(eB);
        settle();
        check(
            QStringLiteral("UX-10 the strips answer clicks beyond their drawn width (left and bottom: %1 px)")
                .arg(reachL),
            l && b && left->isVisible() && film->isVisible() && reachL >= 12 && reachB >= 12,
            QStringLiteral("left %1, bottom %2").arg(l).arg(b));
    }

    // ---- all three folded: strips still on the edges, the canvas takes the rest, clicks land on prints
    click(eL);
    click(eR);
    click(eB);
    settle();
    {
        const QRectF a = rect(area), c = rect(canvas);
        const double s = eL->width();
        QString fitWhy, structWhy;
        const bool layoutOk = !left->isVisible() && !right->isVisible() && !film->isVisible() &&
                              eL->isVisible() && eR->isVisible() && eB->isVisible() &&
                              near(c.left(), a.left() + s) && near(c.right(), a.right() - s) &&
                              near(c.bottom(), a.bottom() - s) && near(c.top(), rect(top).bottom()) &&
                              structure(&structWhy);
        check(QStringLiteral(
                  "UX-10 all three folded: the strips stay on the edges and the canvas takes the rest, "
                  "whole scan at fit"),
              layoutOk && wholeAtFit(&fitWhy), QStringLiteral("canvas %1; %2").arg(r2s(c), fitWhy));
        snapshot(QStringLiteral("U022-all-folded"));
    }
    if (prints > 1) {
        const bool hit1 = clickOtherPrint(&why);
        window_->resize(1100, 700);
        settle();
        QString why2;
        const bool hit2 = clickOtherPrint(&why2);
        check(QStringLiteral(
                  "UX-10 clicks land on the print under the pointer after folding and after resizing "
                  "the window"),
              hit1 && hit2, why + QStringLiteral(" · ") + why2);
    } else {
        skip(QStringLiteral(
                 "UX-10 clicks land on the print under the pointer after folding and after resizing "
                 "the window"),
             QStringLiteral("one print on this scan"));
    }
    // unfold again (mouse)
    click(eL);
    click(eR);
    click(eB);
    settle();

    // ---- window sizes: minimum, default, large (U021: the top bar fits at the minimum too; was a known
    // issue)
    {
        QStringList bad;
        QQuickItem* zoom = findItem(QStringLiteral("topBarZoom"));
        auto topBarFits = [&] { return zoom && rect(zoom).right() <= rect(top).right() + 0.5; };
        for (const QSize sz : {QSize(1100, 700), QSize(1440, 900), QSize(1920, 1080)}) {
            window_->resize(sz);
            settle();
            QString st, f;
            const bool ok = structure(&st) && wholeAtFit(&f) && topBarFits();
            if (sz.width() == 1100) snapshot(QStringLiteral("U022-1100x700"));
            if (!ok)
                bad << QStringLiteral("%1×%2: %3; %4; zoom at %5, top bar %6")
                           .arg(sz.width())
                           .arg(sz.height())
                           .arg(st, f, zoom ? r2s(rect(zoom)) : QStringLiteral("missing"), r2s(rect(top)));
        }
        check(QStringLiteral(
                  "UX-10 window at 1100×700 (minimum), 1440×900, 1920×1080: filmstrip full width, rails "
                  "and canvas above it, strips on the edges, whole scan at fit, top bar fits"),
              bad.isEmpty(), bad.join(QStringLiteral(" | ")));
        window_->resize(1440, 900);
        settle();
    }

    // ---- keyboard: the strip is a button (Tab focus, Space); the View menu shortcuts still work
    {
        eL->forceActiveFocus();
        key(Qt::Key_Space, Qt::NoModifier, true, QStringLiteral(" "));
        key(Qt::Key_Space, Qt::NoModifier, false, QStringLiteral(" "));
        settle();
        const bool folded = !left->isVisible();
        key(Qt::Key_Space, Qt::NoModifier, true, QStringLiteral(" "));
        key(Qt::Key_Space, Qt::NoModifier, false, QStringLiteral(" "));
        settle();
        check(QStringLiteral("UX-10 an edge strip with keyboard focus folds and unfolds its panel (Space)"),
              eL->property("activeFocusOnTab").toBool() && folded && left->isVisible());
        canvas->forceActiveFocus();
    }
    const QString shortcutName =
        QStringLiteral("UX-10 the shortcuts still fold and unfold the panels (Ctrl+Alt+1 / 2 / 3 / 0)");
    if (window_->property("isMac").toBool()) {
        skip(shortcutName,
             QStringLiteral("macOS: the shortcuts belong to the native menu bar, which the offscreen "
                            "window does not have; the menu items call the same commands"));
    } else {
        auto chord = [&](int k, const QString& text) {
            key(k, Qt::ControlModifier | Qt::AltModifier, true, text);
            key(k, Qt::ControlModifier | Qt::AltModifier, false, text);
            settle();
        };
        QStringList steps;
        auto state = [&] {
            return QStringLiteral("%1%2%3")
                .arg(left->isVisible() ? 'L' : '-')
                .arg(right->isVisible() ? 'R' : '-')
                .arg(film->isVisible() ? 'F' : '-');
        };
        chord(Qt::Key_1, QStringLiteral("1"));
        steps << state();
        chord(Qt::Key_2, QStringLiteral("2"));
        steps << state();
        chord(Qt::Key_3, QStringLiteral("3"));
        steps << state();
        chord(Qt::Key_0, QStringLiteral("0")); // "All Panels": none shown → all shown
        steps << state();
        chord(Qt::Key_0, QStringLiteral("0")); // some shown → all folded
        steps << state();
        check(shortcutName, steps == QStringList{"-RF", "--F", "---", "LRF", "---"},
              steps.join(QStringLiteral(" → ")));
        chord(Qt::Key_0, QStringLiteral("0"));
    }

    // ---- stored for the next start: folded state and a width dragged with the grip
    {
        QQuickItem* grip = findItemIf([](QQuickItem* it) {
            return it->property("target").toString() == QLatin1String("rightRailWidth") && it->isVisible();
        });
        const int w0 = app_->settings().value("rightRailWidth").toInt();
        if (grip) {
            // scene positions fixed at the press: the grip moves left while the rail widens
            QQuickItem* scene = window_->contentItem();
            const QPointF at = grip->mapToScene(QPointF(grip->width() / 2, grip->height() / 2));
            mouse(scene, at, QEvent::MouseButtonPress);
            for (int k = 1; k <= 4; ++k)
                mouse(scene, at + QPointF(-10 * k, 0), QEvent::MouseMove);
            mouse(scene, at + QPointF(-40, 0), QEvent::MouseButtonRelease);
        }
        click(eL);
        settle();
        const int w1 = app_->settings().value("rightRailWidth").toInt();
        const bool stored =
            app_->storedSetting(QStringLiteral("rightRailWidth")).toInt() == w1 &&
            app_->storedSetting(QStringLiteral("showLeftRail")).toString() == QLatin1String("false");
        check(QStringLiteral(
                  "UX-10 a rail width dragged with the grip and a folded panel are stored for the next "
                  "start"),
              grip && w1 == w0 + 40 && stored,
              QStringLiteral("right rail %1 → %2 px, stored %3; left rail stored shown = %4")
                  .arg(w0)
                  .arg(w1)
                  .arg(app_->storedSetting(QStringLiteral("rightRailWidth")).toString(),
                       app_->storedSetting(QStringLiteral("showLeftRail")).toString()));
        app_->setSetting(QStringLiteral("showFilmstrip"), false);
        app_->setSetting(QStringLiteral("leftRailWidth"), 300);
        QObject* commands = window_->property("commands").value<QObject*>();
        if (commands) QMetaObject::invokeMethod(commands, "resetLayout");
        settle();
        const QVariantMap s = app_->settings();
        check(QStringLiteral("UX-10 Reset Layout: default rail widths and every panel shown"),
              commands && s.value("showLeftRail").toBool() && s.value("showRightRail").toBool() &&
                  s.value("showFilmstrip").toBool() && s.value("leftRailWidth").toInt() == 254 &&
                  s.value("rightRailWidth").toInt() == 300 && left->isVisible() && right->isVisible() &&
                  film->isVisible() && std::abs(left->width() - 254) < 0.5 &&
                  std::abs(right->width() - 300) < 0.5,
              QStringLiteral("left %1 px, right %2 px").arg(left->width()).arg(right->width()));
    }
    restore();
}

void SelfTest::runToolbar() {
    QMetaObject::invokeMethod(window_, "closeDialogs");
    const QVariantMap saved = app_->settings();
    const QStringList keys{"showLeftRail",   "showRightRail", "showFilmstrip", "leftRailWidth",
                           "rightRailWidth", "rightTab",      "exportFolder",  "exportFormat",
                           "geometryMode",   "applyUpright",  "finalMode",     "finalFormat"};
    auto settle = [] { waitFor([] { return false; }, 250); };
    auto restore = [&] {
        for (const auto& k : keys)
            app_->setSetting(k, saved.value(k));
        window_->resize(1440, 900);
        settle();
    };
    const QString d = QDir(dir_).filePath(QStringLiteral("toolbar"));
    QDir(d).removeRecursively();
    QDir().mkpath(d);
    const QString exportDir = QDir(d).filePath(QStringLiteral("export"));
    for (const auto& [k, v] : std::vector<std::pair<QString, QVariant>>{{"exportFolder", exportDir},
                                                                        {"exportFormat", "tif"},
                                                                        {"geometryMode", "crop"},
                                                                        {"applyUpright", false},
                                                                        {"finalMode", "real"},
                                                                        {"finalFormat", "tif"},
                                                                        {"showLeftRail", true},
                                                                        {"showRightRail", true},
                                                                        {"showFilmstrip", true},
                                                                        {"leftRailWidth", 254},
                                                                        {"rightRailWidth", 300},
                                                                        {"rightTab", 0}})
        app_->setSetting(k, v);
    app_->newProject();
    window_->resize(1440, 900);
    settle();
    QQuickItem* top = findItem(QStringLiteral("topBar"));
    QQuickItem* file = findItem(QStringLiteral("topBarFile"));
    QQuickItem* tools = findItem(QStringLiteral("topBarTools"));
    QQuickItem* toggles = findItem(QStringLiteral("topBarToggles"));
    QQuickItem* eye = findItem(QStringLiteral("overlaysButton"));
    QQuickItem* view = findItem(QStringLiteral("viewSwitch"));
    QQuickItem* viewCompact = findItem(QStringLiteral("viewSwitchCompact"));
    QQuickItem* zoom = findItem(QStringLiteral("topBarZoom"));
    QQuickItem* steps = findItem(QStringLiteral("workflowSteps"));
    auto* canvas = qobject_cast<QQuickItem*>(findCanvas());
    const bool present =
        top && file && tools && toggles && eye && view && viewCompact && zoom && steps && canvas;
    check(QStringLiteral("UX-06 setup: top bar clusters, view switch, workflow steps"), present);
    if (!present) {
        restore();
        return;
    }
    auto rect = [](QQuickItem* it) { return it->mapRectToScene(QRectF(0, 0, it->width(), it->height())); };
    auto r2s = [](const QRectF& r) {
        return QStringLiteral("%1,%2 %3×%4").arg(r.x()).arg(r.y()).arg(r.width()).arg(r.height());
    };
    auto shownView = [&] { return view->isVisible() ? view : viewCompact; };

    // ---- workflow steps before anything is imported: step 1 is next
    auto next = [&] { return steps->property("next").toInt(); };
    const int nextEmpty = next();

    app_->importScans({scan_});
    app_->waitIdle(300000);
    settle();

    // ---- order at a wide window: eye → view switch → zoom field, the view switch as one segmented control
    {
        const QRectF e = rect(eye), v = rect(view), z = rect(zoom), t = rect(top);
        check(
            QStringLiteral("UX-06 the view switch sits between the overlays button (eye) and the zoom field"),
            view->isVisible() && !viewCompact->isVisible() && e.right() <= v.left() &&
                v.right() <= z.left() && t.contains(e) && t.contains(v) && t.contains(z),
            QStringLiteral("eye %1 · view %2 · zoom %3").arg(r2s(e), r2s(v), r2s(z)));
    }

    // ---- every window size × rail widths: inside the bar, no overlap, eye → view → zoom, centre ≥ minimum
    {
        struct Rails {
            QString name;
            bool left, right;
            int lw, rw;
        };
        const std::vector<Rails> rails{{QStringLiteral("default rails"), true, true, 254, 300},
                                       {QStringLiteral("widest rails"), true, true, 380, 420},
                                       {QStringLiteral("left rail folded"), false, true, 254, 300},
                                       {QStringLiteral("both rails folded"), false, false, 254, 300}};
        QStringList bad, levels;
        // 1000×700 is below the window's minimum: only where the platform allows it (offscreen does), to
        // reach the last compaction step (− / + dropped)
        for (const QSize sz :
             {QSize(1000, 700), QSize(1100, 700), QSize(1280, 800), QSize(1440, 900), QSize(1920, 1080)}) {
            for (const auto& r : rails) {
                app_->setSetting(QStringLiteral("showLeftRail"), r.left);
                app_->setSetting(QStringLiteral("showRightRail"), r.right);
                app_->setSetting(QStringLiteral("leftRailWidth"), r.lw);
                app_->setSetting(QStringLiteral("rightRailWidth"), r.rw);
                window_->resize(sz);
                settle();
                if (sz.width() < 1100 && window_->width() != sz.width())
                    continue; // the platform keeps the minimum
                const QRectF t = rect(top);
                std::vector<std::pair<QString, QRectF>> shown;
                for (auto [name, item] : std::vector<std::pair<QString, QQuickItem*>>{{"file", file},
                                                                                      {"tools", tools},
                                                                                      {"toggles", toggles},
                                                                                      {"view", shownView()},
                                                                                      {"zoom", zoom}})
                    if (item->isVisible()) shown.emplace_back(name, rect(item));
                QStringList why;
                for (size_t i = 0; i < shown.size(); ++i) {
                    if (shown[i].second.left() < t.left() - 0.5 || shown[i].second.right() > t.right() + 0.5)
                        why << shown[i].first + QStringLiteral(" outside the bar");
                    if (i > 0 && shown[i - 1].second.right() > shown[i].second.left() + 0.5)
                        why << shown[i - 1].first + QStringLiteral(" overlaps ") + shown[i].first;
                }
                const QRectF e = rect(eye), v = rect(shownView()), z = rect(zoom);
                if (!(e.right() <= v.left() + 0.5 && v.right() <= z.left() + 0.5))
                    why << QStringLiteral("order eye → view → zoom");
                // the canvas keeps 520 px (less below the window's minimum, where the rails are at their
                // minimums)
                if (sz.width() >= 1100 && canvas->width() < 520 - 0.5)
                    why << QStringLiteral("canvas %1 px").arg(canvas->width());
                const int level = top->property("level").toInt();
                if (level == 3 && findItemIf([](QQuickItem* it) {
                        return it->property("iconName").toString() == QLatin1String("zoomIn") &&
                               it->isVisible();
                    }))
                    why << QStringLiteral("− / + still shown at the last step");
                levels << QStringLiteral("%1×%2 %3: level %4")
                              .arg(sz.width())
                              .arg(sz.height())
                              .arg(r.name)
                              .arg(level);
                if (!why.isEmpty())
                    bad << QStringLiteral("%1×%2 %3 (level %4, bar %5): %6")
                               .arg(sz.width())
                               .arg(sz.height())
                               .arg(r.name)
                               .arg(level)
                               .arg(r2s(t), why.join(QStringLiteral(", ")));
                if (sz.width() == 1100 && r.lw == 380) snapshot(QStringLiteral("U021-1100-widest-rails"));
            }
        }
        QStringList needed;
        for (const auto& v : top->property("neededWidths").toList())
            needed << QString::number(v.toDouble());
        observe(QStringLiteral("UX-06 top bar levels"),
                {{"levels", QJsonArray::fromStringList(levels)},
                 {"neededWidths", needed.join(QStringLiteral(" / "))}});
        const bool reachedLast = levels.join(QLatin1Char(' ')).contains(QLatin1String("level 3"));
        check(QStringLiteral(
                  "UX-06 top bar at 1100×700 … 1920×1080 with default, widest and folded rails: nothing "
                  "outside the bar or overlapping, eye → view → zoom, canvas at least 520 px wide"),
              bad.isEmpty(), bad.join(QStringLiteral(" | ")));
        if (reachedLast)
            check(QStringLiteral(
                      "UX-06 below the window's minimum (1000×700): − / + dropped, still inside the bar"),
                  bad.isEmpty());
        else
            skip(QStringLiteral(
                     "UX-06 below the window's minimum (1000×700): − / + dropped, still inside the bar"),
                 QStringLiteral("the platform keeps the window at its minimum size"));
    }

    // ---- the compact view switch (narrowest case) changes the view; the 1–4 keys still update it
    {
        app_->setSetting(QStringLiteral("showLeftRail"), true);
        app_->setSetting(QStringLiteral("showRightRail"), true);
        app_->setSetting(QStringLiteral("leftRailWidth"), 380);
        app_->setSetting(QStringLiteral("rightRailWidth"), 420);
        window_->resize(1100, 700);
        settle();
        canvas->setProperty("mode", QStringLiteral("scan"));
        settle();
        bool picked = false, keyed = false;
        if (viewCompact->isVisible()) {
            viewCompact->forceActiveFocus();
            key(Qt::Key_Down, Qt::NoModifier, true);
            key(Qt::Key_Down, Qt::NoModifier, false);
            settle();
            picked = canvas->property("mode").toString() == QLatin1String("print");
            canvas->forceActiveFocus();
            key(Qt::Key_4, Qt::NoModifier, true, QStringLiteral("4"));
            key(Qt::Key_4, Qt::NoModifier, false, QStringLiteral("4"));
            settle();
            keyed = canvas->property("mode").toString() == QLatin1String("composite") &&
                    viewCompact->property("currentText").toString() ==
                        top->property("modes").toList().at(3).toMap().value("text").toString();
            key(Qt::Key_1, Qt::NoModifier, true, QStringLiteral("1"));
            key(Qt::Key_1, Qt::NoModifier, false, QStringLiteral("1"));
            settle();
        }
        check(QStringLiteral(
                  "UX-06 narrow window: the compact view switch changes the view (keyboard) and follows "
                  "the 1–4 keys"),
              viewCompact->isVisible() && picked && keyed,
              QStringLiteral("compact shown %1, picked %2, keys %3 (%4)")
                  .arg(viewCompact->isVisible())
                  .arg(picked)
                  .arg(keyed)
                  .arg(viewCompact->property("currentText").toString()));
        app_->setSetting(QStringLiteral("leftRailWidth"), 254);
        app_->setSetting(QStringLiteral("rightRailWidth"), 300);
        window_->resize(1440, 900);
        settle();
    }

    // ---- workflow steps follow the current print; a click leads to the step
    {
        const QString id = app_->currentItemId();
        QStringList trail;
        trail << QString::number(nextEmpty);
        trail << QString::number(next()); // imported (reviewed when the detection is sure)
        app_->setReviewed(id, true);
        settle();
        trail << QString::number(next());
        app_->exportImageAreas({id});
        app_->waitIdle(300000);
        settle();
        trail << QString::number(next());
        QVariantList rows;
        auto c = connect(app_, &AppController::relinkResults, this, [&](const QVariantList& r) { rows = r; });
        QVariantList urls;
        for (const auto& f : filesMatching(exportDir, QStringLiteral("*_EDIT_v001.tif")))
            urls << f;
        app_->relinkFiles(urls);
        app_->waitIdle(300000);
        disconnect(c);
        app_->applyRelink(rows);
        QMetaObject::invokeMethod(window_, "closeDialogs");
        settle();
        trail << QString::number(next());
        app_->exportFinal({id}, {});
        app_->waitIdle(300000);
        settle();
        trail << QString::number(next());
        const QString summary = findItem(QStringLiteral("workflowSummary"))
                                    ? findItem(QStringLiteral("workflowSummary"))->property("text").toString()
                                    : QString();
        // the importing step counts as done once a scan is in; detection of the fixture is sure (reviewed)
        const bool trailOk = trail.size() == 6 && trail.at(0) == QLatin1String("0") &&
                             (trail.at(1) == QLatin1String("1") || trail.at(1) == QLatin1String("2")) &&
                             trail.at(2) == QLatin1String("2") && trail.at(3) == QLatin1String("3") &&
                             trail.at(4) == QLatin1String("4") && trail.at(5) == QLatin1String("5");
        check(QStringLiteral(
                  "UX-06 workflow steps follow the print: import → detect/review → export → relink → "
                  "composite/final"),
              trailOk, trail.join(QStringLiteral(" → ")) + QStringLiteral(" · ") + summary);
        QQuickItem* s3 = findItem(QStringLiteral("workflowStep3"));
        QQuickItem* s5 = findItem(QStringLiteral("workflowStep5"));
        app_->setSetting(QStringLiteral("rightTab"), 0);
        canvas->setProperty("mode", QStringLiteral("scan"));
        settle();
        bool dialog = false;
        {
            QVariant open;
            QMetaObject::invokeMethod(window_, "dialogOpen", Q_RETURN_ARG(QVariant, open));
            dialog = open.toBool();
        }
        const QString at3 = s3 ? QStringLiteral("%1,%2 visible %3")
                                     .arg(s3->mapToScene(QPointF(10, 10)).x())
                                     .arg(s3->mapToScene(QPointF(10, 10)).y())
                                     .arg(s3->isVisible())
                               : QStringLiteral("missing");
        const bool clicked3 = s3 && click(s3);
        const int tab3 = app_->settings().value("rightTab").toInt();
        const bool toExport = clicked3 && tab3 == 1;
        const bool clicked5 = s5 && click(s5);
        const int tab5 = app_->settings().value("rightTab").toInt();
        const bool toComposite =
            clicked5 && tab5 == 2 && canvas->property("mode").toString() == QLatin1String("composite");
        check(QStringLiteral("UX-06 a click on a workflow step leads to it (Export tab; Return tab with the "
                             "Composite view)"),
              toExport && toComposite,
              QStringLiteral(
                  "step 3 at %1, clicked %2 → tab %3; step 5 clicked %4 → tab %5, view %6; dialog open %7")
                  .arg(at3)
                  .arg(clicked3)
                  .arg(tab3)
                  .arg(clicked5)
                  .arg(tab5)
                  .arg(canvas->property("mode").toString())
                  .arg(dialog));
        snapshot(QStringLiteral("U021-steps"));
        canvas->setProperty("mode", QStringLiteral("scan"));
    }
    restore();
}

void SelfTest::observe(const QString& name, const QJsonObject& data) {
    QJsonObject o = data;
    o.insert(QStringLiteral("name"), name);
    observations_.append(o);
    qInfo().noquote() << "OBSERVE" << name << QJsonDocument(data).toJson(QJsonDocument::Compact);
}

int SelfTest::runScenario(const QString& name) {
    QDir().mkpath(dir_);
    window_->resize(1440, 900);
    app_->setSetting(QStringLiteral("exportFolder"), QDir(dir_).filePath(QStringLiteral("export")));
    int rc = 2;
    if (name == QLatin1String("relink"))
        rc = scenarioRelink();
    else if (name == QLatin1String("count"))
        rc = scenarioCount();
    else if (name == QLatin1String("upright"))
        rc = scenarioUpright();
    else if (name == QLatin1String("miniswhite"))
        rc = scenarioMinIsWhite();
    else if (name == QLatin1String("memory"))
        rc = scenarioMemory();
    QJsonObject report{
        {"scenario", name},
        {"version", app_->version()},
        {"commit", app_->buildCommit()},
        {"build", app_->buildId()},
        {"scan", scan_},
        {"observations", observations_},
        {"knownIssues", known_},
        {"checks", checks_},
        {"ok", allOk_},
        {"note",
         "Observations of current behaviour (Stage A reproduction); checks, when a scenario has them, decide "
         "the exit code."}};
    QFile f(QDir(dir_).filePath(QStringLiteral("scenario-%1.json").arg(name)));
    if (f.open(QIODevice::WriteOnly)) f.write(QJsonDocument(report).toJson());
    return rc;
}

int SelfTest::scenarioMemory() {
    // FK-005 measurement: `--scan <directory>` of scans (e.g. an fkfixture corpus), FK_BENCH_SCANS=N takes
    // the first N. Records memoryReport() after each stage; the comparison across N is in
    // PERFORMANCE-BUDGET.md.
    QStringList files = filesMatching(scan_, QStringLiteral("*.tif"));
    std::sort(files.begin(), files.end());
    const int n = qEnvironmentVariableIntValue("FK_BENCH_SCANS");
    if (n > 0 && n < files.size()) files = files.mid(0, n);
    if (files.isEmpty()) return 1;
    app_->setSetting(QStringLiteral("exportFormat"), QStringLiteral("tif"));
    app_->setSetting(QStringLiteral("geometryMode"), QStringLiteral("crop"));
    app_->setSetting(QStringLiteral("applyUpright"), false);
    QObject* canvas = findCanvas();
    // waiting for jobs as the event loop would: deferred deletes too (see `stage`)
    auto idle = [&](int ms) {
        app_->waitIdle(ms);
        QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
    };
    QElapsedTimer t;
    t.start();
    auto stage = [&](const QString& name) {
        waitFor([] { return false; }, 1000); // settle: timers
        // the self-test runs before the application's event loop starts, and objects deleted later (QML
        // delegates scrolled away while switching scans) are only deleted by that loop: do what it does, or
        // they pile up and look like a leak (≈ 1.3 MB per switch on the VM)
        QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
        QVariantMap r = app_->memoryReport();
        r["seconds"] = static_cast<double>(t.elapsed()) / 1000.0;
        r["scans"] = static_cast<int>(files.size());
        int prints = 0;
        for (const auto& sc : app_->project().scans)
            prints += static_cast<int>(sc.items.size());
        r["prints"] = prints;
        observe(name, QJsonObject::fromVariantMap(r));
    };
    app_->newProject();
    stage(QStringLiteral("0-idle"));
    QVariantList urls;
    for (const auto& f : files)
        urls << f;
    app_->importScans(urls);
    idle(7200000);
    stage(QStringLiteral("1-detected"));
    std::vector<QString> ids;
    for (const auto& sc : app_->project().scans)
        ids.push_back(toQ(sc.scanId));
    // FK_BENCH_ROUNDS=R switches through every scan R times (default 2) and records each round when R > 2:
    // growth per round separates a leak from memory that settles
    const int rounds = std::max(2, qEnvironmentVariableIntValue("FK_BENCH_ROUNDS"));
    for (int round = 0; round < rounds; ++round) {
        for (const auto& id : ids) {
            app_->selectScan(id);
            idle(600000);
        }
        if (rounds > 2) stage(QStringLiteral("2-switched-round-%1").arg(round + 1));
    }
    stage(QStringLiteral("2-switched-twice"));
    for (const auto& id : ids) {
        app_->selectScan(id);
        idle(600000);
        if (!canvas) break;
        QMetaObject::invokeMethod(canvas, "zoomTo", Q_ARG(double, 100.0));
        waitFor([&] { return canvas->property("badge").toString() == QLatin1String("Full"); }, 30000);
        idle(600000);
        QMetaObject::invokeMethod(canvas, "fit");
    }
    stage(QStringLiteral("3-full-resolution-each"));
    // composite previews for the prints of the first 10 scans (exported unchanged, relinked)
    QStringList items;
    for (size_t i = 0; i < ids.size() && i < 10; ++i)
        for (const auto& sc : app_->project().scans)
            if (toQ(sc.scanId) == ids[i])
                for (const auto& it : sc.items)
                    items << toQ(it.itemId);
    const QString exportDir = app_->settings().value("exportFolder").toString();
    app_->exportImageAreas(items);
    idle(3600000);
    QVariantList rows;
    auto c = connect(app_, &AppController::relinkResults, this, [&](const QVariantList& r) { rows = r; });
    QVariantList edits;
    for (const auto& e : filesMatching(exportDir, QStringLiteral("*_EDIT_v001.tif")))
        edits << e;
    app_->relinkFiles(edits);
    idle(3600000);
    disconnect(c);
    app_->applyRelink(rows);
    QMetaObject::invokeMethod(window_, "closeDialogs");
    if (canvas) canvas->setProperty("mode", QStringLiteral("composite"));
    for (const auto& id : items) {
        app_->selectItem(id);
        idle(600000);
    }
    if (canvas) canvas->setProperty("mode", QStringLiteral("scan"));
    stage(QStringLiteral("4-composite-previews"));
    for (const auto& id : ids)
        app_->detectScan(id);
    waitFor([] { return false; }, 2000);
    app_->jobRunner()->cancelAll();
    idle(600000);
    stage(QStringLiteral("5-detection-cancelled"));
    if (!ids.empty()) app_->selectScan(ids.front());
    idle(600000);
    waitFor([] { return false; }, 3000);
    stage(QStringLiteral("6-steady"));
#if defined(__GLIBC__)
    // what of the steady RSS is memory the app already freed (glibc keeps freed heap pages): measurement
    // only, the product does not trim
    malloc_trim(0);
    stage(QStringLiteral("7-steady-after-malloc-trim"));
#endif
    return 0;
}

int SelfTest::scenarioRelink() {
    const QString exportDir = QDir(dir_).filePath(QStringLiteral("export"));
    QDir(exportDir).removeRecursively();
    app_->setSetting(QStringLiteral("exportFormat"), QStringLiteral("tif"));
    app_->setSetting(QStringLiteral("geometryMode"), QStringLiteral("crop"));
    app_->importScans({scan_});
    app_->waitIdle(300000);
    app_->exportImageAreas({});
    app_->waitIdle(300000);
    const QStringList edits = filesMatching(exportDir, QStringLiteral("*_EDIT_v001.tif"));
    observe(QStringLiteral("export"),
            {{"files", QJsonArray::fromStringList(edits)},
             {"sidecars", filesMatching(exportDir, QStringLiteral("*.fk.json")).size()}});
    if (edits.isEmpty()) return 1;
    const QString f = edits.first();
    const QString gradedDir = QDir(dir_).filePath(QStringLiteral("graded"));
    QDir(gradedDir).removeRecursively();
    QDir().mkpath(gradedDir);

    auto relink = [&](const QString& label, const QString& file, const QString& snap) {
        QVariantList rows;
        auto c = connect(app_, &AppController::relinkResults, this, [&](const QVariantList& r) { rows = r; });
        app_->relinkFiles({file});
        app_->waitIdle(300000);
        disconnect(c);
        QCoreApplication::processEvents();
        if (!snap.isEmpty()) snapshot(snap);
        QMetaObject::invokeMethod(window_, "closeDialogs");
        QJsonArray out;
        for (const auto& r : rows) {
            QVariantMap m = r.toMap();
            out.append(QJsonObject{{"fileName", m.value("fileName").toString()},
                                   {"decision", m.value("decision").toString()},
                                   {"options", static_cast<int>(m.value("options").toList().size())},
                                   {"rotate", m.value("rotate").toInt()},
                                   {"rotationReliable", m.value("rotationReliable").toBool()},
                                   {"matched", m.value("matched").toBool()},
                                   {"source", m.value("source").toString()},
                                   {"ok", m.value("ok").toBool()},
                                   {"accept", m.value("accept").toBool()},
                                   {"message", m.value("message").toString()}});
        }
        observe(label, {{"input", QFileInfo(file).fileName()}, {"rows", out}});
    };

    // R1 the exported file, unchanged (XMP + sidecar present)
    relink(QStringLiteral("R1 unchanged export"), f, QString());
    // R2 same pixels, written again without metadata, arbitrary name, no sidecar
    const QString r2 = QDir(gradedDir).filePath(QStringLiteral("graded_in_editor.tif"));
    observe(QStringLiteral("R2 prepare"), {{"written", rewriteWithoutMetadata(f, r2, fk::FileFormat::tiff)}});
    relink(QStringLiteral("R2 renamed, no XMP, no sidecar"), r2, QStringLiteral("R2-dialog"));
    // R3 same name as the export but in another folder, no XMP, no sidecar
    const QString r3 = QDir(gradedDir).filePath(QFileInfo(f).fileName());
    observe(QStringLiteral("R3 prepare"), {{"written", rewriteWithoutMetadata(f, r3, fk::FileFormat::tiff)}});
    relink(QStringLiteral("R3 export name, other folder, no XMP"), r3, QString());
    // R4 editor duplicate suffix "_1"
    const QString r4 = QDir(gradedDir).filePath(QFileInfo(f).completeBaseName() + QStringLiteral("_1.tif"));
    observe(QStringLiteral("R4 prepare"), {{"written", rewriteWithoutMetadata(f, r4, fk::FileFormat::tiff)}});
    relink(QStringLiteral("R4 name with _1 suffix, no XMP"), r4, QString());
    // R5 JPEG from the editor (8-bit sources only)
    const QString r5 = QDir(gradedDir).filePath(QFileInfo(f).completeBaseName() + QStringLiteral(".jpg"));
    const bool r5ok = rewriteWithoutMetadata(f, r5, fk::FileFormat::jpeg);
    observe(QStringLiteral("R5 prepare"), {{"written", r5ok}});
    if (r5ok) relink(QStringLiteral("R5 JPEG, export name, no XMP"), r5, QString());
    // R7 turned by 180° in the editor, export name, no XMP (FK-002: the turn needs confirmation)
    const QString r7 = QDir(gradedDir).filePath(QStringLiteral("turned"));
    QDir().mkpath(r7);
    const QString r7f = QDir(r7).filePath(QFileInfo(f).fileName());
    observe(QStringLiteral("R7 prepare"), {{"written", rewriteRotated180(f, r7f)}});
    relink(QStringLiteral("R7 turned 180°, export name, no XMP"), r7f, QStringLiteral("R7-dialog"));
    // R6 new session: the project was never saved, the app restarted, the scan imported again
    app_->newProject();
    app_->importScans({scan_});
    app_->waitIdle(300000);
    relink(QStringLiteral("R6 new session, unchanged export"), f, QStringLiteral("R6-dialog"));
    relink(QStringLiteral("R6b new session, renamed no XMP"), r2, QString());
    return 0;
}

int SelfTest::scenarioCount() {
    // UX-01 (U011): detection with the default automatic count, then the scan switched to a manual count and
    // the count corrected, without detecting again
    app_->importScans({scan_});
    app_->waitIdle(300000);
    auto state = [&] {
        const QVariantMap s = app_->currentScan();
        return QJsonObject{{"autoCount", s.value("autoCount").toBool()},
                           {"expectedCount", s.value("expectedCount").toInt()},
                           {"prints", s.value("itemCount").toInt()},
                           {"warnings", QJsonArray::fromStringList(s.value("warnings").toStringList())}};
    };
    observe(QStringLiteral("C1 after detection (default: automatic)"), state());
    snapshot(QStringLiteral("C1-left-rail"));
    const QString id = app_->currentScanId();
    app_->setScanCount(id, false, app_->currentScan().value("expectedCount").toInt());
    observe(QStringLiteral("C2 switched to manual (the kept count), no new detection"), state());
    app_->setScanCount(id, false, app_->currentScan().value("itemCount").toInt());
    QCoreApplication::processEvents();
    observe(QStringLiteral("C3 count set to the detected number, no new detection"), state());
    snapshot(QStringLiteral("C3-left-rail"));
    return 0;
}

int SelfTest::scenarioUpright() {
    app_->importScans({scan_});
    app_->waitIdle(300000);
    QCoreApplication::processEvents();
    const QImage before = window_->grabWindow();
    const QString id = app_->currentItemId();
    app_->rotateUpright(id, 1);
    app_->waitIdle(60000);
    QCoreApplication::processEvents();
    const QImage after = window_->grabWindow();
    // compare the canvas and the filmstrip regions (their places in the window, U022 layout)
    auto region = [&](QQuickItem* it) {
        if (!it) return QRect();
        const qreal dpr = window_->effectiveDevicePixelRatio();
        const QRectF r = it->mapRectToScene(QRectF(0, 0, it->width(), it->height()));
        return QRect(QPoint(qRound(r.left() * dpr), qRound(r.top() * dpr)),
                     QPoint(qRound(r.right() * dpr) - 1, qRound(r.bottom() * dpr) - 1));
    };
    const QRect canvas = region(qobject_cast<QQuickItem*>(findCanvas()));
    const bool canvasSame = before.copy(canvas) == after.copy(canvas);
    const QRect strip = region(findItem(QStringLiteral("filmstrip")));
    const bool stripSame = before.copy(strip) == after.copy(strip);
    observe(QStringLiteral("U1 rotate upright on the current print"),
            {{"uprightAfter", app_->currentItem().value("upright").toInt()},
             {"canvasPixelsUnchanged", canvasSame},
             {"filmstripPixelsUnchanged", stripSame}});
    snapshot(QStringLiteral("U1-after"));
    return 0;
}

int SelfTest::run() {
    QDir().mkpath(dir_);
    const QString exportDir = QDir(dir_).filePath(QStringLiteral("export"));
    QDir(exportDir).removeRecursively();
    app_->setSetting(QStringLiteral("exportFolder"), exportDir);
    app_->setSetting(QStringLiteral("exportFormat"), QStringLiteral("tif"));
    const QString defaultGeometry =
        app_->settings().value("geometryMode").toString(); // fresh settings: the default
    app_->setSetting(QStringLiteral("geometryMode"), QStringLiteral("crop"));
    app_->setSetting(QStringLiteral("applyUpright"), false);
    app_->setSetting(QStringLiteral("finalMode"), QStringLiteral("real"));
    app_->setSetting(QStringLiteral("finalFormat"), QStringLiteral("tif"));
    window_->resize(1440, 900);
    snapshot(QStringLiteral("01-empty"));

    // import + detection
    app_->importScans({scan_});
    const bool idle = app_->waitIdle(180000);
    const int items = app_->counts().value("prints").toInt();
    check(QStringLiteral("detection finished"), idle);
    check(QStringLiteral("prints detected"), items > 0, QString::number(items));
    if (items == 0) return 1;
    QCoreApplication::processEvents();
    snapshot(QStringLiteral("02-scan"));

    // canvas contract: Preview at fit, physical 100 %, Full after the idle debounce
    QObject* canvas = findCanvas();
    check(QStringLiteral("canvas present"), canvas != nullptr);
    if (canvas) {
        check(QStringLiteral("badge Preview at fit"),
              canvas->property("badge").toString() == QLatin1String("Preview"),
              canvas->property("badge").toString());
        const QRectF nav = canvas->property("navigatorView").toRectF();
        check(QStringLiteral("whole scan visible at fit"),
              canvas->property("atFit").toBool() && nav.width() > 0.999 && nav.height() > 0.999,
              QStringLiteral("%1 × %2 of the scan").arg(nav.width(), 0, 'f', 3).arg(nav.height(), 0, 'f', 3));
        QMetaObject::invokeMethod(canvas, "zoomTo", Q_ARG(double, 100.0));
        check(QStringLiteral("physical 100 %"),
              std::abs(canvas->property("zoomPercent").toDouble() - 100.0) < 0.01,
              QString::number(canvas->property("zoomPercent").toDouble()));
        const bool full =
            waitFor([&] { return canvas->property("badge").toString() == QLatin1String("Full"); }, 20000);
        check(QStringLiteral("badge Full at 100 %"), full, canvas->property("badge").toString());
        snapshot(QStringLiteral("03-100pct"));
        QMetaObject::invokeMethod(canvas, "fit");
        canvas->setProperty("mode", QStringLiteral("print"));
        snapshot(QStringLiteral("04-print"));
        canvas->setProperty("mode", QStringLiteral("image"));
        canvas->setProperty("tool", QStringLiteral("inner"));
        snapshot(QStringLiteral("05-image-inner"));
        canvas->setProperty("mode", QStringLiteral("scan"));
        canvas->setProperty("tool", QStringLiteral("select"));
    }

    // UX-02 / D4 (U010): "image only" is straightened by default, labelled as resampled, from the core's plan
    {
        app_->setSetting(QStringLiteral("geometryMode"), defaultGeometry);
        const QVariantMap plan = app_->currentItem().value("exportPlan").toMap();
        check(QStringLiteral("image area exports default to labelled straightening"),
              defaultGeometry == QLatin1String("straighten") && plan.value("resampled").toBool() &&
                  (plan.value("geometry").toString().contains(QLatin1String("straightened")) ||
                   plan.value("geometry").toString().contains(QStringLiteral("拉正"))) &&
                  !app_->currentItem().value("frameBitExact").toBool(),
              app_->settings().value("geometryMode").toString() + QStringLiteral(" · ") +
                  plan.value("geometry").toString());
    }
    // the bit-exact round trip below is about the pixel crop: chosen explicitly
    app_->setSetting(QStringLiteral("geometryMode"), QStringLiteral("crop"));
    // export image areas (all prints, low-confidence ones included for the test)
    app_->exportImageAreas({});
    app_->waitIdle(300000);
    const QStringList edits = filesMatching(exportDir, QStringLiteral("*_EDIT_v001.tif"));
    check(QStringLiteral("image areas exported"), edits.size() == items, QString::number(edits.size()));
    // UX-07 (U003): no sidecar files by default; the project keeps one export record per export
    check(QStringLiteral("no sidecar files by default"),
          filesMatching(exportDir, QStringLiteral("*.fk.json")).isEmpty());
    check(QStringLiteral("export records in the project"), app_->exportRecordCount() == edits.size(),
          QString::number(app_->exportRecordCount()));

    // relink the unchanged exports (matched by XMP), then composite back
    QVariantList rows;
    QMetaObject::Connection c =
        connect(app_, &AppController::relinkResults, this, [&](const QVariantList& r) { rows = r; });
    QVariantList urls;
    for (const auto& e : edits)
        urls << e;
    app_->relinkFiles(urls);
    app_->waitIdle(300000);
    disconnect(c);
    int matched = 0;
    for (const auto& r : rows)
        if (r.toMap().value("decision").toString() == QLatin1String("unique") &&
            r.toMap().value("source").toString().contains(QLatin1String("file_hash")) &&
            r.toMap().value("source").toString().contains(QLatin1String("embedded_ids")) &&
            r.toMap().value("accept").toBool())
            ++matched;
    check(QStringLiteral("relink unchanged exports (file hash, embedded IDs)"), matched == edits.size(),
          QStringLiteral("%1/%2").arg(matched).arg(edits.size()));
    app_->applyRelink(rows);
    QMetaObject::invokeMethod(window_, "closeDialogs");
    check(QStringLiteral("relinked count"), app_->counts().value("relinked").toInt() == matched);

    app_->exportFinal({}, {});
    app_->waitIdle(300000);
    const QStringList finals = filesMatching(exportDir, QStringLiteral("*_FINAL_v001.tif"));
    check(QStringLiteral("final exports"), finals.size() == matched, QString::number(finals.size()));

    // an unchanged graded file composited into the real frame equals the full print bit for bit
    app_->exportFullPrints({});
    app_->waitIdle(300000);
    const QStringList fulls = filesMatching(exportDir, QStringLiteral("*_FULL_v001.tif"));
    bool exact = finals.size() == fulls.size() && !finals.isEmpty();
    QString why;
    for (int i = 0; exact && i < finals.size(); ++i)
        exact = samePixels(finals[i], fulls[i], why);
    check(QStringLiteral("round trip bit-exact (frame and image)"), exact, why);

    // composite mode preview + snapshot
    if (canvas) {
        canvas->setProperty("mode", QStringLiteral("composite"));
        waitFor([&] { return app_->compositePreview(app_->currentItemId().toStdString()).isNull() == false; },
                60000);
        canvas->setProperty("compare", true);
        snapshot(QStringLiteral("06-composite-compare"));
        canvas->setProperty("compare", false);
        canvas->setProperty("mode", QStringLiteral("scan"));
    }

    // synthetic frame
    app_->setSetting(QStringLiteral("finalMode"), QStringLiteral("synthetic"));
    app_->exportFinal({}, {});
    app_->waitIdle(300000);
    const QStringList framed = filesMatching(exportDir, QStringLiteral("*_FRAMED_v001.tif"));
    check(QStringLiteral("synthetic frame exports"), framed.size() == matched,
          QString::number(framed.size()));

    // project round trip
    const QString proj = QDir(dir_).filePath(QStringLiteral("selftest.fkproj"));
    QFile::remove(proj);
    check(QStringLiteral("save project"), app_->saveProjectAs(proj));
    check(QStringLiteral("open project"), app_->openProject(proj));
    app_->waitIdle(120000);
    check(QStringLiteral("project keeps prints"), app_->counts().value("prints").toInt() == items);
    snapshot(QStringLiteral("07-reopened"));

    // UX-07 (U003): after saving and reopening, an unchanged export relinks with no sidecar file anywhere
    {
        QVariantList rows;
        auto c = connect(app_, &AppController::relinkResults, this, [&](const QVariantList& r) { rows = r; });
        app_->relinkFiles({edits.first()});
        app_->waitIdle(300000);
        disconnect(c);
        QMetaObject::invokeMethod(window_, "closeDialogs");
        const QVariantMap r = rows.isEmpty() ? QVariantMap() : rows.first().toMap();
        check(QStringLiteral("reopened project relinks without a sidecar"),
              r.value("matched").toBool() && r.value("ok").toBool(),
              r.value("source").toString() + QStringLiteral(" ") + r.value("message").toString());
        // the portable sidecar is still available as an option
        app_->setSetting(QStringLiteral("writeSidecar"), true);
        app_->exportImageAreas({app_->currentItemId()});
        app_->waitIdle(300000);
        app_->setSetting(QStringLiteral("writeSidecar"), false);
        check(QStringLiteral("portable sidecar written when enabled"),
              filesMatching(exportDir, QStringLiteral("*.fk.json")).size() == 1,
              QString::number(filesMatching(exportDir, QStringLiteral("*.fk.json")).size()));
    }

    runTaskSafety(exportDir, proj);
    runKnownIssues(exportDir);
    runDataProtection();
    runPrivacy();
    runCountSetting();
    runShapeEditing();
    runScanRotation();
    runMaskDefaults();
    runScrubFields();
    runMemoryBudget();
    runDisplayProfile();
    runFormats();
    runLayout();
    runToolbar();

    QJsonObject report{{"product", "FrameKeeper"},
                       {"version", app_->version()},
                       {"commit", app_->buildCommit()},
                       {"build", app_->buildId()},
                       {"platform", app_->platform()},
                       {"qpa", QGuiApplication::platformName()},
                       {"scan", scan_},
                       {"checks", checks_},
                       {"knownIssues", known_},
                       {"observations", observations_}, // measurements recorded alongside the checks (FK-005)
                       {"ok", allOk_},
                       {"scope",
                        "Offscreen QPA, software scene graph. Does not prove monitor colour accuracy or "
                        "drawing in a real window (docs/HUMAN-VERIFICATION.md HV-02/HV-03/HV-04)."}};
    QFile f(QDir(dir_).filePath(QStringLiteral("report.json")));
    if (f.open(QIODevice::WriteOnly)) f.write(QJsonDocument(report).toJson());
    return allOk_ ? 0 : 1;
}

} // namespace fkapp
