# FrameKeeper

[English](README.md) · **简体中文**

FrameKeeper 是一款免费、离线的桌面软件（macOS 与 Windows），面向用平板扫描仪（如 Epson V750）扫描拍立得相片的用户，支持 Polaroid 600 / i-Type / SX-70、Polaroid 圆形画框、Instax Mini / Square / Wide。

1. **自动找出每一张相片**：支持 300–1600 DPI、8/16 位的大尺寸 TIFF，一次扫描多张、倾斜、相互贴边都能处理；绝不裁到真实相框，不确定的结果会标记为需要复核。
2. **导出画面区域**（TIFF、PNG、JPEG 或 Linear DNG），在 Photoshop、Lightroom、Capture One 中调色。像素裁切模式导出的像素与扫描件逐位一致。
3. **重新关联**调色后的文件（依次通过内嵌 XMP、附属文件、文件名匹配，并做内容校验）。
4. **把调色后的画面放回真实扫描相框**：相框的每一个像素都来自原始扫描件；也可以改为生成边框（合成边框）。

扫描件永远不会被修改，不上传任何数据，也不会覆盖已有文件。

## 下载

在 [Releases](https://github.com/fengk623/polaroid-autocrop/releases) 下载最新版本：

| 系统 | 文件 |
| --- | --- |
| macOS 12 及以上（Apple 芯片与 Intel） | `FrameKeeper-<版本>-macOS-universal.dmg` |
| Windows 10/11 64 位，安装版 | `FrameKeeper-<版本>-Windows-x64-Setup.exe` |
| Windows 10/11 64 位，免安装 | `FrameKeeper-<版本>-Windows-x64-portable.zip` |

目前的安装包尚未进行代码签名，首次打开需要按 [docs/INSTALL.md](docs/INSTALL.md) 操作一次；可以用 `SHA256SUMS.txt` 校验文件。

## 快速上手

1. 把一张或多张 TIFF 扫描件拖进窗口（或 **文件 ▸ 导入扫描件…**）。
2. 在胶片条中逐张检查；红色点划线外框表示需要复核。用 **编辑外框 (O)** / **编辑内框 (I)** 拖动角点或边，或在右侧栏输入坐标。
3. **导出画面区域（⌘E / Ctrl+E）**，在调色软件中处理并保存。
4. **重新关联调色后文件（⌥⌘I / Ctrl+Alt+I）**，在 **合成 (4)** 视图中查看，然后 **最终导出（⇧⌘E / Ctrl+Shift+E）**。

小提示：黑色相框请用浅色背板扫描（黑框放在黑色背景上无法从像素中识别出轮廓）；保留扫描仪写入的 DPI 信息；每次使用同一个扫描托架时可以保存为模板。

## 从源码构建

见 [README.md](README.md#build-from-source)。

## 许可

MIT（见 [LICENSE](LICENSE)）。Qt 以 LGPL-3.0 方式使用；其他第三方库见 [THIRD-PARTY-LICENSES.md](THIRD-PARTY-LICENSES.md)。FrameKeeper 与 Polaroid、富士胶片无关，相关名称仅用于说明相纸规格。
