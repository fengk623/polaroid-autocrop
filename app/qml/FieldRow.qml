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
        id: labelText
        objectName: "fieldLabel" // FK-009: a label must not be cut (self-test)
        text: r.label
        font.pixelSize: Theme.fontBody
        color: Theme.textSecondary
        // the section's label column, wider when this label needs it (a translation, a larger font): never cut
        Layout.preferredWidth: Math.max(r.labelWidth, implicitWidth)
        Layout.minimumWidth: implicitWidth
    }
    RowLayout {
        id: holder
        Layout.fillWidth: true
        spacing: 6
    }
}
