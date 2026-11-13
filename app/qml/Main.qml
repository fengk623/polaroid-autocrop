// Main window: left rail · centre column (top bar, canvas, filmstrip) · right rail (ui-reference §4.1).
import QtQuick
import QtQuick.Layouts
import QtQuick.Controls.Basic
import QtQuick.Dialogs
import FrameKeeper
import FrameKeeper.Core

ApplicationWindow {
    id: win
    visible: true
    width: Math.min(1440, Screen.desktopAvailableWidth * 0.9)
    height: Math.min(900, Screen.desktopAvailableHeight * 0.9)
    minimumWidth: Theme.minWindowW
    minimumHeight: Theme.minWindowH
    title: (App.dirty ? "• " : "") + App.projectName + " — FrameKeeper"
    color: Theme.surfacePanel
    font.pixelSize: Theme.fontBody

    readonly property bool isMac: App.platform === "macos"
    readonly property bool typing: activeFocusItem !== null && activeFocusItem.hasOwnProperty("cursorPosition")

    Binding {
        target: Theme
        property: "dark"
        value: App.settings.theme === "light" ? false
             : App.settings.theme === "dark" ? true
             : Qt.styleHints.colorScheme !== Qt.ColorScheme.Light
    }

    // ---------------------------------------------------------------- commands
    QtObject {
        id: cmd
        readonly property string mod: win.isMac ? "⌘" : "Ctrl+"
        function importScans() { scanDialog.open() }
        function importFolder() { folderDialog.purpose = "import"; folderDialog.open() }
        // FK-001: every way of replacing the open project goes through win.replaceProject (save / don't save / cancel)
        function openProject() { projectOpenDialog.open() }
        function recoverWork() {
            const snaps = App.recoverySnapshots()
            if (snaps.length) recoveryDialog.show(snaps)
            else App.notify(qsTr("No unsaved work to recover."))
        }
        function save() { if (App.projectPath.length === 0) projectSaveDialog.open(); else App.saveProject() }
        function saveAs() { projectSaveDialog.open() }
        function newProject() { win.replaceProject("new", "") }
        function relocate() { folderDialog.purpose = "relocate"; folderDialog.open() }
        function chooseExportFolder() { folderDialog.purpose = "export"; folderDialog.open() }
        function exportImageAreas(ids) { exportDialog.openFor(ids === undefined ? App.selectedItemIds : ids, false) }
        function exportFullPrints(ids) { exportDialog.openFor(ids === undefined ? App.selectedItemIds : ids, true) }
        // target-first relink: with a print id only that print is considered (UX-07)
        function relink(target) { relinkFileDialog.target = target || ""; relinkFileDialog.open() }
        function exportFinal(ids) { finalDialog.openFor(ids === undefined ? App.selectedItemIds : ids) }
        function detectAgain(scanId, tmpl) {
            if (App.currentScan.itemCount > 0) { pendingDetect = { scan: scanId, tmpl: tmpl || "", all: false }; redetectConfirm.open() }
            else App.detectScan(scanId, tmpl || "")
        }
        function detectAll(tmpl) { pendingDetect = { scan: "", tmpl: tmpl || "", all: true }; redetectConfirm.open() }
        property var pendingDetect: ({})
        function saveTemplate() { nameDialog.title = qsTr("Save template"); nameDialog.label = qsTr("Template name"); nameDialog.text = ""; nameDialog.open() }
        function cancelJob(id) { App.cancelJob(id) }
        function toggleLeft() { App.setSetting("showLeftRail", !App.settings.showLeftRail) }
        function toggleRight() { App.setSetting("showRightRail", !App.settings.showRightRail) }
        function toggleFilmstrip() { App.setSetting("showFilmstrip", !App.settings.showFilmstrip) }
        function toggleAll() {
            const on = !(App.settings.showLeftRail || App.settings.showRightRail || App.settings.showFilmstrip)
            App.setSetting("showLeftRail", on); App.setSetting("showRightRail", on); App.setSetting("showFilmstrip", on)
        }
        // UX-10: default widths and every panel shown
        function resetLayout() {
            App.setSetting("leftRailWidth", Theme.leftRailDefault); App.setSetting("rightRailWidth", Theme.rightRailDefault)
            App.setSetting("showLeftRail", true); App.setSetting("showRightRail", true); App.setSetting("showFilmstrip", true)
        }
        function filmTypeName(id) {
            return ({ "polaroid_600": qsTr("Polaroid 600 / i-Type / SX-70"), "polaroid_round": qsTr("Polaroid round frame"),
                      "instax_mini": qsTr("Instax Mini"), "instax_square": qsTr("Instax Square"), "instax_wide": qsTr("Instax Wide") })[id] || qsTr("Unknown size")
        }
        function warningText(w) {
            if (w.indexOf("count_mismatch") === 0) {
                const m = /found (\d+), expected (\d+)/.exec(w)
                return m ? qsTr("Found %1 prints, %2 expected.").arg(m[1]).arg(m[2])
                         : qsTr("Print count differs from the expected number.")
            }
            if (w.indexOf("possible_missed") === 0)
                return qsTr("%1 more objects may be prints that were not found. Check the scan; a template or a contrasting background helps.").arg(w.split(": ")[1] || "")
            if (w === "no_prints_found")
                return qsTr("No prints found. If the scan has prints, their frames may match the background: use a template or scan on a contrasting background.")
            if (w.indexOf("dpi_rescaled") === 0) return qsTr("Scanner output was scaled; resolution measured from the prints (%1).").arg(w.split(": ")[1] || "")
            if (w === "dpi_unknown") return qsTr("The scan has no resolution; sizes are estimated.")
            if (w === "too_many_candidates") return qsTr("More than 12 objects were found; only the 12 largest are kept as prints.")
            return w
        }
        function statusText(c) {
            const s = []
            s.push(c.confidence === "high" ? qsTr("high confidence") : c.confidence === "medium" ? qsTr("check recommended") : qsTr("review required"))
            if (c.reviewed) s.push(qsTr("reviewed"))
            if (c.excluded) s.push(qsTr("excluded"))
            if (c.exported) s.push(qsTr("exported"))
            if (c.relinked) s.push(qsTr("relinked"))
            if (c.finalExported) s.push(qsTr("final exported"))
            return s.join(" · ")
        }
    }
    // ---------------------------------------------------------------- single-key shortcuts (typing guard)
    Shortcut { sequence: "V"; enabled: !win.typing; onActivated: canvasArea.canvas.tool = "select" }
    Shortcut { sequence: "H"; enabled: !win.typing; onActivated: canvasArea.canvas.tool = "hand" }
    Shortcut { sequence: "O"; enabled: !win.typing; onActivated: canvasArea.canvas.tool = "outer" }
    Shortcut { sequence: "I"; enabled: !win.typing; onActivated: canvasArea.canvas.tool = "inner" }
    Shortcut { sequence: "1"; enabled: !win.typing; onActivated: canvasArea.canvas.mode = "scan" }
    Shortcut { sequence: "2"; enabled: !win.typing; onActivated: canvasArea.canvas.mode = "print" }
    Shortcut { sequence: "3"; enabled: !win.typing; onActivated: canvasArea.canvas.mode = "image" }
    Shortcut { sequence: "4"; enabled: !win.typing; onActivated: canvasArea.canvas.mode = "composite" }
    Shortcut { sequence: "Z"; enabled: !win.typing; onActivated: canvasArea.canvas.toggleFit() }
    Shortcut { sequence: "X"; enabled: !win.typing; onActivated: App.toggleExcludedSelection() }
    Shortcut { sequence: "Y"; enabled: !win.typing && canvasArea.canvas.mode === "composite"; onActivated: canvasArea.canvas.compare = !canvasArea.canvas.compare }
    Shortcut { sequence: "\\"; enabled: !win.typing && canvasArea.canvas.mode === "composite"; onActivated: canvasArea.canvas.showOriginal = !canvasArea.canvas.showOriginal }
    Shortcut { sequence: "Left"; enabled: !win.typing && !canvasArea.canvas.handleActive; onActivated: App.stepItem(-1) }
    Shortcut { sequence: "Right"; enabled: !win.typing && !canvasArea.canvas.handleActive; onActivated: App.stepItem(1) }
    // a selected handle owns the arrows (Shift = 10 px nudge), also with Shift (UX-03)
    Shortcut { sequence: "Shift+Left"; enabled: !win.typing && !canvasArea.canvas.handleActive; onActivated: App.stepScan(-1) }
    Shortcut { sequence: "Shift+Right"; enabled: !win.typing && !canvasArea.canvas.handleActive; onActivated: App.stepScan(1) }
    Shortcut { sequence: "Escape"; enabled: !win.typing; onActivated: canvasArea.canvas.tool = "select" }
    // UX-03: a lone tap of Shift switches the edited boundary (Outer ↔ Inner). Holding Shift only constrains a
    // drag; Shift+click, Shift+arrows, Shift+wheel and Shift shortcuts never switch (ShiftTap). Not while typing,
    // not with a dialog or menu open, never mid-drag (the canvas refuses).
    ShiftTap {
        id: shiftTap
        objectName: "shiftTap"
        window: win
        enabled: App.settings.shiftSwitchesBoundary === true
        onTapped: win.switchBoundary(true)
    }
    function inPopup(item) {
        for (let p = item; p; p = p.parent)
            if (p === Overlay.overlay) return true
        return false
    }
    function dialogOpen() {
        for (const d of modalDialogs)
            if (d.visible) return true
        return false
    }
    function switchBoundary(fromShift) {
        if (fromShift && (typing || inPopup(activeFocusItem) || dialogOpen())) return false
        return canvasArea.canvas.switchBoundary()
    }
    // modifier shortcuts are owned by the menu bar (native on macOS); these cover non-macOS
    Shortcut { sequences: [StandardKey.ZoomIn, "Ctrl+="]; enabled: !win.isMac; onActivated: canvasArea.canvas.zoomStep(1) }
    Shortcut { sequence: StandardKey.ZoomOut; enabled: !win.isMac; onActivated: canvasArea.canvas.zoomStep(-1) }
    Shortcut { sequence: "Ctrl+Y"; enabled: !win.isMac && App.canRedo; onActivated: App.redo() }

    // ---------------------------------------------------------------- menus
    Loader {
        active: win.isMac
        source: "MacMenuBar.qml"
        onLoaded: { item.cmd = cmd; item.canvas = canvasArea.canvas; item.win = win }
    }
    menuBar: Loader {
        active: !win.isMac
        source: "AppMenuBar.qml"
        onLoaded: { item.cmd = cmd; item.canvas = canvasArea.canvas; item.win = win }
    }

    // ---------------------------------------------------------------- layout
    // UX-10 (ui-reference §4.1): left rail · centre column (top bar, canvas) · right rail above a filmstrip that
    // spans the whole window; each panel folds with a strip on the window edge outside it (also View menu and
    // shortcuts). The canvas refits when its size changes at fit.
    ColumnLayout {
        anchors.fill: parent
        spacing: 0
        RowLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            spacing: 0
            EdgeStrip {
                objectName: "edgeLeft"
                edge: "left"
                open: leftRail.visible
                showText: qsTr("Show Left Rail"); hideText: qsTr("Hide Left Rail")
                shortcutText: win.isMac ? "⌥⌘1" : "Ctrl+Alt+1"
                Layout.fillHeight: true
                onToggled: cmd.toggleLeft()
            }
            LeftRail {
                id: leftRail
                objectName: "leftRail"
                visible: App.settings.showLeftRail
                cmd: cmd
                Layout.preferredWidth: App.settings.leftRailWidth
                Layout.fillHeight: true
            }
            RailGrip { visible: leftRail.visible; target: "leftRailWidth"; minW: Theme.leftRailMin; maxW: Theme.leftRailMax; defW: Theme.leftRailDefault; sign: 1 }
            ColumnLayout {
                Layout.fillWidth: true
                Layout.fillHeight: true
                spacing: 0
                TopBar { objectName: "topBar"; Layout.fillWidth: true; canvas: canvasArea.canvas; cmd: cmd }
                CanvasArea {
                    id: canvasArea
                    cmd: cmd
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    Toasts {
                        id: toasts
                        anchors.horizontalCenter: parent.horizontalCenter
                        anchors.bottom: parent.bottom
                        anchors.bottomMargin: 16
                        width: Math.min(parent.width - 40, 600)
                    }
                }
            }
            RailGrip { visible: rightRail.visible; target: "rightRailWidth"; minW: Theme.rightRailMin; maxW: Theme.rightRailMax; defW: Theme.rightRailDefault; sign: -1 }
            RightRail {
                id: rightRail
                objectName: "rightRail"
                visible: App.settings.showRightRail
                cmd: cmd
                canvas: canvasArea.canvas
                Layout.preferredWidth: App.settings.rightRailWidth
                Layout.fillHeight: true
            }
            EdgeStrip {
                objectName: "edgeRight"
                edge: "right"
                open: rightRail.visible
                reach: open ? 0 : Theme.edgeStripReach // the right rail's scroll bar is next to the strip
                showText: qsTr("Show Right Rail"); hideText: qsTr("Hide Right Rail")
                shortcutText: win.isMac ? "⌥⌘2" : "Ctrl+Alt+2"
                Layout.fillHeight: true
                onToggled: cmd.toggleRight()
            }
        }
        Filmstrip {
            id: filmstrip
            objectName: "filmstrip"
            visible: App.settings.showFilmstrip
            Layout.fillWidth: true
            cmd: cmd
            canvas: canvasArea.canvas
        }
        EdgeStrip {
            objectName: "edgeBottom"
            edge: "bottom"
            open: filmstrip.visible
            reach: open ? Theme.edgeStripReach : 0 // folded: the rails' scroll bars end right above it
            showText: qsTr("Show Filmstrip"); hideText: qsTr("Hide Filmstrip")
            shortcutText: win.isMac ? "⌥⌘3" : "Ctrl+Alt+3"
            Layout.fillWidth: true
            onToggled: cmd.toggleFilmstrip()
        }
    }

    // window-wide drop: projects open; graded files (our IDs, an export name, or not a TIFF) relink; other TIFFs
    // import as scans (classification in App.dropFiles)
    DropArea {
        anchors.fill: parent
        onDropped: (drop) => App.dropFiles(drop.urls)
    }

    // ---------------------------------------------------------------- dialogs
    FileDialog {
        id: scanDialog
        title: qsTr("Import scans")
        fileMode: FileDialog.OpenFiles
        nameFilters: [qsTr("TIFF scans (*.tif *.tiff)")]
        onAccepted: App.importScans(selectedFiles)
    }
    FileDialog {
        id: relinkFileDialog
        property string target: ""
        title: target.length ? qsTr("Choose the graded file for this print") : qsTr("Choose graded files")
        fileMode: target.length ? FileDialog.OpenFile : FileDialog.OpenFiles
        nameFilters: [qsTr("Images (*.tif *.tiff *.png *.jpg *.jpeg)")]
        onAccepted: App.relinkFiles(target.length ? [selectedFile] : selectedFiles, target)
    }
    FileDialog {
        id: projectOpenDialog
        title: qsTr("Open project")
        fileMode: FileDialog.OpenFile
        nameFilters: [qsTr("FrameKeeper projects (*.fkproj)")]
        onAccepted: win.replaceProject("openUrl", selectedFile)
    }
    FileDialog {
        id: projectSaveDialog
        title: qsTr("Save project")
        fileMode: FileDialog.SaveFile
        defaultSuffix: "fkproj"
        nameFilters: [qsTr("FrameKeeper projects (*.fkproj)")]
        onAccepted: {
            const ok = App.saveProjectAs(selectedFile)
            const p = win.saveThen
            win.saveThen = null
            if (ok && p) win.doReplace(p.action, p.url)
        }
        onRejected: win.saveThen = null // no path chosen: the project stays as it is, unsaved
    }
    FolderDialog {
        id: folderDialog
        property string purpose: "import"
        title: purpose === "export" ? qsTr("Export folder") : purpose === "relocate" ? qsTr("Folder with the moved scans") : qsTr("Import folder")
        onAccepted: {
            if (purpose === "export") App.setExportFolder(selectedFolder)
            else if (purpose === "relocate") App.relocateMissing(selectedFolder)
            else App.importFolder(selectedFolder)
        }
    }
    ExportDialog { id: exportDialog; onChooseFolder: cmd.chooseExportFolder() }
    RelinkDialog { id: relinkDialog }
    FinalDialog { id: finalDialog }
    SettingsDialog { id: settingsDialog }
    AboutDialog { id: aboutDialog }
    NameDialog { id: nameDialog; onDone: (t) => App.saveTemplate(t) }
    FkDialog {
        id: redetectConfirm
        title: qsTr("Detect again?")
        contentItem: ColumnLayout {
            spacing: 10
            Text {
                Layout.fillWidth: true
                wrapMode: Text.WordWrap
                font.pixelSize: Theme.fontBody
                color: Theme.textPrimary
                text: qsTr("Detected and edited boundaries of this scan will be replaced. Exported files stay on disk but lose their link to the new prints.")
            }
            RowLayout {
                Layout.alignment: Qt.AlignRight
                FkButton { text: qsTr("Cancel"); onClicked: redetectConfirm.close() }
                FkButton {
                    primary: true
                    text: qsTr("Detect again")
                    onClicked: {
                        redetectConfirm.close()
                        if (cmd.pendingDetect.all) App.detectSelectedScans(cmd.pendingDetect.tmpl)
                        else App.detectScan(cmd.pendingDetect.scan, cmd.pendingDetect.tmpl)
                    }
                }
            }
        }
    }

    Connections {
        target: App
        function onNotification(text, kind) { toasts.push(text, kind) }
        function onRelinkResults(rows) { relinkDialog.show(rows) }
        function onOpenRequested(url) { win.replaceProject("openUrl", url) }
        function onBackupAvailable(path, backupName, backupTime) { backupDialog.show(path, backupName, backupTime) }
        function onRelinkRowUpdated(row) { relinkDialog.updateRow(row) }
        function onExportFinished(folder, written, failed) {
            toasts.push(failed > 0 ? qsTr("%1 written, %2 failed.").arg(written).arg(failed) : qsTr("%n file(s) written.", "", written),
                        failed > 0 ? "warning" : "info", function() { App.revealInFileManager(folder) },
                        win.isMac ? qsTr("Reveal") : qsTr("Show"))
        }
    }

    // ---------------------------------------------------------------- data protection (FK-001, FK-008)
    property var pendingReplace: null
    property var saveThen: null
    property bool quitConfirmed: false
    onClosing: (close) => {
        const info = App.replaceInfo()
        if (!quitConfirmed && (info.unsaved || info.jobs > 0)) {
            close.accepted = false
            replaceProject("quit", "")
        }
    }
    /// Asks before anything replaces the open project: new, open (menu, recent, drop), restore, recover, quit.
    function replaceProject(action, url) {
        const info = App.replaceInfo()
        if (!info.unsaved && info.jobs === 0) { doReplace(action, url); return }
        pendingReplace = { action: action, url: url || "", unsaved: info.unsaved, jobs: info.jobs }
        replaceConfirm.open()
    }
    function doReplace(action, url) {
        if (action === "new") App.newProject()
        else if (action === "openUrl") App.openProject(url)
        else if (action === "restore") App.restoreBackup(url)
        else if (action === "recover") App.recoverSnapshot(url)
        else if (action === "quit") { quitConfirmed = true; win.close() }
    }
    /// "save", "dontsave" or "cancel".
    function finishReplace(choice) {
        const p = pendingReplace
        pendingReplace = null
        replaceConfirm.close()
        if (!p || choice === "cancel") return
        if (choice === "dontsave") {
            App.discardCurrentRecovery()
            doReplace(p.action, p.url)
            return
        }
        if (App.projectPath.length === 0) { saveThen = p; projectSaveDialog.open(); return }
        if (App.saveProject()) doReplace(p.action, p.url)
        // a failed save keeps everything as it was; the error has been shown
    }
    FkDialog {
        id: replaceConfirm
        objectName: "replaceConfirm"
        title: win.pendingReplace && win.pendingReplace.unsaved ? qsTr("Unsaved changes") : qsTr("Tasks running")
        onRejected: win.pendingReplace = null
        contentItem: ColumnLayout {
            spacing: 10
            Text {
                Layout.fillWidth: true; wrapMode: Text.WordWrap; font.pixelSize: Theme.fontBody; color: Theme.textPrimary
                text: !win.pendingReplace ? ""
                      : (win.pendingReplace.unsaved ? (win.pendingReplace.action === "quit" ? qsTr("Save the project before quitting?")
                                                                                            : qsTr("Save the project before closing it?"))
                                                    : qsTr("Continue?"))
                        + (win.pendingReplace.jobs > 0 ? " " + qsTr("%n background task(s) will be cancelled.", "", win.pendingReplace.jobs) : "")
            }
            RowLayout {
                Layout.alignment: Qt.AlignRight
                FkButton {
                    text: win.pendingReplace && win.pendingReplace.unsaved ? qsTr("Don't save") : qsTr("Continue")
                    onClicked: win.finishReplace("dontsave")
                }
                FkButton { text: qsTr("Cancel"); onClicked: win.finishReplace("cancel") }
                FkButton {
                    visible: win.pendingReplace !== null && win.pendingReplace.unsaved
                    primary: true; text: qsTr("Save"); onClicked: win.finishReplace("save")
                }
            }
        }
    }
    FkDialog {
        id: backupDialog
        property string path: ""
        property string backupName: ""
        property string backupTime: ""
        function show(p, n, t) { path = p; backupName = n; backupTime = t; open() }
        title: qsTr("The project file is damaged")
        contentItem: ColumnLayout {
            spacing: 10
            Text {
                Layout.fillWidth: true; wrapMode: Text.WordWrap; font.pixelSize: Theme.fontBody; color: Theme.textPrimary
                text: qsTr("A backup from the last save is available (%1, %2). Restore it? The damaged file is kept next to it under a new name.")
                      .arg(backupDialog.backupName).arg(backupDialog.backupTime)
            }
            RowLayout {
                Layout.alignment: Qt.AlignRight
                FkButton { text: qsTr("Cancel"); onClicked: backupDialog.close() }
                FkButton { primary: true; text: qsTr("Restore backup"); onClicked: { backupDialog.close(); win.replaceProject("restore", backupDialog.path) } }
            }
        }
    }
    FkDialog {
        id: recoveryDialog
        property var snaps: []
        function show(list) { snaps = list; open() }
        title: qsTr("Recover unsaved work")
        contentItem: ColumnLayout {
            spacing: 8
            Text {
                Layout.fillWidth: true; wrapMode: Text.WordWrap; font.pixelSize: Theme.fontBody; color: Theme.textPrimary
                text: qsTr("FrameKeeper was closed with unsaved changes. Recovered work opens unsaved; save it to keep it.")
            }
            Repeater {
                model: recoveryDialog.snaps
                RowLayout {
                    required property var modelData
                    Layout.fillWidth: true
                    spacing: 6
                    Text {
                        Layout.fillWidth: true; elide: Text.ElideMiddle; font.pixelSize: Theme.fontBody; color: Theme.textPrimary
                        text: qsTr("%1 · %2").arg(modelData.name).arg(modelData.savedAt)
                              + (modelData.projectPath ? " · " + modelData.projectPath : " · " + qsTr("never saved"))
                    }
                    FkButton { text: qsTr("Discard"); onClicked: { App.discardSnapshot(modelData.file); recoveryDialog.snaps = App.recoverySnapshots(); if (!recoveryDialog.snaps.length) recoveryDialog.close() } }
                    FkButton { primary: true; text: qsTr("Recover"); onClicked: { recoveryDialog.close(); win.replaceProject("recover", modelData.file) } }
                }
            }
            RowLayout {
                Layout.alignment: Qt.AlignRight
                FkButton { text: qsTr("Later"); onClicked: recoveryDialog.close() }
            }
        }
    }
    Component.onCompleted: {
        const snaps = App.recoverySnapshots()
        if (snaps.length) recoveryDialog.show(snaps)
    }
    function showSettings() { settingsDialog.open() }
    readonly property var commands: cmd // self-test: the same commands as the menus
    readonly property var modalDialogs: [relinkDialog, exportDialog, finalDialog, settingsDialog, aboutDialog,
                                         nameDialog, redetectConfirm, replaceConfirm, backupDialog, recoveryDialog]
    function closeDialogs() {
        for (const d of modalDialogs)
            d.close()
    }
    function showAbout() { aboutDialog.open() }
}
