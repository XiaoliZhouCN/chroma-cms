# 01 · 容器与帧基础：文件类型、单层/双层/多页/动画/视频帧

> 状态：已发布（2026-11-13）
> 实验：`tools/chroma-probe`
> 断言：`unit.container_probe`

## 0. 一句话

颜色管理不是在"像素"开始的，而是在**文件容器**开始的：先确定"这个文件是什么格式、里面有几帧/几层、
色彩元数据存在哪里、缺失时按什么默认值解释"，后面的解码和数学才有意义。

## 1. 概念：容器 / 编码 / 像素格式 / 色彩描述

| 概念 | 定义 | 例子 |
|---|---|---|
| **容器**（container） | 负责装箱的结构：存放数据块、轨道、帧 | PNG chunks、TIFF IFD、ISOBMFF boxes、RIFF chunks |
| **编码**（codec） | 把像素压缩成码流的算法 | DEFLATE（PNG）、JPEG DCT、HEVC、AV1 |
| **像素格式**（pixel format） | 解码后的数据排列：通道、位深、chroma、range | `rgba16`、`yuv420p10le`、`half` |
| **色彩描述**（color description） | 告诉系统"这些码值怎么解释" | ICC profile、cICP、nclx、sRGB chunk、EXR chromaticities |
| **帧/层**（frame/layer） | 数据组织单位 | 单图 1 帧；多页 TIFF；动画 GIF；视频多帧；PSD/EXR 多层 |

四个关键结论：

1. 容器与编码**互相独立**：同一个容器可以装不同编码（如 ISOBMFF 装 HEVC/AV1）；
2. 色彩描述可能**存在、部分存在、完全缺失**；
3. 缺失时"按 sRGB 解释"是**工程默认约定**，不是文件事实；必须记录为默认假设；
4. 解码前就丢弃元数据，后续任何精确色彩管理都不可能补回来。

## 2. 文件类型与色彩元数据位置

| 类型 | 容器结构 | 典型位深/格式 | 色彩元数据常见位置 |
|---|---|---|---|
| PNG | chunks | 8/16-bit、RGB/RGBA/灰度/调色板 | `iCCP`、`sRGB`、`gAMA`+`cHRM`、`cICP` |
| JPEG | markers + APPn | 8-bit YCbCr | APP2 `ICC_PROFILE`、APP1 Exif ColorSpace |
| TIFF | IFD 链 + tags | 8/16-bit、可能多页 | tag 34675（ICC）、PhotometricInterpretation |
| HEIF/AVIF | ISOBMFF boxes | 8/10-bit、HEVC/AV1 | `colr`（nclx / ICC）、`pixi`、`clli`、`mdcv` |
| EXR | header 属性 + part/channel | half/float、线性 | `chromaticities`、`whiteLuminance` |
| GIF/APNG/WebP | 帧块/动画块 | 8-bit | 通常只有 sRGB 默认假设 |
| MP4/MOV/MKV | tracks/samples + VUI/SEI | 8/10/12-bit YUV | VUI、`colr`、HDR 静态元数据 |

## 3. 帧 / 层语义

| 形态 | 语义 | 本仓库当前支持 |
|---|---|---|
| 单层单帧（PNG/JPEG） | 1 帧，元数据文件级 | ✅ |
| 多页（TIFF） | 每页可独立 IFD/元数据 | ✅（计数） |
| 动画（GIF/APNG/WebP） | 帧级 + 全局调色板/色彩假设继承 | ✅（帧计数） |
| 多层（PSD、EXR part/channel） | 层有合成顺序、透明度、局部色彩假设 | EXR channel 计数；PSD 计划中 |
| 视频（MP4/MKV） | 轨道级 VUI/容器级 colr，帧序列 | MP4 尺寸/元数据探测；完整解码在 P2 |

**重要区别**：多页/多帧 ≠ 多层。页/帧是"时间或独立图像"维度，层是"合成"维度。

## 4. 数值例子

### 位深与归一化

同一个归一化码值 0.5 在不同位深下的整数表示：

| 位深 | 0.0 | 0.5 | 1.0 |
|---|---|---|---|
| 8-bit | 0 | 128 | 255 |
| 10-bit | 0 | 512 | 1023 |
| 16-bit | 0 | 32768 | 65535 |

归一化公式：`code = value / (2^bits - 1)`。**不要**在解码阶段提前降位深——10-bit 的 512 与 8-bit 的 128
在数值上近似，但后续 PQ/HLG 的精度损失不可逆。

### 默认假设的两条真实样本

`chroma-probe` 对 `tests/corpus/jpeg/rgb8-no-icc.jpg` 的输出：

```json
"has_icc": false,
"notes": ["JPEG 无 ICC：按 sRGB 解释（默认假设）"]
```

对 `tests/corpus/png/rgba8-plain.png`：

```json
"has_icc": false,
"has_srgb_chunk": false,
"has_cicp": false,
"notes": ["PNG 无 iCCP/sRGB/cICP：按 sRGB 解释（默认假设）"]
```

这两条 note 就是后续色彩管理链路的**输入前提**：如果实现不记录它，就无法区分"文件真的表示 sRGB"
和"我们擅自假设 sRGB"。

## 5. 本仓库实验

```powershell
cmake --build --preset windows-x64-debug
ctest --preset windows-x64-debug -R unit.container_probe

# 单文件探测（JSON）
.\build\windows-x64-debug\Debug\chroma-probe.exe tests\corpus\png\rgba8-plain.png
.\build\windows-x64-debug\Debug\chroma-probe.exe tests\corpus\tiff\rgb8-multipage.tif
```

`tests/corpus/` 小样本（总量 < 80 KB）：

| 文件 | 预期探测结果 |
|---|---|
| `png/rgba8-plain.png` | png / 64×64 / 8-bit / rgba8 / 无色彩元数据（默认假设 sRGB） |
| `png/rgb8-srgb.png` | png / 8-bit / rgb8 / `has_srgb_chunk=true` |
| `png/rgba16.png` | png / 16-bit / rgba16 |
| `jpeg/rgb8-no-icc.jpg` | jpeg / 8-bit / yuv8 / 无 ICC（默认假设） |
| `tiff/rgb8-single.tif` | tiff / frames=1 |
| `tiff/rgb8-multipage.tif` | tiff / frames=2 |
| `gif/static.gif`、`gif/animated.gif` | gif / frames=1、2 |
| `webp/rgb8.webp` | webp / 64×64 |
| `exr/rgb-half.exr` | exr / 8×8 / half / 线性 |

**实际输出示例**（`gif/animated.gif`，省略未变化字段）：

```json
{
  "kind": "gif",
  "width": 64,
  "height": 64,
  "bits_per_channel": 8,
  "pixel_format": "palette8",
  "frames": 2,
  "layers": 1
}
```

## 6. 测试断言

`tests/unit/container_probe_test.cpp`（ctest：`unit.container_probe`）固定了本文章的全部结论：

- 格式识别：PNG/JPEG/TIFF/GIF/WebP/EXR 的 `kind` 精确匹配；
- 尺寸/位深/像素格式提示：例如 PNG 16-bit → `rgba16`；
- 帧/页语义：多页 TIFF `frames=2`、动画 GIF `frames=2`；
- 元数据存在性：`rgb8-srgb.png` 的 `has_srgb_chunk=true`；无 ICC 文件必须带"默认假设"note；
- 健壮性：截断的 PNG（前 20 字节）返回 `kind=png` 且不崩溃；空字节/不存在文件返回 `Unknown` + note。

## 7. 常见误解

| 误解 | 实际 |
|---|---|
| "PNG 一定是 sRGB" | 不一定；iCCP/gAMA+cHRM/cICP 都可能存在，无元数据只是约定 |
| "JPEG 没有 ICC 就是 sRGB" | 工程假设，需要显式记录为默认假设 |
| "先转 RGBA8 再处理颜色" | 丢位深/色度采样/range，不可逆 |
| "多层图 = 多张单图" | 层有合成顺序、透明度、局部色彩假设 |
| "视频首帧对了就行" | 帧级/轨道级元数据可能随场景变化，需逐帧校验 |
| "容器能解开 = 颜色信息齐全" | 容器/编码/色彩描述互相独立，可能缺任意一项 |

## 8. 边界与下一篇

- 本篇只回答"**这是什么容器、元数据在哪、缺什么**"；不解析 ICC 内容、不做传递函数；
- 下一篇 `02`：PNG/JPEG/TIFF 的元数据解析细节（iCCP 结构、APP2 分块、TIFF tag）；
- `03`：HEIF/AVIF/EXR 与 HDR 静态元数据（`colr`/nclx、`clli`/`mdcv`、EXR chromaticities）；
- `04`：解码后的 pixel format / 位深 / range。

> 维护约定：新增容器/格式时，同步更新本表、`tests/corpus/` 与 `unit.container_probe` 断言。
