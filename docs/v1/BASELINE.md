# 基线登记（Stage A，2026-10-30）

需求文档要求：开始开发时登记用户实际安装包的准确版本及源提交，不能仅凭截图推定。

## 源码与构建

| 项 | 值 | 证据 |
| --- | --- | --- |
| 仓库 | https://github.com/fengk623/polaroid-autocrop | — |
| 审查基线（需求文档） | `main` = `379c87d94eda1fdf7f2c2174ec4d7bb0b692f6fa`，VERSION 0.1.0 | CI 运行 [40767335922](https://github.com/fengk623/polaroid-autocrop/actions/runs/40767335922)（push，成功） |
| 0.1.0 安装包 | 草稿 Release v0.1.0（未发布）：`FrameKeeper-0.1.0-macOS-universal.dmg`（64 642 900 B）、`…-Windows-x64-Setup.exe`（48 016 866 B）、`…-Windows-x64-portable.zip`（59 085 493 B） | Release 运行 [40767423677](https://github.com/fengk623/polaroid-autocrop/actions/runs/40767423677)（workflow_dispatch，提交 379c87d，成功） |
| 冻结基线 | `v0.5.0` = PR #2 合并提交 `ce93e057a0e60485075d41bbac912b59919f48db`（只改版本号、文档、CI；无功能变化） | PR #2 head `492de9e` 全部检查成功；发布运行 41119771659 |
| v1.0 工作起点 | `unit/U001-foundation`（基于 `ce93e05`） | 见 UNITS.md |

0.1.0 与 0.5.0 的程序功能相同（0.5.0 只改版本号与 CI 运行环境）；Stage A 复现均在 0.5.0 源码上进行。

## 用户实际安装包

| 项 | 状态 |
| --- | --- |
| 实际安装的文件名、版本 | **未确认**。截图中的界面（中文、“• 未命名 — FrameKeeper”、0.1.0 时期布局）与 0.1.0 草稿安装包一致，但仅为推测。 |
| 源提交 | **未确认**。0.1.0 的“关于”只显示版本号，不显示提交。从 0.6.0-alpha.1 起，“关于”对话框、`--version` 与自检报告显示完整提交 SHA 与构建号，安装包内附 BUILDINFO.txt。 |
| 平台 | Windows（截图为 Windows 标题栏与资源管理器图标） |
| 需要所有者提供 | “帮助 → 关于 FrameKeeper”的截图，以及所装文件名（Setup.exe 或 portable.zip） |

## 开发与复现环境

| 项 | 值 |
| --- | --- |
| 系统 | Ubuntu 24.04.5 LTS（云端开发容器），2 vCPU，7 GiB 内存 |
| 编译器 | GCC 13.3.0；CMake 3.28.3（本地）/ 3.31.6（CI） |
| Qt | 6.8.3（本地自建 host 版：Quick、Controls、offscreen；CI 用官方二进制） |
| 应用验证方式 | 离屏 QPA + 软件场景图（不等同真实桌面窗口，FK-009） |
| 真实扫描（所有者提供，不入库） | img001.tif（4209×5079，16-bit，1200 DPI，1 张）、img20260702_0005.tif（6213×7482，8-bit，800 DPI 标注/826 实测，4 张）、polaroid_92.tif（4102×4991，8-bit，1200 DPI，Adobe RGB，1 张）、polaroid_93.tif（4085×5024，8-bit，1200 DPI，1 张）；img20260702.tif（9692×11680，16-bit，679 MB，4 张）**无法传入**（单文件 400 MB 上限） |

## CI 环境（2026-10-30 起）

Linux `ubuntu-24.04`、macOS `macos-15`（arm64；macos-14 于 2026-11-02 退役，见 AGENTS T18）、Windows `windows-2022`；依赖缓存按任务类型与运行镜像/SDK 区分（AGENTS T17）。
