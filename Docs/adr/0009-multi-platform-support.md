---
status: accepted
owner: XiaoliZhouCN
updated: 2026-11-05
---

# 0009. 多平台支持（macOS）

- **状态**：accepted（方向已定；Metal 实现待 macOS 环境）
- **日期**：2026-11-05
- **决策者**：XiaoliZhouCN
- **相关**：[0002](0002-qt-widgets-native-surface.md)（Qt 宿主）、[0003](0003-dual-render-backend.md)（双后端）、[0008](0008-unified-ffmpeg-decoding.md)（解码）

## 背景

项目原本以 D3D12 为硬约束，等同于 Windows-only。现在要求支持 macOS，这会击穿三处假设：

1. **没有 D3D12**。macOS 的图形 API 是 Metal；Skia 在 macOS 上走 Graphite/Ganesh 的 Metal 后端。
2. **"双后端一致性"实验的设备前提变了**。同一帧两后端比对的结论只在同一台机器、同一设备上成立。
3. **解码已经解决**（[0008](0008-unified-ffmpeg-decoding.md) 之后 FFmpeg 两端一致），剩下的差异集中在渲染与显示配置。

## 决策

1. **每个平台提供两条渲染路径**：
   | 平台 | 原生路径 | Skia 路径 |
   |---|---|---|
   | Windows | D3D12（已完成 M1） | Skia Ganesh D3D12（M3） |
   | macOS | **Metal（待实现）** | Skia Ganesh/Graphite Metal（M3 之后） |
2. **`IRenderer` 保持平台无关**（不出现 D3D12/Metal 类型），平台实现由 CMake 按 `CMAKE_SYSTEM_NAME` 选择源文件。
3. **双后端一致性测试只在 Windows 上作为门禁**；macOS 上改跑"与应用自身黄金图比对"。
   理由：跨平台跨设备的像素级一致性没有意义，而且 macOS 本机本就没有 D3D12 参照。
4. **色彩管线（M2）保持平台无关**：数学在 `src/color/`（CPU）与 `shaders/`（HLSL + SkSL + MSL）中，
   平台差异只体现在着色语言与交换链声明上。
5. macOS 的显示配置读取（M5）另走 `NSColorSpace` / ColorSync，与 Windows 的 WCS 并行实现，
   由 `IDisplayProfile` 抽象隔离。

## 后果

### 正面

- 学习面扩展为"两条渲染路径 × 两个平台"，对比实验更完整；
- 解码与色彩数学天然复用，跨平台成本主要在渲染与窗口层；
- CMake + Qt + FFmpeg 都已经跨平台，工具链不需要换。

### 负面 / 代价

- **原生 Metal 路径无法在当前开发机验证**。在没有 macOS 环境时写它，等于交付未验证代码；
  因此本轮只落接口与目录结构，实现与验证必须等 macOS 机器或 CI。
- 三套后端（D3D12 / Metal / Skia）意味着三份管线初始化代码，`gfx` 层的"薄封装"要重新设计成平台抽象。
- 交换链/呈现语义差异大（DXGI flip model vs `CAMetalLayer` 的 `nextDrawable`），
  呈现节奏与 vsync 处理要分别验证。
- ADR-0007（交换链格式与色彩空间）只对 Windows 有效，macOS 需要单独一份对应决策。

### 需要跟进

- [ ] 获得 macOS 开发环境后：先做 Metal 最小可运行（清屏），再接 `IRenderer`。
- [ ] 为 macOS 补一份"交换链/色彩空间"ADR（对应 ADR-0007）。
- [ ] CMake 增加 `macos-arm64-debug` / `macos-arm64-release` preset 与平台源文件选择。
- [ ] CI：Windows 上跑一致性测试，macOS 上跑黄金图测试——两者都不能替代对方。

## 替代方案

| 方案 | 否决理由 |
|---|---|
| macOS 只做 Skia Metal | 少写一条原生路径，但用户明确要求"再写一条原生 Metal 路径"，且对比实验会缺一半 |
| macOS 只做原生 Metal，不接 Skia | 与"两条路径对比"的学习目标冲突 |
| 用 MoltenVK 走 Vulkan 统一 | 多一层翻译，学不到 Metal 本身；与"原生路径"目标不符 |

## 参考

- [../ARCHITECTURE.md](../ARCHITECTURE.md) §2 分层
- [0003](0003-dual-render-backend.md)、[0007](0007-swapchain-format-and-color-space.md)
