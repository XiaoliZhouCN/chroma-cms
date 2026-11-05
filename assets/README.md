# 测试素材

> 全部素材由本仓库内的工具生成，无第三方版权问题。**不要放入来源不明的照片。**

## 内容

| 文件 | 说明 |
|---|---|
| `test-images/PTMR_9-16-9-full_2048x2048_10bit.png` | 主测试图（16-bit PNG 容器，存 10-bit PQ 码） |
| `test-images/PTMR_9-16-9-full_2048x2048_10bit_preview8.png` | 8-bit 预览（`code10 * 255 / 1023`），用于肉眼核对"码值视图" |
| `test-images/PTMR_9-16-9-full_2048x2048_10bit.csv` | 每个 ramp × step 的 `code10 / nits / value16` 对照表 |

## 规格

- 2048 × 2048，RGB（无 alpha）
- 8 条 ramp，自上而下：`gray - r - g - b - c - y - m - gray`
- 每条 ramp 65 个 step（64 个等距点 + 0 点），水平方向 0 → 10000 nits
- 传递函数 PQ（SMPTE ST 2084 / BT.2100 Table 4），原色 BT.2020，D65
- 存储约定：`value16 = code10 << 6`（低 6 位恒为 0）

## 一处必须知道的命名与内容的偏差

文件名写作 `9-16-9-full`，但**实际 cICP chunk 是 `[9, 16, 0, 1]`**：

| 字段 | 值 | 含义 |
|---|---|---|
| colour_primaries | 9 | BT.2020 |
| transfer_characteristics | 16 | PQ |
| matrix_coefficients | **0** | identity（PNG 像素是 RGB，无 YCbCr） |
| video_full_range_flag | 1 | full range |

生成时用了 `--cicp-matrix 0`。文件名沿用素材既有命名，**读取端不要按名字猜 matrix**，看 cICP。

## 复现

```powershell
python tools/gen_ptmr.py --out assets/test-images/PTMR_9-16-9-full_2048x2048_10bit.png --cicp-matrix 0 --preview
```

脚本只依赖 Python 标准库（手写 PNG 编码），产物字节可复现（无时间戳 chunk）。

## 校验

| 文件 | SHA-256（前 16 位） |
|---|---|
| `PTMR_..._10bit.png` | `362C1A2CACC00434` |
| `PTMR_..._preview8.png` | `4F0DDD615F0BD133` |

应用启动日志里出现 `(10-bit 还原)` 表示加载器识别出了 `value16 = code10 << 6` 的存储约定。
