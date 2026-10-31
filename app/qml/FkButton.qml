// Button 28 tall, radius 4; primary = accent fill (one per dialog/section), secondary = controlFill.
import QtQuick
import QtQuick.Controls.Basic

Button {
    id: b
    property bool primary: false
    property string iconName: ""
    property string disabledReason: ""
    implicitHeight: Theme.buttonH
    leftPadding: Theme.padding
    rightPadding: Theme.padding
    font.pixelSize: Theme.fontBody
    opacity: enabled ? 1 : Theme.disabledOpacity
    hoverEnabled: true
    ToolTip.visible: hovered && (!enabled && disabledReason.length > 0)
    ToolTip.text: disabledReason
    ToolTip.delay: 600
    contentItem: Row {
        spacing: 6
        Icon {
            visible: b.iconName.length > 0
            name: b.iconName
            size: 16
            color: b.primary ? Theme.onAccent : Theme.textPrimary
            anchors.verticalCenter: parent.verticalCenter
        }
        Text {
            objectName: "buttonLabel" // FK-009: a button label must not be cut (self-test)
            text: b.text
            font: b.font
            color: b.primary ? Theme.onAccent : Theme.textPrimary
            anchors.verticalCenter: parent.verticalCenter
            elide: Text.ElideRight
        }
    }
    background: Rectangle {
        radius: Theme.radius
        color: b.primary ? Theme.accent : Theme.controlFill
        border.width: b.primary ? 0 : (Theme.dark ? 0 : 1)
        border.color: Theme.controlStroke
        Rectangle {
            anchors.fill: parent
            radius: Theme.radius
            color: b.down ? Theme.pressedOverlay : (b.hovered ? Theme.hoverOverlay : "transparent")
        }
        Rectangle {
            visible: b.visualFocus
            anchors.fill: parent
            anchors.margins: -2
            radius: Theme.radius + 2
            color: "transparent"
            border.color: Theme.accent
            border.width: 2
        }
    }
}
