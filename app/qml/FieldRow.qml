// Label + control row (pitch 30); the label column is computed per section (widest label + 8, 84–132).
import QtQuick
import QtQuick.Layouts

RowLayout {
    id: r
    property string label: ""
    property int labelWidth: 96
    default property alias control: holder.data
    Layout.fillWidth: true
    spacing: 8
    Text {
        text: r.label
        font.pixelSize: Theme.fontBody
        color: Theme.textSecondary
        Layout.preferredWidth: r.labelWidth
        elide: Text.ElideRight
    }
    RowLayout {
        id: holder
        Layout.fillWidth: true
        spacing: 6
    }
}
