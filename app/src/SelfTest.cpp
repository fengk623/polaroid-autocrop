#include "SelfTest.hpp"

#include "AppController.hpp"
#include "Display.hpp"
#include "Jobs.hpp"
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

#include <algorithm>
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

    // UX-01: changing the expected count keeps the warning computed at detection time
    {
        const QStringList w = app_->currentScan().value("warnings").toStringList();
        const bool hadMismatch =
            std::any_of(w.begin(), w.end(), [](const QString& x) { return x.startsWith("count_mismatch"); });
        const int prints = app_->currentScan().value("itemCount").toInt();
        const QVariant before = app_->settings().value("expectedCount");
        app_->setSetting(QStringLiteral("expectedCount"), prints > 0 ? prints : 1);
        QCoreApplication::processEvents();
        const QStringList w2 = app_->currentScan().value("warnings").toStringList();
        const bool still = std::any_of(w2.begin(), w2.end(),
                                       [](const QString& x) { return x.startsWith("count_mismatch"); });
        if (!hadMismatch)
            knownNotApplicable(QStringLiteral("UX-01"),
                               QStringLiteral("count warning stays after the count is corrected"),
                               QStringLiteral("no count warning on this scan: %1").arg(w.join(';')));
        else
            known(QStringLiteral("UX-01"), QStringLiteral("count warning stays after the count is corrected"),
                  still, QStringLiteral("before %1, after %2").arg(w.join(';'), w2.join(';')));
        app_->setSetting(QStringLiteral("expectedCount"), before);
    }

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

    // FK-010: reducing 16-bit to 8-bit on export is still announced as bit-identical
    {
        const QVariant bits = app_->settings().value("exportBits");
        app_->setSetting(QStringLiteral("exportBits"), 8);
        QCoreApplication::processEvents();
        const bool sixteen = app_->currentScan().value("bits").toInt() == 16;
        const bool claimed = app_->currentItem().value("frameBitExact").toBool();
        if (!sixteen)
            knownNotApplicable(QStringLiteral("FK-010"),
                               QStringLiteral("16→8-bit export still claims bit-identical pixels"),
                               QStringLiteral("scan bits %1").arg(app_->currentScan().value("bits").toInt()));
        else
            known(QStringLiteral("FK-010"),
                  QStringLiteral("16→8-bit export still claims bit-identical pixels"), claimed,
                  QStringLiteral("scan bits %1, frameBitExact %2")
                      .arg(app_->currentScan().value("bits").toInt())
                      .arg(claimed));
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
}

int SelfTest::scenarioMinIsWhite() {
    // FK-011: rows from the TIFF reader are already in normal polarity; the display converter inverts
    // MinIsWhite again, so Preview (proxy) and Full (rows) disagree.
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
    known(QStringLiteral("FK-011"), QStringLiteral("MinIsWhite full-resolution display is inverted"),
          miw && std::abs(fullMean - proxyMean) > 64,
          QStringLiteral("full %1 vs preview %2").arg(fullMean, 0, 'f', 1).arg(proxyMean, 0, 'f', 1));
    return allOk_ ? 0 : 1;
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
    QJsonObject report{
        {"scenario", name},
        {"version", app_->version()},
        {"commit", app_->buildCommit()},
        {"build", app_->buildId()},
        {"scan", scan_},
        {"observations", observations_},
        {"knownIssues", known_},
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

    QJsonObject report{{"product", "FrameKeeper"},
                       {"version", app_->version()},
                       {"commit", app_->buildCommit()},
                       {"build", app_->buildId()},
                       {"platform", app_->platform()},
                       {"qpa", QGuiApplication::platformName()},
                       {"scan", scan_},
                       {"checks", checks_},
                       {"knownIssues", known_},
                       {"ok", allOk_},
                       {"scope",
                        "Offscreen QPA, software scene graph. Does not prove monitor colour accuracy or "
                        "drawing in a real window (docs/HUMAN-VERIFICATION.md HV-02/HV-03/HV-04)."}};
    QFile f(QDir(dir_).filePath(QStringLiteral("report.json")));
    if (f.open(QIODevice::WriteOnly)) f.write(QJsonDocument(report).toJson());
    return allOk_ ? 0 : 1;
}

} // namespace fkapp
