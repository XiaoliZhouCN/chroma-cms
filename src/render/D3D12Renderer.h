#pragma once

#include "gfx/D3D12Common.h"
#include "render/IRenderer.h"

#include <string>

namespace chroma::render {

/// 原生 D3D12 后端。
///
/// M1 范围：设备/队列/交换链/描述符堆/根签名/PSO/纹理上传/fence，显示一张静态图。
/// 不做色彩管理：采样值直接输出到 R10G10B10A2_UNORM 后台缓冲。
class D3D12Renderer final : public IRenderer {
public:
    D3D12Renderer();
    ~D3D12Renderer() override;

    D3D12Renderer(const D3D12Renderer&) = delete;
    D3D12Renderer& operator=(const D3D12Renderer&) = delete;

    bool initialize(void* nativeWindow, uint32_t width, uint32_t height) override;
    void resize(uint32_t width, uint32_t height) override;
    void setImage(const media::ImageData& image) override;
    void present() override;
    const char* backendName() const override;

private:
    static constexpr uint32_t kFrameCount = 2;

    /// 与 shaders/image.hlsl 的 cbuffer ImageConstants 逐字段对应。
    struct FrameConstants {
        float uvScale[2];
        float uvOffset[2];
    };
    static_assert(sizeof(FrameConstants) == 16, "cbuffer 布局约定：两个 float2");

    void createDeviceAndQueue();
    void createSwapChain(void* nativeWindow);
    void createDescriptorHeaps();
    void createRootSignature();
    void createPipeline();
    void createFrameResources();
    void createRenderTargetViews();
    void createConstantBuffer();
    void createImageTexture(const media::ImageData& image);
    void updateFrameConstants();

    void barrier(ID3D12Resource* resource, D3D12_RESOURCE_STATES before,
                 D3D12_RESOURCE_STATES after);
    void waitForGpu();
    void waitForFrame(uint32_t frameIndex);
    void moveToNextFrame();

    std::wstring shaderPath() const;

    gfx::ComPtr<ID3D12Device> m_device;
    gfx::ComPtr<ID3D12CommandQueue> m_queue;
    gfx::ComPtr<IDXGIFactory2> m_factory;
    gfx::ComPtr<IDXGISwapChain3> m_swapChain;

    gfx::ComPtr<ID3D12DescriptorHeap> m_rtvHeap;
    gfx::ComPtr<ID3D12DescriptorHeap> m_srvHeap;
    std::uint32_t m_rtvDescriptorSize = 0;
    D3D12_CPU_DESCRIPTOR_HANDLE m_rtvHandles[kFrameCount] = {};

    gfx::ComPtr<ID3D12RootSignature> m_rootSignature;
    gfx::ComPtr<ID3D12PipelineState> m_pipeline;

    gfx::ComPtr<ID3D12CommandAllocator> m_allocators[kFrameCount];
    gfx::ComPtr<ID3D12GraphicsCommandList> m_commandList;
    gfx::ComPtr<ID3D12Fence> m_fence;
    HANDLE m_fenceEvent = nullptr;
    std::uint64_t m_fenceValue = 0;
    std::uint64_t m_frameFenceValues[kFrameCount] = {};
    std::uint32_t m_frameIndex = 0;

    gfx::ComPtr<ID3D12Resource> m_renderTargets[kFrameCount];
    gfx::ComPtr<ID3D12Resource> m_image;
    gfx::ComPtr<ID3D12Resource> m_imageUpload;
    gfx::ComPtr<ID3D12Resource> m_constantBuffer;
    FrameConstants* m_mappedConstants = nullptr;

    std::uint32_t m_width = 0;
    std::uint32_t m_height = 0;
    std::uint32_t m_imageWidth = 0;
    std::uint32_t m_imageHeight = 0;
    bool m_hasImage = false;
    bool m_initialized = false;
};

} // namespace chroma::render
