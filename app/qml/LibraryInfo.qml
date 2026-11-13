// UX-05 (U020): right rail of the library — the selected print's or scan's information (unknown values say so;
// the capture time is never the scan date) and its rating, pick, colour label and text labels.
import QtQuick
import QtQuick.Layouts
import QtQuick.Controls.Basic
import FrameKeeper.Core

Rectangle {
    id: rail
    objectName: "libraryInfo"
    color: Theme.surfacePanel
    readonly property bool scansList: App.libraryView.show === "scans"
    readonly property string id: scansList ? App.currentScanId : App.currentItemId
    property var info: ({})
    function refresh() { info = id.length ? App.libraryInfo(id) : ({}) }
    onIdChanged: refresh()
    Connections {
        target: App
        function onProjectChanged() { rail.refresh() }
        function onCurrentChanged() { rail.refresh() }
    }
    Component.onCompleted: refresh()
    readonly property string unknown: qsTr("Unknown")
    component Row2: RowLayout {
        property string label
        property string value
        property bool known: value.length > 0
        Layout.fillWidth: true
        spacing: 8
        Text { text: parent.label; Layout.preferredWidth: 96; font.pixelSize: Theme.fontBody; color: Theme.textSecondary; elide: Text.ElideRight }
        Text {
            Layout.fillWidth: true
            text: parent.known ? parent.value : rail.unknown
            font.pixelSize: Theme.fontBody
            color: parent.known ? Theme.textPrimary : Theme.textTertiary
            wrapMode: Text.WrapAnywhere
            maximumLineCount: 3
            elide: Text.ElideRight
        }
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: 0
        Item {
            Layout.fillWidth: true
            implicitHeight: Theme.railHeader
            Text {
                anchors.left: parent.left
                anchors.leftMargin: 12
                anchors.right: parent.right
                anchors.rightMargin: 12
                anchors.verticalCenter: parent.verticalCenter
                text: rail.info.title ? rail.info.title : qsTr("Nothing selected")
                elide: Text.ElideRight
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
                visible: rail.id.length > 0
                Section {
                    title: qsTr("Information")
                    Row2 { label: qsTr("File name"); value: rail.info.fileName || "" }
                    Row2 { label: qsTr("Library"); value: rail.info.library || "" }
                    Row2 { label: qsTr("Copyright"); value: rail.info.copyright || "" }
                    Row2 { label: qsTr("Author"); value: rail.info.artist || "" }
                    Text {
                        visible: !!rail.info.identityHidden && (rail.info.artist || "").length > 0
                        Layout.fillWidth: true
                        wrapMode: Text.WordWrap
                        text: qsTr("Not written to exports: the privacy preset removes identity.")
                        font.pixelSize: Theme.fontCaption
                        color: Theme.textTertiary
                    }
                    Row2 { objectName: "infoCaptured"; label: qsTr("Capture time"); value: rail.info.captured || "" }
                    Row2 { label: qsTr("Scanned"); value: rail.info.scanned || "" }
                    Row2 { label: qsTr("Pixel size"); value: rail.info.pixels || "" }
                    Row2 { label: qsTr("File size"); value: rail.info.fileBytes || "" }
                    Row2 { label: qsTr("DPI"); value: rail.info.dpi || "" }
                    Row2 { visible: rail.info.kind === "scan"; label: qsTr("Prints"); value: rail.info.kind === "scan" ? String(rail.info.prints) : "" }
                    Text {
                        Layout.fillWidth: true
                        wrapMode: Text.WordWrap
                        text: qsTr("The capture time comes only from the photo's own data; the scan date is shown apart.")
                        font.pixelSize: Theme.fontCaption
                        color: Theme.textTertiary
                    }
                }
                Section {
                    title: qsTr("Rating and labels")
                    RowLayout {
                        spacing: 2
                        Repeater {
                            model: 5
                            delegate: Text {
                                required property int index
                                objectName: "star" + (index + 1)
                                text: index < (rail.info.rating || 0) ? "★" : "☆"
                                font.pixelSize: 20
                                color: index < (rail.info.rating || 0) ? Theme.warning : Theme.textTertiary
                                MouseArea {
                                    anchors.fill: parent
                                    enabled: !App.readOnly
                                    cursorShape: Qt.PointingHandCursor
                                    // clicking the current rating again clears it
                                    onClicked: App.libraryApply({ rating: (rail.info.rating === parent.index + 1) ? 0 : parent.index + 1 })
                                }
                            }
                        }
                        Text {
                            text: qsTr("0–5 on the keyboard")
                            font.pixelSize: Theme.fontCaption
                            color: Theme.textTertiary
                            leftPadding: 6
                        }
                    }
                    Segmented {
                        objectName: "infoPick"
                        model: [{ text: qsTr("Picked"), value: "picked", tip: "P" }, { text: qsTr("Unflagged"), value: "none", tip: "U" },
                                { text: qsTr("Rejected"), value: "rejected", tip: "X" }]
                        value: rail.info.pick || "none"
                        enabled: !App.readOnly
                        onActivated: (x) => App.libraryApply({ pick: x })
                    }
                    RowLayout {
                        spacing: 6
                        Repeater {
                            model: [{ c: "none", col: "transparent" }, { c: "red", col: "#E5484D" }, { c: "yellow", col: "#F2C744" },
                                    { c: "green", col: "#38C172" }, { c: "blue", col: "#3D8BFF" }, { c: "purple", col: "#A36EF2" }]
                            delegate: Rectangle {
                                required property var modelData
                                width: 20; height: 20; radius: 10
                                color: modelData.col
                                border.width: rail.info.colorLabel === modelData.c ? 2 : 1
                                border.color: rail.info.colorLabel === modelData.c ? Theme.accent : Theme.lineSection
                                Text { anchors.centerIn: parent; visible: parent.modelData.c === "none"; text: "∅"; font.pixelSize: 11; color: Theme.textTertiary }
                                MouseArea { anchors.fill: parent; enabled: !App.readOnly; onClicked: App.libraryApply({ colorLabel: parent.modelData.c }) }
                            }
                        }
                    }
                    Text { text: qsTr("Text labels (comma-separated)"); font.pixelSize: Theme.fontCaption; color: Theme.textSecondary }
                    TextField {
                        id: labels
                        objectName: "infoLabels"
                        Layout.fillWidth: true
                        enabled: !App.readOnly
                        font.pixelSize: Theme.fontBody
                        color: Theme.textPrimary
                        text: (rail.info.textLabels || []).join(", ")
                        background: Rectangle { radius: Theme.radius; color: Theme.controlFill; border.color: labels.activeFocus ? Theme.accent : Theme.controlStroke; border.width: labels.activeFocus ? 2 : 1 }
                        onEditingFinished: App.libraryApply({ textLabels: text.split(/[,，]/) })
                    }
                }
            }
        }
    }
}
