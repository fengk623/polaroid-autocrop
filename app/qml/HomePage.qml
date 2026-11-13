// UX-05 (U019): the library home page — one card per known library (cover, name, scan and print counts, last
// opened and modified); New, Open, Import project; double-click enters a library; Rename, Show in the file manager
// and Remove from list (never deletes) on the card's menu.
import QtQuick
import QtQuick.Layouts
import QtQuick.Controls.Basic
import FrameKeeper.Core

Rectangle {
    id: home
    objectName: "homePage"
    required property var cmd
    color: Theme.surfaceWell
    property string selected: ""
    onVisibleChanged: if (visible) App.refreshHome()

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 24
        spacing: 16
        RowLayout {
            Layout.fillWidth: true
            spacing: 8
            Text {
                Layout.fillWidth: true
                text: qsTr("Libraries")
                font.pixelSize: Theme.fontDialogTitle + 4
                font.weight: Font.DemiBold
                color: Theme.textPrimary
                elide: Text.ElideRight
            }
            FkButton { objectName: "homeNew"; primary: true; text: qsTr("New Library…"); onClicked: home.cmd.newLibrary() }
            FkButton { objectName: "homeOpen"; text: qsTr("Open Library…"); onClicked: home.cmd.openLibrary() }
            FkButton { objectName: "homeImport"; text: qsTr("Import Project…"); onClicked: home.cmd.importProjectAsLibrary() }
        }
        Text {
            Layout.fillWidth: true
            wrapMode: Text.WordWrap
            text: qsTr("A library keeps your scans, prints, ratings and edits together. The scans stay where they are on disk. Double-click a library to open it.")
            font.pixelSize: Theme.fontBody
            color: Theme.textSecondary
        }
        GridView {
            id: grid
            objectName: "libraryCards"
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            cellWidth: 236
            cellHeight: 248
            model: App.libraryCards
            boundsBehavior: Flickable.StopAtBounds
            ScrollBar.vertical: ScrollBar { policy: ScrollBar.AsNeeded }
            delegate: Item {
                id: cell
                required property var modelData
                required property int index
                objectName: "libraryCard"
                width: grid.cellWidth
                height: grid.cellHeight
                Rectangle {
                    id: card
                    anchors.fill: parent
                    anchors.margins: 8
                    radius: 6
                    color: Theme.surfacePanel
                    border.width: home.selected === cell.modelData.folder || cell.modelData.current ? 2 : 1
                    border.color: home.selected === cell.modelData.folder ? Theme.accent
                                : cell.modelData.current ? Theme.selectionFill : Theme.lineSection
                    ColumnLayout {
                        anchors.fill: parent
                        anchors.margins: 8
                        spacing: 4
                        Rectangle {
                            Layout.fillWidth: true
                            Layout.preferredHeight: 128
                            radius: 4
                            color: Theme.surfaceCanvas
                            clip: true
                            Image {
                                anchors.fill: parent
                                visible: cell.modelData.cover.length > 0
                                source: cell.modelData.cover
                                fillMode: Image.PreserveAspectCrop
                                asynchronous: true
                                cache: false
                            }
                            Text {
                                anchors.centerIn: parent
                                visible: cell.modelData.cover.length === 0
                                text: cell.modelData.name.length ? cell.modelData.name.charAt(0).toUpperCase() : "?"
                                font.pixelSize: 44
                                font.weight: Font.DemiBold
                                color: Theme.textTertiary
                            }
                            Badge {
                                anchors.left: parent.left
                                anchors.top: parent.top
                                anchors.margins: 6
                                visible: cell.modelData.current || cell.modelData.inUse || !cell.modelData.exists
                                text: !cell.modelData.exists ? qsTr("Missing")
                                      : cell.modelData.current ? (cell.modelData.readOnly ? qsTr("Open · read-only") : qsTr("Open"))
                                      : qsTr("In use elsewhere")
                            }
                        }
                        Text {
                            Layout.fillWidth: true
                            text: cell.modelData.name
                            elide: Text.ElideRight
                            font.pixelSize: Theme.fontBody + 1
                            font.weight: Font.DemiBold
                            color: Theme.textPrimary
                        }
                        Text {
                            Layout.fillWidth: true
                            elide: Text.ElideRight
                            font.pixelSize: Theme.fontCaption
                            color: Theme.textSecondary
                            text: !cell.modelData.exists ? qsTr("Folder not found")
                                  : cell.modelData.scans < 0 ? qsTr("Not saved yet")
                                  : qsTr("%1 scans · %2 prints").arg(cell.modelData.scans).arg(cell.modelData.prints)
                        }
                        Text {
                            Layout.fillWidth: true
                            elide: Text.ElideRight
                            font.pixelSize: Theme.fontCaption
                            color: Theme.textTertiary
                            text: cell.modelData.opened ? qsTr("Opened %1").arg(cell.modelData.opened) : ""
                        }
                        Text {
                            Layout.fillWidth: true
                            elide: Text.ElideRight
                            font.pixelSize: Theme.fontCaption
                            color: Theme.textTertiary
                            text: cell.modelData.modified ? qsTr("Modified %1").arg(cell.modelData.modified) : ""
                        }
                    }
                    MouseArea {
                        anchors.fill: parent
                        acceptedButtons: Qt.LeftButton | Qt.RightButton
                        onClicked: (m) => {
                            home.selected = cell.modelData.folder
                            if (m.button === Qt.RightButton) cardMenu.popup()
                        }
                        onDoubleClicked: (m) => { if (m.button === Qt.LeftButton && cell.modelData.exists) home.cmd.enterLibrary(cell.modelData.folder) }
                    }
                    ToolBtn {
                        objectName: "libraryCardMenu"
                        anchors.right: parent.right
                        anchors.bottom: parent.bottom
                        anchors.margins: 6
                        iconName: "more"
                        tip: qsTr("More")
                        onClicked: { home.selected = cell.modelData.folder; cardMenu.popup() }
                    }
                    Menu {
                        id: cardMenu
                        MenuItem { text: qsTr("Open"); enabled: cell.modelData.exists; onTriggered: home.cmd.enterLibrary(cell.modelData.folder) }
                        MenuItem { text: qsTr("Rename…"); enabled: cell.modelData.exists && cell.modelData.readable && !cell.modelData.inUse; onTriggered: home.cmd.renameLibrary(cell.modelData.folder, cell.modelData.name) }
                        MenuItem { text: App.platform === "macos" ? qsTr("Show in Finder") : qsTr("Show in Explorer"); enabled: cell.modelData.exists; onTriggered: App.revealInFileManager(cell.modelData.folder) }
                        MenuSeparator {}
                        MenuItem { text: qsTr("Remove from List"); onTriggered: App.forgetLibrary(cell.modelData.folder) }
                    }
                }
            }
            // nothing yet: one clear first step
            Column {
                anchors.centerIn: parent
                visible: grid.count === 0
                spacing: 12
                Text {
                    anchors.horizontalCenter: parent.horizontalCenter
                    text: qsTr("No libraries yet")
                    font.pixelSize: Theme.fontDialogTitle
                    color: Theme.textPrimary
                }
                FkButton { anchors.horizontalCenter: parent.horizontalCenter; primary: true; text: qsTr("Create a Library…"); onClicked: home.cmd.newLibrary() }
            }
        }
    }
}
