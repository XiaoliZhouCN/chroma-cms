---
extends: ../AGENTS.md
scope: src/media
owner: agent
last_verified: 2026-11-13
---
# src/media · AGENTS 本地扩展

## 继承
继承 `src/AGENTS.md` 与根 `AGENTS.md`。

## 本地约定
- 只负责"文件/容器 → 解码帧"，**不做色彩转换**（转换在 `src/color`）。
- 必须保留原始 pixel format 与色彩元数据（后续 `src/io` 接手，当前先不丢信息为原则）。
- 路径统一 UTF-8；FFmpeg C API 需要 `extern "C"` 隔离。
- 解码失败必须给出可读错误信息，不允许静默返回空帧。

## 验证入口
```powershell
cmake --build --preset windows-x64-debug --target chroma_media
.\build\windows-x64-debug\Debug\ChromaCMS.exe
```

## 本目录专属坑
- FFmpeg 头文件必须包 `extern "C"`，否则链接名会被 C++ 修饰。
- 10-bit/12-bit 数据不要提前降到 8-bit；保留位深信息。
