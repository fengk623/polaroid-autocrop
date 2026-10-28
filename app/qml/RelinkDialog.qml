// Relink results: matched print, method (XMP → sidecar → file name), content check, needed confirmations.
import QtQuick
import QtQuick.Layouts
import QtQuick.Controls.Basic
import FrameKeeper.Core

FkDialog {
    id: d
    property var rows: []
    width: Math.min(720, (parent ? parent.width : 900) - 80)
    title: qsTr("Relink graded files")
    function show(r) {
        rows = r.map(x => Object.assign({}, x))
        open()
    }
    function actionText(a) {
        return ({ "rotate": qsTr("rotate back"), "scale": qsTr("scale to the exported size"), "accept_lower_depth": qsTr("accept lower bit depth"),
                  "choose_icc": qsTr("no colour profile: assume the exported one"), "confirm_content": qsTr("content differs — confirm it is this print"),
                  "manual_alignment": qsTr("different aspect ratio: cannot be placed") })[a] || a
    }
    contentItem: ColumnLayout {
        spacing: 8
        ListView {
            Layout.fillWidth: true
            Layout.preferredHeight: Math.min(360, contentHeight)
            clip: true
            model: d.rows
            spacing: 6
            delegate: Rectangle {
                id: rowItem
                required property var modelData
                required property int index
                width: ListView.view.width
                height: rcol.implicitHeight + 12
                radius: Theme.radius
                color: Theme.surfaceWell
                ColumnLayout {
                    id: rcol
                    anchors.fill: parent
                    anchors.margins: 6
                    spacing: 2
                    RowLayout {
                        Layout.fillWidth: true
                        FkCheck {
                            enabled: rowItem.modelData.matched && rowItem.modelData.ok !== false
                            checked: rowItem.modelData.accept === true
                            onToggled: d.rows[rowItem.index].accept = checked
                        }
                        Text { Layout.fillWidth: true; text: rowItem.modelData.fileName; elide: Text.ElideMiddle; font.pixelSize: Theme.fontBody; color: Theme.textPrimary }
                        Text {
                            text: rowItem.modelData.matched ? "→ " + rowItem.modelData.label + " · " + rowItem.modelData.scanName : qsTr("no match")
                            font.pixelSize: Theme.fontBody
                            color: rowItem.modelData.matched ? Theme.textPrimary : Theme.error
                        }
                    }
                    Text {
                        visible: rowItem.modelData.matched === true
                        Layout.fillWidth: true
                        wrapMode: Text.WordWrap
                        font.pixelSize: Theme.fontCaption
                        color: Theme.textSecondary
                        text: qsTr("Matched by %1 · content check %2 %").arg(rowItem.modelData.source).arg(Math.round((rowItem.modelData.score || 0) * 100))
                              + ((rowItem.modelData.actions || []).length ? " · " + qsTr("needs: %1").arg(rowItem.modelData.actions.map(d.actionText).join(", ")) : "")
                    }
                    Text {
                        visible: (rowItem.modelData.message || "").length > 0
                        Layout.fillWidth: true
                        wrapMode: Text.WordWrap
                        font.pixelSize: Theme.fontCaption
                        color: Theme.error
                        text: rowItem.modelData.message || ""
                    }
                }
            }
        }
        Text {
            Layout.fillWidth: true
            wrapMode: Text.WordWrap
            font.pixelSize: Theme.fontCaption
            color: Theme.textTertiary
            text: qsTr("Checked files are attached to their print. Rotation and size adjustments are applied when compositing.")
        }
        RowLayout {
            Layout.alignment: Qt.AlignRight
            spacing: 8
            FkButton { text: qsTr("Cancel"); onClicked: d.close() }
            FkButton { primary: true; text: qsTr("Relink"); onClicked: { App.applyRelink(d.rows); d.close() } }
        }
    }
}
