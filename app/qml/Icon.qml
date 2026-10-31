// Our own line icon set (24-unit grid, drawn with Qt Quick Shapes; nothing copied from the references).
import QtQuick
import QtQuick.Shapes

Item {
    id: root
    property string name: ""
    property color color: Theme.textPrimary
    property real size: Theme.toolGlyph
    property real stroke: 1.6
    implicitWidth: size
    implicitHeight: size

    readonly property var paths: ({
        "import": "M12 3 V14 M7.5 9.5 L12 14 L16.5 9.5 M4 15 V20 H20 V15",
        "export": "M12 14 V3 M7.5 7.5 L12 3 L16.5 7.5 M4 15 V20 H20 V15",
        "relink": "M10 14 L14 10 M8.5 11.5 L6.3 13.7 A3.2 3.2 0 0 0 10.8 18.2 L13 16 M11 8 L13.2 5.8 A3.2 3.2 0 0 1 17.7 10.3 L15.5 12.5",
        "select": "M6 3 L18 13 L12.5 13.5 L15.5 20 L13 21 L10 14.5 L6 18 Z",
        "hand": "M8 12 V6.5 A1.5 1.5 0 0 1 11 6.5 V11 M11 10 V4.5 A1.5 1.5 0 0 1 14 4.5 V11 M14 10 V5.5 A1.5 1.5 0 0 1 17 5.5 V13 C17 18 14.5 21 11 21 C8 21 6.5 19 5 15.5 L4.2 13.2 A1.4 1.4 0 0 1 6.8 12.2 L8 14.5",
        "outer": "M4 8 V4 H8 M16 4 H20 V8 M20 16 V20 H16 M8 20 H4 V16 M4 4 L4 20 L20 20 L20 4 Z",
        "inner": "M3 3 H21 V21 H3 Z M7 6 H17 V15 H7 Z M7 6 L9 8 M17 6 L15 8 M17 15 L15 13 M7 15 L9 13",
        "foldLeft": "M3 4 H21 V20 H3 Z M9 4 V20 M6 8 H6.5 M6 11 H6.5",
        "foldRight": "M3 4 H21 V20 H3 Z M15 4 V20 M17.5 8 H18 M17.5 11 H18",
        "zoomIn": "M12 5 V19 M5 12 H19",
        "zoomOut": "M5 12 H19",
        "compare": "M12 3 V21 M4 5 H20 V19 H4 Z M8 12 L10 10 M8 12 L10 14 M16 12 L14 10 M16 12 L14 14",
        "eye": "M2.5 12 C5 7 8.5 5 12 5 C15.5 5 19 7 21.5 12 C19 17 15.5 19 12 19 C8.5 19 5 17 2.5 12 Z M12 9 A3 3 0 1 0 12.01 9",
        "check": "M5 12.5 L10 17.5 L19 7",
        "checks": "M2.5 12.5 L7 17 L15.5 7.5 M10 15 L12 17 L21 7.5",
        "warning": "M12 3.5 L21.5 20 H2.5 Z M12 9.5 V14.5 M12 17 V17.6",
        "link": "M9.5 14.5 L14.5 9.5 M8 11.5 L6 13.5 A3.4 3.4 0 0 0 10.5 18 L12.5 16 M11.5 8 L13.5 6 A3.4 3.4 0 0 1 18 10.5 L16 12.5",
        "excluded": "M12 3.5 A8.5 8.5 0 1 0 12.01 3.5 M6 18 L18 6",
        "grid": "M4 4 H10 V10 H4 Z M14 4 H20 V10 H14 Z M4 14 H10 V20 H4 Z M14 14 H20 V20 H14 Z",
        "pencil": "M4 20 L5 15.5 L15.5 5 A2.1 2.1 0 0 1 19 8.5 L8.5 19 Z M13.5 7 L17 10.5",
        "layers": "M12 4 L21 9 L12 14 L3 9 Z M3 13 L12 18 L21 13 M3 17 L12 22 L21 17",
        "folder": "M3 6 H9.5 L11.5 8 H21 V19 H3 Z",
        "trash": "M4 7 H20 M9 7 V4.5 H15 V7 M6.5 7 L7.5 20 H16.5 L17.5 7 M10 11 V16.5 M14 11 V16.5",
        "detect": "M4 9 V4 H9 M15 4 H20 V9 M20 15 V20 H15 M9 20 H4 V15 M8 8 H16 V16 H8 Z",
        "chevronDown": "M6 9 L12 15 L18 9",
        "chevronRight": "M9 6 L15 12 L9 18",
        "chevronUp": "M6 15 L12 9 L18 15",
        "chevronLeft": "M15 6 L9 12 L15 18",
        "close": "M6 6 L18 18 M18 6 L6 18",
        "settings": "M12 8.5 A3.5 3.5 0 1 0 12.01 8.5 M12 2.5 V5 M12 19 V21.5 M2.5 12 H5 M19 12 H21.5 M5.3 5.3 L7.1 7.1 M16.9 16.9 L18.7 18.7 M5.3 18.7 L7.1 16.9 M16.9 7.1 L18.7 5.3",
        "info": "M12 3 A9 9 0 1 0 12.01 3 M12 11 V17 M12 7.5 V8.1",
        "rotateLeft": "M4 10 A8 8 0 1 1 6.5 17.5 M4 4 V10 H10",
        "rotateRight": "M20 10 A8 8 0 1 0 17.5 17.5 M20 4 V10 H14",
        "save": "M5 4 H16 L20 8 V20 H4 V4 Z M8 4 V9 H15 V4 M7 20 V14 H17 V20",
        "plus": "M12 5 V19 M5 12 H19",
        "frame": "M4 3 H20 V21 H4 Z M7 6 H17 V15 H7 Z",
        "undo": "M9 14 L4 9 L9 4 M4 9 H14 A6 6 0 0 1 14 21 H9",
        "redo": "M15 14 L20 9 L15 4 M20 9 H10 A6 6 0 0 0 10 21 H15",
        "snap": "M4 4 H20 V20 H4 Z M8 12 H16 M12 8 V16"
    })

    Shape {
        width: 24
        height: 24
        transform: Scale { xScale: root.size / 24; yScale: root.size / 24 }
        ShapePath {
            strokeColor: root.color
            strokeWidth: root.stroke * 24 / root.size
            fillColor: "transparent"
            capStyle: ShapePath.RoundCap
            joinStyle: ShapePath.RoundJoin
            PathSvg { path: root.paths[root.name] || "" }
        }
    }
}
