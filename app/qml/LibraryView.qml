// UX-05 (U020): inside a library — the prints or the scans as a grid or one at a time, sorted by any of the
// fourteen keys (either direction), filtered by rating, pick, colour and text. Only the cells on screen exist
// and only they ask for thumbnails (FK-005). Double-click a print to edit it; double-click a scan for its prints.
import QtQuick
import QtQuick.Layouts
import QtQuick.Controls.Basic
import FrameKeeper.Core

Rectangle {
    id: view
    objectName: "libraryView"
    required property var cmd
    color: Theme.surfaceWell
    readonly property var v: App.libraryView
    readonly property bool single: v.mode === "single"
    readonly property bool scansList: v.show === "scans"
    readonly property string currentId: scansList ? App.currentScanId : App.currentItemId
    property int rev: 0 // the single view's picture is read again when its scan's images change
    Connections {
        target: App
        function onScanImageChanged(id) { view.rev++ }
    }
    function focusGrid() { if (!single) grid.forceActiveFocus(); else singleArea.forceActiveFocus() }
    onVisibleChanged: if (visible) Qt.callLater(focusGrid)
    onSingleChanged: if (visible) Qt.callLater(focusGrid) // keys follow the view that is shown (once it is)

    ColumnLayout {
        anchors.fill: parent
        spacing: 0
        // ---- toolbar: view, list, sort
        Rectangle {
            Layout.fillWidth: true
            implicitHeight: 40
            color: Theme.surfacePanel
            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: 10
                anchors.rightMargin: 10
                spacing: 8
                Segmented {
                    objectName: "libraryMode"
                    model: [{ text: qsTr("Grid"), value: "grid", tip: qsTr("Grid (G)") },
                            { text: qsTr("Single"), value: "single", tip: qsTr("Single (E)") }]
                    value: view.v.mode
                    onActivated: (x) => App.setLibraryView("mode", x)
                }
                Segmented {
                    objectName: "libraryShow"
                    model: [{ text: qsTr("Prints %1").arg(view.v.prints), value: "prints" },
                            { text: qsTr("Scans %1").arg(view.v.scans), value: "scans" }]
                    value: view.v.show
                    onActivated: (x) => App.setLibraryView("show", x)
                }
                Item { Layout.fillWidth: true }
                Text { text: qsTr("Sort"); font.pixelSize: Theme.fontBody; color: Theme.textSecondary }
                FkCombo {
                    objectName: "librarySort"
                    model: App.librarySortOptions()
                    value: view.v.sort
                    onPicked: (x) => App.setLibraryView("sort", x)
                }
                ToolBtn {
                    objectName: "librarySortDirection"
                    iconName: view.v.descending ? "chevronDown" : "chevronUp"
                    tip: view.v.descending ? qsTr("Descending") : qsTr("Ascending")
                    onClicked: App.setLibraryView("descending", !view.v.descending)
                }
            }
        }
        // ---- filter
        Rectangle {
            Layout.fillWidth: true
            implicitHeight: 36
            color: Theme.surfacePanel
            Rectangle { anchors.top: parent.top; width: parent.width; height: 1; color: Theme.lineSection }
            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: 10
                anchors.rightMargin: 10
                spacing: 8
                Text { text: qsTr("Show"); font.pixelSize: Theme.fontBody; color: Theme.textSecondary }
                FkCombo {
                    objectName: "libraryMinRating"
                    model: [{ text: qsTr("Any rating"), value: 0 }, { text: "★ 1+", value: 1 }, { text: "★ 2+", value: 2 },
                            { text: "★ 3+", value: 3 }, { text: "★ 4+", value: 4 }, { text: "★ 5", value: 5 }]
                    value: view.v.minRating
                    onPicked: (x) => App.setLibraryView("minRating", x)
                }
                FkCombo {
                    objectName: "libraryPick"
                    model: [{ text: qsTr("Any pick"), value: "any" }, { text: qsTr("Picked"), value: "picked" },
                            { text: qsTr("Unflagged"), value: "unflagged" }, { text: qsTr("Rejected"), value: "rejected" }]
                    value: view.v.pick
                    onPicked: (x) => App.setLibraryView("pick", x)
                }
                FkCombo {
                    objectName: "libraryColor"
                    model: [{ text: qsTr("Any colour"), value: "any" }, { text: qsTr("No colour"), value: "none" },
                            { text: qsTr("Red"), value: "red" }, { text: qsTr("Yellow"), value: "yellow" },
                            { text: qsTr("Green"), value: "green" }, { text: qsTr("Blue"), value: "blue" },
                            { text: qsTr("Purple"), value: "purple" }]
                    value: view.v.color
                    onPicked: (x) => App.setLibraryView("color", x)
                }
                TextField {
                    id: search
                    objectName: "librarySearch"
                    Layout.fillWidth: true
                    Layout.minimumWidth: 60
                    Layout.maximumWidth: 240
                    placeholderText: qsTr("File name or label")
                    placeholderTextColor: Theme.textTertiary
                    font.pixelSize: Theme.fontBody
                    color: Theme.textPrimary
                    text: view.v.text
                    background: Rectangle { radius: Theme.radius; color: Theme.controlFill; border.color: search.activeFocus ? Theme.accent : Theme.controlStroke; border.width: search.activeFocus ? 2 : 1 }
                    onTextEdited: App.setLibraryView("text", text)
                    Keys.onEscapePressed: { text = ""; App.setLibraryView("text", ""); view.focusGrid() }
                }
                Text {
                    visible: view.width > 720
                    text: qsTr("%1 shown").arg(view.v.shown)
                    font.pixelSize: Theme.fontCaption
                    color: Theme.textTertiary
                }
            }
        }
        // ---- what the list is limited to (one scan's prints, one folder), each with its way back
        Rectangle {
            visible: (!view.scansList && view.v.scanId.length > 0) || view.v.folder.length > 0
            Layout.fillWidth: true
            implicitHeight: 32
            color: Theme.surfacePanel
            Rectangle { anchors.top: parent.top; width: parent.width; height: 1; color: Theme.lineSection }
            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: 10
                anchors.rightMargin: 10
                spacing: 6
                Text {
                    visible: !view.scansList && view.v.scanId.length > 0
                    Layout.maximumWidth: (parent.width - 80) / 2
                    text: qsTr("Prints of %1").arg(view.v.scanName)
                    elide: Text.ElideMiddle
                    font.pixelSize: Theme.fontBody
                    color: Theme.textPrimary
                }
                ToolBtn {
                    visible: !view.scansList && view.v.scanId.length > 0
                    iconName: "close"; tip: qsTr("All prints")
                    onClicked: App.setLibraryView("scanId", "")
                }
                Text {
                    visible: view.v.folder.length > 0
                    Layout.maximumWidth: (parent.width - 80) / 2
                    text: qsTr("Folder: %1").arg(view.v.folder.replace(/^.*[\\/]/, ""))
                    elide: Text.ElideMiddle
                    font.pixelSize: Theme.fontBody
                    color: Theme.textPrimary
                }
                ToolBtn {
                    visible: view.v.folder.length > 0
                    iconName: "close"; tip: qsTr("All folders")
                    onClicked: App.setLibraryView("folder", "")
                }
                Item { Layout.fillWidth: true }
            }
        }
        // ---- the grid (only the cells on screen exist)
        GridView {
            id: grid
            objectName: "libraryGrid"
            visible: !view.single
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            focus: true
            cacheBuffer: 0
            cellWidth: 176
            cellHeight: 204
            model: App.libraryGrid
            boundsBehavior: Flickable.StopAtBounds
            keyNavigationEnabled: true
            // re-read whenever the list is rebuilt (a sort, a filter, a new rating): an index is only valid in
            // the order it was taken from
            currentIndex: { App.libraryView; return App.libraryGrid.indexOf(view.currentId) }
            onCurrentIndexChanged: {
                const id = App.libraryGrid.idAt(currentIndex)
                if (id.length && id !== view.currentId && activeFocus) App.librarySelect(id)
            }
            ScrollBar.vertical: ScrollBar { policy: ScrollBar.AsNeeded }
            Keys.onReturnPressed: view.open(App.libraryGrid.idAt(currentIndex), view.scansList ? "scan" : "print")
            delegate: LibraryCell {
                cmd: view.cmd
                width: grid.cellWidth
                height: grid.cellHeight
                onActivate: (id, kind) => view.open(id, kind)
                onSelectRequested: (id, mods) => {
                    grid.forceActiveFocus()
                    App.librarySelect(id, (mods & Qt.ShiftModifier) !== 0, (mods & (Qt.ControlModifier | Qt.MetaModifier)) !== 0)
                }
            }
            Text {
                anchors.centerIn: parent
                visible: grid.count === 0
                width: Math.min(parent.width - 40, 460)
                horizontalAlignment: Text.AlignHCenter
                wrapMode: Text.WordWrap
                text: view.v.prints + view.v.scans === 0 ? qsTr("This library is empty. Import scans with the button on the left or drop TIFF files here.")
                                                         : qsTr("Nothing matches the filter.")
                font.pixelSize: Theme.fontBody
                color: Theme.textSecondary
            }
        }
        // ---- one at a time
        Item {
            id: singleArea
            objectName: "librarySingle"
            visible: view.single
            Layout.fillWidth: true
            Layout.fillHeight: true
            focus: view.single
            readonly property int index: { App.libraryView; return App.libraryGrid.indexOf(view.currentId) }
            Keys.onLeftPressed: step(-1)
            Keys.onRightPressed: step(1)
            Keys.onReturnPressed: view.open(view.currentId, view.scansList ? "scan" : "print")
            function step(d) {
                const id = App.libraryGrid.idAt(App.libraryGrid.indexOf(view.currentId) + d)
                if (id.length) App.librarySelect(id)
            }
            onVisibleChanged: if (visible && view.currentId.length) App.libraryRequestPreview(view.scansList ? view.currentId : App.currentScanId)
            Connections {
                target: App
                function onSelectionChanged() { if (singleArea.visible) App.libraryRequestPreview(App.currentScanId) }
            }
            Image {
                anchors.fill: parent
                anchors.margins: 24
                fillMode: Image.PreserveAspectFit
                asynchronous: true
                cache: false
                smooth: true
                source: view.currentId.length === 0 ? ""
                        : view.scansList ? "image://fk/scan/" + view.currentId + "?" + view.rev
                        : "image://fk/print/" + view.currentId + "?" + view.rev
            }
            Text {
                anchors.centerIn: parent
                visible: view.currentId.length === 0
                text: qsTr("Select a print in the grid.")
                font.pixelSize: Theme.fontBody
                color: Theme.textSecondary
            }
            RowLayout {
                anchors.bottom: parent.bottom
                anchors.horizontalCenter: parent.horizontalCenter
                anchors.bottomMargin: 10
                FkButton { text: "‹"; enabled: singleArea.index > 0; onClicked: singleArea.step(-1) }
                Text {
                    text: singleArea.index >= 0 ? qsTr("%1 of %2").arg(singleArea.index + 1).arg(App.libraryGrid.count) : ""
                    font.pixelSize: Theme.fontCaption
                    color: Theme.textSecondary
                }
                FkButton { text: "›"; enabled: singleArea.index >= 0 && singleArea.index < App.libraryGrid.count - 1; onClicked: singleArea.step(1) }
            }
        }
    }
    /// double-click / Enter: a print opens in Edit Photos (same selection); a scan shows its prints
    function open(id, kind) {
        if (!id || !id.length) return
        if (kind === "scan") {
            App.librarySelect(id)
            App.setLibraryView("show", "prints")
            App.setLibraryView("scanId", id)
            return
        }
        App.librarySelect(id)
        App.setSetting("workspace", "edit")
    }
}
