// UX-05/06 (U019): the two workspaces at the top left, where "Project" was — Library (home page, libraries) and
// Edit Photos (boundaries, export, relink, output). G and D switch from the keyboard (Main.qml).
import QtQuick
import QtQuick.Controls.Basic
import FrameKeeper.Core

Segmented {
    objectName: "workspaceSwitch"
    model: [{ text: qsTr("Library"), value: "library", tip: qsTr("Library (G)") },
            { text: qsTr("Edit Photos"), value: "edit", tip: qsTr("Edit Photos (D)") }]
    value: App.settings.workspace
    onActivated: (v) => App.setSetting("workspace", v)
}
