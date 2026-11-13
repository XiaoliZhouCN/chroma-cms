#pragma once

#include <memory>

namespace chroma::render
{
class IRenderer;

/// 后端种类：Native = 手写后端（D3D12 / Metal），Skia = Ganesh 桥接后端。
enum class BackendKind
{
    Native,
    Skia,
};

/// 按平台创建默认后端（Native）：Windows -> D3D12，macOS -> Metal。
std::unique_ptr<IRenderer> createRenderer();

/// 显式创建指定后端。Skia 未编译进当前预设时返回 nullptr。
std::unique_ptr<IRenderer> createRenderer(BackendKind kind);
} // namespace chroma::render
