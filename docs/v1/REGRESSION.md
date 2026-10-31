# 现有功能全量回归（U029，UX-12）

[FEATURE-FREEZE.md](FEATURE-FREEZE.md) 的 81 个条目，每一条对应的证据（`tests/freeze-regression.json`）及本次结果。
由 `tools/freeze-regression.py --report <自检 report.json> --markdown <本文件>` 生成；CI 在每个平台的应用自检之后运行同一检查
（不生成本文件）。“passed” = 对应的自检检查本次全部通过、或对应的核心测试存在（其结果由 CI 核心任务给出）；
“CI” = 由 CI 中的其他步骤覆盖；“manual” = 只能由人验证（原因见证据栏）。

- 本次运行：Linux 离屏，样张（fkfixture，300 dpi，3 张相片），版本 0.7.0-alpha.15；自检结果：245 passed, 0 failed, 0 skipped, 0 known issues (0 no longer reproduce), 0 not applicable。
- 结果：81 条 — 75 passed，3 CI，3 manual，0 失败或无证据。
- macOS 上属于原生菜单栏的快捷键检查按设计跳过（离屏窗口没有原生菜单栏），在 CI 中计为“跳过”，不计为通过。

| ID | 功能 | 证据 | 结果 |
| --- | --- | --- | --- |
| A01 | 新建工程 | “FK-001 new project with unsaved changes asks first” ✓; “new project: the canvas no longer shows the previous scan” ✓ | passed |
| A02 | 打开工程（.fkproj） | `Project.SaveLoadRoundTripIsLossless`; `Compat.OpensProjectsFromEarlierReleases`; “open project” ✓; “project keeps prints” ✓; “FK-001 dropping a project file asks first” ✓ | passed |
| A03 | 导入扫描件（TIFF，可多选） | “detection finished” ✓; “prints detected” ✓; “import hashes the whole scan” ✓ | passed |
| A04 | 导入文件夹 | “FREEZE A04” ✓ | passed |
| A05 | 保存 / 另存为 | `Project.SecondSaveKeepsBackupAndIsAtomic`; “save project” ✓; “FK-001 failed save keeps the project path and unsaved changes” ✓ | passed |
| A06 | 关闭工程（等同新建空工程） | “FK-001 new project with unsaved changes asks first” ✓; “FK-001 cancel keeps the project and its unsaved changes” ✓; (Close Project runs the same command as New Project) | passed |
| A07 | 退出（未保存时确认 保存/放弃/取消） | manual: closing the window with unsaved changes asks save / discard / cancel: needs a real window close (HV-02, HV-03); the same dialog and its answers are checked for New Project and dropped files (FK-001) | manual |
| A08 | 最近工程列表（设置 recentProjects） | “FREEZE A08” ✓ | passed |
| A09 | 只读打开新版本 schema 的工程 | `Project.NewerSchemaOpensReadOnly`; “FK-014 a project last saved by a later version opens” ✓ | passed |
| A10 | 缺失扫描件重新定位（Relocate…）与按指纹在同目录查找 | `Project.RelocatesMovedScansByNameAndFingerprint`; “FREEZE A10” ✓ | passed |
| A11 | 从工程移除扫描件（不动原文件） | “FREEZE A11” ✓ | passed |
| A12 | 在 Finder/资源管理器中显示扫描件 | manual: Reveal in Finder / Explorer opens the system file manager: real desktop (HV-02, HV-03) | manual |
| A13 | 原始扫描只读、绝不修改 | `Export.SourceFileUntouched`; `Composite.NeverOverwritesAndSourceUntouched`; “UX-04 the scan file is unchanged (SHA-256)” ✓ | passed |
| B01 | 自动识别每张相片的 Outer/Inner（矩形、圆形），置信度三级与原因 | `Detection.FindsAndContainsTwoTiltedPrints`; `Detection.WhitePrintOnWhiteLidExcludesTheShadowBand`; “prints detected” ✓; “UX-04 four prints found, in four orientations” ✓ | passed |
| B02 | 每个扫描件相片数（期望数量 1–12，默认 4） | “UX-01 left rail has the count switch and number” ✓; “UX-01 manual count differing from the prints found is flagged” ✓ | passed |
| B03 | 识别方式：自动 / 模板 | “FREEZE B08” ✓; “prints detected” ✓; (the left rail's method choice runs automatic or template detection; both are run) | passed |
| B04 | 重新识别（当前 / 全部扫描件） | “FREEZE B08” ✓; (detecting a scan again (with a template) replaces its prints) | passed |
| B05 | 安全外扩边距（px，下次识别生效） | `Detection.FindsAndContainsTwoTiltedPrints`; (the margin is a detection parameter used with its default in every detection test; no test changes it) | passed |
| B06 | 有效 DPI 测算与提示（tagged/measured） | `Detection.ScaledOutputWithNominalDpiTag` | passed |
| B07 | 识别警告（数量不符、边缘接触、淡边框、背景同色等） | `Detection.CountMismatchIsReported`; `Detection.TouchingPrintsAreSplitContainedAndFlagged`; “UX-01 manual count differing from the prints found is flagged” ✓; “UX-01 automatic: a blank scan says no prints were found” ✓ | passed |
| B08 | 模板：保存当前布局、按模板识别、删除模板 | `Template.SaveLoadRoundTrip`; `Detection.TemplateReproducesItems`; “FREEZE B08” ✓ | passed |
| C01 | 工具：选择 V、抓手 H（空格临时）、编辑外框 O、编辑内框 I、Esc 回到选择 | “UX-03 Shift tap (real key events): Outer → Inner” ✓; “UX-03 tool button (mouse): Edit Inner” ✓ | passed |
| C02 | 拖动角点/边中点自由编辑四边形；圆形拖动 | “UX-03 drag without Shift: free corner” ✓; “UX-03 circle: radius drag (diagonal pointer) keeps rx = ry” ✓ | passed |
| C03 | 方向键微调手柄（Shift ×10） | “UX-03 Shift+→ on a selected corner: 10 px nudge” ✓; “UX-03 circle: arrow nudges keep rx = ry” ✓ | passed |
| C04 | 数值输入四角坐标 | “FREEZE C07” ✓; “UX-09 drag right (mouse)” ✓; (the corner fields call the same command as FREEZE C07's numeric edit) | passed |
| C05 | 内框形状：矩形 / 圆形 | “UX-03 saved and reopened: outer and round inner unchanged” ✓; “UX-03 the export uses the edited circle” ✓ | passed |
| C06 | 外框吸附边缘 | “FREEZE C06” ✓ | passed |
| C07 | 重置为识别结果 | “FREEZE C07” ✓ | passed |
| C08 | 撤销 / 重做（每张相片 100 步，内存中） | “UX-03 rectangle: one drag = one undo step; redo” ✓; “UX-04 undo/redo interleave a print's edit and a whole-scan turn in order” ✓ | passed |
| C09 | 转正旋转（左/右 90°，仅影响导出方向） | `Composite.UnchangedEditRoundTripIsBitExact`; “UX-04 uprights on: the same export whether the scan is turned or not” ✓ | passed |
| C10 | 已复核标记；复核并下一张 | “FREEZE C10” ✓ | passed |
| C11 | 排除 / 包含批处理 | “FREEZE C11” ✓ | passed |
| C12 | 全选相片 | “FREEZE C12” ✓ | passed |
| C13 | 上/下一张相片 ←/→；上/下一个扫描件 Shift+←/→ | “FREEZE C13” ✓ | passed |
| D01 | 视图模式：扫描件 1 / 相片 2 / 画面 3 / 合成 4 | “FREEZE D01” ✓; “UX-06 narrow window: the compact view switch changes the view” ✓ | passed |
| D02 | 缩放：放大/缩小（Ctrl+= / Ctrl+−、滚轮、触控板）、适合 Ctrl+0、100 % Ctrl+1、适合↔100 % 切换 Z | “FREEZE D02” ✓ ×2 | passed |
| D03 | 100 % = 1 图像像素对应 1 设备像素；Preview/Full 分辨率标记 | “physical 100 %” ✓; “badge Preview at fit” ✓; “badge Full at 100 %” ✓ | passed |
| D04 | 导航器（缩略图 + 视口框，点击定位） | “UX-04 turned view at 100 %: full resolution, navigator shows the visible part” ✓ | passed |
| D05 | 显示/隐藏叠加线 Ctrl+Shift+H（眼睛按钮） | “FREEZE D05” ✓ | passed |
| D06 | 合成对比：分屏对比 Y、原图↔合成 \ | “FREEZE D06” ✓ | passed |
| D07 | 面板显示：左栏 Ctrl+Alt+1、右栏 Ctrl+Alt+2、胶片条 Ctrl+Alt+3、全部 Ctrl+Alt+0、重置布局 | “UX-10 the shortcuts still fold and unfold the panels” ✓; “UX-10 an edge strip with keyboard focus folds and unfolds its panel” ✓; “UX-10 all three folded” ✓; “UX-10 Reset Layout” ✓ | passed |
| D08 | 左右栏宽度拖动调整并记忆 | “UX-10 a rail width dragged with the grip and a folded panel are stored for the next start” ✓ | passed |
| D09 | 全屏 F11（macOS ⌃⌘F） | “FREEZE D09” ✓ | passed |
| D10 | 信息条：ICC、位深、DPI | “FREEZE D10” ✓ | passed |
| D11 | 胶片条：缩略图、状态图标（已复核、警告、已导出、已回导…）、右键菜单 | “UX-10 the filmstrip spans the whole window” ✓; “UX-04 the scan thumbnail turns with it” ✓ | passed |
| E01 | 导出画面区域（当前 / 所选 / 全部） | “image areas exported” ✓; “image area exports default to labelled straightening” ✓ | passed |
| E02 | 导出带真实相框的整张相片（FULL） | “UX-04 exported before the turn, relinked after it” ✓; (the full prints exported there are compared bit for bit with the final outputs) | passed |
| E03 | 格式 TIFF / PNG / JPEG（质量）/ Linear DNG | `Export.LinearDngStructure`; `Export.PngSixteenBitExactAndXmp`; “FK-004 "remove identity": TIFF, PNG and JPEG outputs” ✓ | passed |
| E04 | 位深：与扫描件相同 / 8 / 16 | `Export.DepthReductionNeedsConsent`; “FK-010 8-bit export only with explicit consent” ✓ | passed |
| E05 | TIFF 压缩：LZW / ZIP / 无 | `TiffWriteRoundTrip.ExactAndTagged` | passed |
| E06 | 几何：像素裁切 / 拉直（Lanczos-3） | `Composite.StraightenedRoundTripKeepsFrameExact`; “image area exports default to labelled straightening” ✓ | passed |
| E07 | 应用转正旋转（无损） | “UX-04 uprights on: the same export whether the scan is turned or not” ✓; “UX-04 uprights off: exports follow the turned view” ✓ | passed |
| E08 | 导出文件夹：扫描件所在文件夹 / 自选 | “image areas exported” ✓; (the self-test exports into the folder it chose) | passed |
| E09 | 色彩：保留扫描件配置文件 / 转 sRGB / 转 Adobe RGB / 不嵌入 | `Export.IccPoliciesKeepAssignConvertUntagged`; “output converts the chosen profile once: colours as in the scan” ✓ | passed |
| E10 | 元数据与隐私：保留安全字段 / 移除位置 / 移除位置与身份；GPS 从不复制 | “FK-004 the privacy panel states the written fields” ✓; “FK-004 "remove identity": TIFF, PNG and JPEG outputs” ✓ | passed |
| E11 | 永不覆盖：版本号递增 `_EDIT_vNNN`、`_FULL_`、`_FINAL_`、`_FRAMED_` | `TiffWrite.NeverOverwritesAndAbortRemovesTemp`; “UX-04 uprights off: exports follow the turned view” ✓; “FK-005 memory pressure: rebuildable caches dropped” ✓; (second exports are written as _v002 beside _v001) | passed |
| E12 | 低置信度未复核相片导出前列出并确认 | manual: the export dialog lists unreviewed low-confidence prints: needs a low-confidence print, which the generated fixture does not produce (the owner's scans, HV-01) | manual |
| E13 | 侧车 `.fk.json` + 嵌入 XMP ID | “no sidecar files by default” ✓; “portable sidecar written when enabled” ✓; “relink unchanged exports (file hash, embedded IDs)” ✓ | passed |
| F01 | 重新关联调色后文件（多选、拖入窗口） | “relink unchanged exports (file hash, embedded IDs)” ✓; “renamed file relinks once its print is chosen” ✓ | passed |
| F02 | 内容检查：签名分数、尺寸、建议旋转、降位深、缺 ICC、需要人工对齐 | “180° graded file: turn found by the picture, confirmation required” ✓; “graded file without a profile: the export's profile is offered” ✓ | passed |
| F03 | 合成预览、显示合成、对比 | “TS1 a fresh preview matches the current print” ✓; “UX-08 the composite preview and the final output use the print's mask” ✓; “FREEZE D06” ✓ | passed |
| F04 | 蒙版：硬边缘 / 羽化（px）/ 保留边缘（内缩 px） | `NaturalBlend.UnchangedEditStaysBitExactWithEveryMask`; “UX-08 new prints get the natural blend by default” ✓; “UX-09 drag right (mouse)” ✓ | passed |
| F05 | 最终输出：真实扫描相框（默认）/ 生成边框 | “final exports” ✓; “synthetic frame exports” ✓ | passed |
| F06 | 生成边框：四边独立 / 等宽 / 画布比例；单位 % 长边或 px；上下左右、比例、最小边距；颜色来源（扫描相框颜色 / 自定义） | `SyntheticFrame.LayoutModes`; `SyntheticFrame.SampleFrameColorIsTheBandMedian`; “synthetic frame exports” ✓ | passed |
| F07 | 最终输出格式 TIFF / PNG / JPEG、位深 | `Composite.LowerDepthNeedsConsent`; “final exports” ✓ | passed |
| F08 | 最终导出（当前 / 所选 / 全部）；接受较低位深、允许按比例缩放的确认项 | `Composite.SizeAndAspectChecks`; “final exports” ✓; “round trip bit-exact (frame and image)” ✓ | passed |
| G01 | 批处理统计：相片数、已排除、待复核；已导出/已回导/已输出计数 | “FREEZE G01” ✓ | passed |
| G02 | 全部导出画面 / 全部回导 / 全部最终输出 | “image areas exported” ✓; “final exports” ✓; (exports with no selection run on every print) | passed |
| G03 | 任务列表：进度、阶段、用时、取消、清除已完成 | “TS3 export interrupted by opening a project: cancelled” ✓; “TS4 a failing job ends as failed” ✓ | passed |
| H01 | 设置对话框 Ctrl+,：外观（深色/浅色/跟随系统）、语言（系统/中文/英文，重启生效）、默认每扫描件相片数 | “FREEZE H01” ✓; “UX-01 default off: new scans use the default manual count” ✓ | passed |
| H02 | 中英双语界面（478 条中文翻译，CI 检查） | “FK-009 every visible text has glyphs” ✓; CI: tools/check-i18n.py; the text checks run in Chinese in CI (--scenario text) | passed |
| H03 | 帮助 F1（macOS ⌘?；在浏览器打开仓库 README，用户主动操作）、关于 | “FREEZE H03” ✓ | passed |
| H04 | 离线：不上传、不收集使用数据 | CI: tools/check-no-network.py (no network code in the app or core) | CI |
| H05 | macOS 原生菜单栏；Windows/Linux 窗口内菜单；单键快捷键在文本输入时不触发 | “FREEZE H05” ✓; “UX-03 Shift tap while typing in a text field” ✓ | passed |
| H06 | 布局与设置持久化（QSettings；FK_SETTINGS_DIR 覆盖） | “UX-10 a rail width dragged with the grip and a folded panel are stored for the next start” ✓ | passed |
| I01 | `fkcli info / integrity / detect / eval / export / roundtrip / perf` | `eval_corpus`; `realeval_fixture`; (fkcli eval / realeval in CTest; fkcli roundtrip in the round-trip checks) | passed |
| I02 | `fkfixture` 合成扫描与真值 | `eval_corpus`; (fkfixture writes the corpus and every CI self-test scan) | passed |
| I03 | `FrameKeeper --self-test <dir> --scan <tif> [--live] [--scenario …]` | “detection finished” ✓; CI: every CI app job runs --self-test and the scenarios miniswhite, text, memory | passed |
| J01 | macOS 通用 DMG（arm64 + x86_64），ad-hoc 签名 | CI: release.yml: tools/release/test-macos-dmg.sh (mount, check, copy, run arm64 and x86_64, upgrade and rollback) | CI |
| J02 | Windows 安装版（按用户安装、.fkproj 关联、桌面快捷方式可选）与便携 ZIP，随附 VC++ 运行库 | CI: release.yml: tools/release/test-windows-install.sh (install, upgrade from published releases, rollback, uninstall) and the unpacked ZIP | CI |
| J03 | Windows 画布按主显示器 ICC 显示；macOS 按 sRGB | “FK-011 another monitor's profile: the proxy is converted again” ✓ | passed |
