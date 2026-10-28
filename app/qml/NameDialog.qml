// Small text prompt (template name).
import QtQuick
import QtQuick.Layouts
import QtQuick.Controls.Basic

FkDialog {
    id: d
    property alias text: field.text
    property string label: ""
    signal done(string text)
    width: 380
    contentItem: ColumnLayout {
        spacing: 10
        Text { text: d.label; font.pixelSize: Theme.fontBody; color: Theme.textSecondary }
        TextField {
            id: field
            Layout.fillWidth: true
            font.pixelSize: Theme.fontBody
            color: Theme.textPrimary
            background: Rectangle { radius: Theme.radius; color: Theme.controlFill; border.color: field.activeFocus ? Theme.accent : Theme.controlStroke; border.width: field.activeFocus ? 2 : 1 }
            onAccepted: { d.done(text); d.close() }
        }
        RowLayout {
            Layout.alignment: Qt.AlignRight
            FkButton { text: qsTr("Cancel"); onClicked: d.close() }
            FkButton { primary: true; text: qsTr("Save"); enabled: field.text.trim().length > 0; onClicked: { d.done(field.text); d.close() } }
        }
    }
    onOpened: field.forceActiveFocus()
}
