# ChromaCMS · ROADMAP（仓库方向唯一来源）

> 本文件是**仓库方向的唯一来源**。`AGENTS.md`、`README.md`、`docs/handoff/` 都指向这里；
> 若其它文档与本文件冲突，以本文件为准。
>
> 更新日期：2026-11-13（方向重置）。基线：`practice` @ `bb96c7c`。

---

## 0. 定位（双主线）

ChromaCMS 同时承担两个目标：

| 主线 | 目标 | 验收形式 |
|---|---|---|
| **L · 理论学习** | 把"文件字节 → 屏幕像素"的完整色彩管理链路学透 | 中文理论手册 + 可运行实验 + 数值断言 |
| **E · 工程实践** | 可长期维护的跨平台色彩管理引擎 | CPU 参考实现 + 三后端 + CLI 工具 + 三级测试 |

**核心原则：一次实践 = 一篇理论文档 + 一个可运行实验 + 一组测试断言。** 三者缺一不算完成。

### 0.1 三条工程支柱（按优先级）

1. **可长期维护**：明确的分层、数据模型、无隐式转换、可观测的中间状态。
2. **色彩精确度保障**：CPU 参考实现（oracle）是唯一正确性来源；GPU 必须与它对齐；误差可量化（ΔE/ΔE ITP）。
3. **多文件类型覆盖**：静态 SDR/HDR、双层/多层、动图、视频——覆盖"容器 → 解码 → 色彩元数据"的差异。

性能在满足以上三条之后尽可能提高（GPU 管线、缓存、多线程解码），不作为第一阶段目标。

### 0.2 三条硬性约束

- **双平台同步**：每个阶段必须 Windows + macOS 双端验收通过才能进入下一阶段。
- **三后端同步**：手写 D3D12、手写 Metal、Skia（Ganesh D3D12 / Metal）三端同步推进，功能与精度都要对齐。
- **显示方案统一为"双视图对照"**：默认左右并排显示同一画面的两张实现——
  左：裸原生 API（手写 D3D12 / 手写 Metal）；右：Skia + 当前平台后端。
  支持 tab 切换与叠图对比；所有阶段的验收截图统一用这个视图。

### 0.3 依赖策略

- **工程全自写**：色彩数学、管线、元数据解析、测试框架均由本仓库实现。
- **`lcms2` 仅用于**：ICC profile 解析、以及和自写实现的交叉校验；**不用于**管线转换。
- **FFmpeg** 用于容器 demux / 解码；像素格式与色彩元数据保留到本项目的数据模型中。
- 新增其它第三方依赖前必须先问用户。

---

## 1. 目标终态

从**未解码的文件**一直到**屏幕上正确的像素**，全链路可控、可解释、可测试：

```
file bytes
  → 容器识别（PNG/JPEG/TIFF/HEIF/AVIF/EXR/GIF/APNG/WebP/MP4/MKV/...）
  → 解码（codec + pixel format：YUV/RGB、位深、chroma、range、matrix）
  → 色彩元数据解析（ICC / cICP / nclx / cHRM / gAMA / sRGB chunk / EXIF / HDR 静态元数据）
  → 元数据优先级与默认规则（例如：无 ICC 的 JPEG 按 sRGB）
  → 线性化（sRGB / gamma / PQ / HLG / linear）
  → 色域转换（2020/709/P3/ProPhoto/ACES → XYZ → 目标色域，Bradford 适应）
  → 色调映射（HDR↔SDR，可选）
  → 输出编码（scRGB 线性 / PQ / sRGB / P3；位深与量化）
  → 系统合成（Windows DWM / macOS WindowServer）
  → 显示设备
```

每一层都要能回答：**数据是什么、谁解释它、系统/驱动做了什么、怎么验证**。

---

## 2. 学习内容（L0–L5，对应 `docs/color/`）

| 层 | 主题 | 计划文档 |
|---|---|---|
| **L0 文件与容器** | 文件类型差异、容器结构、元数据位置、单层/双层/多页/动图/视频帧 | `01-container-and-frame-basics.md`、`02-png-jpeg-tiff-metadata.md`、`03-heif-avif-exr-metadata.md` |
| **L1 解码** | pixel format、位深、chroma、range、matrix、解码后不要丢信息 | `04-decode-pixel-formats.md`、`08-video-yuv-and-metadata.md` |
| **L2 色彩管理核心** | 传递函数、primaries/白点、RGB↔XYZ、Bradford、色域映射、tone mapping、量化 | `05-transfer-functions.md`、`06-primaries-and-xyz.md`、`07-icc-and-metadata-rules.md`、`09-gamut-and-tone-mapping.md` |
| **L3 系统显示层** | Windows DWM/DXGI/WCS、macOS WindowServer/ColorSync/EDR、截图 vs 屏幕 | `10-windows-display-path.md`、`11-macos-display-path.md` |
| **L4 显示设备** | 面板特性、HDR 能力、近似验证与误差声明 | `12-display-device-and-verification.md` |
| **L5 全链路** | 串联 + 可观测点 + 测试方法 | `13-end-to-end-pipeline.md` |

每篇文档固定结构：**概念 → 公式/数值例子 → 本仓库实验（命令）→ 断言/测试 → 常见误解**。

---

## 3. 工程架构

```
src/
  color/        纯 CPU 色彩管理（不依赖 Qt/GPU）
                transfer functions / primaries / matrices / adaptation /
                gamut mapping / tone mapping / quantization
  io/           容器探测 + 色彩元数据解析（PNG chunks、JPEG APP2、TIFF tags、
                ISOBMFF boxes、EXR header、MP4 boxes；ICC via lcms2）
  media/        FFmpeg demux/decode 封装；产出带 ColorDescription 的 Frame
  gfx/          原生上下文（D3D12 / Metal）
  render/       后端（手写 D3D12、手写 Metal、Skia）
  app/          Qt UI：双视图对照 + 元数据面板 + 管线开关
tools/
  chroma-probe  文件 → 容器/元数据/默认色彩解释（JSON 输出，供测试断言）
  chroma-bench  性能基准
  pattern-gen   合成测试图（渐变、色块、边界值、位深/色域矩阵）
tests/
  unit/ integration/ e2e/ corpus/ golden/
docs/
  color/  testing/  handoff/  ROADMAP.md
```

### 3.1 数据模型（方向性草案）

```cpp
namespace chroma::color {
struct Rgb { double r, g, b; };
struct Xyz { double x, y, z; };

enum class TransferFunction { Linear, Srgb, Gamma22, Gamma24, Pq, Hlg };
struct Primaries { double rx, ry, gx, gy, bx, by, wx, wy; };
struct ColorSpace { Primaries primaries; TransferFunction transfer; };
}

namespace chroma::io {
enum class MetadataKind { ICC, Cicp, Nclx, SrgbChunk, ChrmGama, ExifColorSpace, HdrStatic };
struct ColorMetadata { MetadataKind kind; std::vector<std::byte> bytes; std::string source; };
}

namespace chroma::media {
struct Frame {
    std::uint32_t width, height;
    PixelFormat format;                 // YUV/RGB、位深、chroma、range
    std::vector<Plane> planes;
    chroma::color::ColorDescription color;   // 解析后的色彩描述
    std::vector<chroma::io::ColorMetadata> provenance;  // 元数据来源
};
}
```

### 3.2 设计原则

1. **CPU oracle 单源真相**：`src/color` 定义正确结果；后端只执行。
2. **元数据不丢**：解码层保留原始 pixel format 与元数据，转换必须显式。
3. **无隐式转换**：每段接口声明输入/输出 `ColorDescription`。
4. **可观测**：每阶段可 dump 中间值（probe/测试/日志）。
5. **误差可量化**：所有比对输出 ΔE 指标，不靠肉眼。

---

## 4. 测试体系（详见 `docs/testing/00-index.md`）

| 层 | 对象 | 判据 |
|---|---|---|
| unit | 色彩数学、元数据解析、默认规则 | 绝对误差 1e-6 ~ 1e-5；字段精确匹配 |
| integration | 文件 → Frame → 色彩转换 → 输出 | ΔE2000 < 1.0（SDR）；ΔE ITP < 1.0（HDR） |
| e2e | 三后端 offscreen render → readback → 互比 + 对 CPU oracle | 后端间与 oracle 差异 < 阈值；截图仅辅助 |

测试资产：`tests/corpus/` 小样本入库（<10 MB）；大文件用 `tools/fetch-corpus.ps1`（或 .sh）下载并校验哈希；`pattern-gen` 生成合成图；`tests/golden/` 存参考输出与容差。

---

## 5. 显示方案：双视图对照（三后端同步）

窗口默认布局：

```
┌──────────────────────────────┬──────────────────────────────┐
│  左：裸原生 API                │  右：Skia + 当前平台后端       │
│  Windows: 手写 D3D12          │  Windows: Ganesh D3D12        │
│  macOS:   手写 Metal          │  macOS:   Ganesh Metal        │
└──────────────────────────────┴──────────────────────────────┘
   [Tab: 左 / 右 / 叠图(差值或透明度混合)]   元数据面板 / 管线开关
```

验收要求：
- 两侧使用**同一份输入、同一份目标色彩描述**；
- 截图对比 + 像素/ΔE 量化（e2e 测试）；
- 叠图模式用于快速发现几何/颜色偏差。

---

## 6. 阶段路线图

> 每个阶段都必须**双平台验收通过**才进入下一阶段。
> 每个阶段固定交付：学习文档 + 代码 + 测试 + 双视图截图证据。

### P0 · 治理与基线（约 2–3 天）

- 更新 `AGENTS.md` / `README.md` / `docs/ROADMAP.md` / `docs/color`、`docs/testing` 索引；
- `src/color` 骨架：迁移现有 `gfx/ColorMath.hpp` 的 PQ/矩阵逻辑，补传递函数与矩阵工具；
- `tools/chroma-probe` 骨架：输出文件格式 + 基础元数据 JSON；
- 测试骨架：unit 测试可跑、ctest 自动收集；
- app 双视图骨架（先用现有单图展示左右两份后端输出）。

**验收**：双平台构建绿；ctest 通过；双视图能显示左右两份结果。

**P0 状态（2026-11-13）：完成。** 已交付：`src/color` CPU oracle、`src/io` 容器探测、
`tools/chroma-probe`、`tests/corpus` + 生成/下载脚本、`docs/color/01` 首篇 + `unit.container_probe`、
双视图骨架（左/右/并排 Tab）。**叠图**需要离屏合成/差值渲染，放到 P1。

### P1 · 静态图三轨并行：SDR / HDR / 双层（约 2–3 周）

三条 track 并行推进，每个 track 独立交付文档+代码+测试：

| Track | 内容 | 文档 |
|---|---|---|
| A · SDR | PNG/JPEG/TIFF + ICC；sRGB/P3 目标；完整链路 | 02、05、06、07 |
| B · HDR | PQ/HLG、BT.2020、EXR/AVIF/HEIF、tone mapping；Windows scRGB/HDR 与 mac EDR | 03、09 |
| C · 双层/多层 | 多页 TIFF、分层 PSD/EXR（part/channel）、动图帧元数据继承 | 01 |

**验收（每条 track）**：双平台显示正确；unit+integration 通过；双视图截图；文档中的实验可复现。

### P2 · 视频接入 + 四轨精度收口（约 1–2 周）

- 静态图稳定显示后启动视频：FFmpeg demux/decode、YUV 矩阵/range/transfer、帧时序；
- SDR/HDR/双层/视频四轨并行，统一走 CPU oracle + ΔE 断言；
- 文档：04、08、13。

**验收**：双平台视频正确播放（首帧正确 + 连续帧稳定）；四轨 integration 通过。

### P3 · 系统显示链路实验（贯穿 P1–P2）

- Windows：DWM 合成 / DXGI colorspace / HDR 开关 / WCS-ICC 关联的实验记录；
- macOS：WindowServer/ColorSync/EDR 的实验记录；
- 文档 10、11、12：把"系统到底做了什么"写成可复现实验与结论。

**验收**：文档化实验步骤 + 截图 + 结论；双端结论不要求一致，但必须解释差异。

### P4 · 性能与工程化收口

- `chroma-bench`：解码/转换/上屏耗时基线；
- GPU 管线优化（避免每帧 CPU 转换、缓存、必要的并行）；
- CI 脚本：双平台构建 + ctest + 关键集成用例；
- 文档收口与索引更新。

**验收**：性能基线可复现；CI 绿；文档闭环。

---

## 7. 协作与提交（2026-11-13 变更）

| 项 | 规则 |
|---|---|
| 实现代码 | **由 agent 编写**（`.cpp/.hpp/.mm/.hlsl/.metal/.sksl`）；用户负责方向、关键决策、验收 |
| 报告格式 | 每次改动必须给出：`文件:行` 锚点 + 改了什么 + 为什么 + 验证结果 |
| 验证 | 每次交付前：构建 + ctest + 运行/截图；双平台阶段必须双端验证 |
| 提交 | **agent 在本地 commit**（按可验证里程碑）；**用户负责 push** |
| 远程仓库 | **未经用户明确授权，agent 禁止任何远程写操作**（push / tag / PR / 删除远程分支等） |
| 工作区 | 允许直接在工作区开发；实验性验证可放 `temp_and_scratch/`；不得静默重排无关代码 |
| 依赖 | 新增第三方依赖前必须问；`lcms2` 已批准（仅 ICC 解析/交叉校验） |

---

## 8. 风险与待定

| 项 | 说明 | 处理 |
|---|---|---|
| ICC 解析工作量 | 自写 ICC 解析器（tag 表、TRC/matrix/CLUT）不小 | 自写解析 + lcms2 交叉校验；先支持 matrix/TRC 类 profile，CLUT 类后续 |
| HDR 输出差异 | Windows scRGB/HDR10 与 mac EDR 的系统行为不同 | P3 实验文档化；不要求逐像素一致，要求可解释 |
| 双层/分层格式 | PSD 解析复杂、EXR part 语义特殊 | 先多页 TIFF + EXR part；PSD 作为可选扩展 |
| 测试资产体积 | 多格式样本可能大 | 小样本入库 + 下载脚本 + 合成图为主 |
| 三后端维护成本 | 三端同步推进 | 共用 CPU oracle 与测试；差异用 ΔE 量化 |
| 视频帧时序 | 音视频同步等超出色彩范围 | 本仓库只保证"帧内容正确"，不做 A/V 同步 |

---

## 9. 相关文档

- [`AGENTS.md`](../AGENTS.md)：协作规则与工作方式（agent 入口）
- [`docs/color/00-index.md`](color/00-index.md)：理论学习手册
- [`docs/testing/00-index.md`](testing/00-index.md)：测试方法手册
- [`docs/handoff/`](handoff/)：工程交接与历史方案（保留，不再是方向来源）
