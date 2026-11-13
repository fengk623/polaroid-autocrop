// 6 px rail resize grip; 1 px accent line on hover; double-click resets; widths persist.
import QtQuick
import QtQuick.Layouts
import FrameKeeper.Core

Item {
    id: g
    property string target: ""
    property int minW: 200
    property int maxW: 400
    property int defW: 300
    property int sign: 1
    property int current: 0 // the width shown (narrower than the stored one in a narrow window, UX-06)
    Layout.preferredWidth: 1
    Layout.fillHeight: true
    z: 10
    Rectangle { anchors.fill: parent; color: area.containsMouse || area.pressed ? Theme.accent : Theme.lineRegion }
    MouseArea {
        id: area
        anchors.fill: parent
        anchors.leftMargin: -3
        anchors.rightMargin: -3
        hoverEnabled: true
        cursorShape: Qt.SplitHCursor
        property real startX: 0
        property int startW: 0
        onPressed: (m) => { startX = mapToGlobal(m.x, 0).x; startW = g.current > 0 ? g.current : App.settings[g.target] }
        onPositionChanged: (m) => {
            if (!pressed) return
            const dx = mapToGlobal(m.x, 0).x - startX
            App.setSetting(g.target, Math.max(g.minW, Math.min(g.maxW, Math.round(startW + g.sign * dx))))
        }
        onDoubleClicked: App.setSetting(g.target, g.defW)
    }
}
