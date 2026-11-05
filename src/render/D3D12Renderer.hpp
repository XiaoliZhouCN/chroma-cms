#pragma once

#include "gfx/D3D12Common.hpp"
#include "render/IRenderer.hpp"

#include <cstdint>

namespace chroma::render
{
/// 原生 D3D12 后端。
/// 第二步范围：设备 / 命令队列 / 交换链 / RTV 描述符堆 / fence —— 把后台缓冲清成纯色。
class D3D12Renderer final : public IRenderer
{
public:
    D3D12Renderer();
    ~D3D12Renderer() override;

    D3D12Renderer(const D3D12Renderer &) = delete;            // 禁止拷贝构造
    D3D12Renderer &operator=(const D3D12Renderer &) = delete; // 禁止拷贝赋值

    bool initialize(void *nativeWindow, std::uint32_t width, std::uint32_t height) override;
    void resize(std::uint32_t width, std::uint32_t height) override;
    void present() override;
    const char *backendName() const override; // 告诉调用方"当前这个渲染器到底是哪个实现"

private:
    static constexpr std::uint32_t kFrameCount = 2;

    void createDeviceAndQueue();
    void createSwapChain(void *nativeWindow);
    void createDescriptorHeaps();           // Q: 什么是Descriptor Heap？
                                            // A: 描述符堆, 是GPU资源的集合，类似于一个数组，里面存放了各种资源的描述符（Descriptor），比如纹理、缓冲区等。它们用于告诉GPU如何访问这些资源。
    void createRenderTargets();
    void waitForGpu();

    gfx::ComPtr<ID3D12Device>       m_device;
    gfx::ComPtr<ID3D12CommandQueue> m_queue;
    gfx::ComPtr<IDXGIFactory2>      m_factory;
    gfx::ComPtr<IDXGISwapChain3>    m_swapChain;

    gfx::ComPtr<ID3D12DescriptorHeap> m_rtvHeap;
    std::uint32_t m_rtvDescriptorSize = 0;
    D3D12_CPU_DESCRIPTOR_HANDLE m_rtvHandles[kFrameCount] = {};

    gfx::ComPtr<ID3D12CommandAllocator>     m_allocator;
    gfx::ComPtr<ID3D12GraphicsCommandList>   m_commandList;
    gfx::ComPtr<ID3D12Fence> m_fence;
    HANDLE m_fenceEvent = nullptr;
    std::uint64_t m_fenceValue = 0;

    gfx::ComPtr<ID3D12Resource> m_renderTargets[kFrameCount];

    std::uint32_t m_width       = 0;
    std::uint32_t m_height      = 0;
    std::uint32_t m_frameIndex  = 0;
    bool m_initialized          = false;
};
} // namespace chroma::render