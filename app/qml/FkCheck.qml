// Checkbox 14 px (checked fill accent), label 12; 28 px target.
import QtQuick
import QtQuick.Controls.Basic

CheckBox {
    id: c
    implicitHeight: Theme.hit
    font.pixelSize: Theme.fontBody
    spacing: 8
    indicator: Rectangle {
        implicitWidth: 14; implicitHeight: 14
        x: c.leftPadding
        y: (c.height - height) / 2
        radius: 3
        color: c.checked ? Theme.accent : Theme.controlFill
        border.color: c.visualFocus ? Theme.accent : (Theme.dark ? Theme.lineSection : Theme.controlStroke)
        border.width: c.visualFocus ? 2 : 1
        Icon { anchors.centerIn: parent; visible: c.checked; name: "check"; size: 12; stroke: 2.2; color: Theme.onAccent }
    }
    contentItem: Text {
        leftPadding: c.indicator.width + c.spacing
        text: c.text
        font: c.font
        color: Theme.textPrimary
        verticalAlignment: Text.AlignVCenter
        wrapMode: Text.WordWrap
    }
}
