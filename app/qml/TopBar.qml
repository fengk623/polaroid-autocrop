// Top bar (40, centre column, ui-reference §4.1): Import · Export · Relink │ tools │ status (flex) │
// Compare · Overlays │ Scan · Print · Image · Composite │ zoom. UX-06: the view switch sits between the overlays
// button (eye) and the zoom field. When the column is narrow the bar gives up, in this order, the file buttons
// (File menu, left rail), the wide view switch (a menu button with the same four views) and the − / + buttons
// (zoom field menu, shortcuts); the order eye → view → zoom never changes and nothing runs under the rails.
import QtQuick
import QtQuick.Layouts
import QtQuick.Controls.Basic
import FrameKeeper.Core

Rectangle {
    id: bar
    required property var canvas
    required property var cmd
    implicitHeight: Theme.topBar
    color: Theme.surfacePanel

    // ---- how much fits (0 everything · 1 no file buttons · 2 compact view switch · 3 no − / +)
    readonly property int margins: 2 * (Theme.padding - 6)
    readonly property real fileW: fileCluster.implicitWidth
    readonly property real toolsW: toolCluster.implicitWidth
    readonly property real togglesW: toggleCluster.implicitWidth
    readonly property real viewW: viewSwitch.implicitWidth
    readonly property real viewCompactW: viewMenu.implicitWidth
    readonly property real zoomW: zoomField.implicitWidth + 2 * (Theme.hit + Theme.gap)
    readonly property real zoomCompactW: zoomField.implicitWidth
    // items in the row (status included): gaps between them
    function needed(file, view, zoom) {
        const n = (file ? 1 : 0) + 5
        return margins + (file ? fileW : 0) + toolsW + togglesW + view + zoom + (n - 1) * Theme.clusterGap
    }
    // the widths each level needs (self-test observation)
    readonly property var neededWidths: [needed(true, viewW, zoomW), needed(false, viewW, zoomW),
                                         needed(false, viewCompactW, zoomW), needed(false, viewCompactW, zoomCompactW)]
    readonly property int level: needed(true, viewW, zoomW) <= width ? 0
                               : needed(false, viewW, zoomW) <= width ? 1
                               : needed(false, viewCompactW, zoomW) <= width ? 2 : 3

    readonly property var modes: [
        { text: qsTr("Scan"), value: "scan", tip: qsTr("Whole scan (1)") },
        { text: qsTr("Print"), value: "print", tip: qsTr("Full print with its real frame (2)") },
        { text: qsTr("Image"), value: "image", tip: qsTr("Image area (3)") },
        { text: qsTr("Composite"), value: "composite", tip: qsTr("Graded image inside the real frame (4)") }
    ]

    RowLayout {
        anchors.fill: parent
        anchors.leftMargin: Theme.padding - 6
        anchors.rightMargin: Theme.padding - 6
        spacing: Theme.clusterGap

        Row {
            id: fileCluster
            objectName: "topBarFile"
            visible: bar.level === 0
            spacing: Theme.gap
            ToolBtn { iconName: "import"; tip: qsTr("Import Scans…"); shortcutText: cmd.mod + "I"; onClicked: cmd.importScans() }
            ToolBtn {
                iconName: "export"; tip: qsTr("Export Image Areas…"); shortcutText: cmd.mod + "E"
                enabled: App.counts.prints > 0
                onClicked: cmd.exportImageAreas()
            }
            ToolBtn {
                iconName: "relink"; tip: qsTr("Relink Graded Files…"); shortcutText: cmd.mod + "⌥I"
                enabled: App.counts.prints > 0
                onClicked: cmd.relink()
            }
            Item { width: Theme.clusterGap - 2 * Theme.gap; height: 1 }
            Rectangle { width: 1; height: 20; anchors.verticalCenter: parent.verticalCenter; color: Theme.lineSection }
        }
        Row {
            id: toolCluster
            objectName: "topBarTools"
            spacing: Theme.gap
            ToolBtn { iconName: "select"; tip: qsTr("Select"); shortcutText: "V"; active: canvas.tool === "select"; onClicked: canvas.tool = "select" }
            ToolBtn { iconName: "hand"; tip: qsTr("Hand"); shortcutText: "H"; active: canvas.tool === "hand" || canvas.spaceHeld; onClicked: canvas.tool = "hand" }
            ToolBtn { objectName: "toolOuter"; iconName: "outer"; tip: qsTr("Edit Outer"); shortcutText: "O"; active: canvas.tool === "outer"; onClicked: canvas.tool = "outer" }
            ToolBtn { objectName: "toolInner"; iconName: "inner"; tip: qsTr("Edit Inner"); shortcutText: "I"; active: canvas.tool === "inner"; onClicked: canvas.tool = "inner" }
        }
        // status: scan facts or job progress
        ColumnLayout {
            objectName: "topBarStatus"
            Layout.fillWidth: true
            Layout.minimumWidth: 0
            Layout.preferredWidth: 0
            spacing: 2
            Text {
                Layout.fillWidth: true
                // too narrow to say anything useful: the rails and chips already show the scan facts
                opacity: parent.width >= 90 ? 1 : 0
                elide: Text.ElideMiddle
                font.pixelSize: Theme.fontCaption
                color: Theme.textSecondary
                text: App.jobs.summary.length ? App.jobs.summary
                      : App.currentScan.name ? App.currentScan.name + "  ·  " + App.currentScan.width + " × " + App.currentScan.height
                                               + "  ·  " + qsTr("%1-bit").arg(App.currentScan.bits) : ""
            }
            Rectangle {
                visible: App.jobs.summary.length > 0
                Layout.fillWidth: true
                height: 3
                radius: 1.5
                color: Theme.surfaceWell
                Rectangle {
                    width: parent.width * Math.max(0.02, App.jobs.overallProgress)
                    height: parent.height
                    radius: 1.5
                    color: Theme.accent
                }
            }
        }
        Row {
            id: toggleCluster
            objectName: "topBarToggles"
            spacing: Theme.gap
            ToolBtn {
                iconName: "compare"; tip: qsTr("Compare split"); shortcutText: "Y"
                enabled: canvas.mode === "composite" && App.currentItem.relinked === true
                active: canvas.compare
                onClicked: canvas.compare = !canvas.compare
            }
            ToolBtn {
                objectName: "overlaysButton"
                iconName: "eye"; tip: qsTr("Show/Hide Overlays"); shortcutText: cmd.mod + "⇧H"
                active: canvas.showOverlays
                onClicked: canvas.showOverlays = !canvas.showOverlays
            }
        }
        // UX-06: between the eye and the zoom field; wide = segmented, narrow = a menu button with the same views
        Segmented {
            id: viewSwitch
            objectName: "viewSwitch"
            visible: bar.level < 2
            value: canvas.mode
            model: bar.modes
            onActivated: (v) => canvas.mode = v
        }
        FkCombo {
            id: viewMenu
            objectName: "viewSwitchCompact"
            visible: bar.level >= 2
            implicitWidth: Math.max(96, implicitContentWidth + 36)
            Accessible.name: qsTr("View")
            ToolTip.visible: hovered
            ToolTip.text: qsTr("View: Scan (1) · Print (2) · Image (3) · Composite (4)")
            ToolTip.delay: 600
            model: bar.modes
            value: canvas.mode
            onPicked: (v) => canvas.mode = v
        }
        Row {
            id: zoomCluster
            objectName: "topBarZoom"
            spacing: Theme.gap
            AbstractButton {
                id: zoomField
                objectName: "zoomField"
                implicitWidth: 56
                implicitHeight: Theme.controlH
                anchors.verticalCenter: parent.verticalCenter
                Accessible.name: qsTr("Zoom")
                ToolTip.visible: hovered
                ToolTip.text: qsTr("Zoom")
                ToolTip.delay: 600
                hoverEnabled: true
                onClicked: zoomMenu.open()
                background: Rectangle { radius: Theme.radius; color: Theme.controlFill }
                contentItem: Text {
                    text: Math.round(canvas.zoomPercent * 10) / 10 + " %"
                    horizontalAlignment: Text.AlignHCenter
                    verticalAlignment: Text.AlignVCenter
                    font.pixelSize: Theme.fontBody
                    font.features: { "tnum": 1 }
                    color: Theme.textPrimary
                }
                Menu {
                    id: zoomMenu
                    y: zoomField.height
                    MenuItem { text: qsTr("Fit"); onTriggered: canvas.fit() }
                    Repeater {
                        model: [25, 50, 100, 200, 400, 800]
                        MenuItem { required property int modelData; text: modelData + " %"; onTriggered: canvas.zoomTo(modelData) }
                    }
                }
            }
            ToolBtn { visible: bar.level < 3; iconName: "zoomOut"; tip: qsTr("Zoom Out"); shortcutText: cmd.mod + "−"; onClicked: canvas.zoomStep(-1) }
            ToolBtn { visible: bar.level < 3; iconName: "zoomIn"; tip: qsTr("Zoom In"); shortcutText: cmd.mod + "="; onClicked: canvas.zoomStep(1) }
        }
    }
    Rectangle { anchors.bottom: parent.bottom; width: parent.width; height: 1; color: Theme.lineRegion }
}
