// Export image areas: scope, folder, unreviewed low-confidence prints listed before writing (F04/F05).
import QtQuick
import QtQuick.Layouts
import QtQuick.Controls.Basic
import FrameKeeper.Core

FkDialog {
    id: d
    property var ids: []
    property var plan: ({})
    property bool fullPrint: false
    signal chooseFolder()
    title: fullPrint ? qsTr("Export full prints") : qsTr("Export image areas")
    function openFor(list, full) {
        ids = list
        fullPrint = full
        plan = App.prepareExport(list)
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
            text: qsTr("%n print(s) will be written as %1.", "", d.plan.count || 0).arg((App.settings.exportFormat || "tif").toUpperCase())
        }
        // FK-010: the real output of the first print, from the core's plan
        Text {
            readonly property var p: d.plan.plan || ({})
            visible: p.width !== undefined
            Layout.fillWidth: true
            wrapMode: Text.WordWrap
            font.pixelSize: Theme.fontCaption
            color: Theme.textSecondary
            text: visible ? qsTr("First print: %1 × %2 px, %3-bit, %4 channel(s), %5 · %6")
                              .arg(p.width).arg(p.height).arg(p.bits).arg(p.channels).arg(p.icc || qsTr("untagged")).arg(p.geometry)
                            + (p.bitExact ? " · " + qsTr("pixels identical to the scan") : " · " + qsTr("not identical: %1").arg((p.reasons || []).join("; ")))
                            + ((p.notes || []).length ? " · " + p.notes.join("; ") : "")
                          : ""
        }
        RowLayout {
            Layout.fillWidth: true
            Text { text: qsTr("Folder"); font.pixelSize: Theme.fontBody; color: Theme.textSecondary }
            Text { Layout.fillWidth: true; text: d.plan.folder || ""; elide: Text.ElideMiddle; font.pixelSize: Theme.fontCaption; color: Theme.textPrimary }
            FkButton { text: qsTr("Choose…"); onClicked: d.chooseFolder() }
        }
        FkCheck {
            // FK-010: reducing 16-bit data needs an explicit yes
            id: depthBox
            visible: d.plan.reducesDepth === true
            text: qsTr("Write 8 bits per channel from 16-bit scans (JPEG or the 8-bit setting); the extra precision is lost")
        }
        ColumnLayout {
            visible: (d.plan.unreviewed || []).length > 0
            Layout.fillWidth: true
            spacing: 4
            Text {
                Layout.fillWidth: true
                wrapMode: Text.WordWrap
                font.pixelSize: Theme.fontBody
                color: Theme.error
                text: qsTr("These prints have low confidence and were not reviewed:")
            }
            Repeater {
                model: d.plan.unreviewed || []
                Text { required property var modelData; text: "⚠ " + modelData.label + " · " + modelData.scan; font.pixelSize: Theme.fontCaption; color: Theme.textSecondary }
            }
        }
        Text {
            Layout.fillWidth: true
            wrapMode: Text.WordWrap
            font.pixelSize: Theme.fontCaption
            color: Theme.textTertiary
            text: qsTr("Existing files are never overwritten: a new version number is used instead. Only the image files are written; the project keeps a record of each export so graded files can be relinked later. Save the project before closing.")
        }
        RowLayout {
            Layout.alignment: Qt.AlignRight
            spacing: 8
            FkButton { text: qsTr("Cancel"); onClicked: d.close() }
            FkButton {
                primary: true
                text: (d.plan.unreviewed || []).length > 0 ? qsTr("Export anyway") : qsTr("Export")
                enabled: d.plan.reducesDepth !== true || depthBox.checked
                onClicked: {
                    d.close()
                    if (d.fullPrint) App.exportFullPrints(d.ids, depthBox.checked)
                    else App.exportImageAreas(d.ids, depthBox.checked)
                }
            }
        }
    }
}
