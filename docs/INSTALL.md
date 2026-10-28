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
