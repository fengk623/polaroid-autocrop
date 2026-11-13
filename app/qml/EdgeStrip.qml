// Edge strip (UX-10, ui-reference §4.1): a thin bar on the window edge outside a panel that folds and unfolds
// it. It stays where it is when the panel is folded, so the way back is always under the pointer; the chevron
// points where a click moves the panel. The hit area reaches `reach` px over the neighbour's empty inset; Main
// sets it to 0 where a scroll bar is next to the strip.
import QtQuick
import QtQuick.Controls.Basic
import FrameKeeper.Core

Item {
    id: s
    property string edge: "left"   // "left" | "right" | "bottom": the window edge the strip sits on
    property bool open: true       // the panel is shown
    property string showText: ""   // "Show Left Rail"
    property string hideText: ""   // "Hide Left Rail"
    property string shortcutText: ""
    property int reach: Theme.edgeStripReach
    signal toggled()

    readonly property bool vertical: edge !== "bottom"
    readonly property string tip: open ? hideText : showText
    // logical px across the strip that answer a click (drawn width + reach over the neighbour)
    readonly property int hitWidth: Theme.edgeStrip + reach
    readonly property bool hovered: area.containsMouse

    implicitWidth: vertical ? Theme.edgeStrip : 0
    implicitHeight: vertical ? 0 : Theme.edgeStrip
    z: 5 // above the neighbour it reaches over

    activeFocusOnTab: true
    Accessible.role: Accessible.Button
    Accessible.name: tip
    Accessible.onPressAction: s.toggled()
    Keys.onSpacePressed: s.toggled()
    Keys.onReturnPressed: s.toggled()
    Keys.onEnterPressed: s.toggled()

    Rectangle {
        anchors.fill: parent
        color: s.open ? Theme.surfacePanel : Theme.controlFill
    }
    Rectangle { anchors.fill: parent; color: area.pressed ? Theme.pressedOverlay : area.containsMouse ? Theme.hoverOverlay : "transparent" }
    // hairline between the strip and what it folds
    Rectangle {
        color: Theme.lineRegion
        width: s.vertical ? 1 : parent.width
        height: s.vertical ? parent.height : 1
        x: s.edge === "left" ? parent.width - 1 : 0
        y: 0
    }
    Rectangle {
        anchors.fill: parent
        color: "transparent"
        border.color: Theme.accent
        border.width: s.activeFocus ? 2 : 0
    }
    Icon {
        anchors.centerIn: parent
        size: 12
        stroke: 1.4
        color: area.containsMouse || s.activeFocus ? Theme.accent : s.open ? Theme.textSecondary : Theme.textPrimary
        name: s.edge === "left" ? (s.open ? "chevronLeft" : "chevronRight")
            : s.edge === "right" ? (s.open ? "chevronRight" : "chevronLeft")
            : (s.open ? "chevronDown" : "chevronUp")
    }
    MouseArea {
        id: area
        anchors.fill: parent
        anchors.rightMargin: s.edge === "left" ? -s.reach : 0
        anchors.leftMargin: s.edge === "right" ? -s.reach : 0
        anchors.topMargin: s.edge === "bottom" ? -s.reach : 0
        hoverEnabled: true
        cursorShape: Qt.PointingHandCursor
        onClicked: s.toggled()
    }
    ToolTip.visible: area.containsMouse && tip.length > 0
    ToolTip.text: shortcutText.length ? tip + "  (" + shortcutText + ")" : tip
    ToolTip.delay: 600
}
