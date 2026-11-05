#include "render/D3D12Renderer.h"

#include "media/ImageData.h"

#include <d3dcompiler.h>

#include <algorithm>
#include <cstring>
#include <vector>

namespace chroma::render {
namespace {

constexpr DXGI_FORMAT kSwapChainFormat = DXGI_FORMAT_R10G10B10A2_UNORM;
constexpr DXGI_COLOR_SPACE_TYPE kSwapChainColorSpace = DXGI_COLOR_SPACE_RGB_FULL_G22_NONE_P709;
constexpr UINT kConstantBufferSize = 256;

/// 把 D3DCompile 的错误 blob 变成可读消息。
std::string compileErrorText(ID3DBlob* errorBlob)
{
    if (errorBlob == nullptr || errorBlob->GetBufferSize() == 0) {
        return {};
    }
    return std::string(static_cast<const char*>(errorBlob->GetBufferPointer()),
                       errorBlob->GetBufferSize());
}

} // namespace

D3D12Renderer::D3D12Renderer() = default;

D3D12Renderer::~D3D12Renderer()
{
    if (m_mappedConstants != nullptr && m_constantBuffer) {
        m_constantBuffer->Unmap(0, nullptr);
        m_mappedConstants = nullptr;
    }
    if (m_fenceEvent != nullptr) {
        CloseHandle(m_fenceEvent);
        m_fenceEvent = nullptr;
    }
}

const char* D3D12Renderer::backendName() const
{
    return "D3D12 (native)";
}

// ---------------------------------------------------------------------------
// 初始化
// ---------------------------------------------------------------------------

bool D3D12Renderer::initialize(void* nativeWindow, uint32_t width, uint32_t height)
{
    if (m_initialized) {
        return true;
    }

    m_width = width;
    m_height = height;

    createDeviceAndQueue();
    createSwapChain(nativeWindow);
    createDescriptorHeaps();
    createRootSignature();
    createPipeline();
    createFrameResources();
    createRenderTargetViews();
    createConstantBuffer();

    m_initialized = true;
    return true;
}

void D3D12Renderer::createDeviceAndQueue()
{
#if defined(_DEBUG)
    {
        gfx::ComPtr<ID3D12Debug> debug;
        if (SUCCEEDED(D3D12GetDebugInterface(IID_PPV_ARGS(&debug)))) {
            debug->EnableDebugLayer();
            OutputDebugStringW(L"[gfx] D3D12 调试层已启用\n");
        }
    }
#endif

    gfx::ThrowIfFailed(
        D3D12CreateDevice(nullptr, D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&m_device)),
        "D3D12CreateDevice");

    D3D12_COMMAND_QUEUE_DESC queueDesc{};
    queueDesc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
    gfx::ThrowIfFailed(m_device->CreateCommandQueue(&queueDesc, IID_PPV_ARGS(&m_queue)),
                        "CreateCommandQueue");

    gfx::ThrowIfFailed(CreateDXGIFactory2(0, IID_PPV_ARGS(&m_factory)), "CreateDXGIFactory2");
}

void D3D12Renderer::createSwapChain(void* nativeWindow)
{
    DXGI_SWAP_CHAIN_DESC1 desc{};
    desc.Width = m_width;
    desc.Height = m_height;
    desc.Format = kSwapChainFormat;
    desc.SampleDesc.Count = 1;
    desc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    desc.BufferCount = kFrameCount;
    desc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
    desc.AlphaMode = DXGI_ALPHA_MODE_IGNORE;
    desc.Scaling = DXGI_SCALING_STRETCH;

    gfx::ComPtr<IDXGISwapChain1> swapChain1;
    // 踩坑记录：D3D12 的第一个参数必须是命令队列，不是 device。
    gfx::ThrowIfFailed(
        m_factory->CreateSwapChainForHwnd(m_queue.Get(), static_cast<HWND>(nativeWindow), &desc,
                                          nullptr, nullptr, &swapChain1),
        "CreateSwapChainForHwnd");
    gfx::ThrowIfFailed(swapChain1.As(&m_swapChain), "IDXGISwapChain1::As(IDXGISwapChain3)");

    UINT support = 0;
    if (SUCCEEDED(m_swapChain->CheckColorSpaceSupport(kSwapChainColorSpace, &support)) &&
        (support & DXGI_SWAP_CHAIN_COLOR_SPACE_SUPPORT_FLAG_PRESENT) != 0) {
        gfx::ThrowIfFailed(m_swapChain->SetColorSpace1(kSwapChainColorSpace),
                           "SetColorSpace1");
    }

    m_frameIndex = m_swapChain->GetCurrentBackBufferIndex();
}

void D3D12Renderer::createDescriptorHeaps()
{
    D3D12_DESCRIPTOR_HEAP_DESC rtvDesc{};
    rtvDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
    rtvDesc.NumDescriptors = kFrameCount;
    gfx::ThrowIfFailed(m_device->CreateDescriptorHeap(&rtvDesc, IID_PPV_ARGS(&m_rtvHeap)),
                        "CreateDescriptorHeap(RTV)");

    m_rtvDescriptorSize =
        m_device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
    D3D12_CPU_DESCRIPTOR_HANDLE handle = m_rtvHeap->GetCPUDescriptorHandleForHeapStart();
    for (uint32_t i = 0; i < kFrameCount; ++i) {
        m_rtvHandles[i] = handle;
        handle.ptr += m_rtvDescriptorSize;
    }

    D3D12_DESCRIPTOR_HEAP_DESC srvDesc{};
    srvDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
    srvDesc.NumDescriptors = 1;
    srvDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
    gfx::ThrowIfFailed(m_device->CreateDescriptorHeap(&srvDesc, IID_PPV_ARGS(&m_srvHeap)),
                        "CreateDescriptorHeap(SRV)");
}

void D3D12Renderer::createRootSignature()
{
    D3D12_DESCRIPTOR_RANGE srvRange{};
    srvRange.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
    srvRange.NumDescriptors = 1;
    srvRange.BaseShaderRegister = 0;
    srvRange.RegisterSpace = 0;
    srvRange.OffsetInDescriptorsFromTableStart = 0;

    D3D12_ROOT_PARAMETER params[2] = {};

    params[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
    params[0].Descriptor.ShaderRegister = 0;
    params[0].Descriptor.RegisterSpace = 0;
    params[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;

    params[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    params[1].DescriptorTable.NumDescriptorRanges = 1;
    params[1].DescriptorTable.pDescriptorRanges = &srvRange;
    params[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

    D3D12_STATIC_SAMPLER_DESC sampler{};
    sampler.Filter = D3D12_FILTER_MIN_MAG_MIP_LINEAR;
    sampler.AddressU = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    sampler.AddressV = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    sampler.AddressW = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    sampler.MipLODBias = 0.0f;
    sampler.MaxAnisotropy = 1;
    sampler.ComparisonFunc = D3D12_COMPARISON_FUNC_NEVER;
    sampler.BorderColor = D3D12_STATIC_BORDER_COLOR_TRANSPARENT_BLACK;
    sampler.MinLOD = 0.0f;
    sampler.MaxLOD = D3D12_FLOAT32_MAX;
    sampler.ShaderRegister = 0;
    sampler.RegisterSpace = 0;
    sampler.ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

    D3D12_ROOT_SIGNATURE_DESC desc{};
    desc.NumParameters = 2;
    desc.pParameters = params;
    desc.NumStaticSamplers = 1;
    desc.pStaticSamplers = &sampler;
    desc.Flags = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;

    gfx::ComPtr<ID3DBlob> signature;
    gfx::ComPtr<ID3DBlob> errorBlob;
    const HRESULT hr =
        D3D12SerializeRootSignature(&desc, D3D_ROOT_SIGNATURE_VERSION_1, &signature, &errorBlob);
    if (FAILED(hr)) {
        throw std::runtime_error("D3D12SerializeRootSignature failed: " +
                                 compileErrorText(errorBlob.Get()));
    }

    gfx::ThrowIfFailed(
        m_device->CreateRootSignature(0, signature->GetBufferPointer(), signature->GetBufferSize(),
                                      IID_PPV_ARGS(&m_rootSignature)),
        "CreateRootSignature");
}

void D3D12Renderer::createPipeline()
{
    UINT compileFlags = D3DCOMPILE_ENABLE_STRICTNESS;
#if defined(_DEBUG)
    compileFlags |= D3DCOMPILE_DEBUG | D3DCOMPILE_SKIP_OPTIMIZATION;
#endif

    const std::wstring path = shaderPath();
    gfx::ComPtr<ID3DBlob> vs;
    gfx::ComPtr<ID3DBlob> ps;
    gfx::ComPtr<ID3DBlob> errorBlob;

    HRESULT hr = D3DCompileFromFile(path.c_str(), nullptr, D3D_COMPILE_STANDARD_FILE_INCLUDE,
                                    "VSMain", "vs_5_1", compileFlags, 0, &vs, &errorBlob);
    if (FAILED(hr)) {
        throw std::runtime_error("编译 VSMain 失败: " + compileErrorText(errorBlob.Get()));
    }

    errorBlob.Reset();
    hr = D3DCompileFromFile(path.c_str(), nullptr, D3D_COMPILE_STANDARD_FILE_INCLUDE, "PSMain",
                            "ps_5_1", compileFlags, 0, &ps, &errorBlob);
    if (FAILED(hr)) {
        throw std::runtime_error("编译 PSMain 失败: " + compileErrorText(errorBlob.Get()));
    }

    D3D12_GRAPHICS_PIPELINE_STATE_DESC desc{};
    desc.pRootSignature = m_rootSignature.Get();
    desc.VS = {vs->GetBufferPointer(), vs->GetBufferSize()};
    desc.PS = {ps->GetBufferPointer(), ps->GetBufferSize()};

    desc.RasterizerState.FillMode = D3D12_FILL_MODE_SOLID;
    desc.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
    desc.RasterizerState.FrontCounterClockwise = FALSE;
    desc.RasterizerState.DepthBias = D3D12_DEFAULT_DEPTH_BIAS;
    desc.RasterizerState.DepthBiasClamp = D3D12_DEFAULT_DEPTH_BIAS_CLAMP;
    desc.RasterizerState.SlopeScaledDepthBias = D3D12_DEFAULT_SLOPE_SCALED_DEPTH_BIAS;
    desc.RasterizerState.DepthClipEnable = TRUE;
    desc.RasterizerState.MultisampleEnable = FALSE;
    desc.RasterizerState.AntialiasedLineEnable = FALSE;
    desc.RasterizerState.ForcedSampleCount = 0;
    desc.RasterizerState.ConservativeRaster = D3D12_CONSERVATIVE_RASTERIZATION_MODE_OFF;

    desc.BlendState.RenderTarget[0].BlendEnable = FALSE;
    desc.BlendState.RenderTarget[0].LogicOpEnable = FALSE;
    desc.BlendState.RenderTarget[0].SrcBlend = D3D12_BLEND_ONE;
    desc.BlendState.RenderTarget[0].DestBlend = D3D12_BLEND_ZERO;
    desc.BlendState.RenderTarget[0].BlendOp = D3D12_BLEND_OP_ADD;
    desc.BlendState.RenderTarget[0].SrcBlendAlpha = D3D12_BLEND_ONE;
    desc.BlendState.RenderTarget[0].DestBlendAlpha = D3D12_BLEND_ZERO;
    desc.BlendState.RenderTarget[0].BlendOpAlpha = D3D12_BLEND_OP_ADD;
    desc.BlendState.RenderTarget[0].LogicOp = D3D12_LOGIC_OP_NOOP;
    desc.BlendState.RenderTarget[0].RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;

    desc.DepthStencilState.DepthEnable = FALSE;
    desc.DepthStencilState.StencilEnable = FALSE;

    desc.SampleMask = UINT_MAX;
    desc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
    desc.NumRenderTargets = 1;
    desc.RTVFormats[0] = kSwapChainFormat;
    desc.SampleDesc.Count = 1;

    gfx::ThrowIfFailed(m_device->CreateGraphicsPipelineState(&desc, IID_PPV_ARGS(&m_pipeline)),
                        "CreateGraphicsPipelineState");
}

void D3D12Renderer::createFrameResources()
{
    for (uint32_t i = 0; i < kFrameCount; ++i) {
        gfx::ThrowIfFailed(
            m_device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT,
                                             IID_PPV_ARGS(&m_allocators[i])),
            "CreateCommandAllocator");
    }

    gfx::ThrowIfFailed(
        m_device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, m_allocators[0].Get(),
                                    nullptr, IID_PPV_ARGS(&m_commandList)),
        "CreateCommandList");
    gfx::ThrowIfFailed(m_commandList->Close(), "CommandList::Close(初始)");

    gfx::ThrowIfFailed(m_device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&m_fence)),
                        "CreateFence");
    m_fenceEvent = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    if (m_fenceEvent == nullptr) {
        throw std::runtime_error("CreateEventW(fence) failed");
    }
}

void D3D12Renderer::createRenderTargetViews()
{
    for (uint32_t i = 0; i < kFrameCount; ++i) {
        m_renderTargets[i].Reset();
        gfx::ThrowIfFailed(m_swapChain->GetBuffer(i, IID_PPV_ARGS(&m_renderTargets[i])),
                            "SwapChain::GetBuffer");
        m_device->CreateRenderTargetView(m_renderTargets[i].Get(), nullptr, m_rtvHandles[i]);
    }
}

void D3D12Renderer::createConstantBuffer()
{
    D3D12_HEAP_PROPERTIES heapProps{};
    heapProps.Type = D3D12_HEAP_TYPE_UPLOAD;

    D3D12_RESOURCE_DESC desc{};
    desc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    desc.Width = kConstantBufferSize;
    desc.Height = 1;
    desc.DepthOrArraySize = 1;
    desc.MipLevels = 1;
    desc.Format = DXGI_FORMAT_UNKNOWN;
    desc.SampleDesc.Count = 1;
    desc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;

    gfx::ThrowIfFailed(m_device->CreateCommittedResource(
                           &heapProps, D3D12_HEAP_FLAG_NONE, &desc,
                           D3D12_RESOURCE_STATE_GENERIC_READ, nullptr,
                           IID_PPV_ARGS(&m_constantBuffer)),
                       "CreateCommittedResource(constant buffer)");

    D3D12_RANGE readRange{0, 0};
    gfx::ThrowIfFailed(
        m_constantBuffer->Map(0, &readRange, reinterpret_cast<void**>(&m_mappedConstants)),
        "Map(constant buffer)");
    *m_mappedConstants = FrameConstants{{1.0f, 1.0f}, {0.0f, 0.0f}};
}

// ---------------------------------------------------------------------------
// 图像上传
// ---------------------------------------------------------------------------

void D3D12Renderer::setImage(const media::ImageData& image)
{
    if (!m_initialized) {
        throw std::runtime_error("setImage 在 initialize 之前被调用");
    }
    createImageTexture(image);
}

void D3D12Renderer::createImageTexture(const media::ImageData& image)
{
    waitForGpu();

    m_image.Reset();
    m_imageUpload.Reset();

    D3D12_RESOURCE_DESC textureDesc{};
    textureDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    textureDesc.Width = image.width;
    textureDesc.Height = image.height;
    textureDesc.DepthOrArraySize = 1;
    textureDesc.MipLevels = 1;
    textureDesc.Format = DXGI_FORMAT_R16G16B16A16_UNORM;
    textureDesc.SampleDesc.Count = 1;
    textureDesc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
    textureDesc.Flags = D3D12_RESOURCE_FLAG_NONE;

    D3D12_HEAP_PROPERTIES defaultHeap{};
    defaultHeap.Type = D3D12_HEAP_TYPE_DEFAULT;
    gfx::ThrowIfFailed(
        m_device->CreateCommittedResource(&defaultHeap, D3D12_HEAP_FLAG_NONE, &textureDesc,
                                          D3D12_RESOURCE_STATE_COPY_DEST, nullptr,
                                          IID_PPV_ARGS(&m_image)),
        "CreateCommittedResource(image)");

    D3D12_PLACED_SUBRESOURCE_FOOTPRINT footprint{};
    UINT numRows = 0;
    UINT64 rowSize = 0;
    UINT64 totalBytes = 0;
    m_device->GetCopyableFootprints(&textureDesc, 0, 1, 0, &footprint, &numRows, &rowSize,
                                    &totalBytes);

    D3D12_HEAP_PROPERTIES uploadHeap{};
    uploadHeap.Type = D3D12_HEAP_TYPE_UPLOAD;

    D3D12_RESOURCE_DESC bufferDesc{};
    bufferDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    bufferDesc.Width = totalBytes;
    bufferDesc.Height = 1;
    bufferDesc.DepthOrArraySize = 1;
    bufferDesc.MipLevels = 1;
    bufferDesc.Format = DXGI_FORMAT_UNKNOWN;
    bufferDesc.SampleDesc.Count = 1;
    bufferDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;

    gfx::ThrowIfFailed(
        m_device->CreateCommittedResource(&uploadHeap, D3D12_HEAP_FLAG_NONE, &bufferDesc,
                                          D3D12_RESOURCE_STATE_GENERIC_READ, nullptr,
                                          IID_PPV_ARGS(&m_imageUpload)),
        "CreateCommittedResource(image upload)");

    void* mapped = nullptr;
    gfx::ThrowIfFailed(m_imageUpload->Map(0, nullptr, &mapped), "Map(image upload)");

    const auto* source = reinterpret_cast<const uint8_t*>(image.rgba16.data());
    const UINT64 sourceRowPitch = static_cast<UINT64>(image.width) * 8;  // RGBA16 = 8 B/px
    auto* destination = static_cast<uint8_t*>(mapped) + footprint.Offset;
    for (UINT row = 0; row < numRows; ++row) {
        std::memcpy(destination + static_cast<size_t>(row) * footprint.Footprint.RowPitch,
                    source + row * sourceRowPitch, static_cast<size_t>(rowSize));
    }
    m_imageUpload->Unmap(0, nullptr);

    gfx::ThrowIfFailed(m_allocators[m_frameIndex]->Reset(), "CommandAllocator::Reset(upload)");
    gfx::ThrowIfFailed(m_commandList->Reset(m_allocators[m_frameIndex].Get(), nullptr),
                        "CommandList::Reset(upload)");

    D3D12_TEXTURE_COPY_LOCATION sourceLocation{};
    sourceLocation.pResource = m_imageUpload.Get();
    sourceLocation.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
    sourceLocation.PlacedFootprint = footprint;

    D3D12_TEXTURE_COPY_LOCATION destinationLocation{};
    destinationLocation.pResource = m_image.Get();
    destinationLocation.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
    destinationLocation.SubresourceIndex = 0;

    m_commandList->CopyTextureRegion(&destinationLocation, 0, 0, 0, &sourceLocation, nullptr);
    barrier(m_image.Get(), D3D12_RESOURCE_STATE_COPY_DEST,
            D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);

    gfx::ThrowIfFailed(m_commandList->Close(), "CommandList::Close(upload)");

    ID3D12CommandList* lists[] = {m_commandList.Get()};
    m_queue->ExecuteCommandLists(1, lists);
    waitForGpu();

    D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc{};
    srvDesc.Format = DXGI_FORMAT_R16G16B16A16_UNORM;
    srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
    srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    srvDesc.Texture2D.MipLevels = 1;
    m_device->CreateShaderResourceView(m_image.Get(), &srvDesc,
                                       m_srvHeap->GetCPUDescriptorHandleForHeapStart());

    m_imageWidth = image.width;
    m_imageHeight = image.height;
    m_hasImage = true;
    updateFrameConstants();
}

// ---------------------------------------------------------------------------
// 呈现
// ---------------------------------------------------------------------------

void D3D12Renderer::updateFrameConstants()
{
    if (m_mappedConstants == nullptr || m_imageWidth == 0 || m_imageHeight == 0) {
        return;
    }

    const float imageAspect = static_cast<float>(m_imageWidth) / static_cast<float>(m_imageHeight);
    const float targetAspect = static_cast<float>(m_width) / static_cast<float>(m_height);

    float scaleX = 1.0f;
    float scaleY = 1.0f;
    if (imageAspect > targetAspect) {
        scaleY = targetAspect / imageAspect;  // 上下留边
    } else {
        scaleX = imageAspect / targetAspect;  // 左右留边
    }

    m_mappedConstants->uvScale[0] = scaleX;
    m_mappedConstants->uvScale[1] = scaleY;
    m_mappedConstants->uvOffset[0] = (1.0f - scaleX) * 0.5f;
    m_mappedConstants->uvOffset[1] = (1.0f - scaleY) * 0.5f;
}

void D3D12Renderer::present()
{
    if (!m_initialized || !m_swapChain || !m_hasImage) {
        return;
    }

    waitForFrame(m_frameIndex);

    gfx::ThrowIfFailed(m_allocators[m_frameIndex]->Reset(), "CommandAllocator::Reset");
    gfx::ThrowIfFailed(m_commandList->Reset(m_allocators[m_frameIndex].Get(), nullptr),
                        "CommandList::Reset");

    updateFrameConstants();

    ID3D12Resource* backBuffer = m_renderTargets[m_frameIndex].Get();
    barrier(backBuffer, D3D12_RESOURCE_STATE_PRESENT, D3D12_RESOURCE_STATE_RENDER_TARGET);

    const D3D12_CPU_DESCRIPTOR_HANDLE rtv = m_rtvHandles[m_frameIndex];
    m_commandList->OMSetRenderTargets(1, &rtv, FALSE, nullptr);

    ID3D12DescriptorHeap* heaps[] = {m_srvHeap.Get()};
    m_commandList->SetDescriptorHeaps(1, heaps);
    m_commandList->SetGraphicsRootSignature(m_rootSignature.Get());
    m_commandList->SetPipelineState(m_pipeline.Get());
    m_commandList->SetGraphicsRootConstantBufferView(0,
                                                    m_constantBuffer->GetGPUVirtualAddress());
    m_commandList->SetGraphicsRootDescriptorTable(1, m_srvHeap->GetGPUDescriptorHandleForHeapStart());

    D3D12_VIEWPORT viewport{};
    viewport.Width = static_cast<float>(m_width);
    viewport.Height = static_cast<float>(m_height);
    viewport.MaxDepth = 1.0f;
    D3D12_RECT scissor{0, 0, static_cast<LONG>(m_width), static_cast<LONG>(m_height)};
    m_commandList->RSSetViewports(1, &viewport);
    m_commandList->RSSetScissorRects(1, &scissor);
    m_commandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    m_commandList->DrawInstanced(3, 1, 0, 0);

    barrier(backBuffer, D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_PRESENT);

    gfx::ThrowIfFailed(m_commandList->Close(), "CommandList::Close");

    ID3D12CommandList* lists[] = {m_commandList.Get()};
    m_queue->ExecuteCommandLists(1, lists);

    gfx::ThrowIfFailed(m_swapChain->Present(1, 0), "Present");
    moveToNextFrame();
}

// ---------------------------------------------------------------------------
// 同步
// ---------------------------------------------------------------------------

void D3D12Renderer::barrier(ID3D12Resource* resource, D3D12_RESOURCE_STATES before,
                            D3D12_RESOURCE_STATES after)
{
    D3D12_RESOURCE_BARRIER transition{};
    transition.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    transition.Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE;
    transition.Transition.pResource = resource;
    transition.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    transition.Transition.StateBefore = before;
    transition.Transition.StateAfter = after;
    m_commandList->ResourceBarrier(1, &transition);
}

void D3D12Renderer::waitForFrame(uint32_t frameIndex)
{
    if (m_fence->GetCompletedValue() < m_frameFenceValues[frameIndex]) {
        gfx::ThrowIfFailed(
            m_fence->SetEventOnCompletion(m_frameFenceValues[frameIndex], m_fenceEvent),
            "SetEventOnCompletion");
        WaitForSingleObjectEx(m_fenceEvent, INFINITE, FALSE);
    }
}

void D3D12Renderer::moveToNextFrame()
{
    const UINT64 value = ++m_fenceValue;
    gfx::ThrowIfFailed(m_queue->Signal(m_fence.Get(), value), "Signal(fence)");
    m_frameFenceValues[m_frameIndex] = value;
    m_frameIndex = m_swapChain->GetCurrentBackBufferIndex();
}

void D3D12Renderer::waitForGpu()
{
    if (!m_queue || !m_fence || m_fenceEvent == nullptr) {
        return;
    }

    const UINT64 value = ++m_fenceValue;
    gfx::ThrowIfFailed(m_queue->Signal(m_fence.Get(), value), "Signal(fence)");
    gfx::ThrowIfFailed(m_fence->SetEventOnCompletion(value, m_fenceEvent), "SetEventOnCompletion");
    WaitForSingleObjectEx(m_fenceEvent, INFINITE, FALSE);

    for (auto& fenceValue : m_frameFenceValues) {
        fenceValue = value;
    }
    if (m_swapChain) {
        m_frameIndex = m_swapChain->GetCurrentBackBufferIndex();
    }
}

// ---------------------------------------------------------------------------
// Resize
// ---------------------------------------------------------------------------

void D3D12Renderer::resize(uint32_t width, uint32_t height)
{
    if (!m_initialized || !m_swapChain || width == 0 || height == 0) {
        return;
    }
    if (width == m_width && height == m_height) {
        return;
    }

    waitForGpu();

    for (auto& renderTarget : m_renderTargets) {
        renderTarget.Reset();
    }

    gfx::ThrowIfFailed(m_swapChain->ResizeBuffers(kFrameCount, width, height, kSwapChainFormat, 0),
                        "ResizeBuffers");

    m_width = width;
    m_height = height;

    createRenderTargetViews();
    m_frameIndex = m_swapChain->GetCurrentBackBufferIndex();
    updateFrameConstants();
}

std::wstring D3D12Renderer::shaderPath() const
{
    wchar_t exePath[MAX_PATH] = {};
    const DWORD length = GetModuleFileNameW(nullptr, exePath, MAX_PATH);
    std::wstring directory(exePath, length);
    const auto separator = directory.find_last_of(L"\\/");
    if (separator != std::wstring::npos) {
        directory.resize(separator);
    }
    return directory + L"\\shaders\\image.hlsl";
}

} // namespace chroma::render
