# 真实扫描真值（GT）规范

用于 UX-02（内外框精度）与 UX-07（回导往返）的真实扫描评估。审核意见 12：调参集与独立回归集分开；
标注记录尺寸、DPI 可信度、坐标约定、版本与边缘不确定区；原图、路径及可关联用户的信息未经单独确认不入库，
坐标文件入库前先审内容。

## 1. 数据集划分

| 集合 | 用途 | 规则 |
| --- | --- | --- |
| 调参集（tuning） | 开发检测算法时查看、调参、调试 | 可以反复查看；结论不能用来证明泛化 |
| 回归集（regression） | 每次检测变更后只跑分数，不看图调参 | 开发者不根据回归集逐图调参数；失败样本先登记，再决定是否移入调参集（移入后从回归集删除并记录） |

当前可用的所有者扫描（2026-10-30，全部不入库）：

| 扫描（以 SHA-256 前 12 位标识） | 规格 | 张数 | 背景 / 边框 | 建议集合 |
| --- | --- | --- | --- | --- |
| img20260702_0005 | 6213×7482，8-bit，800 DPI 标注（实测 826） | 4 | 白底、白框 | 调参 |
| polaroid_92 | 4102×4991，8-bit，1200 DPI，Adobe RGB，扫描时已紧裁 | 1 | 无背景 | 调参 |
| img001 | 4209×5079，16-bit，1200 DPI，EPSON Standard RGB | 1 | 无背景 | 回归 |
| polaroid_93 | 4085×5024，8-bit，1200 DPI | 1 | 无背景 | 回归 |
| img20260702 | 9692×11680，16-bit，679 MB | 4 | 白底、白框 | 回归（**阻塞**：无法传入开发环境） |

**缺口（阻塞 U012 的门槛冻结）**：没有 1600 DPI、深色背景、圆形画面、倾斜明显、混合尺寸、单张带背景的样本；回归集
只有 2 个可用文件。需要所有者补充扫描（建议每类 ≥ 2 张，总数 ≥ 12 个文件），或在其电脑上运行评估。

## 2. 标注文件格式（`<scan-sha12>.gt.json`，每个扫描一个）

```json
{
  "schema": "fk.realgt/1",
  "scan": { "sha256": "<完整 SHA-256>", "width": 6213, "height": 7482, "bits": 8, "channels": 3,
            "dpiTagged": 800, "dpiMeasured": 826, "dpiConfidence": "measured|tagged|unknown" },
  "set": "tuning|regression",
  "coordinates": "scan pixels, origin top-left, x right, y down; pixel (x, y) covers [x, x+1) × [y, y+1); corners are continuous positions on the edge line",
  "prints": [
    {
      "id": "P01",
      "outer": { "kind": "quad", "points": [[x, y], [x, y], [x, y], [x, y]] },
      "inner": { "kind": "quad", "points": [[x, y], [x, y], [x, y], [x, y]] },
      "uncertaintyPx": { "outer": [t, r, b, l], "inner": [t, r, b, l] },
      "notes": "e.g. bottom reagent band belongs to the picture; outer edge beside a shadow band"
    }
  ],
  "annotation": { "by": "owner|agent-draft", "reviewedBy": "owner|null", "date": "2026-10-30",
                  "appVersion": "0.6.0-alpha.1", "commit": "<sha>" }
}
```

- Outer：纸面边缘（白框外缘本身），**不含**纸边阴影带或扫描盖亮带；角点按相片自身方向的 左上→右上→右下→左下。
- Inner：成像窗口的边缘（白框与画面的交界）；圆形画面用 `{"kind":"ellipse","cx","cy","rx","ry","angle"}`。
- `uncertaintyPx`：在放大检查中边缘位置无法确定的宽度（例如阴影过渡、试剂残边），每边一个值。
- 开发者（agent）可以起草（`by: agent-draft`），**必须由所有者复核**（`reviewedBy: owner`）后才能作为验收依据。

## 3. 评估输出

每次运行记录：软件版本与提交、每个扫描/每张相片的外框切入（px 与 mm，切入 = 检测外框落在 GT 外框内侧超过不确定区）、
外扩、内框四边误差（有符号，px 与 mm）、漏检/多检、类型与方向；按 DPI、位深、背景、形状分层汇总。门槛在 U012
实施前按回归集冻结；回归集外框切纸必须为 0。

## 4. 隐私

原图与真实路径不入库；GT 文件只含尺寸、坐标、哈希与备注，入库前由所有者确认（审核 D7）。在此之前 GT 存放在
仓库之外（开发环境的临时目录或所有者电脑）。
