// Relink results (RFC-0006 §Relink v2, UX-07): per graded file the candidate prints with their evidence
// (file hash, embedded IDs, file name, picture), the decision (unique → pre-selected; ambiguous/none → the user
// chooses), the needed confirmations, and "assign to another print" which re-checks the file against that print
// (a new alignment is offered when the print has no matching export).
import QtQuick
import QtQuick.Layouts
import QtQuick.Controls.Basic
import FrameKeeper.Core

FkDialog {
    id: d
    property var rows: []
    property var targets: []
    width: Math.min(760, (parent ? parent.width : 900) - 80)
    title: qsTr("Relink graded files")
    function show(r) {
        targets = App.relinkTargets()
        rows = r.map(x => Object.assign({}, x))
        open()
    }
    // a re-check against a chosen print replaces the row of that file
    function updateRow(row) {
        const next = rows.slice()
        for (let i = 0; i < next.length; ++i)
            if (next[i].file === row.file) next[i] = Object.assign({}, row)
        rows = next
    }
    readonly property var choiceKeys: ["itemId", "editVersionId", "label", "scanName", "source", "actions", "score", "signaturePass",
                                       "sizeMatches", "rotate", "depthLower", "ok", "newAlignment", "alignmentKey", "conflict", "alignmentMode",
                                       "rotationReliable", "rotateSource", "allowedRotations", "rotationScores", "gradedW", "gradedH"]
    function choose(index, value) {
        const next = rows.slice()
        const r = Object.assign({}, next[index])
        if (typeof value === "string" && value.startsWith("t:")) {
            App.relinkFiles([r.file], value.substring(2), true)
            return
        }
        const o = (r.options || [])[value]
        if (!o) return
        r.choice = value
        for (const k of choiceKeys) r[k] = o[k]
        r.accept = o.ok === true
        r.message = o.message || ""
        next[index] = r
        rows = next
    }
    // FK-002: the turn chosen by the user is stored with the relink and used by preview and output
    function setRotation(index, deg) {
        const next = rows.slice()
        const r = Object.assign({}, next[index])
        r.rotate = deg
        r.rotateSource = "user"
        r.actions = (r.actions || []).filter(a => a !== "choose_rotation" && a !== "rotate")
        if (deg !== 0) r.actions.push("rotate")
        r.accept = r.ok === true
        next[index] = r
        rows = next
    }
    function indexIn(model, value) {
        for (let i = 0; i < model.length; ++i)
            if (model[i].value === value) return i
        return 0
    }
    function needsOrientation(r) { return (r.actions || []).indexOf("choose_rotation") >= 0 && r.rotateSource !== "user" }
    function orientationModel(r) {
        const m = needsOrientation(r) ? [{ text: qsTr("Choose the orientation…"), value: -1 }] : []
        const scores = r.rotationScores || []
        for (const deg of (r.allowedRotations || []))
            m.push({ text: (deg === 0 ? qsTr("as exported") : qsTr("turned %1°").arg(deg))
                           + " · " + qsTr("picture %1 %").arg(Math.round((scores[deg / 90] || 0) * 100)), value: deg })
        return m
    }
    function actionText(a) {
        return ({ "rotate": qsTr("rotate back"), "scale": qsTr("scale to the exported size"), "accept_lower_depth": qsTr("accept lower bit depth"),
                  "choose_icc": qsTr("no colour profile: assume the exported one"), "confirm_content": qsTr("content differs — confirm it is this print"),
                  "manual_alignment": qsTr("different aspect ratio: cannot be placed"),
                  "choose_rotation": qsTr("orientation unclear: choose it") })[a] || a
    }
    function decisionText(r) {
        if (r.decision === "unique") return r.newAlignment ? qsTr("new alignment") : qsTr("matched")
        if (r.decision === "ambiguous") return qsTr("choose")
        return qsTr("no match")
    }
    function choiceModel(r) {
        const m = [{ text: qsTr("Choose the print…"), value: -1 }]
        const opts = r.options || []
        const inOptions = {}
        for (let i = 0; i < opts.length; ++i) {
            const o = opts[i]
            inOptions[o.itemId] = true
            m.push({ text: o.newAlignment ? qsTr("%1 · %2 · new alignment").arg(o.label).arg(o.scanName)
                                          : qsTr("%1 · %2 · version %3").arg(o.label).arg(o.scanName).arg(o.version),
                     value: i })
        }
        for (const t of d.targets)
            if (!inOptions[t.id]) m.push({ text: qsTr("Assign to %1 · %2").arg(t.label).arg(t.scanName), value: "t:" + t.id })
        return m
    }
    contentItem: ColumnLayout {
        spacing: 8
        ListView {
            Layout.fillWidth: true
            Layout.preferredHeight: Math.min(420, contentHeight)
            clip: true
            model: d.rows
            spacing: 6
            delegate: Rectangle {
                id: rowItem
                required property var modelData
                required property int index
                readonly property var opt: (modelData.options || [])[modelData.choice] || null
                width: ListView.view.width
                height: rcol.implicitHeight + 12
                radius: Theme.radius
                color: Theme.surfaceWell
                ColumnLayout {
                    id: rcol
                    anchors.fill: parent
                    anchors.margins: 6
                    spacing: 3
                    RowLayout {
                        Layout.fillWidth: true
                        spacing: 6
                        FkCheck {
                            enabled: rowItem.opt !== null && rowItem.modelData.ok === true && !d.needsOrientation(rowItem.modelData)
                            checked: rowItem.modelData.accept === true
                            onToggled: d.rows[rowItem.index].accept = checked
                        }
                        Text { Layout.fillWidth: true; text: rowItem.modelData.fileName; elide: Text.ElideMiddle; font.pixelSize: Theme.fontBody; color: Theme.textPrimary }
                        Badge {
                            text: d.decisionText(rowItem.modelData)
                        }
                        FkCombo {
                            Layout.preferredWidth: 260
                            model: d.choiceModel(rowItem.modelData)
                            // index from the model itself (indexOfValue is not re-evaluated when the model changes)
                            currentIndex: d.indexIn(model, rowItem.modelData.choice === undefined ? -1 : rowItem.modelData.choice)
                            onPicked: (v) => { if (v !== -1) d.choose(rowItem.index, v) }
                        }
                    }
                    Text {
                        visible: rowItem.opt !== null
                        Layout.fillWidth: true
                        wrapMode: Text.WordWrap
                        font.pixelSize: Theme.fontCaption
                        color: Theme.textSecondary
                        text: rowItem.opt ? rowItem.opt.evidence.map(e => (e.supports ? "✓ " : "✗ ") + e.text).join(" · ") : ""
                    }
                    RowLayout {
                        // orientation of the graded file (FK-002): shown when more than one turn fits its size
                        visible: rowItem.opt !== null && (rowItem.modelData.allowedRotations || []).length > 1
                        spacing: 6
                        Text {
                            text: qsTr("Orientation")
                            font.pixelSize: Theme.fontCaption
                            color: Theme.textSecondary
                        }
                        FkCombo {
                            Layout.preferredWidth: 240
                            model: d.orientationModel(rowItem.modelData)
                            currentIndex: d.indexIn(model, d.needsOrientation(rowItem.modelData) ? -1 : rowItem.modelData.rotate)
                            onPicked: (v) => { if (v !== -1) d.setRotation(rowItem.index, v) }
                        }
                    }
                    Text {
                        // not decided: what speaks for each candidate, so the choice is informed
                        visible: rowItem.opt === null && (rowItem.modelData.options || []).length > 0
                        Layout.fillWidth: true
                        wrapMode: Text.WordWrap
                        font.pixelSize: Theme.fontCaption
                        color: Theme.textSecondary
                        text: (rowItem.modelData.options || []).map(o => qsTr("%1 · %2: %3").arg(o.label).arg(o.scanName)
                                  .arg(o.evidence.map(e => (e.supports ? "✓ " : "✗ ") + e.text).join(", "))).join("\n")
                    }
                    Text {
                        visible: rowItem.opt !== null && (rowItem.modelData.actions || []).length > 0
                        Layout.fillWidth: true
                        wrapMode: Text.WordWrap
                        font.pixelSize: Theme.fontCaption
                        color: Theme.textSecondary
                        text: qsTr("needs: %1").arg((rowItem.modelData.actions || []).map(d.actionText).join(", "))
                    }
                    Text {
                        visible: (rowItem.modelData.message || "").length > 0
                        Layout.fillWidth: true
                        wrapMode: Text.WordWrap
                        font.pixelSize: Theme.fontCaption
                        color: rowItem.modelData.conflict || rowItem.modelData.decision === "none" || (rowItem.opt && !rowItem.modelData.ok)
                               ? Theme.error : Theme.warning
                        text: rowItem.modelData.message || ""
                    }
                }
            }
        }
        Text {
            Layout.fillWidth: true
            wrapMode: Text.WordWrap
            font.pixelSize: Theme.fontCaption
            color: Theme.textTertiary
            text: qsTr("Checked files are attached to their print. The file name or a similar picture never outweighs contradicting evidence; when they disagree you choose. Checking a file also confirms its orientation and size adjustment; both are stored with the relink and used for the preview and the output.")
        }
        RowLayout {
            Layout.alignment: Qt.AlignRight
            spacing: 8
            FkButton { text: qsTr("Cancel"); onClicked: d.close() }
            FkButton { primary: true; text: qsTr("Relink"); onClicked: { App.applyRelink(d.rows); d.close() } }
        }
    }
}
