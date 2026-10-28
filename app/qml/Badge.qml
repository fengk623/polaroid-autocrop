// Canvas badge / chip capsule: height 20, padding 8, badgeFill, 11 semibold.
import QtQuick

Rectangle {
    id: b
    property string text: ""
    property bool chip: false
    property color textColor: Theme.textPrimary
    implicitHeight: chip ? Theme.chipH : Theme.badgeH
    implicitWidth: t.implicitWidth + 16
    radius: chip ? Theme.radius : height / 2
    color: Theme.badgeFill
    Text {
        id: t
        anchors.centerIn: parent
        text: b.text
        font.pixelSize: Theme.fontBadge
        font.weight: Font.DemiBold
        color: b.textColor
    }
}
