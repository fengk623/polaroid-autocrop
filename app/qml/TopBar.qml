// Top bar (40, centre column): Import · Export · Relink │ tools │ canvas mode │ status (flex) │
// Compare · Overlays │ zoom (ui-reference §4.1). Panels fold with the edge strips (UX-10).
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

    RowLayout {
        anchors.fill: parent
        anchors.leftMargin: Theme.padding - 6
        anchors.rightMargin: Theme.padding - 6
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
        Item { width: Theme.clusterGap - Theme.gap }
        Rectangle { width: 1; height: 20; color: Theme.lineSection }
        Item { width: Theme.clusterGap - Theme.gap }
        ToolBtn { iconName: "select"; tip: qsTr("Select"); shortcutText: "V"; active: canvas.tool === "select"; onClicked: canvas.tool = "select" }
        ToolBtn { iconName: "hand"; tip: qsTr("Hand"); shortcutText: "H"; active: canvas.tool === "hand" || canvas.spaceHeld; onClicked: canvas.tool = "hand" }
        ToolBtn { objectName: "toolOuter"; iconName: "outer"; tip: qsTr("Edit Outer"); shortcutText: "O"; active: canvas.tool === "outer"; onClicked: canvas.tool = "outer" }
        ToolBtn { objectName: "toolInner"; iconName: "inner"; tip: qsTr("Edit Inner"); shortcutText: "I"; active: canvas.tool === "inner"; onClicked: canvas.tool = "inner" }
        Item { width: Theme.clusterGap - Theme.gap }
        Segmented {
            value: canvas.mode
            model: [
                { text: qsTr("Scan"), value: "scan", tip: qsTr("Whole scan (1)") },
                { text: qsTr("Print"), value: "print", tip: qsTr("Full print with its real frame (2)") },
                { text: qsTr("Image"), value: "image", tip: qsTr("Image area (3)") },
                { text: qsTr("Composite"), value: "composite", tip: qsTr("Graded image inside the real frame (4)") }
            ]
            onActivated: (v) => canvas.mode = v
        }
        Item { width: Theme.clusterGap - Theme.gap }
        // status: scan facts or job progress
        ColumnLayout {
            Layout.fillWidth: true
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
        Item { width: Theme.clusterGap - Theme.gap }
        ToolBtn {
            iconName: "compare"; tip: qsTr("Compare split"); shortcutText: "Y"
            enabled: canvas.mode === "composite" && App.currentItem.relinked === true
            active: canvas.compare
            onClicked: canvas.compare = !canvas.compare
        }
        ToolBtn { iconName: "eye"; tip: qsTr("Show/Hide Overlays"); shortcutText: cmd.mod + "⇧H"; active: canvas.showOverlays; onClicked: canvas.showOverlays = !canvas.showOverlays }
        Item { width: Theme.clusterGap - Theme.gap }
        AbstractButton {
            id: zoomField
            implicitWidth: 56
            implicitHeight: Theme.controlH
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
        ToolBtn { iconName: "zoomOut"; tip: qsTr("Zoom Out"); shortcutText: cmd.mod + "−"; onClicked: canvas.zoomStep(-1) }
        ToolBtn { iconName: "zoomIn"; tip: qsTr("Zoom In"); shortcutText: cmd.mod + "="; onClicked: canvas.zoomStep(1) }
    }
    Rectangle { anchors.bottom: parent.bottom; width: parent.width; height: 1; color: Theme.lineRegion }
}
