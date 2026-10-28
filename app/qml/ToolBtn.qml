// Icon tool button with a 28 px target, tooltip "Name (shortcut)" and an accessible name.
import QtQuick
import QtQuick.Controls.Basic

AbstractButton {
    id: b
    property string iconName: ""
    property string tip: ""
    property string shortcutText: ""
    property bool active: false
    implicitWidth: Theme.hit
    implicitHeight: Theme.hit
    hoverEnabled: true
    focusPolicy: Qt.TabFocus
    opacity: enabled ? 1 : Theme.disabledOpacity
    Accessible.name: tip
    ToolTip.visible: hovered && tip.length > 0
    ToolTip.text: shortcutText.length ? tip + "  (" + shortcutText + ")" : tip
    ToolTip.delay: 600
    contentItem: Item {
        Icon {
            anchors.centerIn: parent
            name: b.iconName
            color: b.active ? Theme.accent : Theme.textPrimary
        }
    }
    background: Rectangle {
        radius: Theme.radius
        color: b.down ? Theme.pressedOverlay : (b.hovered || b.active ? Theme.hoverOverlay : "transparent")
        border.color: Theme.accent
        border.width: b.visualFocus ? 2 : 0
    }
}
