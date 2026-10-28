// In-window menu bar (Windows/Linux), Alt mnemonics. Single-key shortcuts are shown in the item text
// only (handled behind the typing guard in Main.qml), never as accelerators (ui-reference §2.11).
import QtQuick
import QtQuick.Controls.Basic
import FrameKeeper.Core

MenuBar {
    id: bar
    property var cmd
    property var canvas
    property var win
    background: Rectangle { color: Theme.surfacePanel; Rectangle { anchors.bottom: parent.bottom; width: parent.width; height: 1; color: Theme.lineRegion } }
    delegate: MenuBarItem {
        id: mbi
        contentItem: Text { text: mbi.text.replace("&", ""); font.pixelSize: Theme.fontBody; color: Theme.textPrimary; horizontalAlignment: Text.AlignHCenter; verticalAlignment: Text.AlignVCenter }
        background: Rectangle { color: mbi.highlighted ? Theme.hoverOverlay : "transparent" }
    }

    component Item2: MenuItem {
        id: mi
        property string keys: ""
        implicitHeight: 26
        contentItem: Item {
            implicitWidth: lbl.implicitWidth + keysText.implicitWidth + 40
            Text { id: lbl; text: mi.text.replace("&", ""); anchors.verticalCenter: parent.verticalCenter; font.pixelSize: Theme.fontBody; color: Theme.textPrimary; opacity: mi.enabled ? 1 : Theme.disabledOpacity }
            Text { id: keysText; text: mi.keys || (mi.action && mi.action.shortcut ? mi.action.shortcut.toString() : ""); anchors.right: parent.right; anchors.verticalCenter: parent.verticalCenter; font.pixelSize: Theme.fontCaption; color: Theme.textTertiary }
        }
        background: Rectangle { color: mi.highlighted ? Theme.selectionFill : "transparent" }
    }
    component Menu2: Menu {
        background: Rectangle { implicitWidth: 280; color: Theme.surfacePanel; border.color: Theme.lineSection; radius: Theme.radius }
    }

    Menu2 {
        title: qsTr("&File")
        Item2 { action: Action { text: qsTr("New Project"); shortcut: StandardKey.New; onTriggered: cmd.newProject() } }
        Item2 { action: Action { text: qsTr("Open Project…"); shortcut: StandardKey.Open; onTriggered: cmd.openProject() } }
        MenuSeparator {}
        Item2 { action: Action { text: qsTr("Import Scans…"); shortcut: "Ctrl+I"; onTriggered: cmd.importScans() } }
        Item2 { action: Action { text: qsTr("Import Folder…"); shortcut: "Ctrl+Shift+I"; onTriggered: cmd.importFolder() } }
        MenuSeparator {}
        Item2 { action: Action { text: qsTr("Save"); shortcut: StandardKey.Save; enabled: !App.readOnly; onTriggered: cmd.save() } }
        Item2 { action: Action { text: qsTr("Save As…"); shortcut: "Ctrl+Shift+S"; onTriggered: cmd.saveAs() } }
        MenuSeparator {}
        Item2 { action: Action { text: qsTr("Export Image Areas…"); shortcut: "Ctrl+E"; enabled: App.counts.prints > 0; onTriggered: cmd.exportImageAreas([]) } }
        Item2 { action: Action { text: qsTr("Export Full Prints…"); enabled: App.counts.prints > 0; onTriggered: cmd.exportFullPrints([]) } }
        Item2 { action: Action { text: qsTr("Relink Graded Files…"); shortcut: "Ctrl+Alt+I"; enabled: App.counts.exported > 0; onTriggered: cmd.relink() } }
        Item2 { action: Action { text: qsTr("Export Final…"); shortcut: "Ctrl+Shift+E"; enabled: App.counts.relinked > 0; onTriggered: cmd.exportFinal([]) } }
        MenuSeparator {}
        Item2 { action: Action { text: qsTr("Close Project"); shortcut: StandardKey.Close; onTriggered: cmd.newProject() } }
        Item2 { action: Action { text: qsTr("Quit"); shortcut: "Ctrl+Q"; onTriggered: win.close() } }
    }
    Menu2 {
        title: qsTr("&Edit")
        Item2 { action: Action { text: qsTr("Undo"); shortcut: StandardKey.Undo; enabled: App.canUndo; onTriggered: App.undo() } }
        Item2 { action: Action { text: qsTr("Redo"); shortcut: "Ctrl+Shift+Z"; enabled: App.canRedo; onTriggered: App.redo() } }
        MenuSeparator {}
        Item2 { action: Action { text: qsTr("Select All Prints"); shortcut: StandardKey.SelectAll; onTriggered: App.selectAllItems() } }
        MenuSeparator {}
        Item2 { action: Action { text: qsTr("Settings…"); shortcut: "Ctrl+,"; onTriggered: win.showSettings() } }
    }
    Menu2 {
        title: qsTr("&View")
        Item2 { action: Action { text: qsTr("Zoom In"); onTriggered: canvas.zoomStep(1) } keys: "Ctrl+=" }
        Item2 { action: Action { text: qsTr("Zoom Out"); onTriggered: canvas.zoomStep(-1) } keys: "Ctrl+−" }
        Item2 { action: Action { text: qsTr("Fit"); shortcut: "Ctrl+0"; onTriggered: canvas.fit() } }
        Item2 { action: Action { text: qsTr("100 %"); shortcut: "Ctrl+1"; onTriggered: canvas.zoomTo(100) } }
        Item2 { text: qsTr("Toggle Fit ↔ 100 %"); keys: "Z"; onTriggered: canvas.toggleFit() }
        MenuSeparator {}
        Item2 { text: qsTr("Scan"); keys: "1"; onTriggered: canvas.mode = "scan" }
        Item2 { text: qsTr("Full Print"); keys: "2"; onTriggered: canvas.mode = "print" }
        Item2 { text: qsTr("Image Area"); keys: "3"; onTriggered: canvas.mode = "image" }
        Item2 { text: qsTr("Composite"); keys: "4"; onTriggered: canvas.mode = "composite" }
        Item2 { text: qsTr("Compare Split"); keys: "Y"; enabled: canvas.mode === "composite"; onTriggered: canvas.compare = !canvas.compare }
        Item2 { text: qsTr("Original ↔ Composite"); keys: "\\"; enabled: canvas.mode === "composite"; onTriggered: canvas.showOriginal = !canvas.showOriginal }
        Item2 { action: Action { text: qsTr("Show/Hide Overlays"); shortcut: "Ctrl+Shift+H"; onTriggered: canvas.showOverlays = !canvas.showOverlays } }
        MenuSeparator {}
        Item2 { action: Action { text: qsTr("Left Rail"); shortcut: "Ctrl+Alt+1"; onTriggered: cmd.toggleLeft() } }
        Item2 { action: Action { text: qsTr("Right Rail"); shortcut: "Ctrl+Alt+2"; onTriggered: cmd.toggleRight() } }
        Item2 { action: Action { text: qsTr("Filmstrip"); shortcut: "Ctrl+Alt+3"; onTriggered: cmd.toggleFilmstrip() } }
        Item2 { action: Action { text: qsTr("All Panels"); shortcut: "Ctrl+Alt+0"; onTriggered: cmd.toggleAll() } }
        Item2 { action: Action { text: qsTr("Full Screen"); shortcut: "F11"; onTriggered: win.visibility = win.visibility === Window.FullScreen ? Window.Windowed : Window.FullScreen } }
        Item2 { text: qsTr("Reset Layout"); onTriggered: cmd.resetLayout() }
    }
    Menu2 {
        title: qsTr("&Print")
        Item2 { text: qsTr("Previous Print"); keys: "←"; onTriggered: App.stepItem(-1) }
        Item2 { text: qsTr("Next Print"); keys: "→"; onTriggered: App.stepItem(1) }
        Item2 { text: qsTr("Previous Scan"); keys: "Shift+←"; onTriggered: App.stepScan(-1) }
        Item2 { text: qsTr("Next Scan"); keys: "Shift+→"; onTriggered: App.stepScan(1) }
        MenuSeparator {}
        Item2 { action: Action { text: qsTr("Mark Reviewed and Next"); shortcut: "Ctrl+Return"; onTriggered: App.reviewAndNext() } }
        Item2 { text: qsTr("Exclude / Include"); keys: "X"; onTriggered: App.toggleExcludedSelection() }
        Item2 { action: Action { text: qsTr("Rotate Upright Left"); shortcut: "Ctrl+["; onTriggered: App.rotateUpright(App.currentItemId, -1) } }
        Item2 { action: Action { text: qsTr("Rotate Upright Right"); shortcut: "Ctrl+]"; onTriggered: App.rotateUpright(App.currentItemId, 1) } }
        Item2 { text: qsTr("Snap Outer to Edges"); onTriggered: App.snapOuter(App.currentItemId) }
        Item2 { text: qsTr("Reset to Detected"); onTriggered: App.resetToDetected(App.currentItemId) }
        MenuSeparator {}
        Item2 { text: qsTr("Detect Again"); enabled: App.currentScanId.length > 0; onTriggered: cmd.detectAgain(App.currentScanId, "") }
        Item2 { action: Action { text: qsTr("Show Scan in Explorer"); shortcut: "Ctrl+Alt+R"; onTriggered: App.revealInFileManager(App.currentScan.path) } }
    }
    Menu2 {
        title: qsTr("&Help")
        Item2 { action: Action { text: qsTr("Help"); shortcut: "F1"; onTriggered: Qt.openUrlExternally("https://github.com/fengk623/polaroid-autocrop#readme") } }
        Item2 { text: qsTr("About FrameKeeper"); onTriggered: win.showAbout() }
    }
}
