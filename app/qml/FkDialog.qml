// In-window dialog shell (Export, Relink, Final output, Settings on Windows, About on Windows).
import QtQuick
import QtQuick.Layouts
import QtQuick.Controls.Basic

Dialog {
    id: d
    modal: true
    anchors.centerIn: Overlay.overlay
    padding: 16
    width: Math.min(560, (parent ? parent.width : 800) - 80)
    closePolicy: Popup.CloseOnEscape
    background: Rectangle {
        color: Theme.surfacePanel
        radius: 6
        border.color: Theme.lineSection
    }
    header: Item {
        implicitHeight: 44
        Text {
            anchors.left: parent.left
            anchors.leftMargin: 16
            anchors.verticalCenter: parent.verticalCenter
            text: d.title
            font.pixelSize: Theme.fontDialogTitle
            font.weight: Font.DemiBold
            color: Theme.textPrimary
        }
    }
    Overlay.modal: Rectangle { color: Qt.rgba(0, 0, 0, 0.45) }
}
