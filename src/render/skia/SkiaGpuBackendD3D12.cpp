#include "render/skia/SkiaGpuBackend.hpp"

#if defined(_WIN32)

#include "gfx/d3d12/D3D12Context.hpp"

#include "include/core/SkColorSpace.h"
#include "include/core/SkSurfaceProps.h"
#include "include/gpu/ganesh/GrBackendSurface.h"
#include "include/gpu/ganesh/GrDirectContext.h"
#include "include/gpu/ganesh/SkSurfaceGanesh.h"
#include "include/gpu/ganesh/d3d/GrD3DBackendContext.h"
#include "include/gpu/ganesh/d3d/GrD3DBackendSurface.h"
#include "include/gpu/ganesh/d3d/GrD3DDirectContext.h"
#include "include/gpu/ganesh/d3d/GrD3DTypes.h"

#include <filesystem>
#include <stdexcept>
#include <string>

namespace chroma::render
{
namespace
{
constexpr DXGI_FORMAT kOffscreenFormat = DXGI_FORMAT_R16G16B16A16_FLOAT;
} // namespace

class SkiaGpuBackendD3D12 final : public SkiaGpuBackend
{
public:
    bool initialize(void *nativeWindow, std::uint32_t width, std::uint32_t height) override;
    void resize(std::uint32_t width, std::uint32_t height) override;

    sk_sp<GrDirectContext> makeGaneshContext() override;
    sk_sp<SkSurface> createSurface(int width, int height) override;

    bool beginFrame() override;
    void endFrame(SkSurface &surface) override;

    std::string shaderPath(const char *name) override;
    const char *backendName() const override;

private:
    void copySurfaceToScreen();

    std::unique_ptr<gfx::D3D12Context> context;
    sk_sp<GrDirectContext> gr;
    gfx::ComPtr<ID3D12Resource> offscreen;
    GrBackendRenderTarget backendRT;
    bool initialized = false;
};

bool SkiaGpuBackendD3D12::initialize(void *nativeWindow,
                                     std::uint32_t width,
                                     std::uint32_t height)
{
    if (initialized) {
        return true;
    }
    if (nativeWindow == nullptr || width == 0 || height == 0) {
        throw std::runtime_error("SkiaGpuBackendD3D12::initialize 参数无效");
    }

    context = std::make_unique<gfx::D3D12Context>();
    context->initialize(nativeWindow, width, height);

    GrD3DBackendContext backend;
    context->adapter()->AddRef();
    backend.fAdapter.reset(context->adapter());
    context->device()->AddRef();
    backend.fDevice.reset(context->device());
    context->queue()->AddRef();
    backend.fQueue.reset(context->queue());

    gr = GrDirectContexts::MakeD3D(backend);
    if (!gr) {
        throw std::runtime_error("GrDirectContexts::MakeD3D 失败");
    }

    initialized = true;
    return true;
}

void SkiaGpuBackendD3D12::resize(std::uint32_t width, std::uint32_t height)
{
    if (!initialized || width == 0 || height == 0) {
        return;
    }

    context->waitForGpu();
    offscreen.Reset();
    backendRT = GrBackendRenderTarget();
    context->resize(width, height);
}

sk_sp<GrDirectContext> SkiaGpuBackendD3D12::makeGaneshContext()
{
    return gr;
}

sk_sp<SkSurface> SkiaGpuBackendD3D12::createSurface(int width, int height)
{
    if (!gr || !context || width <= 0 || height <= 0) {
        return nullptr;
    }

    D3D12_RESOURCE_DESC desc{};
    desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    desc.Width = static_cast<UINT>(width);
    desc.Height = static_cast<UINT>(height);
    desc.DepthOrArraySize = 1;
    desc.MipLevels = 1;
    desc.Format = kOffscreenFormat;
    desc.SampleDesc.Count = 1;
    desc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
    desc.Flags = D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;

    D3D12_HEAP_PROPERTIES heap{};
    heap.Type = D3D12_HEAP_TYPE_DEFAULT;
    gfx::ThrowIfFailed(
        context->device()->CreateCommittedResource(
            &heap, D3D12_HEAP_FLAG_NONE, &desc, D3D12_RESOURCE_STATE_RENDER_TARGET,
            nullptr, IID_PPV_ARGS(&offscreen)),
        "CreateCommittedResource(skia offscreen)");

    GrD3DTextureResourceInfo info(offscreen.Get(), nullptr, D3D12_RESOURCE_STATE_RENDER_TARGET,
                                  kOffscreenFormat, 1, 1,
                                  DXGI_STANDARD_MULTISAMPLE_QUALITY_PATTERN);
    GrBackendRenderTarget rt = GrBackendRenderTargets::MakeD3D(width, height, info);
    backendRT = rt;

    SkSurfaceProps props;
    sk_sp<SkSurface> surface = SkSurfaces::WrapBackendRenderTarget(
        gr.get(), rt, kTopLeft_GrSurfaceOrigin, kRGBA_F16_SkColorType, nullptr, &props);
    if (!surface) {
        throw std::runtime_error("SkSurfaces::WrapBackendRenderTarget 失败");
    }
    return surface;
}

bool SkiaGpuBackendD3D12::beginFrame()
{
    if (!context) {
        return false;
    }
    context->beginFrame();
    return true;
}

void SkiaGpuBackendD3D12::endFrame(SkSurface &surface)
{
    (void)surface;
    if (!context) {
        return;
    }

    copySurfaceToScreen();
    context->endFrame();
}

void SkiaGpuBackendD3D12::copySurfaceToScreen()
{
    if (!context || !offscreen) {
        return;
    }

    ID3D12GraphicsCommandList *cl = context->commandList();
    ID3D12Resource *back = context->currentBackBuffer();

    D3D12_RESOURCE_BARRIER toCopy[2] = {};
    toCopy[0].Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    toCopy[0].Transition.pResource = offscreen.Get();
    toCopy[0].Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    toCopy[0].Transition.StateBefore = D3D12_RESOURCE_STATE_RENDER_TARGET;
    toCopy[0].Transition.StateAfter = D3D12_RESOURCE_STATE_COPY_SOURCE;
    toCopy[1].Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    toCopy[1].Transition.pResource = back;
    toCopy[1].Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    toCopy[1].Transition.StateBefore = D3D12_RESOURCE_STATE_RENDER_TARGET;
    toCopy[1].Transition.StateAfter = D3D12_RESOURCE_STATE_COPY_DEST;
    cl->ResourceBarrier(2, toCopy);

    cl->CopyResource(back, offscreen.Get());

    D3D12_RESOURCE_BARRIER restore[2] = {};
    restore[0] = toCopy[0];
    restore[0].Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_SOURCE;
    restore[0].Transition.StateAfter = D3D12_RESOURCE_STATE_RENDER_TARGET;
    restore[1] = toCopy[1];
    restore[1].Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_DEST;
    restore[1].Transition.StateAfter = D3D12_RESOURCE_STATE_RENDER_TARGET;
    cl->ResourceBarrier(2, restore);

    GrBackendRenderTargets::SetD3DResourceState(
        &backendRT, static_cast<GrD3DResourceStateEnum>(D3D12_RESOURCE_STATE_RENDER_TARGET));
}

std::string SkiaGpuBackendD3D12::shaderPath(const char *name)
{
    wchar_t buffer[MAX_PATH] = {};
    const DWORD length = GetModuleFileNameW(nullptr, buffer, MAX_PATH);
    const std::filesystem::path exeDir =
        std::filesystem::path(std::wstring(buffer, length)).parent_path();
    return (exeDir / "shaders" / name).string();
}

const char *SkiaGpuBackendD3D12::backendName() const
{
    return "Skia (Ganesh D3D12)";
}

std::unique_ptr<SkiaGpuBackend> createSkiaGpuBackend()
{
    return std::make_unique<SkiaGpuBackendD3D12>();
}

} // namespace chroma::render

#endif // _WIN32
