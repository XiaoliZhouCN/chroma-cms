# src/app · 开发说明

## 这个目录是什么
Qt 应用层：窗口、viewport、输入事件、把渲染器/解码器串成可用的程序。

## 文件与职责
| 文件 | 职责 |
|---|---|
| `main.cpp` | 程序入口，创建 `MainWindow` |
| `MainWindow.*` | 无边框 availableGeometry 窗口：默认 Skia 单图；Enter 切 Skia/原生/双图；Tab 单图切 API、双图按住临时覆盖鼠标所在 view |
| `ViewportWidget.*` | 一个对照 view：CSMViewerWidget + AssistInfoWidget；A/D 旋转、Q/E 缩放、R 复位 |
| `CSMViewerWidget.*` | 原生表面 + renderer + 图像上传 + 视图变换；可运行时切换后端 |
| `AssistInfoWidget.*` | ViewportWidget 左上角半透明信息浮层 |

## 依赖关系
- 允许：Qt6、`render`（IRenderer/工厂）、`media`、`color`。
- 禁止：直接调用 `gfx` 平台上下文（由后端封装）。
- 被依赖：无（最终可执行目标）。

## 构建与测试
- target：`ChromaCMS`
- 层级：MainWindow → 两个 ViewportWidget；每个 ViewportWidget → 两个常驻 CSMViewerWidget（Native/Skia） + AssistInfoWidget。
- 单图/双图、两个 view、两个 API 共用同一套旋转/缩放状态；鼠标悬停选择保留，但共享状态下是否选中不影响变换结果。
- 默认 Skia 单图；Enter 循环：Skia 单图 → 原生 API 单图 → 原生 + Skia 双图；切换不 hide/show，避免 native surface 首帧闪烁。
- Tab：按住临时切换鼠标所在 view 的 API，松开恢复；另一个 view 不变；AssistInfo 同步。
- Esc 退出；窗口使用 availableGeometry，不遮挡菜单栏/Dock；背景黑色。
- 默认图片：`assets/test-images/PTMR_8ramps_17_2048x2048_10bit.png`。
- 测试：当前以运行 + 截图为主；后续引入 e2e 截图对比。

```powershell
cmake --build --preset windows-x64-skia
.\build\windows-x64-skia\Release\ChromaCMS.exe   # 双视图需要 skia 预设
```

## 怎么加 UI 功能
1. 先在 `MainWindow`/`ViewportWidget` 里确定归属；
2. 交互类改动同步更新 `docs/handoff/07-windows-interaction.md`（历史）与 `README`；
3. 需要新状态时优先放 `ViewportWidget` 内部，不扩散到 `main.cpp`。

## 相关文档
`docs/ROADMAP.md`（双视图验收）、根 `AGENTS.md` §3/§12
