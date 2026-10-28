// Final export: real scanned frame (default) or synthetic frame; required confirmations listed.
import QtQuick
import QtQuick.Layouts
import QtQuick.Controls.Basic
import FrameKeeper.Core

FkDialog {
    id: d
    property var ids: []
    property var plan: ({})
    title: qsTr("Export final prints")
    function openFor(list) {
        ids = list
        plan = App.prepareFinal(list)
        scaleBox.checked = false
        depthBox.checked = false
        open()
    }
    contentItem: ColumnLayout {
        spacing: 10
        Text {
            Layout.fillWidth: true
            wrapMode: Text.WordWrap
            font.pixelSize: Theme.fontBody
            color: Theme.textPrimary
            text: (App.settings.finalMode === "real" ? qsTr("Graded images placed into the real scanned frame.")
                                                    : qsTr("Graded images with a synthetic frame."))
                  + " " + qsTr("%n print(s) ready.", "", (d.plan.ready || []).length)
        }
        Text {
            visible: (d.plan.missing || []).length > 0
            Layout.fillWidth: true
            wrapMode: Text.WordWrap
            font.pixelSize: Theme.fontCaption
            color: Theme.warning
            text: qsTr("Skipped (no graded file relinked): %1").arg((d.plan.missing || []).map(x => x.label).join(", "))
        }
        FkCheck { id: scaleBox; visible: (d.plan.actions || []).indexOf("scale") >= 0; text: qsTr("Allow scaling graded files that were resized (same aspect ratio)") }
        FkCheck { id: depthBox; visible: (d.plan.actions || []).indexOf("accept_lower_depth") >= 0; text: qsTr("Accept graded files with a lower bit depth than the scan") }
        Text {
            Layout.fillWidth: true
            wrapMode: Text.WordWrap
            font.pixelSize: Theme.fontCaption
            color: Theme.textTertiary
            text: qsTr("Folder: %1").arg(d.plan.folder || "")
        }
        RowLayout {
            Layout.alignment: Qt.AlignRight
            spacing: 8
            FkButton { text: qsTr("Cancel"); onClicked: d.close() }
            FkButton {
                primary: true
                enabled: (d.plan.ready || []).length > 0
                text: qsTr("Export")
                onClicked: {
                    d.close()
                    App.exportFinal(d.ids, { "scale": scaleBox.checked, "accept_lower_depth": depthBox.checked })
                }
            }
        }
    }
}
