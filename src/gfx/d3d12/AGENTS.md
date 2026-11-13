---
extends: ../../../AGENTS.md
scope: src/gfx/d3d12
owner: agent
last_verified: 2026-11-13
---
# src/gfx/d3d12 · AGENTS 本地扩展

## 继承
继承 `src/gfx/AGENTS.md`、`src/AGENTS.md` 与根 `AGENTS.md`。

## 本地约定
- **仅 Windows 编译**（`if(WIN32)`）；不得进入公共头。
- 交换链后台缓冲的状态（PRESENT↔RENDER_TARGET）与 Present 只允许 `D3D12Context` 修改；
  渲染器通过 `beginFrame()/endFrame()/commandList()/currentBackBuffer()` 使用。
- 一次性上传等操作使用 `beginCommandList()/submitAndWait()`，不要在帧中途另起 allocator。

## 验证入口
```powershell
cmake --build --preset windows-x64-debug --target chroma_gfx
.\build\windows-x64-debug\Debug\ChromaCMS.exe
```

## 本目录专属坑
- 交换链颜色空间在 `SetColorSpace1` 时设置；scRGB 16F 与 HDR 开关的关系见 docs/color/10。
- `_DEBUG` 下会启用 D3D12 调试层；Debug 构建的帧开销明显更高。
