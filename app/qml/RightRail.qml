// Right rail "Item": how, for the current print. Tabs Boundaries · Export · Return (ui-reference §4.10).
import QtQuick
import QtQuick.Layouts
import QtQuick.Controls.Basic
import QtQuick.Dialogs
import FrameKeeper.Core

Rectangle {
    id: rail
    required property var cmd
    required property var canvas
    color: Theme.surfacePanel
    readonly property var it: App.currentItem
    readonly property bool hasItem: it.id !== undefined
    readonly property int lw: 96

    function reasonText(r) {
        const map = {
            "touching_split": qsTr("touching prints were separated"),
            "touching_neighbour": qsTr("an edge touches a neighbour"),
            "frame_not_visible": qsTr("frame not visible against the background"),
            "low_frame_contrast": qsTr("faint frame edge"),
            "low_contrast": qsTr("low edge contrast"),
            "refine_residual": qsTr("edge not straight everywhere"),
            "size_implausible": qsTr("size matches no film type"),
            "inner_prior_fallback": qsTr("image edge estimated from the film type"),
            "inner_asymmetric": qsTr("image area off-centre"),
            "inner_circle_prior": qsTr("round image estimated"),
            "touches_scan_edge": qsTr("touches the scan edge"),
            "print_fills_scan": qsTr("the scan is this print"),
            "scan_margin_trimmed": qsTr("scanner margin trimmed"),
            "count_mismatch": qsTr("print count differs from expected"),
            "template": qsTr("from template"),
            "template_snap_far": qsTr("template position moved far")
        }
        return map[r] || r
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
                anchors.verticalCenter: parent.verticalCenter
                text: rail.hasItem ? qsTr("Item %1").arg(rail.it.label) : qsTr("Item")
                font.pixelSize: Theme.fontRailTitle
                font.weight: Font.DemiBold
                color: Theme.textPrimary
            }
            Text {
                anchors.right: parent.right
                anchors.rightMargin: 10
                anchors.verticalCenter: parent.verticalCenter
                visible: rail.hasItem
                text: rail.it.scanName || ""
                width: parent.width * 0.55
                horizontalAlignment: Text.AlignRight
                elide: Text.ElideMiddle
                font.pixelSize: Theme.fontCaption
                color: Theme.textTertiary
            }
        }
        Segmented {
            Layout.alignment: Qt.AlignHCenter
            Layout.bottomMargin: 8
            value: App.settings.rightTab
            model: [{ text: qsTr("Boundaries"), value: 0 }, { text: qsTr("Export"), value: 1 }, { text: qsTr("Return"), value: 2 }]
            onActivated: (v) => App.setSetting("rightTab", v)
        }
        Text {
            visible: !rail.hasItem
            Layout.fillWidth: true
            Layout.margins: Theme.rowInset
            wrapMode: Text.WordWrap
            text: qsTr("Select a print in the filmstrip or on the canvas.")
            font.pixelSize: Theme.fontBody
            color: Theme.textTertiary
        }
        Item { visible: !rail.hasItem; Layout.fillHeight: true }
        Flickable {
            visible: rail.hasItem
            Layout.fillWidth: true
            Layout.fillHeight: true
            contentHeight: stack.implicitHeight
            clip: true
            boundsBehavior: Flickable.StopAtBounds
            ScrollBar.vertical: ScrollBar {}
            StackLayout {
                id: stack
                width: rail.width
                currentIndex: App.settings.rightTab
                // ---------------- Boundaries ----------------
                ColumnLayout {
                    spacing: 0
                    Section {
                        title: qsTr("Outer")
                        note: rail.it.outerSource === "manual" ? qsTr("edited") : (rail.it.outerSource === "template" ? qsTr("template") : qsTr("detected"))
                        resettable: rail.it.outerSource === "manual" || rail.it.innerSource === "manual"
                        resetTip: qsTr("Reset Outer and Inner to detected")
                        onReset: App.resetToDetected(rail.it.id)
                        FieldRow {
                            label: qsTr("Film type"); labelWidth: rail.lw
                            Text { text: cmd.filmTypeName(rail.it.filmType); font.pixelSize: Theme.fontBody; color: Theme.textPrimary }
                        }
                        FieldRow {
                            label: qsTr("Confidence"); labelWidth: rail.lw
                            Rectangle {
                                width: 10; height: 10; radius: rail.it.confidenceClass === "high" ? 5 : 1
                                rotation: rail.it.confidenceClass === "low" ? 45 : 0
                                color: rail.it.confidenceClass === "high" ? Theme.accent : (rail.it.confidenceClass === "medium" ? Theme.warning : Theme.error)
                            }
                            Text {
                                text: (rail.it.confidenceClass === "high" ? qsTr("High") : rail.it.confidenceClass === "medium" ? qsTr("Check recommended") : qsTr("Review required"))
                                      + "  " + Math.round((rail.it.confidence || 0) * 100) + " %"
                                font.pixelSize: Theme.fontBody
                                color: Theme.textPrimary
                            }
                        }
                        Repeater {
                            model: rail.it.reasons || []
                            Text {
                                required property string modelData
                                visible: modelData !== "count_mismatch" || true
                                Layout.fillWidth: true
                                wrapMode: Text.WordWrap
                                text: "• " + rail.reasonText(modelData)
                                font.pixelSize: Theme.fontCaption
                                color: Theme.textTertiary
                            }
                        }
                        FieldRow {
                            label: qsTr("Size"); labelWidth: rail.lw
                            Text {
                                Layout.fillWidth: true
                                text: (rail.it.outerWidthMm || 0).toFixed(1) + " × " + (rail.it.outerHeightMm || 0).toFixed(1) + " mm"
                                font.pixelSize: Theme.fontBody; font.features: { "tnum": 1 }; color: Theme.textPrimary
                            }
                        }
                        FieldRow {
                            label: ""; labelWidth: rail.lw
                            Text {
                                Layout.fillWidth: true
                                text: Math.round(rail.it.outerWidthPx || 0) + " × " + Math.round(rail.it.outerHeightPx || 0) + " px"
                                font.pixelSize: Theme.fontCaption; font.features: { "tnum": 1 }; color: Theme.textTertiary
                            }
                        }
                        FieldRow {
                            label: qsTr("Tilt"); labelWidth: rail.lw
                            Text { text: (rail.it.angleDeg || 0).toFixed(2) + "°"; font.pixelSize: Theme.fontBody; font.features: { "tnum": 1 }; color: Theme.textPrimary }
                        }
                        Repeater {
                            model: 4
                            FieldRow {
                                required property int index
                                label: [qsTr("Top left"), qsTr("Top right"), qsTr("Bottom right"), qsTr("Bottom left")][index]
                                labelWidth: rail.lw
                                readonly property var pt: rail.hasItem && rail.it.outer && rail.it.outer.points ? rail.it.outer.points[index] : [0, 0]
                                NumField { unit: "x"; decimals: 1; value: pt[0]; onCommitted: (v) => App.setOuterNumeric(rail.it.id, index, v, pt[1]) }
                                NumField { unit: "y"; decimals: 1; value: pt[1]; onCommitted: (v) => App.setOuterNumeric(rail.it.id, index, pt[0], v) }
                            }
                        }
                        RowLayout {
                            spacing: 6
                            FkButton { text: qsTr("Snap to edges"); iconName: "snap"; onClicked: App.snapOuter(rail.it.id) }
                            FkButton { text: qsTr("Edit"); iconName: "outer"; onClicked: canvas.tool = "outer" }
                        }
                    }
                    Section {
                        title: qsTr("Inner")
                        note: rail.it.innerSource === "manual" ? qsTr("edited") : qsTr("detected")
                        FieldRow {
                            label: qsTr("Shape"); labelWidth: rail.lw
                            Segmented {
                                value: rail.it.inner ? rail.it.inner.kind : "quad"
                                model: [{ text: qsTr("Rectangle"), value: "quad" }, { text: qsTr("Round"), value: "ellipse" }]
                                onActivated: (v) => App.setInnerKind(rail.it.id, v)
                            }
                        }
                        FieldRow {
                            label: qsTr("Size"); labelWidth: rail.lw
                            Text { text: (rail.it.innerWidthPx || 0) + " × " + (rail.it.innerHeightPx || 0) + " px"; font.pixelSize: Theme.fontBody; font.features: { "tnum": 1 }; color: Theme.textPrimary }
                        }
                        FkButton { text: qsTr("Edit"); iconName: "inner"; onClicked: canvas.tool = "inner" }
                    }
                    Section {
                        title: qsTr("Geometry")
                        FieldRow {
                            label: qsTr("Upright"); labelWidth: rail.lw
                            ToolBtn { iconName: "rotateLeft"; tip: qsTr("Rotate upright left"); shortcutText: cmd.mod + "["; onClicked: App.rotateUpright(rail.it.id, -1) }
                            Text { text: (rail.it.upright || 0) + "°"; font.pixelSize: Theme.fontBody; color: Theme.textPrimary; Layout.preferredWidth: 36; horizontalAlignment: Text.AlignHCenter }
                            ToolBtn { iconName: "rotateRight"; tip: qsTr("Rotate upright right"); shortcutText: cmd.mod + "]"; onClicked: App.rotateUpright(rail.it.id, 1) }
                        }
                        FkCheck { text: qsTr("Reviewed"); checked: rail.it.reviewed === true; onToggled: App.setReviewed(rail.it.id, checked) }
                        FkCheck { text: qsTr("Exclude from batch"); checked: rail.it.excluded === true; onToggled: App.setExcluded(rail.it.id, checked) }
                    }
                    Section {
                        title: qsTr("Safety margin")
                        note: qsTr("next detection")
                        FieldRow {
                            label: qsTr("Outward"); labelWidth: rail.lw
                            NumField { unit: "px"; decimals: 1; from: 0; to: 50; step: 0.5; value: App.settings.safetyMargin; onCommitted: (v) => App.setSetting("safetyMargin", v) }
                        }
                    }
                }
                // ---------------- Export ----------------
                ColumnLayout {
                    spacing: 0
                    Section {
                        title: qsTr("Export image area")
                        FieldRow {
                            label: qsTr("Format"); labelWidth: rail.lw
                            FkCombo {
                                Layout.fillWidth: true
                                model: [{ text: "TIFF", value: "tif" }, { text: "PNG", value: "png" }, { text: "JPEG", value: "jpg" }, { text: qsTr("Linear DNG"), value: "dng" }]
                                value: App.settings.exportFormat
                                onPicked: (v) => App.setSetting("exportFormat", v)
                            }
                        }
                        FieldRow {
                            label: qsTr("Bit depth"); labelWidth: rail.lw
                            FkCombo {
                                Layout.fillWidth: true
                                model: [{ text: qsTr("Same as scan"), value: 0 }, { text: qsTr("8-bit"), value: 8 }, { text: qsTr("16-bit"), value: 16 }]
                                value: App.settings.exportBits
                                onPicked: (v) => App.setSetting("exportBits", v)
                            }
                        }
                        FieldRow {
                            visible: App.settings.exportFormat === "tif"
                            label: qsTr("Compression"); labelWidth: rail.lw
                            FkCombo {
                                Layout.fillWidth: true
                                model: [{ text: "LZW", value: "lzw" }, { text: "ZIP", value: "deflate" }, { text: qsTr("None"), value: "none" }]
                                value: App.settings.exportCompression
                                onPicked: (v) => App.setSetting("exportCompression", v)
                            }
                        }
                        FieldRow {
                            visible: App.settings.exportFormat === "jpg"
                            label: qsTr("Quality"); labelWidth: rail.lw
                            NumField { decimals: 0; from: 50; to: 100; value: App.settings.jpegQuality; onCommitted: (v) => App.setSetting("jpegQuality", Math.round(v)) }
                        }
                        FieldRow {
                            label: qsTr("Geometry"); labelWidth: rail.lw
                            Segmented {
                                value: App.settings.geometryMode
                                model: [{ text: qsTr("Pixel crop"), value: "crop", tip: qsTr("Exact scan pixels; no resampling") },
                                        { text: qsTr("Straighten"), value: "straighten", tip: qsTr("Rotate the tilt away (Lanczos-3 resampling)") }]
                                onActivated: (v) => App.setSetting("geometryMode", v)
                            }
                        }
                        FkCheck { text: qsTr("Apply upright rotation (lossless)"); checked: App.settings.applyUpright; onToggled: App.setSetting("applyUpright", checked) }
                        Text {
                            Layout.fillWidth: true
                            wrapMode: Text.WordWrap
                            font.pixelSize: Theme.fontCaption
                            color: rail.it.frameBitExact ? Theme.textSecondary : Theme.warning
                            text: rail.it.frameBitExact ? qsTr("Pixels stay bit-identical to the scan.")
                                                        : qsTr("Pixels are not bit-identical: %1.").arg(rail.it.notExactReason)
                        }
                        FieldRow {
                            label: qsTr("Folder"); labelWidth: rail.lw
                            Text {
                                Layout.fillWidth: true
                                text: App.settings.exportFolder || qsTr("next to the scans")
                                elide: Text.ElideMiddle
                                font.pixelSize: Theme.fontCaption
                                color: Theme.textSecondary
                            }
                            FkButton { text: qsTr("Choose…"); onClicked: cmd.chooseExportFolder() }
                        }
                        RowLayout {
                            spacing: 6
                            FkButton { primary: true; text: qsTr("Export this print"); onClicked: cmd.exportImageAreas([rail.it.id]) }
                            FkButton { text: qsTr("Selected (%1)").arg(App.selectedItemIds.length); enabled: App.selectedItemIds.length > 1; onClicked: cmd.exportImageAreas(App.selectedItemIds) }
                        }
                        FkButton { text: qsTr("Export full print with real frame"); onClicked: cmd.exportFullPrints([rail.it.id]) }
                    }
                    Section {
                        title: qsTr("Colour")
                        FieldRow {
                            label: qsTr("Profile"); labelWidth: rail.lw
                            FkCombo {
                                Layout.fillWidth: true
                                model: [{ text: qsTr("Keep scan profile"), value: "keep" },
                                        { text: qsTr("Convert to sRGB"), value: "convert_srgb" },
                                        { text: qsTr("Convert to Adobe RGB"), value: "convert_adobe" },
                                        { text: qsTr("Untagged (no profile)"), value: "untagged" }]
                                value: App.settings.colorPolicy
                                onPicked: (v) => App.setSetting("colorPolicy", v)
                            }
                        }
                        Text {
                            Layout.fillWidth: true
                            wrapMode: Text.WordWrap
                            font.pixelSize: Theme.fontCaption
                            color: Theme.textTertiary
                            text: qsTr("Scan profile: %1. Converting happens once, relative colorimetric with black point compensation.").arg(App.currentScan.icc || "")
                        }
                    }
                    Section {
                        title: qsTr("Metadata & privacy")
                        FieldRow {
                            label: qsTr("Preset"); labelWidth: rail.lw
                            FkCombo {
                                Layout.fillWidth: true
                                model: [{ text: qsTr("Keep (safe fields)"), value: "keep_all_safe" },
                                        { text: qsTr("Remove location"), value: "remove_location" },
                                        { text: qsTr("Remove location and identity"), value: "remove_identity" }]
                                value: App.settings.privacy
                                onPicked: (v) => App.setSetting("privacy", v)
                            }
                        }
                        Text {
                            Layout.fillWidth: true
                            wrapMode: Text.WordWrap
                            font.pixelSize: Theme.fontCaption
                            color: Theme.textTertiary
                            text: App.settings.privacy === "remove_identity" ? qsTr("Removes artist, creator, owner, serial number and computer name. GPS is never copied.")
                                  : qsTr("Scanner make/model, software and dates are kept. GPS is never copied.")
                        }
                        // FK-004: what an export of this scan actually carries, stated explicitly
                        Text {
                            id: writtenList
                            // re-read when the preset, the format or the scan changes
                            readonly property var fields: (App.settings.privacy, App.settings.exportFormat, App.currentScanId, App.currentScan.state, App.privacyPreview())
                            Layout.fillWidth: true
                            wrapMode: Text.WordWrap
                            font.pixelSize: Theme.fontCaption
                            color: Theme.textSecondary
                            text: fields.length === 0 ? qsTr("Written: no descriptive fields from the scan.")
                                  : qsTr("Written: %1").arg(fields.map(f => f.name + " “" + f.value + "”" + (f.identity ? " ⚠" : "")).join(" · "))
                        }
                        Text {
                            Layout.fillWidth: true
                            wrapMode: Text.WordWrap
                            font.pixelSize: Theme.fontCaption
                            color: Theme.textTertiary
                            text: qsTr("Never written: GPS location, camera owner, serial numbers. ⚠ = identifies a person or computer.")
                        }
                    }
                }
                // ---------------- Return ----------------
                ColumnLayout {
                    spacing: 0
                    Section {
                        title: qsTr("Re-import")
                        note: rail.it.relinked ? qsTr("relinked") : (rail.it.edits && rail.it.edits.length ? qsTr("exported") : qsTr("not exported"))
                        Repeater {
                            model: rail.it.edits || []
                            ColumnLayout {
                                required property var modelData
                                Layout.fillWidth: true
                                spacing: 1
                                Text {
                                    Layout.fillWidth: true
                                    text: qsTr("Version %1 · %2").arg(modelData.version).arg(modelData.format.toUpperCase())
                                    font.pixelSize: Theme.fontBody
                                    color: Theme.textPrimary
                                }
                                Text {
                                    Layout.fillWidth: true
                                    elide: Text.ElideMiddle
                                    text: modelData.relinkedPath ? qsTr("Graded: %1 (by %2)").arg(modelData.relinkedPath).arg(modelData.matchedBy)
                                                                   + (modelData.transformSet && modelData.rotate ? " · " + qsTr("turned %1°, turned back when compositing").arg(modelData.rotate) : "")
                                                                   + (modelData.transformSet && modelData.scaled ? " · " + qsTr("resized, scaled back") : "")
                                          : modelData.path ? qsTr("Exported: %1").arg(modelData.path)
                                          : qsTr("Aligned manually (no export in this project)")
                                    font.pixelSize: Theme.fontCaption
                                    color: modelData.relinkedPath ? Theme.textSecondary : Theme.textTertiary
                                }
                            }
                        }
                        Text {
                            visible: !!(rail.it.relinkCheck && rail.it.relinkCheck.score !== undefined)
                            Layout.fillWidth: true
                            wrapMode: Text.WordWrap
                            font.pixelSize: Theme.fontCaption
                            color: Theme.textTertiary
                            text: rail.it.relinkCheck ? qsTr("Content check %1 %").arg(Math.round((rail.it.relinkCheck.score || 0) * 100)) : ""
                        }
                        RowLayout {
                            spacing: 6
                            FkButton { text: qsTr("Relink to this print…"); iconName: "relink"; onClicked: cmd.relink(rail.it.id) }
                            FkButton { text: qsTr("Relink graded files…"); onClicked: cmd.relink() }
                        }
                    }
                    Section {
                        title: qsTr("Composite")
                        Text {
                            Layout.fillWidth: true
                            wrapMode: Text.WordWrap
                            font.pixelSize: Theme.fontCaption
                            color: Theme.textTertiary
                            text: rail.it.relinked ? qsTr("The graded image replaces only the image area; every frame pixel comes from the original scan.")
                                                   : qsTr("Relink a graded file first.")
                        }
                        RowLayout {
                            spacing: 6
                            FkButton {
                                text: qsTr("Show composite")
                                enabled: rail.it.relinked === true
                                onClicked: { canvas.mode = "composite"; App.requestCompositePreview(rail.it.id) }
                            }
                            FkButton { text: qsTr("Compare"); enabled: rail.it.relinked === true; onClicked: { canvas.mode = "composite"; canvas.compare = true } }
                        }
                    }
                    Section {
                        title: qsTr("Mask")
                        FieldRow {
                            label: qsTr("Edge"); labelWidth: rail.lw
                            FkCombo {
                                Layout.fillWidth: true
                                model: [{ text: qsTr("Hard edge"), value: "hard_edge" }, { text: qsTr("Feather"), value: "feather" }, { text: qsTr("Preserve edge"), value: "preserve_edge" }]
                                value: App.settings.maskMode
                                onPicked: (v) => App.setSetting("maskMode", v)
                            }
                        }
                        FieldRow {
                            visible: App.settings.maskMode === "feather"
                            label: qsTr("Feather"); labelWidth: rail.lw
                            NumField { unit: "px"; from: 0; to: 5; step: 0.5; value: App.settings.featherPx; onCommitted: (v) => App.setSetting("featherPx", v) }
                        }
                        FieldRow {
                            visible: App.settings.maskMode === "preserve_edge"
                            label: qsTr("Inset"); labelWidth: rail.lw
                            NumField { unit: "px"; from: 0; to: 20; step: 0.5; value: App.settings.preserveEdgePx; onCommitted: (v) => App.setSetting("preserveEdgePx", v) }
                        }
                    }
                    Section {
                        title: qsTr("Final output")
                        FieldRow {
                            label: qsTr("Frame"); labelWidth: rail.lw
                            Segmented {
                                value: App.settings.finalMode
                                model: [{ text: qsTr("Real scanned frame"), value: "real" }, { text: qsTr("Synthetic frame"), value: "synthetic" }]
                                onActivated: (v) => App.setSetting("finalMode", v)
                            }
                        }
                        Text {
                            Layout.fillWidth: true
                            wrapMode: Text.WordWrap
                            font.pixelSize: Theme.fontCaption
                            color: Theme.textTertiary
                            text: App.settings.finalMode === "real" ? qsTr("Default: the graded image inside the untouched scanned frame.")
                                                                    : qsTr("A generated border around the graded image (no real frame pixels).")
                        }
                        FieldRow {
                            label: qsTr("Format"); labelWidth: rail.lw
                            FkCombo {
                                Layout.fillWidth: true
                                model: [{ text: "TIFF", value: "tif" }, { text: "PNG", value: "png" }, { text: "JPEG", value: "jpg" }]
                                value: App.settings.finalFormat
                                onPicked: (v) => App.setSetting("finalFormat", v)
                            }
                        }
                        ColumnLayout {
                            visible: App.settings.finalMode === "synthetic"
                            Layout.fillWidth: true
                            spacing: 6
                            FieldRow {
                                label: qsTr("Mode"); labelWidth: rail.lw
                                FkCombo {
                                    Layout.fillWidth: true
                                    model: [{ text: qsTr("Four sides"), value: "sides" }, { text: qsTr("Equal width"), value: "equal" }, { text: qsTr("Canvas ratio"), value: "canvas_ratio" }]
                                    value: App.settings.frameMode
                                    onPicked: (v) => App.setSetting("frameMode", v)
                                }
                            }
                            FieldRow {
                                label: qsTr("Unit"); labelWidth: rail.lw
                                Segmented {
                                    value: App.settings.frameUnit
                                    model: [{ text: qsTr("% long edge"), value: "percent" }, { text: "px", value: "px" }]
                                    onActivated: (v) => App.setSetting("frameUnit", v)
                                }
                            }
                            FieldRow {
                                visible: App.settings.frameMode === "sides"
                                label: qsTr("Top / bottom"); labelWidth: rail.lw
                                NumField { decimals: 1; from: 0; to: 5000; value: App.settings.frameTop; onCommitted: (v) => App.setSetting("frameTop", v) }
                                NumField { decimals: 1; from: 0; to: 5000; value: App.settings.frameBottom; onCommitted: (v) => App.setSetting("frameBottom", v) }
                            }
                            FieldRow {
                                visible: App.settings.frameMode !== "canvas_ratio"
                                label: App.settings.frameMode === "equal" ? qsTr("Width") : qsTr("Left / right")
                                labelWidth: rail.lw
                                NumField { decimals: 1; from: 0; to: 5000; value: App.settings.frameMode === "equal" ? App.settings.frameTop : App.settings.frameLeft
                                           onCommitted: (v) => App.setSetting(App.settings.frameMode === "equal" ? "frameTop" : "frameLeft", v) }
                                NumField { visible: App.settings.frameMode === "sides"; decimals: 1; from: 0; to: 5000; value: App.settings.frameRight; onCommitted: (v) => App.setSetting("frameRight", v) }
                            }
                            FieldRow {
                                visible: App.settings.frameMode === "canvas_ratio"
                                label: qsTr("Ratio"); labelWidth: rail.lw
                                FkCombo {
                                    Layout.fillWidth: true
                                    model: [{ text: "4:5", value: "4:5" }, { text: "1:1", value: "1:1" }, { text: "3:4", value: "3:4" }, { text: "2:3", value: "2:3" }, { text: "9:16", value: "9:16" }]
                                    value: App.settings.frameRatioW + ":" + App.settings.frameRatioH
                                    onPicked: (v) => { const p = v.split(":"); App.setSetting("frameRatioW", Number(p[0])); App.setSetting("frameRatioH", Number(p[1])) }
                                }
                            }
                            FieldRow {
                                visible: App.settings.frameMode === "canvas_ratio"
                                label: qsTr("Min margin"); labelWidth: rail.lw
                                NumField { decimals: 1; from: 0; to: 5000; value: App.settings.frameMinMargin; onCommitted: (v) => App.setSetting("frameMinMargin", v) }
                            }
                            FieldRow {
                                label: qsTr("Colour"); labelWidth: rail.lw
                                FkCombo {
                                    Layout.fillWidth: true
                                    model: [{ text: qsTr("Scanned frame colour"), value: "scanned" }, { text: qsTr("Custom"), value: "custom" }]
                                    value: App.settings.frameColorSource
                                    onPicked: (v) => App.setSetting("frameColorSource", v)
                                }
                                Rectangle {
                                    visible: App.settings.frameColorSource === "custom"
                                    width: 24; height: 24; radius: Theme.radius
                                    color: App.settings.frameColor
                                    border.color: Theme.lineSection
                                    TapHandler { onTapped: colorDialog.open() }
                                }
                            }
                            ColorDialog {
                                id: colorDialog
                                selectedColor: App.settings.frameColor
                                onAccepted: App.setSetting("frameColor", selectedColor.toString())
                            }
                        }
                        RowLayout {
                            spacing: 6
                            FkButton { primary: true; text: qsTr("Export final"); enabled: rail.it.relinked === true; onClicked: cmd.exportFinal([rail.it.id]) }
                            FkButton { text: qsTr("Selected (%1)").arg(App.selectedItemIds.length); enabled: App.selectedItemIds.length > 1; onClicked: cmd.exportFinal(App.selectedItemIds) }
                        }
                        Text {
                            visible: (rail.it.finalExport || "").length > 0
                            Layout.fillWidth: true
                            elide: Text.ElideMiddle
                            font.pixelSize: Theme.fontCaption
                            color: Theme.textSecondary
                            text: qsTr("Last: %1").arg(rail.it.finalExport || "")
                        }
                    }
                }
            }
        }
    }
}
