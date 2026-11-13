#pragma once

// gfx 层的共享入口头（当前只作为层标识与 include 锚点；
// 后续可放 Format / Framebuffer 等跨后端共享声明）。
//
// 注意：色彩数学不在这里，统一放在 src/color/。

namespace chroma::gfx
{
inline constexpr int kGfxLayerVersion = 1;
} // namespace chroma::gfx
