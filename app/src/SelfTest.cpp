#include "SelfTest.hpp"

#include "AppController.hpp"
#include "util.hpp"

#include "fk/image_io.hpp"
#include "fk/util.hpp"

#include <QCoreApplication>
#include <QDir>
#include <QDirIterator>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QGuiApplication>
#include <QJsonDocument>
#include <QQuickItem>
#include <QQuickWindow>
#include <QRectF>

#include <cstring>
#include <functional>

namespace fkapp {

SelfTest::SelfTest(AppController* app, QQuickWindow* window, QString outDir, QString scan, QObject* parent)
    : QObject(parent), app_(app), window_(window), dir_(std::move(outDir)), scan_(std::move(scan)) {}

void SelfTest::check(const QString& name, bool ok, const QString& detail) {
    checks_.append(QJsonObject{{"name", name}, {"ok", ok}, {"detail", detail}});
    allOk_ = allOk_ && ok;
    qInfo().noquote() << (ok ? "PASS" : "FAIL") << name << detail;
}

void SelfTest::snapshot(const QString& name) {
    QCoreApplication::processEvents();
    const QImage img = window_->grabWindow();
    const QString path = QDir(dir_).filePath(QStringLiteral("snapshot-%1.png").arg(name));
    const bool ok = !img.isNull() && img.save(path);
    check(QStringLiteral("snapshot %1").arg(name), ok,
          QStringLiteral("%1×%2").arg(img.width()).arg(img.height()));
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
QStringList filesMatching(const QString& dir, const QString& pattern) {
    QStringList out;
    QDirIterator it(dir, {pattern}, QDir::Files);
    while (it.hasNext())
        out << it.next();
    out.sort();
    return out;
}
/// Writes `src` again without any metadata (no XMP, no sidecar): what an external editor typically produces.
bool rewriteWithoutMetadata(const QString& src, const QString& dst, fk::FileFormat fmt) {
    fk::ImageInfo info;
    auto img = fk::readImageFile(toPath(src), &info);
    if (!img) return false;
    fk::WriteOptions opt;
    opt.format = fmt;
    opt.bits = fmt == fk::FileFormat::jpeg ? 8 : img->bits();
    if (fmt == fk::FileFormat::jpeg && img->bits() != 8) return false;
    opt.icc = info.icc;
    opt.jpegQuality = 92;
    QFile::remove(dst);
    auto sink = fk::createSink(toPath(dst), opt);
    if (!sink) return false;
    if (!(*sink)->begin({img->width(), img->height()}, img->channels(), img->bits())) return false;
    for (int64_t y = 0; y < img->height(); ++y)
        if (!(*sink)->writeRows(img->row(y), 1, img->rowBytes())) return false;
    return static_cast<bool>((*sink)->finish());
}
} // namespace

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
    QJsonObject report{
        {"scenario", name},
        {"version", app_->version()},
        {"scan", scan_},
        {"observations", observations_},
        {"note", "Observations of current behaviour (Stage A reproduction), not pass/fail checks."}};
    QFile f(QDir(dir_).filePath(QStringLiteral("scenario-%1.json").arg(name)));
    if (f.open(QIODevice::WriteOnly)) f.write(QJsonDocument(report).toJson());
    return rc;
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
    // R6 new session: the project was never saved, the app restarted, the scan imported again
    app_->newProject();
    app_->importScans({scan_});
    app_->waitIdle(300000);
    relink(QStringLiteral("R6 new session, unchanged export"), f, QStringLiteral("R6-dialog"));
    relink(QStringLiteral("R6b new session, renamed no XMP"), r2, QString());
    return 0;
}

int SelfTest::scenarioCount() {
    // detection with the default expected count, then the user changes the count without detecting again
    app_->importScans({scan_});
    app_->waitIdle(300000);
    auto warnings = [&] {
        return QJsonArray::fromStringList(app_->currentScan().value("warnings").toStringList());
    };
    observe(QStringLiteral("C1 after detection"),
            {{"expectedSetting", app_->settings().value("expectedCount").toInt()},
             {"prints", app_->counts().value("prints").toInt()},
             {"warnings", warnings()}});
    app_->setSetting(QStringLiteral("expectedCount"), app_->counts().value("prints").toInt());
    QCoreApplication::processEvents();
    observe(QStringLiteral("C2 count set to the detected number, no new detection"),
            {{"expectedSetting", app_->settings().value("expectedCount").toInt()}, {"warnings", warnings()}});
    snapshot(QStringLiteral("C2-left-rail"));
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
    // compare the canvas region only (left of the right rail, above the filmstrip)
    const QRect canvas(260, 60, 860, 640);
    const bool canvasSame = before.copy(canvas) == after.copy(canvas);
    const QRect strip(260, 760, 860, 120);
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

    // export image areas (all prints, low-confidence ones included for the test)
    app_->exportImageAreas({});
    app_->waitIdle(300000);
    const QStringList edits = filesMatching(exportDir, QStringLiteral("*_EDIT_v001.tif"));
    check(QStringLiteral("image areas exported"), edits.size() == items, QString::number(edits.size()));
    check(QStringLiteral("sidecars written"),
          filesMatching(exportDir, QStringLiteral("*.fk.json")).size() == edits.size());

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
        if (r.toMap().value("matched").toBool() &&
            r.toMap().value("source").toString() == QLatin1String("xmp"))
            ++matched;
    check(QStringLiteral("relink by XMP"), matched == edits.size(),
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

    QJsonObject report{{"product", "FrameKeeper"},
                       {"version", app_->version()},
                       {"platform", app_->platform()},
                       {"qpa", QGuiApplication::platformName()},
                       {"scan", scan_},
                       {"checks", checks_},
                       {"ok", allOk_},
                       {"scope",
                        "Offscreen QPA, software scene graph. Does not prove monitor colour accuracy or "
                        "drawing in a real window (docs/HUMAN-VERIFICATION.md HV-02/HV-03/HV-04)."}};
    QFile f(QDir(dir_).filePath(QStringLiteral("report.json")));
    if (f.open(QIODevice::WriteOnly)) f.write(QJsonDocument(report).toJson());
    return allOk_ ? 0 : 1;
}

} // namespace fkapp
