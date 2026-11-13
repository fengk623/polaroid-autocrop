// Theme tokens — values from docs/ui-reference.md §4 (each token names its row). No literal colours or
// sizes elsewhere in the views.
pragma Singleton
import QtQuick

QtObject {
    id: t
    // "dark" | "light" — set from App.settings.theme (with "system" resolved in Main.qml)
    property bool dark: true

    // §4.2 colours
    readonly property color surfacePanel: dark ? "#2A2B2E" : "#ECEDF0"
    readonly property color surfaceCanvas: dark ? "#5B5C5F" : "#8A8B8E"
    readonly property color surfaceWell: dark ? "#1B1C1F" : "#DCDEE2"
    readonly property color controlFill: dark ? "#3B3C40" : "#FFFFFF"
    readonly property color controlStroke: dark ? "transparent" : "#C4C7CC"
    readonly property color lineSection: dark ? "#38393D" : "#D2D4D9"
    readonly property color lineRegion: dark ? "#17181A" : "#B4B7BD"
    readonly property color textPrimary: dark ? "#F0F1F3" : "#1A1B1E"
    readonly property color textSecondary: dark ? "#C6C8CD" : "#42454B"
    readonly property color textTertiary: dark ? "#9B9EA4" : "#63666D"
    readonly property color accent: dark ? "#35B5A8" : "#0B6F66"
    readonly property color onAccent: dark ? "#0B1A19" : "#FFFFFF"
    readonly property color selectionFill: dark ? "#2C4949" : "#CCDBDD"
    readonly property color hoverOverlay: Qt.rgba(textPrimary.r, textPrimary.g, textPrimary.b, 0.06)
    readonly property color pressedOverlay: Qt.rgba(textPrimary.r, textPrimary.g, textPrimary.b, 0.12)
    readonly property color error: dark ? "#FF8E80" : "#B3261E"
    readonly property color warning: dark ? "#F2C744" : "#8A6100"
    readonly property color badgeFill: Qt.rgba(surfacePanel.r, surfacePanel.g, surfacePanel.b, 0.85)
    readonly property real disabledOpacity: 0.38

    // CONTRACT-ui-core §4 overlay colours (legend only; the canvas draws them in C++)
    readonly property color overlayOuter: "#3D8BFF"
    readonly property color overlayInner: "#F2C744"
    readonly property color overlayManual: "#FF8A3D"
    readonly property color overlayLow: "#FF4D4D"
    readonly property color overlayRelinked: "#38C172"

    // §4.1 regions
    readonly property int railHeader: 40
    readonly property int topBar: 40
    readonly property int filmstrip: 148
    readonly property int leftRailDefault: 254
    readonly property int leftRailMin: 232
    readonly property int leftRailMax: 380
    readonly property int rightRailDefault: 300
    readonly property int rightRailMin: 280
    readonly property int rightRailMax: 420
    readonly property int minWindowW: 1100
    readonly property int minWindowH: 700
    readonly property int edgeStrip: 8        // §4.1 Edge strips (UX-10): drawn width
    readonly property int edgeStripReach: 6   // §4.1 Edge strips: extra hit area over a neighbour's empty inset

    // §4.3 type
    readonly property int fontDialogTitle: 15
    readonly property int fontRailTitle: 13
    readonly property int fontSectionTitle: 12
    readonly property int fontBody: 12
    readonly property int fontCaption: 11
    readonly property int fontBadge: 11

    // §4.4 controls
    readonly property int hit: 28
    readonly property int sectionHeader: 28
    readonly property int rowInset: 14
    readonly property int controlH: 24
    readonly property int rowPitch: 30
    readonly property int numFieldW: 64
    readonly property int comboMinW: 120
    readonly property int buttonH: 28
    readonly property int radius: 4
    readonly property int listRow: 26
    readonly property int padding: 12
    readonly property int gap: 4
    readonly property int clusterGap: 16
    readonly property int badgeH: 20
    readonly property int chipH: 20
    readonly property int toolGlyph: 18
}
