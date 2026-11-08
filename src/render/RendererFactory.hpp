#pragma once

#include <memory>

namespace chroma::render
{
class IRenderer;

/// 按平台创建后端：Windows -> D3D12，macOS -> Metal。
/// app 层只依赖这个工厂和 IRenderer，不直接包含任何平台头文件。
std::unique_ptr<IRenderer> createRenderer();
} // namespace chroma::render