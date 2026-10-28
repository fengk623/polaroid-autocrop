// Filmstrip (148): one cell per print, status badges derived from the project (CONTRACT §3, ui-ref §4.9).
import QtQuick
import QtQuick.Layouts
import QtQuick.Controls.Basic
import FrameKeeper.Core

Rectangle {
    id: strip
    required property var cmd
    required property var canvas
    color: Theme.surfacePanel
    implicitHeight: Theme.filmstrip

    Rectangle { anchors.top: parent.top; width: parent.width; height: 1; color: Theme.lineRegion }

    Text {
        visible: App.items.count === 0
        anchors.centerIn: parent
        text: qsTr("Detected prints appear here.")
        font.pixelSize: Theme.fontBody
        color: Theme.textTertiary
    }

    ListView {
        id: list
        anchors.fill: parent
        anchors.topMargin: 8
        anchors.bottomMargin: 8
        anchors.leftMargin: 8
        anchors.rightMargin: 8
        orientation: ListView.Horizontal
        spacing: 8
        clip: true
        model: App.items
        boundsBehavior: Flickable.StopAtBounds
        ScrollBar.horizontal: ScrollBar { height: 6 }
        currentIndex: App.items.indexOf(App.currentItemId)
        onCurrentIndexChanged: if (currentIndex >= 0) positionViewAtIndex(currentIndex, ListView.Contain)

        delegate: AbstractButton {
            id: cell
            required property string itemId
            required property string label
            required property string scanName
            required property string thumb
            required property string confidence
            required property string detection
            required property bool reviewed
            required property bool excluded
            required property bool exported
            required property bool relinked
            required property bool composited
            required property bool finalExported
            required property bool current
            required property bool selected
            width: Math.min(162, Math.max(72, img.implicitWidth > 0 ? img.implicitWidth * 108 / Math.max(1, img.implicitHeight) : 88))
            height: list.height
            hoverEnabled: true
            Accessible.name: label + " " + scanName
            ToolTip.visible: hovered
            ToolTip.delay: 600
            ToolTip.text: label + " · " + scanName + "\n" + cmd.statusText(cell)
            onClicked: App.selectItem(itemId, false, false)
            onDoubleClicked: canvas.mode = "print"
            TapHandler {
                acceptedModifiers: Qt.ShiftModifier
                onTapped: App.selectItem(cell.itemId, true, false)
            }
            TapHandler {
                acceptedModifiers: Qt.ControlModifier | Qt.MetaModifier
                onTapped: App.selectItem(cell.itemId, false, true)
            }
            TapHandler {
                acceptedButtons: Qt.RightButton
                onTapped: { App.selectItem(cell.itemId, false, false); ctx.popup() }
            }
            Menu {
                id: ctx
                MenuItem { text: qsTr("Mark reviewed"); onTriggered: App.setReviewed(cell.itemId, true) }
                MenuItem { text: cell.excluded ? qsTr("Include") : qsTr("Exclude"); onTriggered: App.setExcluded(cell.itemId, !cell.excluded) }
                MenuItem { text: qsTr("Reset to detected"); onTriggered: App.resetToDetected(cell.itemId) }
                MenuItem { text: qsTr("Export image area…"); onTriggered: cmd.exportImageAreas([cell.itemId]) }
            }
            contentItem: ColumnLayout {
                spacing: 4
                opacity: cell.excluded ? 0.4 : 1
                Rectangle {
                    Layout.fillWidth: true
                    Layout.preferredHeight: 108
                    color: Theme.surfaceWell
                    Image {
                        id: img
                        anchors.fill: parent
                        source: cell.thumb
                        fillMode: Image.PreserveAspectFit
                        cache: false
                        asynchronous: false
                    }
                    // status icons over a scrim (max 4 + "+n")
                    Rectangle {
                        anchors.left: parent.left
                        anchors.right: parent.right
                        anchors.bottom: parent.bottom
                        height: 20
                        gradient: Gradient {
                            GradientStop { position: 0; color: "transparent" }
                            GradientStop { position: 1; color: Qt.rgba(0, 0, 0, 0.55) }
                        }
                        Row {
                            anchors.left: parent.left
                            anchors.leftMargin: 4
                            anchors.bottom: parent.bottom
                            anchors.bottomMargin: 3
                            spacing: 3
                            Icon { visible: cell.confidence === "low" && !cell.reviewed; name: "warning"; size: 14; color: Theme.overlayLow }
                            Icon { visible: cell.confidence === "medium" && !cell.reviewed; name: "warning"; size: 14; color: "white" }
                            Icon { visible: cell.reviewed; name: "check"; size: 14; color: "white" }
                            Icon { visible: cell.detection === "manual"; name: "pencil"; size: 14; color: "white" }
                            Icon { visible: cell.detection === "template"; name: "grid"; size: 14; color: "white" }
                            Icon { visible: cell.exported && !cell.relinked; name: "export"; size: 14; color: "white" }
                            Icon { visible: cell.relinked && !cell.finalExported; name: "link"; size: 14; color: "white" }
                            Icon { visible: cell.finalExported; name: "checks"; size: 14; color: "white" }
                            Icon { visible: cell.excluded; name: "excluded"; size: 14; color: "white" }
                        }
                    }
                    Rectangle {
                        anchors.fill: parent
                        color: "transparent"
                        border.width: cell.current ? 2 : (cell.selected ? 1 : 0)
                        border.color: cell.current ? Theme.accent : Qt.rgba(Theme.textPrimary.r, Theme.textPrimary.g, Theme.textPrimary.b, 0.6)
                    }
                    Icon { visible: cell.selected && !cell.current; name: "check"; size: 14; anchors.right: parent.right; anchors.top: parent.top; anchors.margins: 3 }
                }
                Text {
                    Layout.fillWidth: true
                    Layout.preferredHeight: 16
                    text: cell.label + " · " + cell.scanName
                    elide: Text.ElideMiddle
                    font.pixelSize: Theme.fontCaption
                    color: cell.confidence === "low" && !cell.reviewed ? Theme.error : (cell.current ? Theme.textPrimary : Theme.textSecondary)
                }
            }
            background: Rectangle { color: cell.hovered ? Theme.hoverOverlay : "transparent"; radius: 2 }
        }
    }
}
