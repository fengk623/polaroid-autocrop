// Segmented switch: height 26, radius 4; selected = controlFill + textPrimary; optional 6 px accent dots.
import QtQuick
import QtQuick.Controls.Basic

Rectangle {
    id: root
    property var model: []        // [{text, value, tip, dot}]
    property var value
    signal activated(var value)
    implicitHeight: 26
    implicitWidth: row.implicitWidth + 4
    radius: Theme.radius
    color: Theme.surfaceWell
    Row {
        id: row
        anchors.fill: parent
        anchors.margins: 2
        spacing: 2
        Repeater {
            model: root.model
            delegate: AbstractButton {
                id: seg
                required property var modelData
                height: row.height
                width: Math.max(44, label.implicitWidth + 20)
                hoverEnabled: true
                Accessible.name: modelData.text
                ToolTip.visible: hovered && (modelData.tip || "").length > 0
                ToolTip.text: modelData.tip || ""
                ToolTip.delay: 600
                onClicked: root.activated(modelData.value)
                background: Rectangle {
                    radius: Theme.radius - 1
                    color: root.value === seg.modelData.value ? Theme.controlFill
                                                              : (seg.hovered ? Theme.hoverOverlay : "transparent")
                    border.color: Theme.accent
                    border.width: seg.visualFocus ? 2 : 0
                }
                contentItem: Item {
                    Text {
                        id: label
                        anchors.centerIn: parent
                        text: seg.modelData.text
                        font.pixelSize: Theme.fontBody
                        color: root.value === seg.modelData.value ? Theme.textPrimary : Theme.textSecondary
                    }
                    Rectangle {
                        visible: !!seg.modelData.dot
                        width: 6; height: 6; radius: 3
                        color: Theme.accent
                        anchors.right: parent.right
                        anchors.top: parent.top
                        anchors.margins: 3
                    }
                }
            }
        }
    }
}
