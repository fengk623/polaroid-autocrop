// UX-05 (U020): one cell of the library grid — the picture (asked for when the cell is created, i.e. on screen),
// the print's label or the scan's name, stars, colour label, pick flag. Click selects (Shift extends, Ctrl/⌘
// toggles), double-click opens, the ⋯ menu edits, opens or moves it in the manual order.
import QtQuick
import QtQuick.Layouts
import QtQuick.Controls.Basic
import FrameKeeper.Core

Item {
    id: cell
    objectName: "libraryCell"
    required property var cmd
    required property string entryId
    required property string scanId
    required property string kind
    required property string title
    required property string subtitle
    required property string thumb
    required property int rating
    required property string pick
    required property string colorLabel
    required property bool current
    required property bool selected
    required property bool missing
    required property bool excluded
    signal activate(string id, string kind)
    signal selectRequested(string id, int mods)
    Component.onCompleted: App.libraryRequestThumb(scanId)

    readonly property var colors: ({ "red": "#E5484D", "yellow": "#F2C744", "green": "#38C172", "blue": "#3D8BFF", "purple": "#A36EF2" })
    Rectangle {
        anchors.fill: parent
        anchors.margins: 5
        radius: 6
        color: cell.selected ? Theme.selectionFill : (ma.containsMouse ? Theme.hoverOverlay : Theme.surfacePanel)
        border.width: cell.current ? 2 : 0
        border.color: Theme.accent
        ColumnLayout {
            anchors.fill: parent
            anchors.margins: 6
            spacing: 3
            Item {
                Layout.fillWidth: true
                Layout.fillHeight: true
                Image {
                    anchors.fill: parent
                    source: cell.thumb
                    fillMode: Image.PreserveAspectFit
                    asynchronous: true
                    cache: false
                    opacity: cell.excluded || cell.pick === "rejected" ? 0.45 : 1
                }
                Text {
                    anchors.centerIn: parent
                    visible: cell.missing
                    text: qsTr("Missing")
                    font.pixelSize: Theme.fontCaption
                    color: Theme.warning
                }
                Rectangle { // colour label
                    visible: cell.colorLabel !== "none" && cell.colorLabel.length > 0
                    anchors.right: parent.right
                    anchors.top: parent.top
                    width: 12; height: 12; radius: 6
                    color: cell.colors[cell.colorLabel] || Theme.textTertiary
                    border.color: Theme.surfacePanel
                }
                Text { // pick flag
                    visible: cell.pick === "picked" || cell.pick === "rejected"
                    anchors.left: parent.left
                    anchors.top: parent.top
                    text: cell.pick === "picked" ? "⚑" : "✕"
                    font.pixelSize: 14
                    color: cell.pick === "picked" ? Theme.textPrimary : Theme.error
                }
            }
            Text {
                Layout.fillWidth: true
                text: cell.title
                elide: Text.ElideRight
                font.pixelSize: Theme.fontBody
                font.weight: Font.DemiBold
                color: Theme.textPrimary
            }
            RowLayout {
                Layout.fillWidth: true
                spacing: 4
                Text {
                    Layout.fillWidth: true
                    text: cell.subtitle
                    elide: Text.ElideMiddle
                    font.pixelSize: Theme.fontCaption
                    color: Theme.textTertiary
                }
                Text {
                    text: cell.rating > 0 ? "★".repeat(cell.rating) : ""
                    font.pixelSize: Theme.fontCaption
                    color: Theme.warning
                }
            }
        }
        MouseArea {
            id: ma
            anchors.fill: parent
            hoverEnabled: true
            acceptedButtons: Qt.LeftButton | Qt.RightButton
            onClicked: (m) => {
                cell.selectRequested(cell.entryId, m.modifiers)
                if (m.button === Qt.RightButton) menu.popup()
            }
            onDoubleClicked: (m) => { if (m.button === Qt.LeftButton) cell.activate(cell.entryId, cell.kind) }
        }
        ToolBtn {
            anchors.right: parent.right
            anchors.bottom: parent.bottom
            anchors.margins: 2
            visible: ma.containsMouse || cell.current
            iconName: "more"
            tip: qsTr("More")
            onClicked: { cell.selectRequested(cell.entryId, 0); menu.popup() }
        }
        Menu {
            id: menu
            MenuItem { text: cell.kind === "scan" ? qsTr("Show Its Prints") : qsTr("Edit in Edit Photos"); onTriggered: cell.activate(cell.entryId, cell.kind) }
            MenuSeparator {}
            MenuItem { text: qsTr("Move to Start"); enabled: !App.readOnly; onTriggered: App.libraryMove(cell.entryId, "first") }
            MenuItem { text: qsTr("Move Earlier"); enabled: !App.readOnly; onTriggered: App.libraryMove(cell.entryId, "earlier") }
            MenuItem { text: qsTr("Move Later"); enabled: !App.readOnly; onTriggered: App.libraryMove(cell.entryId, "later") }
            MenuItem { text: qsTr("Move to End"); enabled: !App.readOnly; onTriggered: App.libraryMove(cell.entryId, "last") }
        }
    }
}
