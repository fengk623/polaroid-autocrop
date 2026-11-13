// UX-05 (U019): left rail of the Library workspace — the workspace switch, the known libraries (the open one
// marked) and what to do with libraries. The library's folders and import / export join it in U020.
import QtQuick
import QtQuick.Layouts
import QtQuick.Controls.Basic
import FrameKeeper.Core

Rectangle {
    id: rail
    required property var cmd
    color: Theme.surfacePanel

    ColumnLayout {
        anchors.fill: parent
        spacing: 0
        Item {
            Layout.fillWidth: true
            implicitHeight: Theme.railHeader
            WorkspaceSwitch {
                anchors.left: parent.left
                anchors.leftMargin: 12
                anchors.verticalCenter: parent.verticalCenter
            }
        }
        Flickable {
            Layout.fillWidth: true
            Layout.fillHeight: true
            contentHeight: col.implicitHeight
            clip: true
            boundsBehavior: Flickable.StopAtBounds
            ScrollBar.vertical: ScrollBar { policy: ScrollBar.AsNeeded }
            ColumnLayout {
                id: col
                width: rail.width
                spacing: 0
                Section {
                    title: qsTr("Libraries")
                    note: App.libraryCards.length > 0 ? qsTr("%n known", "", App.libraryCards.length) : ""
                    Repeater {
                        model: App.libraryCards
                        delegate: Rectangle {
                            required property var modelData
                            Layout.fillWidth: true
                            implicitHeight: 28
                            radius: Theme.radius
                            color: modelData.current ? Theme.selectionFill : (ma.containsMouse ? Theme.hoverOverlay : "transparent")
                            Text {
                                anchors.left: parent.left
                                anchors.leftMargin: 8
                                anchors.right: parent.right
                                anchors.rightMargin: 8
                                anchors.verticalCenter: parent.verticalCenter
                                text: modelData.name + (modelData.exists ? "" : "  " + qsTr("(missing)"))
                                elide: Text.ElideRight
                                font.pixelSize: Theme.fontBody
                                font.weight: modelData.current ? Font.DemiBold : Font.Normal
                                color: modelData.exists ? Theme.textPrimary : Theme.textTertiary
                            }
                            MouseArea {
                                id: ma
                                anchors.fill: parent
                                hoverEnabled: true
                                onDoubleClicked: if (modelData.exists) rail.cmd.enterLibrary(modelData.folder)
                            }
                        }
                    }
                    Text {
                        visible: App.libraryCards.length === 0
                        Layout.fillWidth: true
                        wrapMode: Text.WordWrap
                        text: qsTr("No libraries yet.")
                        font.pixelSize: Theme.fontCaption
                        color: Theme.textTertiary
                    }
                }
                Section {
                    title: qsTr("Manage")
                    FkButton { Layout.fillWidth: true; text: qsTr("New Library…"); onClicked: rail.cmd.newLibrary() }
                    FkButton { Layout.fillWidth: true; text: qsTr("Open Library…"); onClicked: rail.cmd.openLibrary() }
                    FkButton { Layout.fillWidth: true; text: qsTr("Import Project…"); onClicked: rail.cmd.importProjectAsLibrary() }
                }
            }
        }
    }
}
