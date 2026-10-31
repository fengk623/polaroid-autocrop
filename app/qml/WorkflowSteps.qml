// Workflow steps (UX-06): Import · Detect / review · Export image area · Relink · Composite / final, for the
// current print. Done steps are filled, the next one is ringed and named below; a click goes where the step is
// done (import dialog, right-rail tab, Composite view). Status is shown by shape and text, not by colour alone.
import QtQuick
import QtQuick.Layouts
import QtQuick.Controls.Basic
import FrameKeeper.Core

Item {
    id: w
    required property var cmd
    required property var canvas
    readonly property var it: App.currentItem
    readonly property bool hasItem: it.id !== undefined

    readonly property var names: [qsTr("Import"), qsTr("Detect / review"), qsTr("Export image area"), qsTr("Relink"),
                                  qsTr("Composite / final output")]
    readonly property var done: [
        App.counts.scans > 0,
        hasItem && (it.reviewed === true || it.confidenceClass === "high"),
        hasItem && (it.edits || []).length > 0,
        hasItem && it.relinked === true,
        hasItem && (it.finalExport || "").length > 0
    ]
    // the first step not done (5 = all done)
    readonly property int next: {
        for (let i = 0; i < 5; ++i)
            if (!done[i]) return i
        return 5
    }
    readonly property string summary: next >= 5 ? qsTr("All steps done for this print")
                                                : qsTr("Next: %1 (step %2 of 5)").arg(names[next]).arg(next + 1)

    function go(i) {
        if (i === 0) cmd.importScans()
        else if (i === 1) App.setSetting("rightTab", 0)
        else if (i === 2) App.setSetting("rightTab", 1)
        else if (i === 3) App.setSetting("rightTab", 2)
        else { App.setSetting("rightTab", 2); canvas.mode = "composite" }
    }

    implicitHeight: col.implicitHeight + 8
    ColumnLayout {
        id: col
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.leftMargin: 12
        anchors.rightMargin: 10
        spacing: 4
        RowLayout {
            spacing: 0
            Layout.fillWidth: true
            Repeater {
                model: 5
                RowLayout {
                    id: step
                    required property int index
                    spacing: 0
                    Layout.fillWidth: index > 0
                    Rectangle { // connector to the previous step
                        visible: step.index > 0
                        Layout.fillWidth: true
                        Layout.minimumWidth: 6
                        Layout.leftMargin: 3
                        Layout.rightMargin: 3
                        implicitHeight: 1
                        color: w.done[step.index - 1] ? Theme.accent : Theme.lineSection
                    }
                    AbstractButton {
                        objectName: "workflowStep" + (step.index + 1)
                        implicitWidth: 20
                        implicitHeight: 20
                        hoverEnabled: true
                        focusPolicy: Qt.TabFocus
                        Accessible.name: w.names[step.index] + " — " + (w.done[step.index] ? qsTr("done")
                                         : step.index === w.next ? qsTr("next step") : qsTr("not done"))
                        ToolTip.visible: hovered
                        ToolTip.text: qsTr("Step %1: %2").arg(step.index + 1).arg(w.names[step.index])
                                      + " — " + (w.done[step.index] ? qsTr("done") : step.index === w.next ? qsTr("next step") : qsTr("not done"))
                        ToolTip.delay: 400
                        onClicked: w.go(step.index)
                        background: Rectangle {
                            radius: width / 2
                            color: w.done[step.index] ? Theme.accent : "transparent"
                            border.width: parent.visualFocus ? 2 : (step.index === w.next ? 2 : 1)
                            border.color: parent.visualFocus || step.index === w.next ? Theme.accent : Theme.lineSection
                        }
                        contentItem: Item {
                            Icon {
                                visible: w.done[step.index]
                                anchors.centerIn: parent
                                name: "check"
                                size: 14
                                stroke: 2
                                color: Theme.onAccent
                            }
                            Text {
                                visible: !w.done[step.index]
                                anchors.centerIn: parent
                                text: step.index + 1
                                font.pixelSize: Theme.fontCaption
                                font.weight: step.index === w.next ? Font.DemiBold : Font.Normal
                                color: step.index === w.next ? Theme.textPrimary : Theme.textTertiary
                            }
                        }
                    }
                }
            }
        }
        Text {
            objectName: "workflowSummary"
            Layout.fillWidth: true
            elide: Text.ElideRight
            text: w.summary
            font.pixelSize: Theme.fontCaption
            color: Theme.textSecondary
        }
    }
}
