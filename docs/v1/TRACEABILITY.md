# 需求追踪表（26 项）

工单 → Unit → PR/提交 → 测试 → Tag/Release 的双向追踪。每个 Unit 完成后更新本表；状态只能依据证据变更。
状态：未开始 · 进行中 · 部分完成 · 已完成（自动验证）· 已完成（含真机/人工验收）· 待验证 · 阻塞。
证据标记沿用需求文档：U 用户实测 · R 已有定向复现 · S 源码确定 · C 历史 CI。

## 需求 → Unit → 测试 → 状态

| 需求 | 优先级 | 简述 | Unit | 测试（现有/计划） | 当前证据 | 状态 |
| --- | --- | --- | --- | --- | --- | --- |
| UX-01 | P1 | 自动识别片数开关 | U011 | `CountSetting.*` ×4、`Detection.*` 自动计数 4 项；`fkcli eval --enforce` 漏检提示门槛；自检 UX-01 17 项（鼠标点击，单张/多张/空白混合，保存重开） | U：图 1；R：2026-10-30 自检复现；U011：自动模式无默认数量警告，切换/改数量即时更新警告，疑似漏检与空白扫描提示；夹具与所有者 2 张真实扫描件自检通过 | 已完成（自动验证；真实窗口待所有者确认） |
| UX-02 | P1 | 内外框精度；纯画面与像素裁切区分 | U010（导出）、U012（检测） | `fkcli realeval` 门槛 G1–G6（REAL-SCAN-GT §3，修改前冻结）；`Detection.*` 白底白框/模糊边/伸出扫描边界；`Geometry.StraightenedImageAreaFillsTheOutputExactly`；ctest `realeval_fixture` | U：图 2–5；R：polaroid_92 拉正导出四边残白 6–17 px、4-up 外框在阴影带外 0.9–1.9 mm；U012（草稿 GT，7 张）：切入 1→0，外扩 0.97/1.90→0.25/0.55 mm，拉正导出残白 0.37→0.05 mm | 部分完成（U010 导出；U012 检测在调参集达标。缺：白底回归样本、圆形/深色/1600 DPI 真实样本、所有者复核 GT） |
| UX-03 | P1 | Shift 切换与规则形状 | U013 | 核心 `Edit.*`；自检 `runShapeEditing`（真实输入事件）；变异测试 | U：图 2 | 已完成（自动验证，PR #15 已合并；真实窗口待验收） |
| UX-04 | P1 | 整张扫描件旋转 | U014 | 核心 `ScanRotation.*`、`Geometry.QuarterTurns…`；自检 `runScanRotation`（四个方向的相片、点选、导出/回链、SHA-256）；变异测试 | U：图 6–7；R：转正只改导出方向、画布不变 | 已完成（自动验证，PR #16 已合并；真实窗口待验收） |
| UX-05 | P1 | 图库首页与图库内审阅 | U018、U019、U020 | 计划 | U：图 8–12 | 未开始 |
| UX-06 | P1 | 两个工作区与顶部工具栏 | U021 | 第 1 部分：自检 `runToolbar`（眼睛 → 视图切换 → 缩放；1100×700…1920×1080 × 默认/最宽/折叠栏宽共 16 种：不出界、不重叠、顺序不变、画布 ≥ 480 px；窄窗口下拉按钮可用并跟随 1–4 键；步骤提示随相片进度变化并可点击转到）；`runLayout` 顶栏常规检查（原 known-issue）。整套自检 2× 与中文 | U：图 13 | 部分完成：第 1 部分待合并（PR #25）；第 2 部分（两个工作区）待图库 U018–U020 |
| UX-07 | P1 | 回导修复、取消必需侧车 | U003、U004（主体）、U005、U006、U007 | `ExportRecords.*`（U003）；`RelinkV2.*` ×12（U004）；自检 U003 4 项、U004 9 项（替换 3 个已知问题）；场景 `relink` R1–R6 | U：图 14–15；R：R2/R4/R6 复现；U003：导出不再需要侧车；U004：改名无元数据→按画面列为候选、指定相片后自动选中；`_1` 后缀按文件名+画面匹配；新会话→指定相片新建对位，合成与整张相片逐像素一致 | 部分完成（U003 侧车可选；U004 证据模型与目标优先；U005 旋转与缩放确认并持久化；ICC U006、变更校验 U007；图 14 文件名空白待 Windows） |
| UX-08 | P1 | 自然融合蒙版默认值 | U015 | 核心 `NaturalBlend.*`、`MaskChoice.*`、`DeltaE2000.*`；`fkcli maskeval`（真实扫描 7 张）；自检 `runMaskDefaults`；变异测试 | U：图 16–17 | 已合并（PR #17；100%/200% 目视待所有者确认） |
| UX-09 | P2 | 数值拖动与双击输入 | U016 | 自检 `runScrubFields`（真实鼠标/键盘，1× 与 2×）；变异测试 | U：图 16–17 | 已合并（PR #18；真实触控板待验收） |
| UX-10 | P2 | 边缘折叠条与全宽底部导航 | U022 | 自检 `runLayout` 14 项（含准备）：胶片条全宽且左右栏/画布在其上方结束、折叠条在窗口三边；三面板分别折叠/展开（鼠标），折叠条不移动、画布变大并重新适合完整扫描件；命中区超出绘制宽度；三面板同时折叠；折叠后与改变窗口大小后单击仍选中指针下的相片；1100×700 / 1440×900 / 1920×1080 无重叠、顶栏不溢出；键盘（空格）；快捷键 Ctrl+Alt+1/2/3/0（macOS 跳过：原生菜单）；栏宽与折叠状态写入设置；重置布局；顶栏无折叠按钮。整套自检 2× 与中文运行 | U：图 18–24 | 待合并（PR #24；真实窗口高 DPI 外观待验收） |
| UX-11 | P2 | 简化应用图标 | U023 | `tools/check-icons.py`（CI）：尺寸齐全、透明、无太阳；旧图标该检查失败 | U：第 8 项；U023：太阳已去掉，各尺寸单独绘制 | 待合并（PR #22；任务栏/Dock 图标缓存待真机确认） |
| UX-12 | P1 | worklog、完成总结、使用指南 | U001 起每个 Unit；U029 定稿 | `tools/check-docs.py`；`tools/check-guide.py`（快捷键都在指南中）；`tools/freeze-regression.py`（冻结清单 81 项均有证据，自检后核对）；自检 “FREEZE <ID> …” 23 项（`--scenario freeze`） | WORKLOG 自 2026-10-30 起；U029：完成总结 COMPLETION.md、全量回归 REGRESSION.md、指南定稿 | 待合并（PR #29；所有者通读指南） |
| FK-001 | P1 | 未保存工程切换保护 | U008 | 自检 FK-001 5 项 | S：修复前只有退出时确认；U008：新建/打开/最近/拖入/恢复/退出统一询问，取消与保存失败不丢失修改 | 已完成（自动验证；真实窗口待验收） |
| FK-002 | P1 | 回链旋转传递到合成 | U005 | 核心 `RelinkTransform.*` ×3；自检 FK-002 5 项；场景 R7 | R：修复前内容检测建议 180°，最终输出与未旋转相片不一致；U005：确认的旋转保存于工程，最终输出与整张相片导出逐像素一致，保存重开后仍一致 | 已完成（自动验证；真实窗口待验收） |
| FK-003 | P1 | 缺失 ICC 解释与持久化 | U006 | 核心 `GradedProfile.*` ×3；自检 FK-003 4 项 | S：修复前无配置的调色图被当作扫描件色彩空间（核心测试中平均偏差约 1%）；U006：按确认的配置只转换一次，平均差 0.0000，重开工程后仍可解析 | 已完成（自动验证；真实窗口与外部软件待验收） |
| FK-004 | P1 | 隐私预设覆盖 XMP 作者 | U009 | 核心 `Privacy.*`（含原 `KnownIssue.FK004_*`、四种格式字节级读回）；自检 FK-004 2 项 | R：修复前 XMP 含 dc:creator；U009：所有通道统一过滤，明示写入字段，读回无身份信息 | 已完成（自动验证；外部软件读取待验收） |
| FK-005 | P1 | 统一内存预算与缓存生命周期 | U017 | `CacheLedger.*` ×6、`Memory.SystemMemoryReported`；自检 FK-005 9 项（批量识别、切换、100% 显示、合成预览、任务预留、内存压力、回导检查、取消）；`--scenario memory` 批次 1/10/50/100（PERFORMANCE-BUDGET §3）；CI 三平台 5/20 张批次测量；变异测试 9 项 | S：源码确认（识别保留全部预览图、每张看过的扫描件各留 ≤96 MB 解码缓存、合成预览从不释放、无预留）；U017：一个字节预算，屏幕外内容按最久未用释放并可完全重建 | 已合并（PR #19；真实 8/16 GB 设备与系统内存压力通知待验收） |
| FK-006 | P1 | 异步代次、取消与失败状态 | U002（框架）、U017 | 自检 TS1–TS4（U002） | U002：变异测试——去掉过期检查后 TS1 存旧预览、TS2 编辑被覆盖 | 部分完成（框架：U002；预算与队列：U017） |
| FK-007 | P1 | 源文件与调色图变更校验 | U007 | 核心 `SourceIdentity.*` ×5（含原 `KnownIssue.FK007_*`）；自检 FK-007 6 项 | R：修复前同路径同尺寸替换被当作原扫描；U007：全文件哈希核对，替换/中间改动/读取中改动被拒绝并说明原因；再次调色（同画面）照常使用；改名移动按内容找回 | 已完成（自动验证；真机待验收） |
| FK-008 | P1 | 备份恢复与可重现工程 | U008（UX-05 图库部分在 U018） | 核心 `DataProtection.*` ×5（含原 `KnownIssue.FK008_*`）；自检 FK-008 6 项 | R：修复前损坏工程只报“damaged”；U008：提示并恢复备份、保留损坏原件、损坏文件不覆盖好备份、恢复快照、输出参数随工程保存 | 部分完成（工程部分完成；图库在 U018） |
| FK-009 | P1 | Windows 文字与真实窗口验收 | U024 | 自检 `runTextLayout` / `--scenario text` 4 项（字形无方框；不超出自身框与窗口；不被面板侧面裁掉、标签/标题/按钮文字不省略；关键控件不被覆盖），矩阵 深浅色 × 3 尺寸 × 栏显示/折叠 × 右栏三页 + 3 个对话框；CI 英文 1×、中文 1×/2×、英文 1.5×，三个平台；“FK-010 the 8-bit refusal tells the user without the core's developer note”；变异测试 | C：历史 offscreen 截图问题——U024 查明：Windows 离屏平台找不到字体，退回方框字体引擎（已修复自检环境，检查能识别）；U024 发现右栏导回页溢出、设置标签省略、开发者说明外露 | 部分完成：自动检查待合并（PR #26）；真实窗口（HV-02/03/07）待验收 |
| FK-010 | P1 | 真实输出计划驱动无损提示 | U010 | 核心 `ExportPlan.*` ×3；自检 FK-010 2 项 | R：修复前 16→8 位仍标“逐像素一致”；U010：提示来自核心输出计划，降位深需明确确认 | 已完成（自动验证；真实窗口待验收） |
| FK-011 | P2 | 多显示器色彩与灰度预览一致 | U025 | 场景 `miniswhite` 8/16-bit 常规检查（CI）；自检 `runDisplayProfile` 8 项（模拟换显示器：预览重新转换且与原始分辨率一致、100% 补丁与合成预览重做、缩略图跟随、旧配置文件的结果不显示、输出不变、换回一致）；变异测试 7 项 | R：同一行预览均值 15.8 vs 原始分辨率 239.2；U025：两者均为 15.8 | 待合并（PR #21；真实双屏、热插拔、macOS 窗口色彩标记待验收） |
| FK-012 | P2 | DNG 与外部软件互操作 | U026 | CI（Core linux）LibRaw/rawpy 独立解码本软件的线性 DNG：相机空间数据与 16 位 TIFF 线性化后在 1 LSB 以内（实测约 0.3 LSB）、色彩矩阵按写入读取（`tools/dng-interop.py`）；`TiffRead.LinearDngIsRefusedWithGuidance`（先红后绿）；自检 “FK-012 a Linear DNG given back …”（回导与拖入） | S：FORMAT-MATRIX 冻结；PRD F05 A5 引用的 `test_dng` 不存在 | 待合并（PR #23；DNG 回导 v1.0 不支持为建议，待所有者确认；Capture One / Photoshop / Lightroom 打开记录待验收） |
| FK-013 | P2 | 实现、文档与测试契约一致 | U027（U001 起逐步） | `tools/check-claims.py`（CI：规范中提到的测试/文件/CI 作业/API 名称存在，否则须注明计划）；`ApiContract.*`（编译期签名与枚举值、稳定名称）；`tools/test_report.py` 分开 通过/失败/跳过/未启动/阻塞；自检结果计数 通过/失败/跳过/已知问题/不适用 | S：U027 首次检查 89 处无实物的声明（不存在的测试、C ABI、mobile-lite 作业、7 个签名不符），已全部改正或标注计划 | 待合并（PR #27） |
| FK-014 | P2 | 版本冻结与可追溯发行包 | U001（构建信息）、U028 | 发布工作流版本/提交一致性检查（U001）；U028：`tools/release/check-package.py`（包内容、许可证文本、BUILDINFO、版本）、`tools/release/test-windows-install.sh`（干净运行器上安装/从已发布版本升级/回退/卸载，用户数据逐字节核对）、`tools/release/test-macos-dmg.sh`（DMG 挂载、复制运行、升级/回退）、`Compat.*`（旧版本文件、未知字段保留、回退往返）、`tools/check-licenses.py` | S：安装包缺完整许可证文本；叠放 PR 无测试包 | 待合并（PR #28；真机安装 HV-08/09、签名与公证待所有者决定） |

## 已知问题测试登记（修复前失败）

| 测试 | 类型 | 需求 | 期望（v1.0 行为） | 当前结果 | 转常规于 |
| --- | --- | --- | --- | --- | --- |
| `KnownIssue.FK004_RemoveIdentityDropsCreatorFromSourceXmp` | 核心 gtest，WILL_FAIL | FK-004 | 移除身份后 XMP 不含作者 | 已转常规（U009）：`Privacy.RemoveIdentityDropsCreatorFromSourceXmp` 等 | U009 |
| `KnownIssue.FK007_ReplacedScanAtSamePathIsNotTrusted` | 核心 gtest，WILL_FAIL | FK-007 | 同路径被替换的扫描不被信任 | 已转常规（U007）：`SourceIdentity.ReplacedScanAtSamePathIsNotTrusted` | U007 |
| `KnownIssue.FK008_DamagedProjectOffersItsBackup` | 核心 gtest，WILL_FAIL | FK-008 | 损坏工程提示可用备份 | 已转常规（U008）：`DataProtection.DamagedProjectOffersItsBackup` | U008 |
| UX-01 count warning stays after the count is corrected | 应用自检 known-issue | UX-01 | 更正片数后警告更新 | 已转常规（U011）：“corrected manual count (mouse): the warning goes”等 17 项 | U011 |
| UX-07 renamed graded file without XMP or sidecar: no match | 应用自检 known-issue | UX-07 | 可选目标并完成回导 | 已转常规（U004）：“renamed file without metadata: offered by its picture, not pre-selected”“renamed file relinks once its print is chosen” | U004 |
| UX-07 editor suffix _1 without XMP: no match | 应用自检 known-issue | UX-07 | 证据匹配或手动选目标 | 已转常规（U004）：“editor suffix _1 matched by name and picture” | U004 |
| UX-07 new session: unchanged export cannot be relinked | 应用自检 known-issue | UX-07 | 保存后的工程重开可回导；未保存时有恢复快照 | 已转常规（U003 保存重开；U004 新建对位 4 项）；恢复快照仍待 U008 | U003/U004（U008） |
| FK-002 180° same-size graded file composited without rotation | 应用自检 known-issue | FK-002 | 合成使用确认的旋转 | 已转常规（U005）：5 项 FK-002 检查 | U005 |
| FK-010 16→8-bit export still claims bit-identical pixels | 应用自检 known-issue | FK-010 | 提示依据真实输出计划 | 已转常规（U010）：“FK-010 16→8-bit export is not announced as bit-identical”“FK-010 8-bit export only with explicit consent” | U010 |
| FK-011 MinIsWhite full-resolution display is inverted | 应用自检 known-issue（场景） | FK-011 | 预览与原始分辨率一致 | 已转常规（U025）：“FK-011 MinIsWhite 8/16-bit: Preview and Full show the same brightness” | U025 |
| UX-06 the top bar is wider than its column at 1100×700 with both rails shown | 应用自检 known-issue（`runLayout`，U022 发现） | UX-06 | 窄窗口顶栏不溢出，眼睛→视图切换→缩放顺序正确 | 已转常规（U021 第 1 部分）：“UX-10 window at 1100×700 … top bar fits”与 `runToolbar` 16 种尺寸组合 | U021 |

## Release 登记

自 2026-10-30 16:13 起不逐 Unit 发布（PLAN §4）；下表只登记已发布版本与合并情况。

| 版本 | Unit | PR | 提交 | Tag | Release | 平台 |
| --- | --- | --- | --- | --- | --- | --- |
| v0.5.0 | 基线 | #2 | `ce93e057a0e60485075d41bbac912b59919f48db` | `v0.5.0` | [Pre-release](https://github.com/fengk623/polaroid-autocrop/releases/tag/v0.5.0)（草稿由所有者发布） | macOS universal、Windows x64 |
| v0.6.0-alpha.1 | U001 | #3 | `3c009f881254fd3494861e63f2a1a636640245fe` | `v0.6.0-alpha.1` | [Pre-release](https://github.com/fengk623/polaroid-autocrop/releases/tag/v0.6.0-alpha.1)（运行 41122756426，发布后 SHA-256 校验通过） | macOS universal、Windows x64 |
| v0.6.0-alpha.2 | U002 | #4 | `7413e55c81cefe184c3e9dd14a05ca8c8401c08c` | 不建 | 不发布（运行 41128749850 已取消；所有者调整为全部完成后统一发布） | — |
| v0.6.0-alpha.3 | U003 | #5 | 已合并 | 不建 | 不单独发布 | — |
