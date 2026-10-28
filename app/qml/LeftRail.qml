// Left rail "Project": what and which (ui-reference §2.2): Project & Files · Scans · Detection ·
// Templates · Batch · Jobs.
import QtQuick
import QtQuick.Layouts
import QtQuick.Controls.Basic
import FrameKeeper.Core

Rectangle {
    id: rail
    required property var cmd
    color: Theme.surfacePanel

    ColumnLayout {
        anchors.fill: parent
        spacing: 0
        // header
        Item {
            Layout.fillWidth: true
            implicitHeight: Theme.railHeader
            Text {
                anchors.left: parent.left
                anchors.leftMargin: 12
                anchors.verticalCenter: parent.verticalCenter
                text: qsTr("Project")
                font.pixelSize: Theme.fontRailTitle
                font.weight: Font.DemiBold
                color: Theme.textPrimary
            }
        }
        Flickable {
            Layout.fillWidth: true
            Layout.fillHeight: true
            contentHeight: col.implicitHeight
            clip: true
            boundsBehavior: Flickable.StopAtBounds
            ScrollBar.vertical: ScrollBar { policy: ScrollBar.AsNeeded }
            ColumnLayout {
                id: col
                width: rail.width
                spacing: 0

                Section {
                    title: qsTr("Project & Files")
                    note: App.dirty ? qsTr("unsaved") : ""
                    Text {
                        Layout.fillWidth: true
                        text: App.projectName + (App.readOnly ? "  " + qsTr("(read-only)") : "")
                        font.pixelSize: Theme.fontBody
                        color: Theme.textPrimary
                        elide: Text.ElideRight
                    }
                    Text {
                        visible: App.projectPath.length > 0
                        Layout.fillWidth: true
                        text: App.projectPath
                        font.pixelSize: Theme.fontCaption
                        color: Theme.textTertiary
                        elide: Text.ElideMiddle
                    }
                    RowLayout {
                        spacing: 6
                        FkButton { text: qsTr("Import…"); iconName: "import"; onClicked: cmd.importScans() }
                        FkButton { text: qsTr("Folder…"); iconName: "folder"; onClicked: cmd.importFolder() }
                    }
                    RowLayout {
                        spacing: 6
                        FkButton { text: qsTr("Open…"); onClicked: cmd.openProject() }
                        FkButton { text: qsTr("Save"); iconName: "save"; enabled: !App.readOnly; onClicked: cmd.save() }
                    }
                }

                Section {
                    id: scansSec
                    title: qsTr("Scans")
                    note: App.counts.scans > 0 ? qsTr("%1 scans · %2 prints").arg(App.counts.scans).arg(App.counts.prints) : ""
                    Text {
                        visible: App.scans.count === 0
                        Layout.fillWidth: true
                        wrapMode: Text.WordWrap
                        text: qsTr("Drop TIFF scans here or use Import. Scans are never modified.")
                        font.pixelSize: Theme.fontCaption
                        color: Theme.textTertiary
                    }
                    ListView {
                        id: scanList
                        visible: App.scans.count > 0
                        Layout.fillWidth: true
                        Layout.leftMargin: -Theme.rowInset
                        Layout.rightMargin: -Theme.rowInset
                        implicitHeight: Math.min(Math.max(52, count * 52), 312)
                        clip: true
                        model: App.scans
                        boundsBehavior: Flickable.StopAtBounds
                        ScrollBar.vertical: ScrollBar {}
                        delegate: AbstractButton {
                            id: row
                            required property string scanId
                            required property string name
                            required property string path
                            required property string sizeText
                            required property string depthText
                            required property string state
                            required property string error
                            required property string thumb
                            required property int itemCount
                            required property int needReview
                            required property bool current
                            width: ListView.view.width
                            height: 52
                            hoverEnabled: true
                            Accessible.name: name
                            onClicked: App.selectScan(scanId)
                            background: Rectangle {
                                color: row.current ? Theme.selectionFill : (row.hovered ? Theme.hoverOverlay : "transparent")
                                Rectangle { visible: row.current; width: 3; height: parent.height; color: Theme.accent }
                            }
                            contentItem: RowLayout {
                                spacing: 8
                                Item { width: 6 }
                                Rectangle {
                                    Layout.preferredWidth: 40
                                    Layout.preferredHeight: 40
                                    color: Theme.surfaceWell
                                    Image {
                                        anchors.fill: parent
                                        source: row.thumb
                                        fillMode: Image.PreserveAspectFit
                                        cache: false
                                        asynchronous: false
                                    }
                                }
                                ColumnLayout {
                                    Layout.fillWidth: true
                                    spacing: 1
                                    Text {
                                        Layout.fillWidth: true
                                        text: row.name
                                        font.pixelSize: Theme.fontBody
                                        color: Theme.textPrimary
                                        elide: Text.ElideMiddle
                                    }
                                    Text {
                                        Layout.fillWidth: true
                                        font.pixelSize: Theme.fontCaption
                                        color: row.state === "error" || row.state === "missing" ? Theme.error
                                               : (row.current ? Theme.textSecondary : Theme.textTertiary)
                                        elide: Text.ElideRight
                                        text: row.state === "detecting" ? qsTr("Detecting…")
                                              : row.state === "loading" ? qsTr("Opening…")
                                              : row.state === "missing" ? qsTr("Missing — Relocate…")
                                              : row.state === "error" ? row.error
                                              : row.sizeText + " · " + row.depthText + " · "
                                                + qsTr("%n print(s)", "", row.itemCount)
                                                + (row.needReview > 0 ? " · " + qsTr("%1 to review").arg(row.needReview) : "")
                                    }
                                }
                                ToolBtn {
                                    iconName: "close"
                                    tip: qsTr("Remove from project (the file is not touched)")
                                    implicitWidth: 24; implicitHeight: 24
                                    visible: row.hovered
                                    onClicked: App.removeScan(row.scanId)
                                }
                                Item { width: 4 }
                            }
                            TapHandler {
                                acceptedButtons: Qt.RightButton
                                onTapped: scanMenu.popup()
                            }
                            Menu {
                                id: scanMenu
                                MenuItem { text: qsTr("Detect again"); onTriggered: cmd.detectAgain(row.scanId) }
                                MenuItem { text: App.platform === "macos" ? qsTr("Reveal in Finder") : qsTr("Show in Explorer"); onTriggered: App.revealInFileManager(row.path) }
                                MenuItem { text: qsTr("Remove from project"); onTriggered: App.removeScan(row.scanId) }
                            }
                        }
                    }
                    FkButton {
                        visible: App.scans.count > 0 && App.currentScan.state === "missing"
                        text: qsTr("Relocate…")
                        onClicked: cmd.relocate()
                    }
                }

                Section {
                    title: qsTr("Detection")
                    note: App.currentScan.dpiUsed > 0 ? qsTr("%1 dpi").arg(Math.round(App.currentScan.dpiUsed)) : ""
                    FieldRow {
                        label: qsTr("Prints per scan")
                        FkSpin {
                            from: 1; to: 12
                            value: App.settings.expectedCount
                            onValueModified: App.setSetting("expectedCount", value)
                        }
                    }
                    FieldRow {
                        label: qsTr("Method")
                        FkCombo {
                            id: methodCombo
                            Layout.fillWidth: true
                            model: [{ text: qsTr("Automatic"), value: "" }].concat(App.templates.map(t => ({ text: qsTr("Template: %1").arg(t), value: t })))
                            value: ""
                        }
                    }
                    Repeater {
                        model: App.currentScan.warnings || []
                        Text {
                            required property string modelData
                            Layout.fillWidth: true
                            wrapMode: Text.WordWrap
                            text: "⚠ " + cmd.warningText(modelData)
                            font.pixelSize: Theme.fontCaption
                            color: Theme.warning
                        }
                    }
                    RowLayout {
                        spacing: 6
                        FkButton {
                            text: qsTr("Detect again")
                            iconName: "detect"
                            enabled: App.currentScanId.length > 0
                            onClicked: cmd.detectAgain(App.currentScanId, methodCombo.currentValue)
                        }
                        FkButton {
                            text: qsTr("All scans")
                            enabled: App.counts.scans > 1
                            onClicked: cmd.detectAll(methodCombo.currentValue)
                        }
                    }
                }

                Section {
                    title: qsTr("Templates")
                    expanded: false
                    note: App.templates.length ? App.templates.length.toString() : ""
                    Text {
                        Layout.fillWidth: true
                        wrapMode: Text.WordWrap
                        text: qsTr("A template stores the print positions of a scanning jig and reuses them for later scans.")
                        font.pixelSize: Theme.fontCaption
                        color: Theme.textTertiary
                    }
                    Repeater {
                        model: App.templates
                        RowLayout {
                            required property string modelData
                            Layout.fillWidth: true
                            Text { text: modelData; font.pixelSize: Theme.fontBody; color: Theme.textPrimary; Layout.fillWidth: true; elide: Text.ElideRight }
                            ToolBtn { iconName: "trash"; tip: qsTr("Delete template"); implicitWidth: 24; implicitHeight: 24; onClicked: App.deleteTemplate(modelData) }
                        }
                    }
                    FkButton {
                        text: qsTr("Save current layout…")
                        enabled: App.currentScan.itemCount > 0
                        onClicked: cmd.saveTemplate()
                    }
                }

                Section {
                    title: qsTr("Batch")
                    note: qsTr("%1 prints · %2 excluded · %3 to review").arg(App.counts.prints).arg(App.counts.excluded).arg(App.counts.needReview)
                    FkButton { text: qsTr("Export all image areas…"); iconName: "export"; enabled: App.counts.prints > 0; onClicked: cmd.exportImageAreas([]) }
                    FkButton { text: qsTr("Relink graded files…"); iconName: "relink"; enabled: App.counts.exported > 0; onClicked: cmd.relink() }
                    FkButton { text: qsTr("Export all finals…"); iconName: "frame"; enabled: App.counts.relinked > 0; onClicked: cmd.exportFinal([]) }
                    Text {
                        Layout.fillWidth: true
                        wrapMode: Text.WordWrap
                        font.pixelSize: Theme.fontCaption
                        color: Theme.textTertiary
                        text: qsTr("Exported %1 · relinked %2 · final %3").arg(App.counts.exported).arg(App.counts.relinked).arg(App.counts.finals)
                    }
                }

                Section {
                    title: qsTr("Jobs")
                    note: App.jobs.count > 0 ? App.jobs.count.toString() : ""
                    Text {
                        visible: App.jobs.count === 0
                        text: qsTr("No jobs")
                        font.pixelSize: Theme.fontCaption
                        color: Theme.textTertiary
                    }
                    Repeater {
                        model: App.jobs
                        ColumnLayout {
                            required property int jobId
                            required property string title
                            required property real progress
                            required property string state
                            required property real elapsed
                            Layout.fillWidth: true
                            spacing: 2
                            RowLayout {
                                Layout.fillWidth: true
                                Text {
                                    Layout.fillWidth: true
                                    text: title
                                    elide: Text.ElideRight
                                    font.pixelSize: Theme.fontCaption
                                    color: state === "running" ? Theme.textPrimary : Theme.textTertiary
                                }
                                Text {
                                    text: state === "running" ? Math.round(progress * 100) + " %"
                                          : state === "cancelled" ? qsTr("cancelled") : qsTr("done %1 s").arg(elapsed.toFixed(1))
                                    font.pixelSize: Theme.fontCaption
                                    font.features: { "tnum": 1 }
                                    color: Theme.textTertiary
                                }
                                ToolBtn {
                                    visible: state === "running"
                                    iconName: "close"; tip: qsTr("Cancel")
                                    implicitWidth: 22; implicitHeight: 22
                                    onClicked: cmd.cancelJob(jobId)
                                }
                            }
                            Rectangle {
                                visible: state === "running"
                                Layout.fillWidth: true
                                height: 3
                                color: Theme.surfaceWell
                                Rectangle { width: parent.width * progress; height: 3; color: Theme.accent }
                            }
                        }
                    }
                    FkButton { visible: App.jobs.count > 0; text: qsTr("Clear finished"); onClicked: App.jobs.clearFinished() }
                }
                Item { Layout.fillHeight: true }
            }
        }
    }
}
