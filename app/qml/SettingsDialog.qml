// Settings: appearance and language (applies after restart), defaults.
import QtQuick
import QtQuick.Layouts
import QtQuick.Controls.Basic
import FrameKeeper.Core

FkDialog {
    id: d
    title: qsTr("Settings")
    contentItem: ColumnLayout {
        spacing: 8
        FieldRow {
            label: qsTr("Appearance"); labelWidth: 110
            Segmented {
                value: App.settings.theme
                model: [{ text: qsTr("Dark"), value: "dark" }, { text: qsTr("Light"), value: "light" }, { text: qsTr("System"), value: "system" }]
                onActivated: (v) => App.setSetting("theme", v)
            }
        }
        FieldRow {
            label: qsTr("Language"); labelWidth: 110
            FkCombo {
                model: [{ text: qsTr("System"), value: "system" }, { text: "English", value: "en" }, { text: "简体中文", value: "zh_CN" }]
                value: App.settings.language
                onPicked: (v) => App.setSetting("language", v)
            }
            Text { text: qsTr("after restart"); font.pixelSize: Theme.fontCaption; color: Theme.textTertiary }
        }
        FieldRow {
            label: qsTr("Prints per scan"); labelWidth: 110
            FkSpin { from: 1; to: 12; value: App.settings.expectedCount; onValueModified: App.setSetting("expectedCount", value) }
        }
        Text {
            Layout.fillWidth: true
            wrapMode: Text.WordWrap
            font.pixelSize: Theme.fontCaption
            color: Theme.textTertiary
            text: qsTr("FrameKeeper works offline: it never uploads images and sends no usage data.")
        }
        RowLayout {
            Layout.alignment: Qt.AlignRight
            FkButton { primary: true; text: qsTr("Done"); onClicked: d.close() }
        }
    }
}
