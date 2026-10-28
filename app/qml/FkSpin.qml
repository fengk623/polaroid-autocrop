// Compact integer spin field in the theme (− value +), 24 tall.
import QtQuick
import QtQuick.Controls.Basic

SpinBox {
    id: s
    implicitHeight: Theme.controlH
    implicitWidth: 104
    font.pixelSize: Theme.fontBody
    editable: true
    contentItem: TextInput {
        text: s.textFromValue(s.value, s.locale)
        font: s.font
        color: Theme.textPrimary
        horizontalAlignment: Qt.AlignHCenter
        verticalAlignment: Qt.AlignVCenter
        readOnly: !s.editable
        validator: s.validator
        inputMethodHints: Qt.ImhFormattedNumbersOnly
        selectByMouse: true
    }
    up.indicator: Rectangle {
        x: s.width - width
        height: s.height
        width: 26
        radius: Theme.radius
        color: s.up.pressed ? Theme.pressedOverlay : "transparent"
        Icon { anchors.centerIn: parent; name: "plus"; size: 12; color: Theme.textSecondary }
    }
    down.indicator: Rectangle {
        height: s.height
        width: 26
        radius: Theme.radius
        color: s.down.pressed ? Theme.pressedOverlay : "transparent"
        Icon { anchors.centerIn: parent; name: "zoomOut"; size: 12; color: Theme.textSecondary }
    }
    background: Rectangle {
        radius: Theme.radius
        color: Theme.controlFill
        border.width: s.activeFocus ? 2 : (Theme.dark ? 0 : 1)
        border.color: s.activeFocus ? Theme.accent : Theme.controlStroke
    }
}
