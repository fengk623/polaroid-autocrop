# 格式能力矩阵（Stage A 冻结，2026-10-30，源码 v0.5.0）

审核意见 9：先冻结格式能力，再决定 DNG 回导范围，不直接删减已有能力，也不扩展成 RAW 解码。
状态：**已实现**（代码存在且有自动测试）· **已验证**（独立工具/真实软件读回并记录版本）· **未验证** · **不支持**。
“已实现”不等于“已验证”：外部软件兼容性只有按版本/系统记录后才写“已验证”（FK-012）。

## 1. 导出（画面区域 / 整张相片 / 最终输出）

| 格式 | 位深 | 色彩 | 透明度 | 嵌入元数据 | 实现状态 | 外部读取验证 |
| --- | --- | --- | --- | --- | --- | --- |
| TIFF（LZW/ZIP/无压缩，BigTIFF 自动） | 8 / 16 | 灰度、RGB；ICC 嵌入或不嵌入 | 写入器支持 alpha 通道；圆形画面的透明外区尚未实现（U010） | XMP（我们的 ID + 选定 dc 字段）、基本 TIFF 标签 | 已实现（test_export、test_tiff_io） | 未验证（Photoshop / Lightroom / Capture One 待记录，U026） |
| PNG | 8 / 16 | 灰度、RGB；iCCP | 同 TIFF | iTXt XMP | 已实现（test_png_jpeg） | 未验证 |
| JPEG | 8（16-bit 源降为 8，显式提示） | RGB/灰度；ICC APP2 | 无（圆形外区须明示填充色，U010） | APP1 XMP | 已实现（test_png_jpeg） | 未验证 |
| Linear DNG（LinearRaw，16-bit，3 通道） | 16 | ColorMatrix1 + 线性化 | 无 | DNG 标签 + XMP | 已实现（结构/标签测试，output_stage 自检） | **独立解码（U026，CI）**：LibRaw（rawpy）按原样读出的相机空间数据与同一画面区域的 16 位 TIFF 导出线性化后一致（增益 1.0000–1.0001、平均误差约 0.3 LSB，圆形与矩形画面各 2 张），色彩矩阵按写入读取；LibRaw 自己的 sRGB 渲染把 D50 矩阵当 D65 处理，红色有偏差（仅记录）。Lightroom / Photoshop / Capture One 仍待记录 |

导出几何：像素裁切（无重采样，可能含角部边框）、拉直（Lanczos-3 单次重采样）；转正 90° 旋转无损。最终输出格式为 TIFF/PNG/JPEG（不含 DNG）。

## 2. 回导（读取调色后文件）

| 格式 / 子类型 | 读取实现 | 限制 | 状态 |
| --- | --- | --- | --- |
| TIFF 8/16-bit，灰度/RGB，可带 alpha，LZW/ZIP/无/PackBits/JPEG 压缩，条带/瓦片/BigTIFF | `readImageFile` → TiffSource | 支持 MinIsBlack、MinIsWhite、RGB、YCbCr（JPEG 压缩）；浮点、有符号、CMYK、Lab、调色板、LinearRaw 不支持（明确报错） | 已实现 |
| PNG 8/16-bit | libpng | — | 已实现 |
| JPEG 8-bit | libjpeg-turbo | 低于扫描位深，需确认“接受较低位深” | 已实现 |
| DNG（LinearRaw，包括本软件导出的） | **不读**：TiffSource 拒绝 PhotometricInterpretation 34892，并说明改用 16 位 TIFF（U026） | 回导文件过滤不列 .dng；拖入的 .dng 进入回导并显示同样说明，不会被当作扫描件导入 | **不支持**（U026 建议 v1.0 保持不支持，待所有者确认）；不改扩展名伪装 |
| 相机 RAW / Capture One 会话 / Lightroom 目录 | 不读 | 需求文档明确不在本轮范围 | 不支持（范围外） |
| 拖入窗口的 TIFF | 名称不含 `_edit_v` 的 TIFF 被当作**扫描件导入**，其余图像进入回导 | 改过名的调色 TIFF 拖入时会被导入为新扫描件（源码观察，Main.qml DropArea） | 已实现（行为待 U004 评审） |

## 3. 往返建议（发布前写入使用指南）

- 可靠母版：**16-bit TIFF**（与扫描件相同 ICC，或明确转换一次）。
- JPEG 往返可用，但位深降低、压缩有损，合成会提示。
- DNG：当前只作为导出给 RAW 工作流的选项；调色后请导出 TIFF 回导。

## 4. 待记录的外部软件验证（FK-012，U026；需要真实软件）

| 软件 | 版本/系统 | 打开 TIFF/PNG/JPEG/DNG | ICC 保留 | 位深 | XMP 去留 | 回导结果 |
| --- | --- | --- | --- | --- | --- | --- |
| Capture One | 待提供 | 待测 | 待测 | 待测 | 待测 | 待测 |
| Photoshop | 待提供 | 待测 | 待测 | 待测 | 待测 | 待测 |
| Lightroom Classic | 待提供 | 待测 | 待测 | 待测 | 待测 | 待测 |
