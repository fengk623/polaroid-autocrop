// Canvas region: ScanCanvas + resolution badges (top right), chips (bottom left), navigator (bottom right),
// empty and error states. Badge texts are fixed by CONTRACT-ui-core §4.
import QtQuick
import QtQuick.Layouts
import QtQuick.Controls.Basic
import FrameKeeper
import FrameKeeper.Core

Item {
    id: area
    required property var cmd
    property alias canvas: scanCanvas

    ScanCanvas {
        id: scanCanvas
        objectName: "scanCanvas"
        anchors.fill: parent
        app: App
        surroundColor: Theme.surfaceCanvas
        focus: true
        Keys.onPressed: (e) => {
            if (scanCanvas.handleActive && (e.key === Qt.Key_Left || e.key === Qt.Key_Right || e.key === Qt.Key_Up || e.key === Qt.Key_Down)) {
                const s = (e.modifiers & Qt.ShiftModifier) ? 10 : 1
                scanCanvas.nudge(e.key === Qt.Key_Left ? -s : e.key === Qt.Key_Right ? s : 0,
                                 e.key === Qt.Key_Up ? -s : e.key === Qt.Key_Down ? s : 0)
                e.accepted = true
            } else if (e.key === Qt.Key_Space && !e.isAutoRepeat) {
                scanCanvas.spaceHeld = true
                e.accepted = true
            }
        }
        Keys.onReleased: (e) => { if (e.key === Qt.Key_Space && !e.isAutoRepeat) { scanCanvas.spaceHeld = false; e.accepted = true } }
    }

    // badges
    Column {
        anchors.top: parent.top
        anchors.right: parent.right
        anchors.margins: 10
        spacing: 4
        Badge {
            // scanCanvas.badge is the contract state (Preview / Full… / Full); the label is translated
            visible: App.currentScanId.length > 0 && scanCanvas.status.length === 0
            text: scanCanvas.badge === "Full" ? qsTr("Full resolution")
                : scanCanvas.badge === "Full…" ? qsTr("Loading full resolution…") : qsTr("Preview")
            anchors.right: parent.right
        }
        Badge { visible: scanCanvas.mode === "composite" && scanCanvas.showOriginal; text: qsTr("Original"); anchors.right: parent.right }
        Badge {
            visible: scanCanvas.mode === "composite" && App.currentItem.relinked !== true && App.currentItemId.length > 0
            text: qsTr("No graded file relinked")
            anchors.right: parent.right
        }
    }
    // UX-03: which boundary the handles edit, always stated (colour of its outline + words), with the Shift hint
    Rectangle {
        objectName: "editTargetBadge"
        visible: App.currentItemId.length > 0 && scanCanvas.editTarget.length > 0 && scanCanvas.status.length === 0
        anchors.top: parent.top
        anchors.horizontalCenter: parent.horizontalCenter
        anchors.topMargin: 10
        implicitHeight: Theme.badgeH
        implicitWidth: editRow.implicitWidth + 16
        radius: height / 2
        color: Theme.badgeFill
        property string text: editText.text
        Row {
            id: editRow
            anchors.centerIn: parent
            spacing: 6
            Rectangle {
                anchors.verticalCenter: parent.verticalCenter
                width: 10; height: 10; radius: 2
                color: "transparent"
                border.width: 2
                border.color: scanCanvas.editTarget === "inner" ? "#F2C744" : "#3D8BFF"
            }
            Text {
                id: editText
                text: (scanCanvas.editTarget === "inner" ? qsTr("Editing Inner (image area)") : qsTr("Editing Outer (paper edge)"))
                      + (App.settings.shiftSwitchesBoundary === true ? "  ·  " + qsTr("tap Shift to switch") : "")
                font.pixelSize: Theme.fontBadge
                font.weight: Font.DemiBold
                color: Theme.textPrimary
            }
        }
    }
    // chips: ICC · depth · DPI
    Row {
        visible: App.currentScanId.length > 0
        anchors.left: parent.left
        anchors.bottom: parent.bottom
        anchors.margins: 10
        spacing: 6
        Badge { chip: true; text: (App.currentScan.icc || "").length > 24 ? App.currentScan.icc.substring(0, 23) + "…" : (App.currentScan.icc || "")
                ToolTip.visible: chipHover.hovered; ToolTip.text: App.currentScan.icc || ""; HoverHandler { id: chipHover } }
        Badge { chip: true; text: qsTr("%1-bit").arg(App.currentScan.bits || 0) }
        Badge { chip: true; text: App.currentScan.dpi > 0 ? qsTr("%1 dpi").arg(Math.round(App.currentScan.dpi)) : qsTr("DPI unknown") }
    }
    // navigator (zoomed past fit)
    Rectangle {
        id: nav
        visible: !scanCanvas.atFit && App.currentScanId.length > 0
        width: 200; height: 150
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        anchors.margins: 12
        color: Theme.surfaceWell
        border.color: Theme.lineSection
        Image {
            id: navImg
            anchors.fill: parent
            anchors.margins: 4
            // the rotation in the URL reloads the navigator when the whole scan is turned (UX-04)
            source: App.currentScanId.length ? "image://fk/scan/" + App.currentScanId + "?" + (App.currentScan.rotation || 0) + "-" + Date.now() : ""
            fillMode: Image.PreserveAspectFit
            cache: false
            visible: scanCanvas.mode === "scan"
        }
        Rectangle {
            visible: navImg.visible
            readonly property rect v: scanCanvas.navigatorView
            x: navImg.x + (navImg.width - navImg.paintedWidth) / 2 + v.x * navImg.paintedWidth
            y: navImg.y + (navImg.height - navImg.paintedHeight) / 2 + v.y * navImg.paintedHeight
            width: v.width * navImg.paintedWidth
            height: v.height * navImg.paintedHeight
            color: "transparent"
            border.color: Theme.textPrimary
            border.width: 1
        }
        MouseArea {
            anchors.fill: parent
            function go(m) {
                const ox = navImg.x + (navImg.width - navImg.paintedWidth) / 2, oy = navImg.y + (navImg.height - navImg.paintedHeight) / 2
                scanCanvas.centerOnRelative((m.x - ox) / navImg.paintedWidth, (m.y - oy) / navImg.paintedHeight)
            }
            onPressed: (m) => go(m)
            onPositionChanged: (m) => { if (pressed) go(m) }
        }
        FkButton { text: qsTr("Fit"); anchors.top: parent.bottom; anchors.right: parent.right; anchors.topMargin: -32; anchors.rightMargin: 4; implicitHeight: 24; onClicked: scanCanvas.fit() }
    }
    // empty state
    ColumnLayout {
        visible: App.scans.count === 0
        anchors.centerIn: parent
        spacing: 12
        Icon { Layout.alignment: Qt.AlignHCenter; name: "frame"; size: 48; color: Theme.textPrimary; opacity: 0.8 }
        Text {
            Layout.alignment: Qt.AlignHCenter
            text: qsTr("Drop scans of instant prints here")
            font.pixelSize: Theme.fontDialogTitle
            color: Theme.textPrimary
        }
        Text {
            Layout.alignment: Qt.AlignHCenter
            Layout.maximumWidth: 420
            horizontalAlignment: Text.AlignHCenter
            wrapMode: Text.WordWrap
            text: qsTr("TIFF, 8 or 16 bit, any resolution. Each print is found with its real frame; the scan file is never changed. Tip: scan dark frames on a light backing.")
            font.pixelSize: Theme.fontBody
            color: Theme.textPrimary
            opacity: 0.85
        }
        FkButton { Layout.alignment: Qt.AlignHCenter; primary: true; text: qsTr("Import Scans…"); iconName: "import"; onClicked: cmd.importScans() }
    }
    // loading / error card
    Rectangle {
        visible: App.scans.count > 0 && scanCanvas.status.length > 0
        anchors.centerIn: parent
        width: Math.min(parent.width - 40, statusText.implicitWidth + 40)
        height: col.implicitHeight + 24
        radius: 6
        color: Theme.badgeFill
        ColumnLayout {
            id: col
            anchors.centerIn: parent
            spacing: 8
            Text {
                id: statusText
                text: scanCanvas.status
                font.pixelSize: Theme.fontBody
                color: Theme.textPrimary
                Layout.alignment: Qt.AlignHCenter
            }
            RowLayout {
                visible: App.currentScan.state === "missing" || App.currentScan.state === "error" || App.currentScan.state === "changed"
                Layout.alignment: Qt.AlignHCenter
                FkButton { text: qsTr("Relocate…"); onClicked: cmd.relocate() }
                FkButton { text: qsTr("Remove from project"); onClicked: App.removeScan(App.currentScanId) }
            }
        }
    }
}
