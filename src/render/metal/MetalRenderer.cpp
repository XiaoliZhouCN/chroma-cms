#include "render/metal/MetalRendererImpl.hpp"

namespace chroma::render
{
MetalRenderer::MetalRenderer()
    : m_impl(std::make_unique<Impl>())
{
}

MetalRenderer::~MetalRenderer() = default;

bool MetalRenderer::initialize(void *nativeWindow, std::uint32_t width, std::uint32_t height)
{
    return m_impl->initializeObjc(nativeWindow, width, height);
}

void MetalRenderer::resize(std::uint32_t width, std::uint32_t height)
{
    if (!m_impl->initialized || width == 0 || height == 0) {
        return;
    }
    m_impl->context->resize(width, height);
}

void MetalRenderer::present()
{
    if (!m_impl->initialized) {
        return;
    }
    m_impl->presentObjc();
}

const char *MetalRenderer::backendName() const
{
    return "Metal";
}

void MetalRenderer::setImage(std::uint32_t width, std::uint32_t height,
                             const std::uint16_t *rgba16)
{
    if (!m_impl->initialized || rgba16 == nullptr || width == 0 || height == 0) {
        return;
    }
    m_impl->setImageObjc(width, height, rgba16);
}

void MetalRenderer::setQuarterTurns(int quarterTurns)
{
    m_impl->quarterTurns = ((quarterTurns % 4) + 4) % 4;
}

void MetalRenderer::setZoom(float zoom)
{
    m_impl->zoom = (zoom > 0.0f) ? zoom : 1.0f;
}
} // namespace chroma::render
