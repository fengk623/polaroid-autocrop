# Installing FrameKeeper / 安装说明

## macOS (12 Monterey or later, Apple Silicon and Intel)

1. Open `FrameKeeper-<version>-macOS-universal.dmg` and drag **FrameKeeper** to **Applications**.
2. The app is not notarised yet. On the first launch macOS says it cannot verify the developer:
   - Right-click (or Control-click) FrameKeeper in Applications ▸ **Open** ▸ **Open**, or
   - macOS 15+: open it once, then **System Settings ▸ Privacy & Security ▸ Open Anyway**.
   This is needed only once.

## Windows 10 / 11 (64-bit)

- **Installer**: run `FrameKeeper-<version>-Windows-x64-Setup.exe`. No administrator rights are needed
  (it installs for the current user unless you choose otherwise).
- **Portable**: unzip `FrameKeeper-<version>-Windows-x64-portable.zip` anywhere and run `FrameKeeper.exe`.
- The files are not code-signed yet, so SmartScreen may show "Windows protected your PC": click
  **More info ▸ Run anyway**.

Verify downloads with `SHA256SUMS.txt` (macOS: `shasum -a 256 <file>`; Windows PowerShell:
`Get-FileHash <file>`).

FrameKeeper works offline. It never uploads images and sends no usage data.

## Upgrading, going back, uninstalling

- **Upgrade**: Windows — run the newer installer (it replaces the program in place; Qt's plugin folders are
  replaced whole). Portable — unzip the new version into a new folder. macOS — drag the new FrameKeeper to
  Applications and choose **Replace**.
- **Go back to an earlier version** (rollback): install the earlier release the same way (its installer, its
  ZIP, or its app from its DMG). Your settings, templates, unsaved-work recovery files and projects stay where
  they are. Projects keep schema `fk.project/1` across all versions so far: an earlier version opens a project
  saved by a later one. Saving it in 0.6.0-alpha.1 or 0.5.0 keeps the prints, boundaries, edits, export
  records and chosen ICC profiles, but drops what those versions do not know — automatic counting, scan rotation,
  per-print masks, confirmed relink adjustments and the saved output settings (they return to their defaults).
  From 0.7.0-alpha.15 on, a version keeps everything a later version wrote and tells you when a later version
  last saved the project. (Details: the project format document, §Compatibility, in the source.)
- **Uninstall**: Windows — Settings ▸ Apps ▸ FrameKeeper ▸ Uninstall (or `unins000.exe` in the program folder);
  portable — delete the folder; macOS — move FrameKeeper from Applications to the Trash. Uninstalling removes
  the program and its `.fkproj` file association only; your data stays.

Where your data lives (never inside the program folder; not removed by uninstalling):

| | Windows (installer and portable) | macOS |
| --- | --- | --- |
| Settings | `%APPDATA%\FrameKeeper\FrameKeeper.ini` | `~/.config/FrameKeeper/FrameKeeper.ini` |
| Templates, unsaved-work recovery | `%APPDATA%\FrameKeeper\FrameKeeper\` (`templates`, `recovery`) | `~/Library/Application Support/FrameKeeper/FrameKeeper/` |
| Projects, exports, final images | where you save them | where you save them |

Each package carries `BUILDINFO.txt` (version, commit, build), `LICENSE`, `THIRD-PARTY-LICENSES.md` and the full
licence texts in `licenses/`. Signing: Windows files are not code-signed; the macOS app is ad-hoc signed and not
notarised (hence the first-launch steps above).

---

## macOS（12 Monterey 及以上，Apple 芯片与 Intel 均可）

1. 打开 `FrameKeeper-<版本>-macOS-universal.dmg`，把 **FrameKeeper** 拖到 **应用程序**。
2. 目前尚未公证，首次打开时 macOS 会提示无法验证开发者：
   - 在“应用程序”中右键点按 FrameKeeper ▸ **打开** ▸ **打开**；或
   - macOS 15 及以上：先打开一次，然后到 **系统设置 ▸ 隐私与安全性 ▸ 仍要打开**。
   只需操作一次。

## Windows 10 / 11（64 位）

- **安装版**：运行 `FrameKeeper-<版本>-Windows-x64-Setup.exe`，默认只为当前用户安装，不需要管理员权限。
- **免安装版**：把 `FrameKeeper-<版本>-Windows-x64-portable.zip` 解压到任意位置，运行 `FrameKeeper.exe`。
- 文件尚未签名，SmartScreen 可能提示“Windows 已保护你的电脑”：点击 **更多信息 ▸ 仍要运行**。

可用 `SHA256SUMS.txt` 校验下载文件（macOS：`shasum -a 256 文件名`；Windows PowerShell：`Get-FileHash 文件名`）。

FrameKeeper 完全离线工作，不上传图片，也不发送任何使用数据。

## 升级、回退与卸载

- **升级**：Windows 安装版——直接运行新版本安装程序（原位替换程序；Qt 插件文件夹整体替换）。免安装版——把新版本解压到新文件夹。
  macOS——把新的 FrameKeeper 拖到“应用程序”，选择 **替换**。
- **回退到旧版本**：用同样方式安装旧版本（它的安装程序、ZIP 或 DMG 中的应用）。设置、模板、未保存工作的恢复文件和工程都留在原处。
  到目前为止所有版本的工程格式都是 `fk.project/1`：旧版本能打开新版本保存的工程。在 0.6.0-alpha.1 或 0.5.0 中保存时，
  相片、边界、编辑、导出记录和选定的 ICC 配置文件都会保留，但这些版本不认识的内容会丢失并恢复为默认值——自动识别片数、
  扫描件旋转、每张相片的蒙版、已确认的回导调整以及工程中保存的输出设置。从 0.7.0-alpha.15 起，程序会原样保留更新版本
  写入的全部内容，并在工程最后由更新版本保存时提示。（详见源代码中工程格式文档的 §Compatibility。）
- **卸载**：Windows——设置 ▸ 应用 ▸ FrameKeeper ▸ 卸载（或运行程序文件夹中的 `unins000.exe`）；免安装版——删除文件夹；
  macOS——把 FrameKeeper 从“应用程序”移到废纸篓。卸载只移除程序和 `.fkproj` 文件关联，你的数据保留。

你的数据位置（从不放在程序文件夹内，卸载不会删除）：

| | Windows（安装版与免安装版） | macOS |
| --- | --- | --- |
| 设置 | `%APPDATA%\FrameKeeper\FrameKeeper.ini` | `~/.config/FrameKeeper/FrameKeeper.ini` |
| 模板、未保存工作的恢复文件 | `%APPDATA%\FrameKeeper\FrameKeeper\`（`templates`、`recovery`） | `~/Library/Application Support/FrameKeeper/FrameKeeper/` |
| 工程、导出与最终成片 | 你保存的位置 | 你保存的位置 |

每个安装包都包含 `BUILDINFO.txt`（版本、提交、构建号）、`LICENSE`、`THIRD-PARTY-LICENSES.md` 以及 `licenses/` 中的完整许可证文本。
签名状态：Windows 文件未做代码签名；macOS 应用为临时（ad-hoc）签名，未公证（因此首次打开需要上面的步骤）。
