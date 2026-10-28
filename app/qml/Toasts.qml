// Non-modal notifications at the canvas bottom centre, 6 s (errors stay until closed).
import QtQuick
import QtQuick.Layouts
import QtQuick.Controls.Basic

Column {
    id: toasts
    spacing: 6
    property var items: []
    function push(text, kind, action, actionText) {
        model.append({ text: text, kind: kind, actionText: actionText || "", stamp: Date.now() })
        if (action) actions[model.count - 1] = action
    }
    property var actions: ({})
    ListModel { id: model }
    Repeater {
        model: model
        delegate: Rectangle {
            id: t
            required property string text
            required property string kind
            required property string actionText
            required property int index
            width: Math.min(560, row.implicitWidth + 24)
            height: row.implicitHeight + 16
            radius: 6
            color: Theme.surfacePanel
            border.color: kind === "error" ? Theme.error : (kind === "warning" ? Theme.warning : Theme.lineSection)
            anchors.horizontalCenter: parent ? parent.horizontalCenter : undefined
            Timer { running: t.kind !== "error"; interval: 6000; onTriggered: model.remove(t.index) }
            RowLayout {
                id: row
                anchors.centerIn: parent
                width: Math.min(536, implicitWidth)
                spacing: 8
                Icon { name: t.kind === "error" || t.kind === "warning" ? "warning" : "info"; size: 16; color: t.kind === "error" ? Theme.error : (t.kind === "warning" ? Theme.warning : Theme.accent) }
                Text { Layout.maximumWidth: 420; text: t.text; wrapMode: Text.WordWrap; font.pixelSize: Theme.fontBody; color: Theme.textPrimary }
                FkButton { visible: t.actionText.length > 0; text: t.actionText; implicitHeight: 24; onClicked: { const a = toasts.actions[t.index]; if (a) a(); model.remove(t.index) } }
                ToolBtn { iconName: "close"; tip: qsTr("Close"); implicitWidth: 22; implicitHeight: 22; onClicked: model.remove(t.index) }
            }
        }
    }
}
