// Rail section: 28 px header (chevron, title, note, optional reset), content below, hairline above.
import QtQuick
import QtQuick.Layouts
import QtQuick.Controls.Basic

ColumnLayout {
    id: sec
    property string title: ""
    property string note: ""
    property bool expanded: true
    property bool resettable: false
    property string resetTip: ""
    signal reset()
    default property alias content: body.data
    spacing: 0
    Layout.fillWidth: true

    Rectangle { Layout.fillWidth: true; height: 1; color: Theme.lineSection }
    AbstractButton {
        id: header
        Layout.fillWidth: true
        implicitHeight: Theme.sectionHeader
        hoverEnabled: true
        onClicked: sec.expanded = !sec.expanded
        Accessible.name: sec.title
        background: Rectangle { color: header.hovered ? Theme.hoverOverlay : "transparent" }
        contentItem: RowLayout {
            spacing: 6
            Item { width: 2 }
            Icon { name: sec.expanded ? "chevronDown" : "chevronRight"; size: 12; color: Theme.textSecondary }
            Text {
                objectName: "sectionTitle"
                text: sec.title
                font.pixelSize: Theme.fontSectionTitle
                font.weight: Font.DemiBold
                color: Theme.textPrimary
            }
            Text {
                text: sec.note
                font.pixelSize: Theme.fontCaption
                color: Theme.textTertiary
                elide: Text.ElideRight
                Layout.fillWidth: true
            }
            ToolBtn {
                visible: sec.resettable
                iconName: "rotateLeft"
                tip: sec.resetTip
                implicitWidth: 24; implicitHeight: 24
                onClicked: sec.reset()
            }
            Item { width: 6 }
        }
    }
    ColumnLayout {
        id: body
        visible: sec.expanded
        Layout.fillWidth: true
        Layout.leftMargin: Theme.rowInset
        Layout.rightMargin: Theme.rowInset
        Layout.bottomMargin: 10
        spacing: 6
    }
}
