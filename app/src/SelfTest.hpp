// `FrameKeeper --self-test <dir> --scan <tif>` (RFC-0011): drives the real UI controllers offscreen
// through import → detect → export → relink → final (real frame and synthetic), captures window snapshots
// and writes report.json. It cannot prove monitor colour or drawing in a real window (HV-02/HV-03).
#pragma once

#include <QEvent>
#include <QJsonArray>
#include <QJsonObject>
#include <QObject>
#include <QPointF>
#include <QString>

#include <functional>

class QQuickItem;
class QQuickWindow;

namespace fkapp {

class AppController;

class SelfTest : public QObject {
    Q_OBJECT
  public:
    SelfTest(AppController* app, QQuickWindow* window, QString outDir, QString scan,
             QObject* parent = nullptr);
    /// Runs synchronously (spinning the event loop); returns the process exit code.
    int run();
    /// FK-013: outcomes counted apart — "N passed, N failed, N skipped, N known issues (N no longer
    /// reproduce), N not applicable". A skip or a known issue is never counted as passed.
    QString summary() const;
    /// `--scenario <name>`: reproduces reported user flows and records what actually happens (observations,
    /// not assertions) in <dir>/scenario-<name>.json plus snapshots. Names: "relink", "count", "upright",
    /// "miniswhite", "memory".
    int runScenario(const QString& name);

  private:
    void check(const QString& name, bool ok, const QString& detail = {});
    /// A check whose precondition this scan does not meet (e.g. an untagged scan for a colour conversion):
    /// recorded as skipped with the reason, never as passed.
    void skip(const QString& name, const QString& why);
    void snapshot(const QString& name);
    QObject* findCanvas() const;
    QQuickItem* findItem(const QString& objectName) const;
    /// A left click through the window (a real input event), at `local` or the item's centre; false when the
    /// item is hidden or outside the window.
    bool click(QQuickItem* item, QPointF local = QPointF(-1, -1));
    void observe(const QString& name, const QJsonObject& data);
    /// Known issue (expected to reproduce until fixed, like ctest WILL_FAIL): reproduced → recorded, run
    /// passes; not reproduced → the run fails so the check is turned into a normal one with the fix.
    void known(const QString& ticket, const QString& name, bool reproduced, const QString& detail = {});
    /// A known-issue check whose precondition this scan does not meet (e.g. an 8-bit scan for a 16-bit
    /// issue): recorded as not applicable, neither reproduced nor a failure.
    void knownNotApplicable(const QString& ticket, const QString& name, const QString& why);
    void runKnownIssues(const QString& exportDir);
    /// FK-001 / FK-008 (U008): the replace guard, failed saves, backup restore, recovery snapshots, output
    /// settings.
    void runDataProtection();
    /// FK-004 (U009): privacy presets on a scan copy carrying identity, outputs read back byte for byte.
    void runPrivacy();
    /// UX-01 (U011): automatic count by default, per-scan override with the left-rail controls (mouse
    /// input), warnings updated without detecting again, single/multi/blank scans in one project, saved.
    void runCountSetting();
    /// UX-03 (U013): Shift tap switches Outer/Inner (real key events), Shift-drag keeps rectangles and
    /// circles, Shift+click / Shift+arrows / typing / dialogs / focus loss never switch, undo/redo, menu and
    /// tool buttons, saved and reopened.
    void runShapeEditing();
    /// UX-04 (U014): whole-scan rotation on a scan with four prints turned 0/90/180/270°: every print follows
    /// (clicks at the expected screen positions), count kept, four turns restore the view, one undo, exports
    /// before the turn relink bit-exact after it, uprights never doubled, saved, source file unchanged.
    void runScanRotation();
    /// UX-08 (U015): natural-blend default for new prints, per-print mask (right rail, typed width, undo,
    /// explicit batch), preview and final with the same mask, edge-change hint, saved, older projects
    /// unchanged.
    void runMaskDefaults();
    /// UX-09 (U016): drag-to-adjust and double-click input on the mask width (real mouse and keys):
    /// thresholds, bounds, Esc, invalid characters, Enter / focus loss, one undo step per drag, no preview
    /// backlog.
    void runScrubFields();
    /// FK-005 (U017): one byte budget for the caches on six generated scans: batch detection, switching
    /// scans, full-resolution view, composite previews given back and recomputed identically, job memory
    /// reserved before the job starts, memory pressure (output unchanged), relink checks independent of the
    /// cache, cancelled detection.
    void runMemoryBudget();
    /// FK-011 (U025): the window moves to a monitor with another profile (simulated): the proxy, the full
    /// resolution and the thumbnails are converted again, Preview and Full agree, outputs unchanged, a proxy
    /// converted for the previous monitor never shown; and back.
    void runDisplayProfile();
    /// FK-012 (U026): a Linear DNG export given back (relink, drag and drop) is refused with guidance and
    /// never imported as a scan.
    void runFormats();
    /// UX-10 (U022): edge strips fold and unfold each panel (mouse, keyboard, shortcuts), the filmstrip spans
    /// the window, the canvas refits the whole scan and clicks still land on the print under the pointer
    /// after folding and resizing; widths and folded state are stored; Reset Layout.
    void runLayout();
    /// UX-06 (U021): the view switch between the overlays button and the zoom field; the top bar never
    /// overflows or overlaps at any window size and rail width (it compacts in a fixed order); the centre
    /// column keeps its minimum; the workflow steps follow the current print and lead to each step.
    void runToolbar();
    /// FK-009 (U024): every visible text has glyphs in its font (no boxes), stays inside its box and its
    /// clipping parent (no cut words), key controls are not covered; dark and light, three window sizes,
    /// rails shown and folded, the three right-rail tabs and the main dialogs. Language and scale come from
    /// the process (CI runs English and Chinese, 1× and 2×). Returns the number of problems.
    int runTextLayout();
    void doubleClick(QQuickItem* item, QPointF local = QPointF(-1, -1));
    /// Real input through the window system interface (as Qt Test): a key, a mouse event on `item` at
    /// `local`.
    void key(int key, Qt::KeyboardModifiers mods, bool press, const QString& text = {});
    void mouse(QQuickItem* item, QPointF local, QEvent::Type type,
               Qt::KeyboardModifiers mods = Qt::NoModifier);
    QQuickItem* findItemIf(const std::function<bool(QQuickItem*)>& pred) const;
    int inputTs_ = 0;
    void runTaskSafety(const QString& exportDir, const QString& projectPath);
    int scenarioMinIsWhite();
    QJsonArray known_;
    int scenarioRelink();
    int scenarioCount();
    int scenarioUpright();
    /// FK-005: memory after each stage of a batch (`--scan <dir>`, FK_BENCH_SCANS=N).
    int scenarioMemory();
    QJsonArray observations_;

    AppController* app_;
    QQuickWindow* window_;
    QString dir_, scan_;
    QJsonArray checks_;
    bool allOk_ = true;
};

} // namespace fkapp
