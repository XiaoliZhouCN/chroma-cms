#pragma once

#include <windows.h>
#include <d3d11.h>
#include <dxgi1_6.h>
#include <wrl/client.h>

#include <QWidget>

namespace cp
{
/// HDR 色块预览：在子 HWND 上创建 scRGB FP16 swapchain，直接交给 DWM 合成。
/// 这样 scRGB 值 > 1 的颜色不会被 [0,1] 裁剪（HDR 开启时由系统显示真实 HDR）。
class HdrSwatch final : public QWidget
{
public:
    explicit HdrSwatch(QWidget *parent = nullptr);
    ~HdrSwatch() override;

    /// linear=true 表示传入 CCCS scRGB 线性值；false 表示 sRGB 编码值（0..1）。
    void setColor(float r, float g, float b, bool linear);

    /// 屏幕/DPI 变化后重新创建 swapchain（原生子窗口与 Qt 布局重新对齐）。
    void refreshSurface();

protected:
    void showEvent(QShowEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;
    void paintEvent(QPaintEvent *event) override;
    bool event(QEvent *event) override;

private:
    bool ensureDevice();
    bool ensureSwapchain();
    void present();

    Microsoft::WRL::ComPtr<ID3D11Device> m_device;
    Microsoft::WRL::ComPtr<ID3D11DeviceContext> m_context;
    Microsoft::WRL::ComPtr<IDXGISwapChain1> m_swapChain;
    Microsoft::WRL::ComPtr<ID3D11RenderTargetView> m_rtv;
    bool m_deviceAttempted = false;
    bool m_swapChainReady = false;
    int m_swapWidth = 0;
    int m_swapHeight = 0;
    float m_r = 0.0f;
    float m_g = 0.0f;
    float m_b = 0.0f;
    bool m_linear = true;
};
} // namespace cp
