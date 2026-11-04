---
status: active
owner: XiaoliZhouCN
updated: 2026-11-04
---

# ChromaCMS

> Windows 图像 / 视频显示工具 · 双渲染路径 · 自研色彩管理
> 同时是一个**学习型 Agent 产品**：一边做工具，一边实践"文档驱动 + Agent 协作"。

## 这是什么

一个用 Qt 承载窗口、用 D3D12 与 Skia(D3D12) 两条路径显示图像与视频的工具。

它不以功能为卖点，以**三条可验证的学习目标**为卖点：

1. **自己搭一条呈现链路**：DXGI 交换链 → 命令队列 → fence 同步 → 呈现，不使用任何渲染框架代劳。
2. **自己写色彩管理**：传输函数、色域矩阵、色调映射、抖动，全部落到 HLSL / SkSL，**不使用** Skia、Qt、Windows ICM 自带的色彩转换。
3. **证明两套实现等价**：同一份颜色数学，在原生 D3D12 与 Skia 后端上产出**像素级一致**的结果。

## 技术栈

| 层 | 选择 | 记录 |
|---|---|---|
| 窗口 / UI | Qt 6.11 Widgets，自绘 `QWidget` 承载原生表面 | [ADR-0002](Docs/adr/0002-qt-widgets-native-surface.md) |
| 图形 API | D3D12 原生 / Skia Ganesh D3D12 | [ADR-0003](Docs/adr/0003-dual-render-backend.md) |
| 色彩管理 | 自研 HLSL / SkSL，CPU 双精度参考实现作 oracle | [ADR-0004](Docs/adr/0004-self-implemented-color-management.md) |
| 视频解码 | FFmpeg（读取原始色彩元数据） | [ADR-0005](Docs/adr/0005-ffmpeg-for-video-decode.md) |
| 构建 | CMake 4.2 + VS 2026 生成器；Skia 源码构建（M3 前置） | [ADR-0006](Docs/adr/0006-cmake-vcpkg-source-built-skia.md) |
| 平台 | Windows 10/11 x64，D3D12 特性等级 ≥ 12_0（本机 Intel UHD 为 12_1） | — |

## 当前状态

| 项 | 值 |
|---|---|
| 阶段 | **M0 完成**：构建骨架 + 空窗口 + 测试链路（尚无渲染代码），下一步 M1 |
| 日常分支 | `develop` |
| 默认分支 | `main`（**保持为空**，仅作为占位） |
| 下一步 | 见 [Docs/ROADMAP.md](Docs/ROADMAP.md) 的 M1 出口条件 |

## 从哪读起

- 想知道**读哪份文档** → [Docs/INDEX.md](Docs/INDEX.md)（唯一检索入口）
- 想知道**规则边界** → [AGENTS.md](AGENTS.md)
- 想知道**怎么把工程跑起来** → [Docs/BUILD.md](Docs/BUILD.md)
- 想知道**颜色怎么算** → [Docs/COLOR_MANAGEMENT.md](Docs/COLOR_MANAGEMENT.md) → [Docs/COLOR_SPEC.md](Docs/COLOR_SPEC.md)

## 仓库结构

```
src/app/        Qt 外壳：窗口、菜单、状态栏、原生表面宿主
src/render/     渲染后端：IRenderer + D3D12 原生 + Skia D3D12
src/gfx/        D3D12 薄封装：设备、队列、交换链、fence、PSO
src/color/      CPU 参考实现（双精度）：传输函数、矩阵、ICC、显示配置
src/media/      图像 / 视频源：WIC、FFmpeg，输出 FrameData + ColorMetadata
shaders/hlsl/   原生后端着色器
shaders/sksl/   Skia 后端着色器
tests/          单元测试、黄金图、双后端一致性测试
Docs/           文档（见 INDEX）
instruments/    测量工件说明（不入库大文件）
scripts/        校验脚本
```

## 协作

本仓库按 [Docs/AGENT_WORKFLOW.md](Docs/AGENT_WORKFLOW.md) 协作：任务先声明范围，变更同提交带文档，收工写会话记录。
提交规范见 [Docs/CONVENTIONS.md](Docs/CONVENTIONS.md)。
