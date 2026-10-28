// Combo box (min 120×24, radius 4) over [{text, value}] models.
import QtQuick
import QtQuick.Controls.Basic

ComboBox {
    id: c
    property var value
    signal picked(var value)
    textRole: "text"
    valueRole: "value"
    implicitHeight: Theme.controlH
    implicitWidth: Math.max(Theme.comboMinW, implicitContentWidth + 36)
    font.pixelSize: Theme.fontBody
    currentIndex: Math.max(0, indexOfValue(value))
    onActivated: (i) => c.picked(valueAt(i))
    contentItem: Text {
        leftPadding: 8
        text: c.displayText
        font: c.font
        color: Theme.textPrimary
        verticalAlignment: Text.AlignVCenter
        elide: Text.ElideRight
    }
    indicator: Icon {
        name: "chevronDown"
        size: 12
        color: Theme.textSecondary
        x: c.width - width - 8
        y: (c.height - height) / 2
    }
    background: Rectangle {
        radius: Theme.radius
        color: Theme.controlFill
        border.width: c.visualFocus ? 2 : (Theme.dark ? 0 : 1)
        border.color: c.visualFocus ? Theme.accent : Theme.controlStroke
    }
    delegate: ItemDelegate {
        id: d
        required property var model
        required property int index
        width: c.width
        height: Theme.listRow
        highlighted: c.highlightedIndex === index
        contentItem: Text {
            text: d.model[c.textRole]
            font.pixelSize: Theme.fontBody
            color: Theme.textPrimary
            verticalAlignment: Text.AlignVCenter
        }
        background: Rectangle { color: d.highlighted ? Theme.selectionFill : Theme.surfacePanel }
    }
    popup.background: Rectangle {
        color: Theme.surfacePanel
        border.color: Theme.lineSection
        radius: Theme.radius
    }
}
