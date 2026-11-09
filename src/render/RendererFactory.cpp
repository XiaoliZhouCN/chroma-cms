#include "render/RendererFactory.hpp"

#include "render/IRenderer.hpp"

#if defined(_WIN32)
#include "render/d3d12/D3D12Renderer.hpp"
#elif defined(__APPLE__)
#include "render/metal/MetalRenderer.hpp"
#else
#error "ChromaCMS: 暂不支持的平台"
#endif

namespace chroma::render
{
std::unique_ptr<IRenderer> createRenderer()
{
#if defined(_WIN32)
    return std::make_unique<D3D12Renderer>();
#elif defined(__APPLE__)
    return std::make_unique<MetalRenderer>();
#endif
}
} // namespace chroma::render