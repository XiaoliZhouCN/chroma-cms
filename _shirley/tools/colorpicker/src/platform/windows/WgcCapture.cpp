#include "WgcCapture.hpp"

#if defined(CHROMA_HAS_WGC)

#include <winrt/base.h>
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Graphics.Capture.h>
#include <winrt/Windows.Graphics.DirectX.Direct3D11.h>
#include <winrt/Windows.Security.Authorization.AppCapabilityAccess.h>

#include <windows.graphics.capture.interop.h>
#include <windows.graphics.directx.direct3d11.interop.h>
#include <shobjidl_core.h>

#include <algorithm>
#include <map>
#include <cmath>
#include <cstdio>
#include <cstring>

namespace cp::win
{
namespace
{
float halfToFloat(std::uint16_t value)
{
    const std::uint32_t sign = (value >> 15) & 0x1;
    const std::uint32_t exponent = (value >> 10) & 0x1F;
    const std::uint32_t mantissa = value & 0x3FF;
    if (exponent == 0) {
        if (mantissa == 0) {
            return sign ? -0.0f : 0.0f;
        }
        const float subnormal = static_cast<float>(mantissa) / 1024.0f;
        return (sign ? -1.0f : 1.0f) * std::ldexp(subnormal, -14);
    }
    if (exponent == 0x1F) {
        return mantissa == 0 ? (sign ? -INFINITY : INFINITY) : NAN;
    }
    const float result = std::ldexp(1.0f + static_cast<float>(mantissa) / 1024.0f,
                                    static_cast<int>(exponent) - 15);
    return sign ? -result : result;
}
} // namespace

struct WgcCapture::Impl
{
    winrt::com_ptr<ID3D11Device> device;
    winrt::com_ptr<ID3D11DeviceContext> context;
    winrt::com_ptr<ID3D11Texture2D> copyTexture;
    winrt::Windows::Graphics::Capture::GraphicsCaptureItem item{nullptr};
    winrt::Windows::Graphics::Capture::Direct3D11CaptureFramePool framePool{nullptr};
    winrt::Windows::Graphics::Capture::GraphicsCaptureSession session{nullptr};
    DXGI_FORMAT format = DXGI_FORMAT_UNKNOWN;
    int width = 0;
    int height = 0;
    bool hasFrame = false;
};

WgcCapture::WgcCapture() : m_impl(std::make_unique<Impl>()) {}

WgcCapture::~WgcCapture()
{
    close();
}

bool WgcCapture::initialize(HMONITOR monitor, HWND owner)
{
    int step = 0;
    try {
        step = 1;
        try {
            winrt::init_apartment(winrt::apartment_type::multi_threaded);
        }
        catch (const winrt::hresult_error &) {
        }

        step = 2;
        if (!winrt::Windows::Graphics::Capture::GraphicsCaptureSession::IsSupported()) {
            std::fprintf(stderr, "[wgc] GraphicsCaptureSession::IsSupported() = false\n");
            return false;
        }

        step = 3;
        const D3D_FEATURE_LEVEL levels[] = {D3D_FEATURE_LEVEL_11_1, D3D_FEATURE_LEVEL_11_0};
        D3D_FEATURE_LEVEL level = D3D_FEATURE_LEVEL_11_0;
        if (FAILED(D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr,
                                     D3D11_CREATE_DEVICE_BGRA_SUPPORT, levels,
                                     static_cast<UINT>(std::size(levels)), D3D11_SDK_VERSION,
                                     m_impl->device.put(), &level, m_impl->context.put()))) {
            std::fprintf(stderr, "[wgc] D3D11CreateDevice 失败\n");
            return false;
        }

        step = 4;
        winrt::com_ptr<IDXGIDevice> dxgiDevice;
        winrt::check_hresult(m_impl->device->QueryInterface(IID_PPV_ARGS(dxgiDevice.put())));

        step = 5;
        winrt::com_ptr<::IInspectable> inspectable;
        winrt::check_hresult(
            CreateDirect3D11DeviceFromDXGIDevice(dxgiDevice.get(), inspectable.put()));
        const auto direct3DDevice =
            inspectable.as<winrt::Windows::Graphics::DirectX::Direct3D11::IDirect3DDevice>();

        // 0) 同一显示器本次进程内已选过：复用 GraphicsCaptureItem，避免反复弹 picker。
        static std::map<HMONITOR, winrt::Windows::Graphics::Capture::GraphicsCaptureItem>
            cachedItems;
        winrt::Windows::Graphics::Capture::GraphicsCaptureItem item{nullptr};
        const auto cached = cachedItems.find(monitor);
        if (cached != cachedItems.end()) {
            item = cached->second;
            std::fprintf(stderr, "[wgc] reuse cached capture item for monitor=%p\n",
                         static_cast<void *>(monitor));
        }

        // 1) 首选：programmatic monitor capture（需要系统截图权限）。
        try {
            step = 6;
            const auto factory = winrt::get_activation_factory<
                winrt::Windows::Graphics::Capture::GraphicsCaptureItem,
                ::IGraphicsCaptureItemInterop>();
            step = 7;
            winrt::com_ptr<::IInspectable> itemInspectable;
            winrt::check_hresult(factory->CreateForMonitor(
                monitor,
                winrt::guid_of<winrt::Windows::Graphics::Capture::GraphicsCaptureItem>(),
                itemInspectable.put_void()));
            item = itemInspectable.as<winrt::Windows::Graphics::Capture::GraphicsCaptureItem>();
        }
        catch (const winrt::hresult_error &error) {
            std::fprintf(stderr,
                         "[wgc] programmatic CreateForMonitor 失败: step=%d hr=0x%08lx\n", step,
                         static_cast<unsigned long>(error.code().value));
        }

        // 2) 回退：系统 picker，让用户选择显示器（系统 UI 即用户同意）。
        if (!item) {
            if (owner == nullptr) {
                std::fprintf(stderr, "[wgc] 无 owner HWND，无法弹出 picker\n");
                return false;
            }
            step = 8;
            try {
                winrt::Windows::Graphics::Capture::GraphicsCapturePicker picker;
                const auto initializeWithWindow = picker.as<::IInitializeWithWindow>();
                winrt::check_hresult(initializeWithWindow->Initialize(owner));
                auto picked = picker.PickSingleItemAsync().get();
                if (!picked) {
                    std::fprintf(stderr, "[wgc] picker 取消/未选择\n");
                    return false;
                }

                // 必须是显示器，而不是窗口；否则采样坐标会错位。
                winrt::com_ptr<::IMonitorGraphicsCaptureItemInterop> monitorInterop;
                if (FAILED(winrt::get_unknown(picked)->QueryInterface(
                        IID_PPV_ARGS(monitorInterop.put())))) {
                    std::fprintf(stderr, "[wgc] picker 选择的是窗口，请选择显示器\n");
                    return false;
                }
                HMONITOR pickedMonitor = nullptr;
                winrt::check_hresult(monitorInterop->GetMonitor(&pickedMonitor));
                if (pickedMonitor != monitor) {
                    std::fprintf(stderr, "[wgc] picker 选择的不是目标显示器，请重试\n");
                    return false;
                }
                item = picked;
            }
            catch (const winrt::hresult_error &error) {
                std::fprintf(stderr, "[wgc] picker 失败: hr=0x%08lx\n",
                             static_cast<unsigned long>(error.code().value));
                return false;
            }
        }

        m_impl->item = item;
        cachedItems.insert_or_assign(monitor, item);
        step = 9;
        const auto size = m_impl->item.Size();
        step = 10;
        m_impl->framePool =
            winrt::Windows::Graphics::Capture::Direct3D11CaptureFramePool::CreateFreeThreaded(
                direct3DDevice,
                winrt::Windows::Graphics::DirectX::DirectXPixelFormat::R16G16B16A16Float, 2,
                size);
        step = 11;
        m_impl->session = m_impl->framePool.CreateCaptureSession(m_impl->item);
        try {
            m_impl->session.IsCursorCaptureEnabled(false);
        }
        catch (const winrt::hresult_error &) {
        }
        try {
            m_impl->session.IsBorderRequired(false);
        }
        catch (const winrt::hresult_error &) {
        }
        m_impl->session.StartCapture();
        std::fprintf(stderr, "[wgc] capture started: %d x %d\n", size.Width, size.Height);
        return true;
    }
    catch (const winrt::hresult_error &error) {
        std::fprintf(stderr, "[wgc] initialize 失败: step=%d hr=0x%08lx\n", step,
                     static_cast<unsigned long>(error.code().value));
        close();
        return false;
    }
    catch (const std::exception &error) {
        std::fprintf(stderr, "[wgc] initialize 异常: %s\n", error.what());
        close();
        return false;
    }
}

void WgcCapture::close()
{
    try {
        if (m_impl->session) {
            m_impl->session.Close();
        }
        if (m_impl->framePool) {
            m_impl->framePool.Close();
        }
    }
    catch (const winrt::hresult_error &) {
    }
    m_impl->session = nullptr;
    m_impl->framePool = nullptr;
    m_impl->item = nullptr;
    m_impl->copyTexture = nullptr;
    m_impl->context = nullptr;
    m_impl->device = nullptr;
    m_impl->format = DXGI_FORMAT_UNKNOWN;
    m_impl->width = 0;
    m_impl->height = 0;
    m_impl->hasFrame = false;
}

bool WgcCapture::captureFrame()
{
    if (!m_impl->session || !m_impl->framePool) {
        return false;
    }
    try {
        // free-threaded frame pool 里可能积压多帧；每次取色前 drain 到最新一帧，
        // 避免旧帧/空帧导致界面"闪烁等待捕获"。
        for (int attempt = 0; attempt < 8; ++attempt) {
            auto frame = m_impl->framePool.TryGetNextFrame();
            if (!frame) {
                break;
            }
            auto surface = frame.Surface();
            winrt::com_ptr<ID3D11Texture2D> texture;
            const HRESULT result = winrt::get_unknown(surface)->QueryInterface(
                IID_PPV_ARGS(texture.put()));
            if (FAILED(result) || !texture) {
                frame.Close();
                continue;
            }

            D3D11_TEXTURE2D_DESC desc{};
            texture->GetDesc(&desc);
            if (m_impl->copyTexture) {
                D3D11_TEXTURE2D_DESC current{};
                m_impl->copyTexture->GetDesc(&current);
                if (current.Format != desc.Format || current.Width != desc.Width ||
                    current.Height != desc.Height) {
                    m_impl->copyTexture = nullptr;
                }
            }
            if (!m_impl->copyTexture) {
                D3D11_TEXTURE2D_DESC target = desc;
                target.Usage = D3D11_USAGE_DEFAULT;
                target.BindFlags = 0;
                target.CPUAccessFlags = 0;
                target.MiscFlags = 0;
                target.MipLevels = 1;
                target.ArraySize = 1;
                target.SampleDesc.Count = 1;
                target.SampleDesc.Quality = 0;
                if (FAILED(m_impl->device->CreateTexture2D(&target, nullptr,
                                                           m_impl->copyTexture.put()))) {
                    frame.Close();
                    std::fprintf(stderr, "[wgc] CreateTexture2D(copy) 失败\n");
                    break;
                }
            }

            m_impl->context->CopyResource(m_impl->copyTexture.get(), texture.get());
            m_impl->format = desc.Format;
            m_impl->width = static_cast<int>(desc.Width);
            m_impl->height = static_cast<int>(desc.Height);
            m_impl->hasFrame = true;
            frame.Close();
        }
        return m_impl->hasFrame;
    }
    catch (const winrt::hresult_error &error) {
        std::fprintf(stderr, "[wgc] capture 失败: 0x%08lx\n",
                     static_cast<unsigned long>(error.code().value));
        return m_impl->hasFrame;
    }
}

WgcCapture::Sample WgcCapture::sampleRect(int x, int y, int width, int height)
{
    Sample sample;
    if (!m_impl->hasFrame || !m_impl->copyTexture) {
        return sample;
    }

    const int x0 = std::max(0, x);
    const int y0 = std::max(0, y);
    const int x1 = std::min(m_impl->width, x + width);
    const int y1 = std::min(m_impl->height, y + height);
    if (x1 <= x0 || y1 <= y0) {
        return sample;
    }

    D3D11_TEXTURE2D_DESC desc{};
    m_impl->copyTexture->GetDesc(&desc);
    desc.Usage = D3D11_USAGE_STAGING;
    desc.BindFlags = 0;
    desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    desc.MiscFlags = 0;
    desc.MipLevels = 1;
    desc.ArraySize = 1;
    desc.SampleDesc.Count = 1;
    desc.SampleDesc.Quality = 0;

    winrt::com_ptr<ID3D11Texture2D> staging;
    if (FAILED(m_impl->device->CreateTexture2D(&desc, nullptr, staging.put()))) {
        return sample;
    }
    const D3D11_BOX box{static_cast<UINT>(x0), static_cast<UINT>(y0), 0,
                        static_cast<UINT>(x1), static_cast<UINT>(y1), 1};
    m_impl->context->CopySubresourceRegion(staging.get(), 0, 0, 0, 0, m_impl->copyTexture.get(),
                                           0, &box);

    D3D11_MAPPED_SUBRESOURCE mapped{};
    if (FAILED(m_impl->context->Map(staging.get(), 0, D3D11_MAP_READ, 0, &mapped))) {
        return sample;
    }

    const int sampleWidth = x1 - x0;
    const int sampleHeight = y1 - y0;
    sample.ok = true;
    sample.format = m_impl->format;
    sample.x = x0;
    sample.y = y0;
    sample.width = sampleWidth;
    sample.height = sampleHeight;
    sample.rgba.resize(static_cast<std::size_t>(sampleWidth) * sampleHeight * 4);

    for (int row = 0; row < sampleHeight; ++row) {
        const auto *sourceRow =
            static_cast<const std::uint8_t *>(mapped.pData) + row * mapped.RowPitch;
        for (int col = 0; col < sampleWidth; ++col) {
            const std::size_t base = static_cast<std::size_t>(row * sampleWidth + col) * 4;
            if (m_impl->format == DXGI_FORMAT_R16G16B16A16_FLOAT) {
                const auto *pixel = reinterpret_cast<const std::uint16_t *>(sourceRow) + col * 4;
                sample.rgba[base + 0] = halfToFloat(pixel[0]);
                sample.rgba[base + 1] = halfToFloat(pixel[1]);
                sample.rgba[base + 2] = halfToFloat(pixel[2]);
                sample.rgba[base + 3] = halfToFloat(pixel[3]);
            }
            else if (m_impl->format == DXGI_FORMAT_B8G8R8A8_UNORM) {
                const auto *pixel = sourceRow + col * 4;
                sample.rgba[base + 0] = pixel[2] / 255.0f;
                sample.rgba[base + 1] = pixel[1] / 255.0f;
                sample.rgba[base + 2] = pixel[0] / 255.0f;
                sample.rgba[base + 3] = pixel[3] / 255.0f;
            }
            else {
                sample.ok = false;
                break;
            }
        }
    }

    m_impl->context->Unmap(staging.get(), 0);
    return sample;
}

DXGI_FORMAT WgcCapture::frameFormat() const
{
    return m_impl->format;
}

int WgcCapture::width() const
{
    return m_impl->width;
}

int WgcCapture::height() const
{
    return m_impl->height;
}
} // namespace cp::win

#else // !CHROMA_HAS_WGC

#include <cstdio>

namespace cp::win
{
struct WgcCapture::Impl
{
};

WgcCapture::WgcCapture() = default;
WgcCapture::~WgcCapture() = default;

bool WgcCapture::initialize(HMONITOR)
{
    std::fprintf(stderr, "[wgc] 当前构建未启用 WGC（需要 MSVC + Windows SDK C++/WinRT）\n");
    return false;
}

void WgcCapture::close() {}

bool WgcCapture::captureFrame()
{
    return false;
}

WgcCapture::Sample WgcCapture::sampleRect(int, int, int, int)
{
    return {};
}

DXGI_FORMAT WgcCapture::frameFormat() const
{
    return DXGI_FORMAT_UNKNOWN;
}

int WgcCapture::width() const
{
    return 0;
}

int WgcCapture::height() const
{
    return 0;
}
} // namespace cp::win

#endif // CHROMA_HAS_WGC
