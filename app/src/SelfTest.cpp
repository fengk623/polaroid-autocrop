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
#include <QJsonDocument>
#include <QQuickItem>
#include <QQuickWindow>
#include <QRectF>
#include <QUrl>

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
                      item.value("notExactReason").toString().contains(QLatin1String("8 bits")),
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
    // project and the output converts once — colours as in the scan.
    {
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
        auto told = [&](const QString& what) {
            return std::any_of(notes.begin(), notes.end(),
                               [&](const QString& n) { return n.contains(what); });
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
              replaced && finals() == f1 && told(QStringLiteral("replaced by a different picture")),
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
                  told(QStringLiteral("changed since it was imported")),
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

    // FK-008 recovery snapshot: written while unsaved, offered after a crash, recovered unsaved
    app_->writeRecovery();
    const int ownHidden = static_cast<int>(app_->recoverySnapshots().size());
    app_->newProject(); // what a crash looks like to the next session: the snapshot is still there
    const QVariantList snaps = app_->recoverySnapshots();
    const bool recovered =
        !snaps.isEmpty() && app_->recoverSnapshot(snaps.first().toMap().value("file").toString());
    app_->waitIdle(300000);
    check(QStringLiteral("FK-008 unsaved work is recovered from the snapshot (unsaved, same path)"),
          ownHidden == 0 && recovered && scans() == 1 && app_->dirty() && app_->projectPath() == saved,
          QStringLiteral("snapshots %1, recovered %2, scans %3, path %4")
              .arg(snaps.size())
              .arg(recovered)
              .arg(scans())
              .arg(app_->projectPath()));
    // "Don't save" drops the snapshot
    ask(QStringLiteral("new"), QString());
    answer(QStringLiteral("dontsave"));
    check(QStringLiteral("FK-008 \"Don't save\" discards the snapshot"),
          scans() == 0 && app_->recoverySnapshots().isEmpty(),
          QStringLiteral("scans %1, snapshots %2").arg(scans()).arg(app_->recoverySnapshots().size()));

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
                  plan.value("geometry").toString().contains(QLatin1String("straightened")) &&
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
