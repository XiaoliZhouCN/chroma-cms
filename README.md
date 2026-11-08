# ChromaCMS · practice

> 手写渲染器练习：Qt 窗口 + 自写 D3D12 / Metal 后端，把 10-bit PQ（HDR）测试图正确显示，
> 并实现色彩管理（PQ EOTF → 色域转换 → 显示输出）。

## 当前状态（`practice` @ `e545d91`）

| 平台 | 后端 | 进度 |
|---|---|---|
| Windows | 自写 D3D12 | 设备/交换链 → 三角形 → 纹理 → FFmpeg 解码 → PQ→BT.709/scRGB（`outputScale=0.8`）✅；**交互待适配** |
| macOS | 自写 Metal | M1 清屏 → M2 全屏三角形 → M3 纹理 → M4 PQ→Display-P3（`outputScale=1.0`）→ 90° 旋转/缩放 ✅ |

两端色域是**刻意不同**的：Windows 走 BT.709/scRGB，macOS 走 Display-P3 线性。
共享 PQ 与 BT.2020→XYZ，最后一步矩阵按平台不同，不要求逐像素一致。

## 协作入口

所有 AI 协作规则、进度、验证配方都在 [`AGENTS.md`](AGENTS.md) 和 [`docs/handoff/`](docs/handoff/)。

- 第一次接续 / 不了解进度：先读 [`docs/handoff/README.md`](docs/handoff/README.md)
- 回 Windows 适配交互：直接看 [`docs/handoff/07-windows-interaction.md`](docs/handoff/07-windows-interaction.md)
- macOS 历史方案：`docs/handoff/03-metal-plan.md`

## 构建

Windows（PowerShell）：

```powershell
cmake --preset windows-x64-debug -DQT_ROOT="D:/Applications/QtCreator/6.11.1/msvc2022_64"
cmake --build --preset windows-x64-debug
.\build\windows-x64-debug\Debug\ChromaCMS.exe
```

macOS：

```bash
export QT_ROOT=$(brew --prefix qt)
cmake --preset macos-arm64-debug
cmake --build --preset macos-arm64-debug
open build/macos-arm64-debug/ChromaCMS.app
```

## 规则

- 用户写实现代码（`.cpp` / `.hpp` / `.mm` / `.hlsl` / `.metal`）；agent 写 CMake、建空文件、给逐处改法。
- 交付前在 `temp_and_scratch/` 副本里验证，不在用户工作区做实验。
- 不自动提交；用户说 commit 才提交，提交前构建必须绿。
- 不要 merge `develop`（与 `practice` 无共同祖先）。
