#include "render/RendererFactory.hpp"

#include "render/IRenderer.hpp"

#include <cstdlib>
#include <string>

namespace
{
std::string readEnv(const char *name)
{
#if defined(_MSC_VER)
    char *value = nullptr;
    std::size_t length = 0;
    if (_dupenv_s(&value, &length, name) != 0 || value == nullptr) {
        return {};
    }
    std::string result(value);
    std::free(value);
    return result;
#else
    const char *value = std::getenv(name);
    return (value != nullptr) ? std::string(value) : std::string{};
#endif
}
} // namespace

#if defined(_WIN32)
#include "render/d3d12/D3D12Renderer.hpp"
#elif defined(__APPLE__)
#include "render/metal/MetalRenderer.hpp"
#endif

#if defined(CHROMA_HAS_SKIA)
#include "render/skia/SkiaRenderer.hpp"
#endif

namespace chroma::render {

std::unique_ptr<IRenderer> createRenderer()
{
    std::string requested = readEnv("CHROMA_BACKEND");
#if defined(CHROMA_DEFAULT_BACKEND)
    if (requested.empty()) {
        requested = CHROMA_DEFAULT_BACKEND;
    }
#endif

#if defined(CHROMA_HAS_SKIA)
    if (requested == "skia") {
        return std::make_unique<SkiaRenderer>();
    }
#endif

#if defined(_WIN32)
    return std::make_unique<D3D12Renderer>();
#elif defined(__APPLE__)
    return std::make_unique<MetalRenderer>();
#endif
}

} // namespace chroma::render