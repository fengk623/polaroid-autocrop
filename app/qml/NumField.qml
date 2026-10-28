// Numeric field 64×24 with unit suffix; arrows step (Shift ×10); commits on Enter / focus loss.
import QtQuick
import QtQuick.Controls.Basic

TextField {
    id: f
    property real value: 0
    property real step: 1
    property int decimals: 1
    property real from: -1e9
    property real to: 1e9
    property string unit: ""
    signal committed(real value)
    implicitWidth: Theme.numFieldW + (unit.length ? 16 : 0)
    implicitHeight: Theme.controlH
    font.pixelSize: Theme.fontBody
    font.features: { "tnum": 1 }
    color: Theme.textPrimary
    selectByMouse: true
    horizontalAlignment: TextInput.AlignRight
    rightPadding: unit.length ? 22 : 6
    leftPadding: 6
    topPadding: 0
    bottomPadding: 0
    validator: DoubleValidator { bottom: f.from; top: f.to; decimals: f.decimals }
    text: Number(value).toFixed(decimals)
    onEditingFinished: {
        const v = Number(text.replace(",", "."))
        if (!isNaN(v)) f.committed(Math.min(f.to, Math.max(f.from, v)))
        text = Qt.binding(function() { return Number(f.value).toFixed(f.decimals) })
    }
    Keys.onUpPressed: (e) => { f.committed(Math.min(f.to, f.value + f.step * (e.modifiers & Qt.ShiftModifier ? 10 : 1))) }
    Keys.onDownPressed: (e) => { f.committed(Math.max(f.from, f.value - f.step * (e.modifiers & Qt.ShiftModifier ? 10 : 1))) }
    background: Rectangle {
        radius: Theme.radius
        color: Theme.controlFill
        border.width: f.activeFocus ? 2 : (Theme.dark ? 0 : 1)
        border.color: f.activeFocus ? Theme.accent : Theme.controlStroke
        Text {
            visible: f.unit.length > 0
            text: f.unit
            anchors.right: parent.right
            anchors.rightMargin: 6
            anchors.verticalCenter: parent.verticalCenter
            font.pixelSize: Theme.fontCaption
            color: Theme.textTertiary
        }
    }
}
