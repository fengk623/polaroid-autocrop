// Native macOS menu bar (Qt Labs Platform). Single-key shortcuts appear in the titles only, so that they
// never fire while typing (ui-reference §2.5, typing guard).
import QtQuick
import Qt.labs.platform as P
import FrameKeeper.Core

P.MenuBar {
    id: bar
    property var cmd
    property var canvas
    property var win

    P.Menu {
        title: qsTr("File")
        P.MenuItem { text: qsTr("New Project"); shortcut: StandardKey.New; onTriggered: cmd.newProject() }
        P.MenuItem { text: qsTr("Open Project…"); shortcut: StandardKey.Open; onTriggered: cmd.openProject() }
        P.MenuItem { text: qsTr("Recover Unsaved Work…"); onTriggered: cmd.recoverWork() }
        P.MenuSeparator {}
        P.MenuItem { text: qsTr("Import Scans…"); shortcut: "Ctrl+I"; onTriggered: cmd.importScans() }
        P.MenuItem { text: qsTr("Import Folder…"); shortcut: "Ctrl+Shift+I"; onTriggered: cmd.importFolder() }
        P.MenuSeparator {}
        P.MenuItem { text: qsTr("Save"); shortcut: StandardKey.Save; enabled: !App.readOnly; onTriggered: cmd.save() }
        P.MenuItem { text: qsTr("Save As…"); shortcut: "Ctrl+Shift+S"; onTriggered: cmd.saveAs() }
        P.MenuSeparator {}
        P.MenuItem { text: qsTr("Export Image Areas…"); shortcut: "Ctrl+E"; enabled: App.counts.prints > 0; onTriggered: cmd.exportImageAreas([]) }
        P.MenuItem { text: qsTr("Export Full Prints…"); enabled: App.counts.prints > 0; onTriggered: cmd.exportFullPrints([]) }
        P.MenuItem { text: qsTr("Relink Graded Files…"); shortcut: "Ctrl+Alt+I"; enabled: App.counts.prints > 0; onTriggered: cmd.relink() }
        P.MenuItem { text: qsTr("Export Final…"); shortcut: "Ctrl+Shift+E"; enabled: App.counts.relinked > 0; onTriggered: cmd.exportFinal([]) }
        P.MenuSeparator {}
        P.MenuItem { text: qsTr("Close Project"); shortcut: StandardKey.Close; onTriggered: cmd.newProject() }
        P.MenuItem { text: qsTr("Quit"); role: P.MenuItem.QuitRole; shortcut: StandardKey.Quit; onTriggered: win.close() }
        P.MenuItem { text: qsTr("About FrameKeeper"); role: P.MenuItem.AboutRole; onTriggered: win.showAbout() }
        P.MenuItem { text: qsTr("Settings…"); role: P.MenuItem.PreferencesRole; shortcut: "Ctrl+,"; onTriggered: win.showSettings() }
    }
    P.Menu {
        title: qsTr("Edit")
        P.MenuItem { text: qsTr("Undo"); shortcut: StandardKey.Undo; enabled: App.canUndo; onTriggered: App.undo() }
        P.MenuItem { text: qsTr("Redo"); shortcut: StandardKey.Redo; enabled: App.canRedo; onTriggered: App.redo() }
        P.MenuSeparator {}
        P.MenuItem { text: qsTr("Select All Prints"); shortcut: StandardKey.SelectAll; onTriggered: App.selectAllItems() }
    }
    P.Menu {
        title: qsTr("View")
        P.MenuItem { text: qsTr("Zoom In"); shortcut: "Ctrl+="; onTriggered: canvas.zoomStep(1) }
        P.MenuItem { text: qsTr("Zoom Out"); shortcut: "Ctrl+-"; onTriggered: canvas.zoomStep(-1) }
        P.MenuItem { text: qsTr("Fit"); shortcut: "Ctrl+0"; onTriggered: canvas.fit() }
        P.MenuItem { text: qsTr("100 %"); shortcut: "Ctrl+1"; onTriggered: canvas.zoomTo(100) }
        P.MenuItem { text: qsTr("Toggle Fit ↔ 100 %") + "   Z"; onTriggered: canvas.toggleFit() }
        P.MenuSeparator {}
        P.MenuItem { text: qsTr("Scan") + "   1"; onTriggered: canvas.mode = "scan" }
        P.MenuItem { text: qsTr("Full Print") + "   2"; onTriggered: canvas.mode = "print" }
        P.MenuItem { text: qsTr("Image Area") + "   3"; onTriggered: canvas.mode = "image" }
        P.MenuItem { text: qsTr("Composite") + "   4"; onTriggered: canvas.mode = "composite" }
        P.MenuItem { text: qsTr("Compare Split") + "   Y"; enabled: canvas.mode === "composite"; onTriggered: canvas.compare = !canvas.compare }
        P.MenuItem { text: qsTr("Original ↔ Composite") + "   \\"; enabled: canvas.mode === "composite"; onTriggered: canvas.showOriginal = !canvas.showOriginal }
        P.MenuItem { text: qsTr("Show/Hide Overlays"); shortcut: "Ctrl+Shift+H"; onTriggered: canvas.showOverlays = !canvas.showOverlays }
        P.MenuSeparator {}
        P.MenuItem { text: qsTr("Left Rail"); shortcut: "Ctrl+Alt+1"; onTriggered: cmd.toggleLeft() }
        P.MenuItem { text: qsTr("Right Rail"); shortcut: "Ctrl+Alt+2"; onTriggered: cmd.toggleRight() }
        P.MenuItem { text: qsTr("Filmstrip"); shortcut: "Ctrl+Alt+3"; onTriggered: cmd.toggleFilmstrip() }
        P.MenuItem { text: qsTr("All Panels"); shortcut: "Ctrl+Alt+0"; onTriggered: cmd.toggleAll() }
        P.MenuItem { text: qsTr("Full Screen"); shortcut: "Meta+Ctrl+F"; onTriggered: win.visibility = win.visibility === Window.FullScreen ? Window.Windowed : Window.FullScreen }
        P.MenuItem { text: qsTr("Reset Layout"); onTriggered: cmd.resetLayout() }
    }
    P.Menu {
        title: qsTr("Print")
        P.MenuItem { text: qsTr("Previous Print") + "   ←"; onTriggered: App.stepItem(-1) }
        P.MenuItem { text: qsTr("Next Print") + "   →"; onTriggered: App.stepItem(1) }
        P.MenuItem { text: qsTr("Previous Scan") + "   ⇧←"; onTriggered: App.stepScan(-1) }
        P.MenuItem { text: qsTr("Next Scan") + "   ⇧→"; onTriggered: App.stepScan(1) }
        P.MenuSeparator {}
        P.MenuItem { text: qsTr("Mark Reviewed and Next"); shortcut: "Ctrl+Return"; onTriggered: App.reviewAndNext() }
        P.MenuItem { text: qsTr("Exclude / Include") + "   X"; onTriggered: App.toggleExcludedSelection() }
        P.MenuItem { text: qsTr("Rotate Upright Left"); shortcut: "Ctrl+["; onTriggered: App.rotateUpright(App.currentItemId, -1) }
        P.MenuItem { text: qsTr("Rotate Upright Right"); shortcut: "Ctrl+]"; onTriggered: App.rotateUpright(App.currentItemId, 1) }
        P.MenuItem { text: qsTr("Snap Outer to Edges"); onTriggered: App.snapOuter(App.currentItemId) }
        P.MenuItem { text: qsTr("Reset to Detected"); onTriggered: App.resetToDetected(App.currentItemId) }
        P.MenuSeparator {}
        P.MenuItem { text: qsTr("Detect Again"); enabled: App.currentScanId.length > 0; onTriggered: cmd.detectAgain(App.currentScanId, "") }
        P.MenuItem { text: qsTr("Reveal Scan in Finder"); shortcut: "Ctrl+Alt+R"; onTriggered: App.revealInFileManager(App.currentScan.path) }
    }
    P.Menu {
        title: qsTr("Help")
        P.MenuItem { text: qsTr("FrameKeeper Help"); shortcut: "Ctrl+?"; onTriggered: Qt.openUrlExternally("https://github.com/fengk623/polaroid-autocrop#readme") }
    }
}
