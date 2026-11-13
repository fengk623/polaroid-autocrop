// UX-05 (U018, RFC-0018): a new library, or an old project imported into a new library as a copy. Asks for the
// name and the folder it goes into (Documents unless another is chosen); shows the folder the library will get
// and, for an import, what the pre-check found. The caller replaces the open project through win.replaceProject.
import QtQuick
import QtQuick.Layouts
import QtQuick.Controls.Basic
import QtQuick.Dialogs
import FrameKeeper.Core

FkDialog {
    id: d
    objectName: "libraryDialog"
    property string mode: "new" // "new" or "import"
    property string projectUrl: ""
    property string parentFolder: ""
    property alias name: nameField.text
    signal accepted2(string mode, string parentFolder, string name, string projectUrl)
    width: 480
    title: mode === "import" ? qsTr("Import project into a new library") : qsTr("New library")

    function openNew() {
        mode = "new"; projectUrl = ""; parentFolder = App.defaultLibraryParent(); name = qsTr("My library"); open()
    }
    function openImport(url, suggestedName) {
        mode = "import"; projectUrl = url; parentFolder = App.defaultLibraryParent(); name = suggestedName; open()
    }
    readonly property var target: App.libraryTarget(parentFolder, name)
    readonly property var check: mode === "import" && name.trim().length > 0 ? App.checkProjectImport(projectUrl, parentFolder, name) : ({})
    readonly property bool canAccept: name.trim().length > 0 && target.free && (mode !== "import" || check.ok === true)

    contentItem: ColumnLayout {
        spacing: 10
        Text { text: qsTr("Name"); font.pixelSize: Theme.fontBody; color: Theme.textSecondary }
        TextField {
            id: nameField
            objectName: "libraryName"
            Layout.fillWidth: true
            font.pixelSize: Theme.fontBody
            color: Theme.textPrimary
            background: Rectangle { radius: Theme.radius; color: Theme.controlFill; border.color: nameField.activeFocus ? Theme.accent : Theme.controlStroke; border.width: nameField.activeFocus ? 2 : 1 }
            onAccepted: if (d.canAccept) okButton.clicked()
        }
        Text { text: qsTr("Location"); font.pixelSize: Theme.fontBody; color: Theme.textSecondary }
        RowLayout {
            Layout.fillWidth: true
            spacing: 6
            Text {
                Layout.fillWidth: true
                text: d.parentFolder
                elide: Text.ElideMiddle
                font.pixelSize: Theme.fontBody
                color: Theme.textPrimary
            }
            FkButton { text: qsTr("Choose…"); onClicked: parentDialog.open() }
        }
        Text {
            Layout.fillWidth: true
            wrapMode: Text.Wrap
            font.pixelSize: Theme.fontCaption
            color: d.target.free ? Theme.textSecondary : Theme.error
            text: !d.target.parentExists ? qsTr("This folder does not exist.")
                  : !d.target.free ? qsTr("A folder named %1 already exists here; choose another name.").arg(d.target.folderName)
                  : qsTr("The library is the folder %1. Scans stay where they are; it only refers to them.").arg(d.target.folderName)
        }
        Text {
            visible: d.mode === "import"
            Layout.fillWidth: true
            wrapMode: Text.Wrap
            font.pixelSize: Theme.fontCaption
            color: d.check.ok ? Theme.textSecondary : Theme.error
            text: !d.check.ok ? (d.check.error || "")
                  : qsTr("%1 scans, %2 prints, %3 export records are copied into the library. The project file is only read and stays usable.")
                        .arg(d.check.scans).arg(d.check.prints).arg(d.check.exportRecords)
                    + (d.check.missingScans > 0 ? " " + qsTr("%1 scans are missing: the library refers to them as the project did; relocate them later.").arg(d.check.missingScans) : "")
        }
        RowLayout {
            Layout.alignment: Qt.AlignRight
            FkButton { text: qsTr("Cancel"); onClicked: d.close() }
            FkButton {
                id: okButton
                objectName: "libraryCreate"
                primary: true
                enabled: d.canAccept
                text: d.mode === "import" ? qsTr("Import") : qsTr("Create")
                onClicked: { d.close(); d.accepted2(d.mode, d.parentFolder, d.name.trim(), d.projectUrl) }
            }
        }
    }
    FolderDialog {
        id: parentDialog
        title: qsTr("Folder for the library")
        onAccepted: d.parentFolder = App.localFile(selectedFolder)
    }
    onOpened: { nameField.forceActiveFocus(); nameField.selectAll() }
}
