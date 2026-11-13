# src/app · 开发说明

## 这个目录是什么
Qt 应用层：窗口、viewport、输入事件、把渲染器/解码器串成可用的程序。

## 文件与职责
| 文件 | 职责 |
|---|---|
| `main.cpp` | 程序入口，创建 `MainWindow` |
| `MainWindow.*` | 主窗口与状态栏（显示图像/后端信息） |
| `ViewportWidget.*` | 原生表面、渲染器生命周期、交互按键（A/D/Q/E/R） |

## 依赖关系
- 允许：Qt6、`render`（IRenderer/工厂）、`media`、`color`。
- 禁止：直接调用 `gfx` 平台上下文（由后端封装）。
- 被依赖：无（最终可执行目标）。

## 构建与测试
- target：`ChromaCMS`
- 测试：当前以运行 + 截图为主；后续双视图会引入 e2e 截图对比。

## 怎么加 UI 功能
1. 先在 `MainWindow`/`ViewportWidget` 里确定归属；
2. 交互类改动同步更新 `docs/handoff/07-windows-interaction.md`（历史）与 `README`；
3. 需要新状态时优先放 `ViewportWidget` 内部，不扩散到 `main.cpp`。

## 相关文档
`docs/ROADMAP.md`（双视图验收）、根 `AGENTS.md` §3/§12
