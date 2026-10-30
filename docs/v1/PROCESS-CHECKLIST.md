# 开发规范对照与每个 Unit 的检查表

SpektraLab 开发规范（AGENTS.md、PRD、ARCHITECTURE、CONTRACT、README、CI 与 git 历史中的 306 条规则 + 30 条
界面/API 规则）的**逐条对照**见 [docs/reference-audit.md §5](../reference-audit.md)（行号 SL-001…SL-336，
每行给出 SpektraLab 出处、我们的做法、适用性与对应文件）。2026-10-30 起这些行号由 FK-nnn 改为 SL-nnn，
避免与需求工单 FK-001…FK-014 混淆。本文件把其中与 v1.0 开发流程直接相关的规则落成每个 Unit 必须逐项勾选的检查表。

## 1. 每个 Unit 的检查表（写进 PR 描述，逐项给证据）

| # | 检查项 | 依据（SL 行 / 本仓库规则） | 证据形式 |
| --- | --- | --- | --- |
| P1 | 范围、明确不包含、依赖、验收条件已写入 UNITS.md | SL-031（每个功能有范围外清单）；需求文档 §七 | UNITS.md 链接 |
| P2 | 新功能先有 PRD，新模块/接口先有 RFC（状态 Accepted 再写代码）；难以逆转的选择写 ADR | AGENTS §3.1–3.2；SL-025…SL-028 | PRD/RFC 文件 |
| P3 | 接口或项目格式变化：API-SPEC、CONTRACT、序列化、兼容测试、迁移说明、RFC 同一提交更新 | AGENTS §3.5；SL-003、SL-007；需求文档 §七 | 提交 diff |
| P4 | 修复类：回归测试先红后绿，提交说明里展示失败输出 | AGENTS §3.6；SL “Show a new regression test red before green” | 已知问题测试转常规的提交 |
| P5 | 每条验收标准对应自动测试或明确的人工验收项 | AGENTS §3.3；docs/acceptance-matrix.md | TRACEABILITY.md |
| P6 | 提交说明写明行为变化、验证方式、跑过的门禁 | AGENTS §3.7 | git log |
| P7 | 速度、内存、质量的说法有测量（设备、OS、CPU、内存、输入） | AGENTS §3.8；SL “Measure, then claim” | PERFORMANCE-BUDGET.md / WORKLOG |
| P8 | 未经验证的设备、平台、外部软件不写“已验证” | AGENTS §3.8；需求文档 §二 证据标记 | Release 说明“未验证”一节 |
| P9 | 界面变化：离屏截图更新并人工查看；真实窗口截图列为待验收或附上 | SL-002、SL-006；AGENTS T2 | 自检截图、HUMAN-VERIFICATION |
| P10 | 界面文字不承诺超出测量的内容（例如“逐像素一致”只出现在有测试覆盖的路径） | SL-016 | 代码审查 + 测试 |
| P11 | 新增界面文字有中文翻译（CI `check-i18n`） | SL-017；H02 | CI |
| P12 | FEATURE-FREEZE 受影响条目回归通过；未提及功能未被删除或降级 | 需求文档 §一 | PR 描述列出条目 |
| P13 | ARCHITECTURE.md 只写已实现的内容，计划项标“计划” | AGENTS §3.9；SL-008、SL-012 | 文档 diff |
| P14 | CI 三平台全绿后才合并；红灯先修，不加新功能 | AGENTS §3.4 | PR 检查 |
| P15 | 合并前提供绑定 PR head 提交的测试包与测试说明 | 审核意见 2；需求授权 §三 | PR 中的 Release 工作流 artifacts |
| P16 | 发布：唯一 Tag、Pre-release、版本/提交/构建号/包/Tag 一致，SHA-256 校验 | 需求授权 §四–五；FK-014 | Release 页面、发布工作流日志 |
| P17 | WORKLOG、UNITS、TRACEABILITY、USER-GUIDE、PROGRESS 同步更新 | UX-12；AGENTS §3.10 | 文档 diff |
| P18 | 测试拥有自己的输入：临时目录、注入设置目录（FK_SETTINGS_DIR）、不碰用户设置与原图 | SL “A test owns its inputs” | 测试代码 |
| P19 | 不提交原图、私人照片、构建产物、密钥；不复制参考项目的代码、品牌、图标、文字 | 原始提示词约束；reference-audit §8 | `tools/check-repo.py`、审查 |
| P20 | 陷阱：踩过并耗时的问题写进 AGENTS §5 | SL “traps”；AGENTS §5 | AGENTS diff |

## 2. 与 SpektraLab 规范的有意差异（v1.0 起）

| SpektraLab | 我们 | 原因 |
| --- | --- | --- |
| “Do not commit or push unless asked” | 按 Unit 自主提交、推送、建 PR、合并、发布 | 所有者 2026-10-30 授权（不绕过仓库保护与审批） |
| 发布为 ad-hoc zip，修复不改版本号 | 每个 Unit 一个 SemVer 预发布（DMG、安装版、便携 ZIP、SHA256SUMS、BUILDINFO） | 所有者要求“按 Unit 发布”与可追溯 |
| `XCTExpectFailure(strict: true)` 标记已知失败 | 核心 ctest `WILL_FAIL` + 应用自检 known-issue（不再复现即变红） | 同一原则，换成本仓库的测试框架 |
| macOS 单平台 | macOS + Windows 同步交付，Linux 只用于开发与 CI | 产品范围 |
