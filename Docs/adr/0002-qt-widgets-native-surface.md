---
status: accepted
owner: XiaoliZhouCN
updated: 2026-11-04
---

# 0002. Qt Widgets 自绘控件承载原生表面

- **状态**：accepted
- **日期**：2026-11-04
- **决策者**：XiaoliZhouCN

## 背景

需要一个 Windows 原生窗口句柄（HWND）交给 DXGI 创建交换链，同时用 Qt 承担菜单、文件对话框、状态栏。

约束：

- 渲染由我们自己控制，Qt 不得参与像素合成；
- 需要稳定的 HWND 与正确的物理像素尺寸（高 DPI）；
- 界面元素很少（菜单 + 状态栏 + 一个视口）。

## 决策

用 **Qt Widgets**，视口是一个自绘 `QWidget` 子类 `ViewportWidget`：

1. `setAttribute(Qt::WA_NativeWindow)` 保证有独立 HWND；
2. `setAttribute(Qt::WA_OpaquePaintEvent)` + `WA_NoSystemBackground`，禁止 Qt 擦背景；
3. 在 `paintEvent` 中调用 `IRenderer::present()`，不使用 `QPainter` 绘制视口内容；
4. 交换链尺寸取 `width() * devicePixelRatio()`，`devicePixelRatio` 变化（拖到另一块屏）时重建交换链；
5. HWND 可能因重新 parent 而失效，`ViewportWidget` 负责检测并通知 renderer 重新绑定。

## 后果

### 正面

- 句柄与像素尺寸完全可控，高 DPI 行为可预期；
- Qt 只做"外壳"，渲染链路干净，便于学习与调试；
- 无 QML 场景图参与，不存在与 Skia 合成路径打架的问题。

### 负面 / 代价

- 需要自己处理 `WM_PAINT` 节奏与 vsync 的关系（后续由 `PlaybackClock` 统一）；
- 控件自绘意味着窗口尺寸变化时的重绘时机要自己管；
- 放弃 QML 的声明式 UI 便利（本项目 UI 需求小，可接受）。

### 需要跟进

- [ ] M1 验证：拖动窗口跨不同 DPI 显示器时，交换链正确重建且无闪烁。

## 替代方案

| 方案 | 否决理由 |
|---|---|
| Qt Quick + `QQuickRhiItem` / `QQuickWindow` | 场景图自带合成与呈现路径，会与"D3D12 自己控制呈现"冲突，学习目标被稀释 |
| `QWindow::fromWinId` 嵌入外部窗口 | 多一层所有权，句柄生命周期更复杂 |
| 纯 Win32，不用 Qt | 放弃 Qt 的菜单/对话框/国际化，收益不足以抵消成本 |

## 参考

- Qt 文档：`Qt::WA_NativeWindow`、`Qt::WA_OpaquePaintEvent`、`QWidget::paintEvent`
- [../ARCHITECTURE.md](../ARCHITECTURE.md) §3
