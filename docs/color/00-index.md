# 色彩管理学习手册（docs/color）

> 目标：把"文件字节 → 屏幕像素"的链路讲清楚、做成可运行实验、并用测试断言固定结论。
> 方向与阶段安排见 [`../ROADMAP.md`](../ROADMAP.md)。

## 0. 怎么用这套手册

每篇文章固定五段，缺一不可：

1. **概念**：这一步在做什么、为什么需要它；
2. **公式 / 数值例子**：给出可手算的数值（例如 PQ 码值 0.5 对应多少 nits）；
3. **本仓库实验**：一条可复现的命令（`chroma-probe` / 测试 / 双视图对比）；
4. **测试断言**：这份知识对应哪条 unit/integration/e2e 测试；
5. **常见误解**：实际工程里最容易搞错的地方。

学习方式建议：

- **先跑实验再看公式**：用 `chroma-probe` 打印真实文件的元数据，再去理解每个字段；
- **每篇的断言都要能跑通**：测试失败就说明理解（或实现）有偏差；
- **对照双视图**：左裸原生 API、右 Skia，同一输入同一目标，差值就是问题所在。

## 1. 分层索引

| 层 | 编号 | 标题 | 状态 | 对应实验 | 对应测试 |
|---|---|---|---|---|---|
| L0 | 01 | [容器与帧基础：文件类型、单层/双层/多页/动图/视频帧](01-container-and-frame-basics.md) | **已发布** | `chroma-probe <file>` | `unit.container_probe` |
| L0 | 02 | PNG / JPEG / TIFF 的容器结构与色彩元数据 | 计划 | `chroma-probe` + 样例文件 | `png_metadata_test`、`jpeg_metadata_test`、`tiff_metadata_test` |
| L0 | 03 | HEIF / AVIF / EXR 与 HDR 静态元数据（MDCV/CLLI、chromaticities） | 计划 | `chroma-probe` | `heif_avif_metadata_test`、`exr_metadata_test` |
| L1 | 04 | 解码后的像素格式：位深、chroma 子采样、range、matrix | 计划 | `chroma-probe --frame` | `pixel_format_test` |
| L2 | 05 | 传递函数：sRGB / gamma / PQ / HLG / linear | 计划 | `tools/pattern-gen` + unit | `transfer_function_test` |
| L2 | 06 | primaries、白点与 RGB↔XYZ：矩阵怎么来、Bradford 适应 | 计划 | pattern + 双视图 | `primaries_matrix_test`、`adaptation_test` |
| L2 | 07 | ICC profile 与元数据优先级规则（ICC vs cICP vs 默认 sRGB） | 计划 | `chroma-probe --icc` | `icc_parse_test`、`metadata_priority_test` |
| L2 | 08 | 视频：YUV 矩阵、limited/full range、BT.601/709/2020、HDR 元数据 | 计划 | 视频样例 + 首帧 probe | `video_color_test` |
| L2 | 09 | 色域映射与色调映射：相对色度、黑点补偿、HDR↔SDR | 计划 | pattern + ΔE 报告 | `gamut_mapping_test`、`tonemap_test` |
| L3 | 10 | Windows 显示链路：DXGI colorspace、DWM 合成、HDR 开关、WCS/ICC | 计划 | 系统设置对照实验 | `display_settings_matrix`（文档化） |
| L3 | 11 | macOS 显示链路：WindowServer/CoreAnimation、ColorSync、CAMetalLayer、EDR | 计划 | 系统设置对照实验 | 同上 |
| L4 | 12 | 显示设备与验证：面板色域/亮度、近似验证协议与误差声明 | 计划 | 灰阶/色块实测协议 | 文档化 |
| L5 | 13 | 全链路串联：从文件到屏幕的可观测点 | 计划 | 双视图 + probe + 测试 | e2e 套件 |

> 状态：计划 → 编写中 → 已发布。已发布的文章必须带可运行断言。

## 2. 学习路线（与 ROADMAP 阶段对应）

```
P0 基线
 └─ 01 容器与帧基础
P1 静态图三轨并行
 ├─ A 轨 SDR：02 → 05 → 06 → 07
 ├─ B 轨 HDR：03 → 09
 └─ C 轨 双层/多层：01（扩展）
P2 视频接入
 └─ 04 → 08 → 13
P3 系统显示链路（贯穿）
 └─ 10 / 11 / 12
```

## 3. 代码映射（学完能落到哪里）

| 知识点 | 代码位置 | 状态 |
|---|---|---|
| 传递函数 | `src/color/Transfer.*` | ✅ 已落地 |
| primaries / 矩阵 / 适应 | `src/color/Primaries.*`、`src/color/Matrix.*` | ✅ 已落地 |
| 输出编码（相对亮度 ↔ scRGB） | `src/color/Output.*` | ✅ 已落地 |
| 色域映射 / tone mapping | `src/color/gamut.*`、`src/color/tonemap.*` | P1 计划 |
| 元数据解析 | `src/io/FileProbe.*`（容器/存在性） | ✅ 已落地；ICC 内容 P1 |
| 解码 | `src/media/ImageLoader.*` | 图像解码已落地；Frame/ColorDescription P1 |
| 显示目标决策 | `src/app/`（目标色彩描述选择） | P1 计划 |
| GPU 执行 | `src/render/`（三后端） | ✅ 已落地 |

## 4. 参考标准（后面文章会引用）

- ITU-R BT.709 / BT.2020 / BT.2100 / BT.2390
- SMPTE ST 2084（PQ）、ARIB STD-B67（HLG）
- ICC.1:2022（ICC profile 规范）
- ISO/IEC 23000-22（HEIF）、ISO/IEC 23008-12（AVIF 图像项）、ISO/IEC 14496-12（ISOBMFF）
- PNG W3C 规范、EXR 规范（OpenEXR）
- CIE 15（色度学）、CIE 1931/1976
- Windows：DXGI colorspace 文档、WCS、DWM；macOS：ColorSync、Core Animation EDR
