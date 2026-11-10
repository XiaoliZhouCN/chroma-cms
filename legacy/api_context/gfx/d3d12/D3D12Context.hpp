#pragma once

#include "gfx/D3D12Common.hpp"

#include <cstdint>

namespace chroma::gfx
{
/// 原生 D3D12 上下文：设备 / 队列 / 交换链 / RTV / 命令录制 / 同步。
/// 只负责"帧生命周期与原生资源"，不包含任何绘制逻辑。
/// D3D12Renderer、以及将来的 SkiaRenderer（Ganesh D3D）共用这一层。
class D3D12Context
{
public:
    D3D12Context();
    ~D3D12Context();

    D3D12Context(const D3D12Context &) = delete;
    D3D12Context &operator=(const D3D12Context &) = delete;

    void initialize(void *nativeWindow, std::uint32_t width, std::uint32_t height);
    void resize(std::uint32_t width, std::uint32_t height);
    void waitForGpu();

    /// 帧生命周期：beginFrame -> 渲染器录制 clear/draw -> endFrame（含 Present）
    void beginFrame();
    void endFrame();

    /// 一次性命令（纹理上传等）：beginCommandList -> 录制 -> submitAndWait
    void beginCommandList();
    void submitAndWait();

    ID3D12GraphicsCommandList *commandList() const;
    D3D12_CPU_DESCRIPTOR_HANDLE currentRtv() const;

    ID3D12Device        *device() const;
    ID3D12CommandQueue  *queue() const;
    IDXGIAdapter1       *adapter() const;
    IDXGISwapChain3     *swapChain() const;
    ID3D12Resource      *currentBackBuffer() const;   // m_renderTargets[m_frameIndex]
    DXGI_FORMAT          swapChainFormat() const;

    std::uint32_t width() const;
    std::uint32_t height() const;
    std::uint32_t frameIndex() const;

private:
    static constexpr std::uint32_t kFrameCount = 2;

    void createDeviceAndQueue();
    void createSwapChain(void *nativeWindow);
    void createDescriptorHeaps();
    void createRenderTargets();

    gfx::ComPtr<ID3D12Device>       m_device;
    gfx::ComPtr<ID3D12CommandQueue> m_queue;
    gfx::ComPtr<IDXGIFactory2>      m_factory;
    gfx::ComPtr<IDXGIAdapter1>      m_adapter;
    gfx::ComPtr<IDXGISwapChain3>    m_swapChain;

    gfx::ComPtr<ID3D12DescriptorHeap> m_rtvHeap;
    std::uint32_t m_rtvDescriptorSize = 0;
    D3D12_CPU_DESCRIPTOR_HANDLE m_rtvHandles[kFrameCount] = {};
    gfx::ComPtr<ID3D12Resource> m_renderTargets[kFrameCount];

    gfx::ComPtr<ID3D12CommandAllocator>     m_allocator;
    gfx::ComPtr<ID3D12GraphicsCommandList> m_commandList;
    gfx::ComPtr<ID3D12Fence> m_fence;
    HANDLE m_fenceEvent = nullptr;
    std::uint64_t m_fenceValue = 0;

    std::uint32_t m_width = 0;
    std::uint32_t m_height = 0;
    std::uint32_t m_frameIndex = 0;
    bool m_initialized = false;
};
} // namespace chroma::gfx