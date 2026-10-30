# 需求追踪表（26 项）

工单 → Unit → PR/提交 → 测试 → Tag/Release 的双向追踪。每个 Unit 完成后更新本表；状态只能依据证据变更。
状态：未开始 · 进行中 · 部分完成 · 已完成（自动验证）· 已完成（含真机/人工验收）· 待验证 · 阻塞。
证据标记沿用需求文档：U 用户实测 · R 已有定向复现 · S 源码确定 · C 历史 CI。

## 需求 → Unit → 测试 → 状态

| 需求 | 优先级 | 简述 | Unit | 测试（现有/计划） | 当前证据 | 状态 |
| --- | --- | --- | --- | --- | --- | --- |
| UX-01 | P1 | 自动识别片数开关 | U011 | 应用 known-issue “count warning stays after the count is corrected” → 修复后转常规 | U：图 1；R：2026-10-30 自检复现（WORKLOG） | 未开始（已复现） |
| UX-02 | P1 | 内外框精度；纯画面与像素裁切区分 | U010（导出）、U012（检测） | 真实扫描 GT 回归集；导出边缘白框测量 | U：图 2–5；R：polaroid_92 导出四边残白 4–10 px；4-up 外框落在亮带 ≈0.8–0.9 mm | 部分完成（U010 导出：默认标注拉正、输出计划、圆形画面；检测精度 U012） |
| UX-03 | P1 | Shift 切换与规则形状 | U013 | 真实输入事件回归（计划） | U：图 2 | 未开始 |
| UX-04 | P1 | 整张扫描件旋转 | U014 | 自检场景 `upright`（现状观察）；整图旋转回归（计划） | U：图 6–7；R：转正只改导出方向、画布不变 | 未开始（已复现） |
| UX-05 | P1 | 图库首页与图库内审阅 | U018、U019、U020 | 计划 | U：图 8–12 | 未开始 |
| UX-06 | P1 | 两个工作区与顶部工具栏 | U021 | FEATURE-FREEZE 回归 | U：图 13 | 未开始 |
| UX-07 | P1 | 回导修复、取消必需侧车 | U003、U004（主体）、U005、U006、U007 | `ExportRecords.*`（U003）；`RelinkV2.*` ×12（U004）；自检 U003 4 项、U004 9 项（替换 3 个已知问题）；场景 `relink` R1–R6 | U：图 14–15；R：R2/R4/R6 复现；U003：导出不再需要侧车；U004：改名无元数据→按画面列为候选、指定相片后自动选中；`_1` 后缀按文件名+画面匹配；新会话→指定相片新建对位，合成与整张相片逐像素一致 | 部分完成（U003 侧车可选；U004 证据模型与目标优先；U005 旋转与缩放确认并持久化；ICC U006、变更校验 U007；图 14 文件名空白待 Windows） |
| UX-08 | P1 | 自然融合蒙版默认值 | U015 | 计划 | U：图 16–17 | 未开始 |
| UX-09 | P2 | 数值拖动与双击输入 | U016 | 计划 | U：图 16–17 | 未开始 |
| UX-10 | P2 | 边缘折叠条与全宽底部导航 | U022 | 计划 | U：图 18–24 | 未开始 |
| UX-11 | P2 | 简化应用图标 | U023 | 计划 | U：第 8 项 | 未开始 |
| UX-12 | P1 | worklog、完成总结、使用指南 | U001 起每个 Unit；U029 定稿 | 文档检查（tools/check-docs.py） | WORKLOG 自 2026-10-30 起 | 进行中 |
| FK-001 | P1 | 未保存工程切换保护 | U008 | 自检 FK-001 5 项 | S：修复前只有退出时确认；U008：新建/打开/最近/拖入/恢复/退出统一询问，取消与保存失败不丢失修改 | 已完成（自动验证；真实窗口待验收） |
| FK-002 | P1 | 回链旋转传递到合成 | U005 | 核心 `RelinkTransform.*` ×3；自检 FK-002 5 项；场景 R7 | R：修复前内容检测建议 180°，最终输出与未旋转相片不一致；U005：确认的旋转保存于工程，最终输出与整张相片导出逐像素一致，保存重开后仍一致 | 已完成（自动验证；真实窗口待验收） |
| FK-003 | P1 | 缺失 ICC 解释与持久化 | U006 | 核心 `GradedProfile.*` ×3；自检 FK-003 4 项 | S：修复前无配置的调色图被当作扫描件色彩空间（核心测试中平均偏差约 1%）；U006：按确认的配置只转换一次，平均差 0.0000，重开工程后仍可解析 | 已完成（自动验证；真实窗口与外部软件待验收） |
| FK-004 | P1 | 隐私预设覆盖 XMP 作者 | U009 | 核心 `Privacy.*`（含原 `KnownIssue.FK004_*`、四种格式字节级读回）；自检 FK-004 2 项 | R：修复前 XMP 含 dc:creator；U009：所有通道统一过滤，明示写入字段，读回无身份信息 | 已完成（自动验证；外部软件读取待验收） |
| FK-005 | P1 | 统一内存预算与缓存生命周期 | U017 | 计划（批次内存测量） | S | 未开始 |
| FK-006 | P1 | 异步代次、取消与失败状态 | U002（框架）、U017 | 自检 TS1–TS4（U002） | U002：变异测试——去掉过期检查后 TS1 存旧预览、TS2 编辑被覆盖 | 部分完成（框架：U002；预算与队列：U017） |
| FK-007 | P1 | 源文件与调色图变更校验 | U007 | 核心 `SourceIdentity.*` ×5（含原 `KnownIssue.FK007_*`）；自检 FK-007 6 项 | R：修复前同路径同尺寸替换被当作原扫描；U007：全文件哈希核对，替换/中间改动/读取中改动被拒绝并说明原因；再次调色（同画面）照常使用；改名移动按内容找回 | 已完成（自动验证；真机待验收） |
| FK-008 | P1 | 备份恢复与可重现工程 | U008（UX-05 图库部分在 U018） | 核心 `DataProtection.*` ×5（含原 `KnownIssue.FK008_*`）；自检 FK-008 6 项 | R：修复前损坏工程只报“damaged”；U008：提示并恢复备份、保留损坏原件、损坏文件不覆盖好备份、恢复快照、输出参数随工程保存 | 部分完成（工程部分完成；图库在 U018） |
| FK-009 | P1 | Windows 文字与真实窗口验收 | U024 | 计划；真机部分待验收 | C：历史 offscreen 截图问题；本地 Linux 截图中英文可读 | 未开始 |
| FK-010 | P1 | 真实输出计划驱动无损提示 | U010 | 核心 `ExportPlan.*` ×3；自检 FK-010 2 项 | R：修复前 16→8 位仍标“逐像素一致”；U010：提示来自核心输出计划，降位深需明确确认 | 已完成（自动验证；真实窗口待验收） |
| FK-011 | P2 | 多显示器色彩与灰度预览一致 | U025 | 应用 known-issue “MinIsWhite full-resolution display is inverted”（场景 `miniswhite`） | R：同一行预览均值 15.8 vs 原始分辨率 239.2 | 未开始（已复现） |
| FK-012 | P2 | DNG 与外部软件互操作 | U026 | 计划；FORMAT-MATRIX 冻结 | S | 未开始 |
| FK-013 | P2 | 实现、文档与测试契约一致 | U027（U001 起逐步） | 测试报告区分已知问题（U001） | S | 未开始 |
| FK-014 | P2 | 版本冻结与可追溯发行包 | U001（构建信息）、U028 | 发布工作流版本/提交一致性检查（U001） | S | 进行中 |

## 已知问题测试登记（修复前失败）

| 测试 | 类型 | 需求 | 期望（v1.0 行为） | 当前结果 | 转常规于 |
| --- | --- | --- | --- | --- | --- |
| `KnownIssue.FK004_RemoveIdentityDropsCreatorFromSourceXmp` | 核心 gtest，WILL_FAIL | FK-004 | 移除身份后 XMP 不含作者 | 已转常规（U009）：`Privacy.RemoveIdentityDropsCreatorFromSourceXmp` 等 | U009 |
| `KnownIssue.FK007_ReplacedScanAtSamePathIsNotTrusted` | 核心 gtest，WILL_FAIL | FK-007 | 同路径被替换的扫描不被信任 | 已转常规（U007）：`SourceIdentity.ReplacedScanAtSamePathIsNotTrusted` | U007 |
| `KnownIssue.FK008_DamagedProjectOffersItsBackup` | 核心 gtest，WILL_FAIL | FK-008 | 损坏工程提示可用备份 | 已转常规（U008）：`DataProtection.DamagedProjectOffersItsBackup` | U008 |
| UX-01 count warning stays after the count is corrected | 应用自检 known-issue | UX-01 | 更正片数后警告更新 | 复现 | U011 |
| UX-07 renamed graded file without XMP or sidecar: no match | 应用自检 known-issue | UX-07 | 可选目标并完成回导 | 已转常规（U004）：“renamed file without metadata: offered by its picture, not pre-selected”“renamed file relinks once its print is chosen” | U004 |
| UX-07 editor suffix _1 without XMP: no match | 应用自检 known-issue | UX-07 | 证据匹配或手动选目标 | 已转常规（U004）：“editor suffix _1 matched by name and picture” | U004 |
| UX-07 new session: unchanged export cannot be relinked | 应用自检 known-issue | UX-07 | 保存后的工程重开可回导；未保存时有恢复快照 | 已转常规（U003 保存重开；U004 新建对位 4 项）；恢复快照仍待 U008 | U003/U004（U008） |
| FK-002 180° same-size graded file composited without rotation | 应用自检 known-issue | FK-002 | 合成使用确认的旋转 | 已转常规（U005）：5 项 FK-002 检查 | U005 |
| FK-010 16→8-bit export still claims bit-identical pixels | 应用自检 known-issue | FK-010 | 提示依据真实输出计划 | 已转常规（U010）：“FK-010 16→8-bit export is not announced as bit-identical”“FK-010 8-bit export only with explicit consent” | U010 |
| FK-011 MinIsWhite full-resolution display is inverted | 应用自检 known-issue（场景） | FK-011 | 预览与原始分辨率一致 | 复现 | U025 |

## Release 登记

自 2026-10-30 16:13 起不逐 Unit 发布（PLAN §4）；下表只登记已发布版本与合并情况。

| 版本 | Unit | PR | 提交 | Tag | Release | 平台 |
| --- | --- | --- | --- | --- | --- | --- |
| v0.5.0 | 基线 | #2 | `ce93e057a0e60485075d41bbac912b59919f48db` | `v0.5.0` | [Pre-release](https://github.com/fengk623/polaroid-autocrop/releases/tag/v0.5.0)（草稿由所有者发布） | macOS universal、Windows x64 |
| v0.6.0-alpha.1 | U001 | #3 | `3c009f881254fd3494861e63f2a1a636640245fe` | `v0.6.0-alpha.1` | [Pre-release](https://github.com/fengk623/polaroid-autocrop/releases/tag/v0.6.0-alpha.1)（运行 41122756426，发布后 SHA-256 校验通过） | macOS universal、Windows x64 |
| v0.6.0-alpha.2 | U002 | #4 | `7413e55c81cefe184c3e9dd14a05ca8c8401c08c` | 不建 | 不发布（运行 41128749850 已取消；所有者调整为全部完成后统一发布） | — |
| v0.6.0-alpha.3 | U003 | #5 | 已合并 | 不建 | 不单独发布 | — |
