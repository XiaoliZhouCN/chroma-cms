---
status: active
owner: XiaoliZhouCN
updated: 2026-11-04
---

# 路线图

> 每个里程碑必须有**可验证的出口条件**。没有出口条件的条目不算里程碑，只是愿望。

## 当前：M0（文档骨架 + 构建骨架）

**出口条件**

- [x] 文档框架落地，`check-docs` 通过
- [x] git 仓库建立，`develop` 为日常分支，`main` 为空
- [ ] `CMakeLists.txt` + `CMakePresets.json` + `vcpkg.json` 可用
- [ ] Skia D3D12 源码构建成功，`external/skia/out/Release` 有产物
- [ ] 空窗口启动，`ctest` 有一个能跑的占位用例

## M1：原生 D3D12 显示静态图（sRGB 直通）

**目标**：把呈现链路跑通——**先不做颜色管理**，只证明"能看到一张正确的图"。

- DXGI 交换链（FLIP_DISCARD）、命令队列、fence 双帧在飞
- `QWidget` 原生表面宿主（`WA_NativeWindow` + `WA_OpaquePaintEvent`）
- WIC 解码 PNG → 上传纹理 → 全屏 quad → Present
- 处理 DPI 缩放（`devicePixelRatio` → 物理像素）

**出口条件**

- 打开一张 8-bit sRGB PNG，与 Windows 照片查看器目视一致
- 窗口缩放、拖动、最大化无撕裂、无崩溃
- 关闭时无 D3D12 对象泄漏（Debug Layer 无报告）

## M2：自研色彩管线（SDR）

**目标**：把 [COLOR_SPEC.md](COLOR_SPEC.md) 变成两遍渲染的 HLSL 实现，并用 CPU 参考实现验证。

- `src/color/` CPU 参考实现（双精度）+ 矩阵推导 + 常量生成脚本
- Pass 1：sRGB / gamma2.2 解码 → 线性 16F
- Pass 2：编码 + 8×8 Bayer 抖动 → 8-bit / 10-bit 输出
- 源色域 → 工作空间 → 显示色域 两条矩阵链
- YUV 输入路径（601/709，full/limited）
- 状态栏显示 `ColorMetadata` 与"是否假定了默认值"

**出口条件**

- SPEC §8 的 A1–A8 全部由 `ctest -R color` 覆盖并通过
- 灰阶渐变无可见色带
- 与 CPU 参考实现逐像素 ΔE00 ≤ 0.5

## M3：Skia D3D12 后端 + 双后端一致性

**目标**：同一套数学在 Skia 上复现，并证明等价。

- `GrDirectContext::MakeDirect3D` 接入**同一个** `ID3D12Device`
- 交换链后台缓冲 wrap 成 `SkSurface`（**【待验证】** 是否支持直接包裹）
- `SkRuntimeEffect` 加载 `.sksl` 实现 Pass 1 / Pass 2
- 像素探针验证：Skia 未插入任何自动色彩转换
- 运行时切换后端（菜单项）

**出口条件**

- 同一帧双后端最大像素差 ≤ 1 LSB
- 切换后端画面不跳变
- 性能差距在 20% 以内（仅记录，不作门槛）

## M4：视频（FFmpeg）

**目标**：播放视频，并**逐帧显示解码器给出的色彩元数据**。

- 解封装、解码、`AVFrame` → `FrameData`
- 从 `AVFrame`/`AVCodecParameters` 读 primaries/transfer/matrix/range
- 解码线程 + 双缓冲交接 + `PlaybackClock`
- 暂停、单帧步进、时间轴拖动

**出口条件**

- 播放 1080p H.264 视频，音画无所谓（无音频），帧率稳定
- 状态栏元数据与 `ffprobe` 输出一致
- 人为篡改元数据（如标 709 实际 601）能复现色偏 → 证明元数据链路是通的

## M5：显示配置与宽色域

- 读 Windows 显示 profile（WCS）→ 目标矩阵 + TRC
- Display P3 内容在 P3 屏上正确显示
- 多显示器不同 profile 时切换目标
- 无 profile 时回退 sRGB 并提示

**出口条件**

- P3 测试图在 sRGB 模拟下与浏览器（Chrome 色彩管理）目视一致
- 拔插显示器后目标 profile 正确刷新

## M6：HDR（二期，单独 ADR）

- PQ / HLG 解码与编码
- 10-bit 或 16F 交换链 + 正确的 `DXGI_COLOR_SPACE_TYPE`
- HDR10 静态元数据
- SDR 内容在 HDR 显示器上的呈现策略

**出口条件**：由 HDR ADR 定义（尚未编写）。

## 待决问题

| # | 问题 | 影响 | 状态 |
|---|---|---|---|
| Q1 | 交换链用 10-bit SDR 还是 scRGB 16F | 决定 ADR-0007 与 Pass 2 输出 | **proposed**，见 [ADR-0007](adr/0007-swapchain-format-and-color-space.md) |
| Q2 | Skia 能否直接 wrap 交换链后台缓冲为 `SkSurface` | 决定 M3 的呈现路径 | 待验证 |
| Q3 | Qt 走 vcpkg 还是官方安装器 | 决定 BUILD §5 的默认路径 | 待定 |
| Q4 | 是否需要多显示器同时渲染 | 影响 L0 的设备/交换链管理 | 待定 |
| Q5 | 视频播放是否需要音频 | 影响线程模型与时钟 | 待定（当前假设：不需要） |
| Q6 | 黄金图是否引入 Git LFS | 影响仓库体积 | 待定（当前假设：合成图 < 200 KB，不用 LFS） |

新问题直接加进本表，不要只在聊天里讨论——聊天结论会丢。
