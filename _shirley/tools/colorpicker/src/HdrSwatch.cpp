#include "HdrSwatch.hpp"

#include <QColor>
#include <QPaintEvent>
#include <QPainter>
#include <QResizeEvent>
#include <QEvent>
#include <QShowEvent>
#include <QTimer>

#include <algorithm>
#include <iterator>
#include <cmath>
#include <cstdio>

namespace cp
{
namespace
{
float srgbToLinear(float value)
{
    const float clamped = std::clamp(value, 0.0f, 1.0f);
    return clamped <= 0.04045f ? clamped / 12.92f
                               : std::pow((clamped + 0.055f) / 1.055f, 2.4f);
}
} // namespace

HdrSwatch::HdrSwatch(QWidget *parent) : QWidget(parent)
{
    setAttribute(Qt::WA_NativeWindow);
    setAttribute(Qt::WA_OpaquePaintEvent);
    setAttribute(Qt::WA_NoSystemBackground);
    setMinimumSize(120, 120);
}

HdrSwatch::~HdrSwatch() = default;

void HdrSwatch::setColor(float r, float g, float b, bool linear)
{
    m_r = r;
    m_g = g;
    m_b = b;
    m_linear = linear;
    if (!m_swapChainReady && isVisible()) {
        // 例如显示模式切换导致 device removed，下一帧尝试自动恢复。
        if (ensureSwapchain()) {
            present();
        }
        return;
    }
    if (m_swapChainReady) {
        present();
    }
}

bool HdrSwatch::ensureDevice()
{
    if (m_device) {
        return true;
    }
    if (m_deviceAttempted) {
        return false;
    }
    m_deviceAttempted = true;

    const D3D_FEATURE_LEVEL levels[] = {D3D_FEATURE_LEVEL_11_1, D3D_FEATURE_LEVEL_11_0};
    D3D_FEATURE_LEVEL level = D3D_FEATURE_LEVEL_11_0;
    if (FAILED(D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr,
                                 D3D11_CREATE_DEVICE_BGRA_SUPPORT, levels,
                                 static_cast<UINT>(std::size(levels)), D3D11_SDK_VERSION,
                                 m_device.GetAddressOf(), &level, m_context.GetAddressOf()))) {
        std::fprintf(stderr, "[swatch] D3D11CreateDevice 失败，HDR 预览禁用\n");
        return false;
    }
    return true;
}

bool HdrSwatch::ensureSwapchain()
{
    if (!ensureDevice()) {
        return false;
    }
    if (m_swapChainReady) {
        return true;
    }

    const int pixelWidth = std::max(1, static_cast<int>(std::lround(width() * devicePixelRatioF())));
    const int pixelHeight = std::max(1, static_cast<int>(std::lround(height() * devicePixelRatioF())));

    Microsoft::WRL::ComPtr<IDXGIDevice> dxgi;
    if (FAILED(m_device.As(&dxgi))) {
        return false;
    }
    Microsoft::WRL::ComPtr<IDXGIAdapter> adapter;
    if (FAILED(dxgi->GetAdapter(&adapter))) {
        return false;
    }
    Microsoft::WRL::ComPtr<IDXGIFactory2> factory;
    if (FAILED(adapter->GetParent(IID_PPV_ARGS(&factory)))) {
        return false;
    }

    DXGI_SWAP_CHAIN_DESC1 desc{};
    desc.Width = static_cast<UINT>(pixelWidth);
    desc.Height = static_cast<UINT>(pixelHeight);
    desc.Format = DXGI_FORMAT_R16G16B16A16_FLOAT;
    desc.Stereo = FALSE;
    desc.SampleDesc.Count = 1;
    desc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    desc.BufferCount = 2;
    desc.Scaling = DXGI_SCALING_STRETCH;
    desc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
    desc.AlphaMode = DXGI_ALPHA_MODE_IGNORE;
    desc.Flags = 0;

    const HWND hwnd = reinterpret_cast<HWND>(winId());
    if (FAILED(factory->CreateSwapChainForHwnd(m_device.Get(), hwnd, &desc, nullptr, nullptr,
                                               m_swapChain.GetAddressOf()))) {
        std::fprintf(stderr, "[swatch] CreateSwapChainForHwnd(FP16) 失败，回退 Qt 预览\n");
        return false;
    }

    Microsoft::WRL::ComPtr<IDXGISwapChain3> swapChain3;
    if (SUCCEEDED(m_swapChain.As(&swapChain3))) {
        swapChain3->SetColorSpace1(DXGI_COLOR_SPACE_RGB_FULL_G10_NONE_P709);
    }

    Microsoft::WRL::ComPtr<ID3D11Texture2D> buffer;
    if (FAILED(m_swapChain->GetBuffer(0, IID_PPV_ARGS(&buffer)))) {
        return false;
    }
    D3D11_RENDER_TARGET_VIEW_DESC rtvDesc{};
    rtvDesc.Format = DXGI_FORMAT_R16G16B16A16_FLOAT;
    rtvDesc.ViewDimension = D3D11_RTV_DIMENSION_TEXTURE2D;
    if (FAILED(m_device->CreateRenderTargetView(buffer.Get(), &rtvDesc, m_rtv.GetAddressOf()))) {
        return false;
    }

    m_swapWidth = pixelWidth;
    m_swapHeight = pixelHeight;
    m_swapChainReady = true;
    return true;
}

void HdrSwatch::present()
{
    if (!m_swapChainReady || !m_rtv) {
        return;
    }
    float r = m_r;
    float g = m_g;
    float b = m_b;
    if (!m_linear) {
        r = srgbToLinear(r);
        g = srgbToLinear(g);
        b = srgbToLinear(b);
    }
    const float color[4] = {r, g, b, 1.0f};
    m_context->ClearRenderTargetView(m_rtv.Get(), color);
    // 不等待垂直同步；HDR 设置切换/跨屏时 Present 可能阻塞 UI 线程。
    const HRESULT result = m_swapChain->Present(1, DXGI_PRESENT_DO_NOT_WAIT);
    if (result == DXGI_ERROR_WAS_STILL_DRAWING) {
        return;
    }
    if (result == DXGI_ERROR_DEVICE_REMOVED || result == DXGI_ERROR_DEVICE_RESET) {
        m_rtv.Reset();
        m_swapChain.Reset();
        m_swapChainReady = false;
        return;
    }
}

void HdrSwatch::refreshSurface()
{
    m_rtv.Reset();
    m_swapChain.Reset();
    m_swapChainReady = false;
    if (isVisible() && ensureSwapchain()) {
        present();
    }
}

bool HdrSwatch::event(QEvent *event)
{
    if (event->type() == QEvent::ScreenChangeInternal) {
        // Qt 已把原生子窗口移到了新屏幕；下一轮事件循环重建 swapchain，
        // 避免旧的 scRGB 表面继续以旧尺寸覆盖 UI。
        QTimer::singleShot(0, this, [this] { refreshSurface(); });
    }
    return QWidget::event(event);
}

void HdrSwatch::showEvent(QShowEvent *event)
{
    QWidget::showEvent(event);
    if (ensureSwapchain()) {
        present();
    }
}

void HdrSwatch::resizeEvent(QResizeEvent *event)
{
    QWidget::resizeEvent(event);
    if (!m_swapChainReady) {
        return;
    }
    const int pixelWidth = std::max(1, static_cast<int>(std::lround(width() * devicePixelRatioF())));
    const int pixelHeight = std::max(1, static_cast<int>(std::lround(height() * devicePixelRatioF())));
    if (pixelWidth == m_swapWidth && pixelHeight == m_swapHeight) {
        return;
    }
    m_rtv.Reset();
    m_swapChain.Reset();
    m_swapChainReady = false;
    if (ensureSwapchain()) {
        present();
    }
}

void HdrSwatch::paintEvent(QPaintEvent *event)
{
    Q_UNUSED(event);
    // 正常路径由 D3D11 swapchain 显示；如果 swapchain 创建失败，这里给出近似回退。
    if (m_swapChainReady) {
        return;
    }
    QPainter painter(this);
    const auto encode = [](float value) {
        value = std::clamp(value, 0.0f, 1.0f);
        return value <= 0.0031308f ? 12.92f * value
                                   : 1.055f * std::pow(value, 1.0f / 2.4f) - 0.055f;
    };
    const float r = m_linear ? encode(m_r) : std::clamp(m_r, 0.0f, 1.0f);
    const float g = m_linear ? encode(m_g) : std::clamp(m_g, 0.0f, 1.0f);
    const float b = m_linear ? encode(m_b) : std::clamp(m_b, 0.0f, 1.0f);
    painter.fillRect(rect(), QColor::fromRgbF(r, g, b));
}
} // namespace cp
