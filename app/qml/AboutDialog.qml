import QtQuick
import QtQuick.Layouts
import QtQuick.Controls.Basic
import FrameKeeper.Core

FkDialog {
    id: d
    title: qsTr("About FrameKeeper")
    contentItem: ColumnLayout {
        spacing: 8
        Image { source: "qrc:/qt/qml/FrameKeeper/resources/icon.png"; sourceSize.width: 64; sourceSize.height: 64; Layout.alignment: Qt.AlignHCenter }
        Text { Layout.alignment: Qt.AlignHCenter; text: "FrameKeeper " + App.version; font.pixelSize: Theme.fontDialogTitle; font.weight: Font.DemiBold; color: Theme.textPrimary }
        Text {
            Layout.fillWidth: true
            wrapMode: Text.WordWrap
            horizontalAlignment: Text.AlignHCenter
            font.pixelSize: Theme.fontBody
            color: Theme.textSecondary
            text: qsTr("Finds instant prints in flatbed scans, exports the image area for grading, and puts the graded image back into the real scanned frame.")
        }
        Text {
            Layout.fillWidth: true
            wrapMode: Text.WordWrap
            horizontalAlignment: Text.AlignHCenter
            font.pixelSize: Theme.fontCaption
            color: Theme.textTertiary
            text: qsTr("Open source (MIT). Uses Qt (LGPL-3.0), libtiff, libpng, libjpeg-turbo, zlib, Little CMS and nlohmann/json — see THIRD-PARTY-LICENSES. Not affiliated with Polaroid or Fujifilm.")
        }
        Text {
            Layout.alignment: Qt.AlignHCenter
            text: "<a href='https://github.com/fengk623/polaroid-autocrop'>github.com/fengk623/polaroid-autocrop</a>"
            font.pixelSize: Theme.fontCaption
            color: Theme.accent
            linkColor: Theme.accent
            onLinkActivated: (l) => Qt.openUrlExternally(l)
        }
        FkButton { Layout.alignment: Qt.AlignRight; primary: true; text: qsTr("OK"); onClicked: d.close() }
    }
}
