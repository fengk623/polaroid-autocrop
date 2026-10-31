// Numeric field 64×24 with unit suffix; arrows step (Shift ×10); commits on Enter / focus loss.
// `scrubbable` (UX-09, opt-in per field — wider use needs the consistency review UX-09 asks for): press and drag
// left/right to change the value (2 logical px per step; Shift ×10, Alt ×0.1), past a 4 px threshold so a click is
// never a drag; the value shows live and `scrubbed` reports it at most every 100 ms; release = `scrubFinished`
// (one undo step), Esc = `scrubCancelled`. A double-click (or Enter with the focus) opens keyboard input: Enter or
// focus loss commits, Esc cancels, invalid characters are refused by the validator.
import QtQuick
import QtQuick.Controls.Basic

TextField {
    id: f
    property real value: 0
    property real step: 1
    property int decimals: 1
    property real from: -1e9
    property real to: 1e9
    property string unit: ""
    property bool scrubbable: false
    property real pixelsPerStep: 2
    readonly property int dragThreshold: 4
    property bool editing: !scrubbable
    property bool scrubbing: false
    property real liveValue: value
    signal committed(real value)
    signal scrubStarted()
    signal scrubbed(real value)
    signal scrubFinished(real value)
    signal scrubCancelled()
    implicitWidth: Theme.numFieldW + (unit.length ? 16 : 0)
    implicitHeight: Theme.controlH
    font.pixelSize: Theme.fontBody
    font.features: { "tnum": 1 }
    color: Theme.textPrimary
    selectByMouse: !scrubbable || editing
    readOnly: scrubbable && !editing
    horizontalAlignment: TextInput.AlignRight
    rightPadding: unit.length ? 22 : 6
    leftPadding: 6
    topPadding: 0
    bottomPadding: 0
    validator: DoubleValidator { bottom: f.from; top: f.to; decimals: f.decimals; notation: DoubleValidator.StandardNotation }
    text: Number(scrubbing ? liveValue : value).toFixed(decimals)
    function clamp(v) { return Math.min(f.to, Math.max(f.from, v)) }
    function showValue() { text = Qt.binding(function() { return Number(f.scrubbing ? f.liveValue : f.value).toFixed(f.decimals) }) }
    function startEditing() {
        editing = true
        forceActiveFocus()
        selectAll()
    }
    onEditingFinished: {
        if (scrubbable && !editing) return // read-only display: nothing typed
        // U029 (freeze C04): the field shows the value rounded to `decimals`; leaving it without changing the
        // text must not commit that rounded value (it moved a corner and marked the boundary edited)
        if (text === Number(f.value).toFixed(f.decimals)) {
            showValue()
            if (scrubbable) editing = false
            return
        }
        const v = Number(text.replace(",", "."))
        if (text.length > 0 && !isNaN(v)) f.committed(clamp(v))
        showValue()
        if (scrubbable) editing = false
    }
    onActiveFocusChanged: if (!activeFocus && scrubbing) cancelScrub()
    function cancelScrub() {
        scrubbing = false
        throttle.stop()
        liveValue = value
        scrubCancelled()
    }
    Keys.onUpPressed: (e) => { f.committed(clamp(f.value + f.step * (e.modifiers & Qt.ShiftModifier ? 10 : 1))) }
    Keys.onDownPressed: (e) => { f.committed(clamp(f.value - f.step * (e.modifiers & Qt.ShiftModifier ? 10 : 1))) }
    Keys.onEscapePressed: (e) => {
        if (f.scrubbing) {
            f.cancelScrub()
        } else if (f.scrubbable && f.editing) {
            f.editing = false // keyboard input cancelled: the value is unchanged
            f.showValue()
        } else {
            e.accepted = false
            return
        }
        e.accepted = true
    }
    Keys.onReturnPressed: (e) => { if (f.scrubbable && !f.editing) { f.startEditing(); e.accepted = true } else e.accepted = false }
    Keys.onEnterPressed: (e) => { if (f.scrubbable && !f.editing) { f.startEditing(); e.accepted = true } else e.accepted = false }

    Timer {
        id: throttle
        interval: 100 // live updates at most every 100 ms: no backlog of previews while dragging
        onTriggered: f.scrubbed(f.liveValue)
    }
    MouseArea {
        id: scrub
        objectName: "scrubArea"
        anchors.fill: parent
        enabled: f.scrubbable && !f.editing && f.enabled
        cursorShape: Qt.SizeHorCursor
        preventStealing: true
        property real x0: 0
        property real v0: 0
        onPressed: (m) => {
            f.forceActiveFocus()
            x0 = m.x
            v0 = f.value
        }
        onPositionChanged: (m) => {
            const dx = m.x - x0
            if (!f.scrubbing) {
                if (Math.abs(dx) < f.dragThreshold) return
                f.liveValue = v0
                f.scrubbing = true
                f.scrubStarted()
            }
            const mult = (m.modifiers & Qt.ShiftModifier) ? 10 : (m.modifiers & Qt.AltModifier) ? 0.1 : 1
            const steps = Math.round(dx / f.pixelsPerStep)
            const v = Number(f.clamp(v0 + steps * f.step * mult).toFixed(f.decimals))
            if (v !== f.liveValue) {
                f.liveValue = v
                if (!throttle.running) throttle.start()
            }
        }
        onReleased: {
            if (!f.scrubbing) return // a click: nothing changes
            throttle.stop()
            const v = f.liveValue
            f.scrubFinished(v)
            f.scrubbing = false
        }
        onCanceled: if (f.scrubbing) f.cancelScrub()
        onDoubleClicked: f.startEditing()
    }

    background: Rectangle {
        radius: Theme.radius
        color: Theme.controlFill
        border.width: f.activeFocus ? 2 : (Theme.dark ? 0 : 1)
        border.color: f.activeFocus ? (f.editing || !f.scrubbable ? Theme.accent : Theme.textTertiary) : Theme.controlStroke
        Text {
            visible: f.unit.length > 0
            text: f.unit
            anchors.right: parent.right
            anchors.rightMargin: 6
            anchors.verticalCenter: parent.verticalCenter
            font.pixelSize: Theme.fontCaption
            color: Theme.textTertiary
        }
    }
}
