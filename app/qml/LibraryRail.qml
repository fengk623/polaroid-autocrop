// UX-05 (U019): left rail of the Library workspace — the workspace switch, the known libraries (the open one
// marked) and what to do with libraries; inside a library its folders, and import / export at the bottom (U020).
import QtQuick
import QtQuick.Layouts
import QtQuick.Controls.Basic
import FrameKeeper.Core

Rectangle {
    id: rail
    required property var cmd
    property bool home: true
    signal homeRequested(bool home)
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
                    Rectangle {
                        objectName: "allLibraries"
                        Layout.fillWidth: true
                        implicitHeight: 28
                        radius: Theme.radius
                        color: rail.home ? Theme.selectionFill : (hma.containsMouse ? Theme.hoverOverlay : "transparent")
                        Text {
                            anchors.left: parent.left
                            anchors.leftMargin: 8
                            anchors.verticalCenter: parent.verticalCenter
                            text: qsTr("All Libraries")
                            font.pixelSize: Theme.fontBody
                            font.weight: Font.DemiBold
                            color: Theme.textPrimary
                        }
                        MouseArea { id: hma; anchors.fill: parent; hoverEnabled: true; onClicked: rail.homeRequested(true) }
                    }
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
                                onClicked: if (modelData.current) rail.homeRequested(false)
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
                // U020: the open library's folders (references; clicking one shows only its scans)
                Section {
                    visible: App.isLibrary
                    title: qsTr("Folders")
                    note: App.isLibrary ? qsTr("%1 scans · %2 prints").arg(App.counts.scans).arg(App.counts.prints) : ""
                    Repeater {
                        model: App.isLibrary ? [{ path: "", name: qsTr("All folders"), scans: App.counts.scans, exists: true }].concat(App.libraryFolders()) : []
                        delegate: Rectangle {
                            required property var modelData
                            Layout.fillWidth: true
                            implicitHeight: 26
                            radius: Theme.radius
                            color: App.libraryView.folder === modelData.path ? Theme.selectionFill : (fma.containsMouse ? Theme.hoverOverlay : "transparent")
                            RowLayout {
                                anchors.fill: parent
                                anchors.leftMargin: 8
                                anchors.rightMargin: 8
                                Text {
                                    Layout.fillWidth: true
                                    text: modelData.name + (modelData.exists ? "" : "  " + qsTr("(missing)"))
                                    elide: Text.ElideMiddle
                                    font.pixelSize: Theme.fontBody
                                    color: modelData.exists ? Theme.textPrimary : Theme.textTertiary
                                }
                                Text { text: String(modelData.scans); font.pixelSize: Theme.fontCaption; color: Theme.textTertiary }
                            }
                            MouseArea {
                                id: fma
                                anchors.fill: parent
                                hoverEnabled: true
                                onClicked: { App.setLibraryView("folder", modelData.path); rail.homeRequested(false) }
                            }
                        }
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
        // U020: import and export at the bottom (the requirement's place)
        Rectangle { Layout.fillWidth: true; implicitHeight: 1; color: Theme.lineSection; visible: App.isLibrary }
        RowLayout {
            visible: App.isLibrary
            Layout.fillWidth: true
            Layout.margins: 10
            spacing: 6
            FkButton {
                objectName: "libraryImport"
                Layout.fillWidth: true
                text: qsTr("Import…"); iconName: "import"
                enabled: !App.readOnly
                onClicked: rail.cmd.importScans()
            }
            FkButton {
                objectName: "libraryExport"
                Layout.fillWidth: true
                text: qsTr("Export…"); iconName: "export"
                enabled: App.counts.prints > 0
                onClicked: rail.cmd.exportImageAreas()
            }
        }
    }
}
