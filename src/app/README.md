# src/app · 开发说明

## 这个目录是什么
Qt 应用层：窗口、viewport、输入事件、把渲染器/解码器串成可用的程序。

## 文件与职责
| 文件 | 职责 |
|---|---|
| `main.cpp` | 程序入口，创建 `MainWindow` |
| `MainWindow.*` | 双视图主窗口：左（手写原生）/右（Skia）/并排 Tab + 状态栏 |
| `ViewportWidget.*` | 原生表面、指定后端（Native/Skia）、交互按键（A/D/Q/E/R） |

## 依赖关系
- 允许：Qt6、`render`（IRenderer/工厂）、`media`、`color`。
- 禁止：直接调用 `gfx` 平台上下文（由后端封装）。
- 被依赖：无（最终可执行目标）。

## 构建与测试
- target：`ChromaCMS`
- 双视图：左 = 手写原生后端，右 = Skia；默认并排，Tab 可切 左/右/并排（叠图 P1）。
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
