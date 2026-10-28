// Main window: left rail · centre column (top bar, canvas, filmstrip) · right rail (ui-reference §4.1).
import QtQuick
import QtQuick.Layouts
import QtQuick.Controls.Basic
import QtQuick.Dialogs
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
        function openProject() { projectOpenDialog.open() }
        function save() { if (App.projectPath.length === 0) projectSaveDialog.open(); else App.saveProject() }
        function saveAs() { projectSaveDialog.open() }
        function newProject() { App.newProject() }
        function relocate() { folderDialog.purpose = "relocate"; folderDialog.open() }
        function chooseExportFolder() { folderDialog.purpose = "export"; folderDialog.open() }
        function exportImageAreas(ids) { exportDialog.openFor(ids === undefined ? App.selectedItemIds : ids, false) }
        function exportFullPrints(ids) { exportDialog.openFor(ids === undefined ? App.selectedItemIds : ids, true) }
        function relink() { relinkFileDialog.open() }
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
        function resetLayout() { App.setSetting("leftRailWidth", Theme.leftRailDefault); App.setSetting("rightRailWidth", Theme.rightRailDefault) }
        function filmTypeName(id) {
            return ({ "polaroid_600": qsTr("Polaroid 600 / i-Type / SX-70"), "polaroid_round": qsTr("Polaroid round frame"),
                      "instax_mini": qsTr("Instax Mini"), "instax_square": qsTr("Instax Square"), "instax_wide": qsTr("Instax Wide") })[id] || qsTr("Unknown size")
        }
        function warningText(w) {
            if (w.indexOf("count_mismatch") === 0) return qsTr("Print count differs from the expected number.")
            if (w.indexOf("dpi_rescaled") === 0) return qsTr("Scanner output was scaled; resolution measured from the prints (%1).").arg(w.split(": ")[1] || "")
            if (w === "dpi_unknown") return qsTr("The scan has no resolution; sizes are estimated.")
            if (w === "too_many_candidates") return qsTr("More objects than the maximum of 12 prints.")
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
    Shortcut { sequence: "Left"; enabled: !win.typing && !canvasArea.canvas.hasHandle(); onActivated: App.stepItem(-1) }
    Shortcut { sequence: "Right"; enabled: !win.typing && !canvasArea.canvas.hasHandle(); onActivated: App.stepItem(1) }
    Shortcut { sequence: "Shift+Left"; enabled: !win.typing; onActivated: App.stepScan(-1) }
    Shortcut { sequence: "Shift+Right"; enabled: !win.typing; onActivated: App.stepScan(1) }
    Shortcut { sequence: "Escape"; enabled: !win.typing; onActivated: canvasArea.canvas.tool = "select" }
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
    RowLayout {
        anchors.fill: parent
        spacing: 0
        LeftRail {
            id: leftRail
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
            TopBar { Layout.fillWidth: true; canvas: canvasArea.canvas; cmd: cmd }
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
            Filmstrip { visible: App.settings.showFilmstrip; Layout.fillWidth: true; cmd: cmd; canvas: canvasArea.canvas }
        }
        RailGrip { visible: rightRail.visible; target: "rightRailWidth"; minW: Theme.rightRailMin; maxW: Theme.rightRailMax; defW: Theme.rightRailDefault; sign: -1 }
        RightRail {
            id: rightRail
            visible: App.settings.showRightRail
            cmd: cmd
            canvas: canvasArea.canvas
            Layout.preferredWidth: App.settings.rightRailWidth
            Layout.fillHeight: true
        }
    }

    // window-wide drop: TIFFs import as scans, projects open, other images offer relink
    DropArea {
        anchors.fill: parent
        onDropped: (drop) => {
            const scans = [], graded = []
            for (const u of drop.urls) {
                const s = u.toString().toLowerCase()
                if (s.endsWith(".fkproj")) { App.openProject(u); return }
                if ((s.endsWith(".tif") || s.endsWith(".tiff")) && s.indexOf("_edit_v") < 0) scans.push(u)
                else graded.push(u)
            }
            if (scans.length) App.importScans(scans)
            if (graded.length) App.relinkFiles(graded)
        }
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
        title: qsTr("Choose graded files")
        fileMode: FileDialog.OpenFiles
        nameFilters: [qsTr("Images (*.tif *.tiff *.png *.jpg *.jpeg)")]
        onAccepted: App.relinkFiles(selectedFiles)
    }
    FileDialog {
        id: projectOpenDialog
        title: qsTr("Open project")
        fileMode: FileDialog.OpenFile
        nameFilters: [qsTr("FrameKeeper projects (*.fkproj)")]
        onAccepted: App.openProject(selectedFile)
    }
    FileDialog {
        id: projectSaveDialog
        title: qsTr("Save project")
        fileMode: FileDialog.SaveFile
        defaultSuffix: "fkproj"
        nameFilters: [qsTr("FrameKeeper projects (*.fkproj)")]
        onAccepted: App.saveProjectAs(selectedFile)
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
        function onExportFinished(folder, written, failed) {
            toasts.push(failed > 0 ? qsTr("%1 written, %2 failed.").arg(written).arg(failed) : qsTr("%n file(s) written.", "", written),
                        failed > 0 ? "warning" : "info", function() { App.revealInFileManager(folder) },
                        win.isMac ? qsTr("Reveal") : qsTr("Show"))
        }
    }

    onClosing: (close) => {
        if (App.dirty && App.counts.scans > 0 && !quitConfirm.confirmed) {
            close.accepted = false
            quitConfirm.open()
        }
    }
    FkDialog {
        id: quitConfirm
        property bool confirmed: false
        title: qsTr("Unsaved changes")
        contentItem: ColumnLayout {
            spacing: 10
            Text { Layout.fillWidth: true; wrapMode: Text.WordWrap; font.pixelSize: Theme.fontBody; color: Theme.textPrimary
                   text: qsTr("Save the project before quitting?") }
            RowLayout {
                Layout.alignment: Qt.AlignRight
                FkButton { text: qsTr("Don't save"); onClicked: { quitConfirm.confirmed = true; quitConfirm.close(); win.close() } }
                FkButton { text: qsTr("Cancel"); onClicked: quitConfirm.close() }
                FkButton { primary: true; text: qsTr("Save"); onClicked: { quitConfirm.close(); cmd.save(); if (!App.dirty) { quitConfirm.confirmed = true; win.close() } } }
            }
        }
    }
    function showSettings() { settingsDialog.open() }
    function closeDialogs() { relinkDialog.close(); exportDialog.close(); finalDialog.close() }
    function showAbout() { aboutDialog.open() }
}
