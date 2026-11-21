---
extends: ../AGENTS.md
scope: src/probe
owner: agent
last_verified: 2026-11-22
---
# src/probe · AGENTS 本地扩展

## 继承
继承 `src/AGENTS.md` 与根 `AGENTS.md`。未在此声明的规则沿用父级。

## 本地约定
- `ProbeConfig` / `ProbeCsvWriter` 只处理配置、坐标和 CSV，不依赖 renderer / app。
- Probe 读取 Stage B：`present()` 后的 renderer offscreen surface，不读取 DWM/WindowServer 的 display buffer。
- 采样坐标使用源图坐标，由 `CSMViewerWidget::imagePointToSurface()` 映射到 renderer 物理像素。
- 不把 probe 的文件 I/O 放进 `SkiaRenderer::drawFrame()`。

## 验证入口
```bash
cmake --build --preset macos-arm64-skia
CHROMA_PROBE=1 build/macos-arm64-skia/ChromaCMS.app/Contents/MacOS/ChromaCMS
python3 src/probe/verify_probe.py
```

## 本目录专属坑
- Probe 输出是 shader 的 raw linear F16 值，不是屏幕最终颜色。
- `image` / `loc_config` 使用仓库相对路径，加载时以 `CHROMA_SOURCE_DIR` 为根解析。
