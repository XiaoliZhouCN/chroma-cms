#include "gfx/d3d12/D3D12Context.hpp"

#include <d3dcompiler.h>

#include <cstring>
#include <string>
#include <vector>

namespace chroma::gfx {
namespace 
{
constexpr DXGI_FORMAT kSwapChainFormat = DXGI_FORMAT_R16G16B16A16_FLOAT;
constexpr DXGI_COLOR_SPACE_TYPE kSwapChainColorSpace = DXGI_COLOR_SPACE_RGB_FULL_G10_NONE_P709;
} // namespace

D3D12Context::D3D12Context() = default;

D3D12Context::~D3D12Context()
{
    try {
        if (m_queue && m_fence && m_fenceEvent != nullptr) {
            waitForGpu();
        }
    }
    catch (...) {
    }
    if (m_fenceEvent != nullptr) {
        CloseHandle(m_fenceEvent);
        m_fenceEvent = nullptr;
    }
}

/* ==================== 访问器 ==================== */
ID3D12GraphicsCommandList *D3D12Context::commandList() const    { return m_commandList.Get(); }
D3D12_CPU_DESCRIPTOR_HANDLE D3D12Context::currentRtv() const    { return m_rtvHandles[m_frameIndex]; }
ID3D12Device *D3D12Context::device() const                      { return m_device.Get(); }
ID3D12CommandQueue *D3D12Context::queue() const                 { return m_queue.Get(); }
IDXGIAdapter1 *D3D12Context::adapter() const                    { return m_adapter.Get(); }
IDXGISwapChain3 *D3D12Context::swapChain() const                { return m_swapChain.Get(); }
DXGI_FORMAT D3D12Context::swapChainFormat() const               { return kSwapChainFormat; }
std::uint32_t D3D12Context::width() const                       { return m_width; }
std::uint32_t D3D12Context::height() const                      { return m_height; }
std::uint32_t D3D12Context::frameIndex() const                  { return m_frameIndex; }

/* ==================== Initialize ==================== */
void D3D12Context::initialize(void *nativeWindow, std::uint32_t width, std::uint32_t height)
{
    if (m_initialized) {
        return;
    }
    m_width = width;
    m_height = height;

    createDeviceAndQueue();
    createSwapChain(nativeWindow);
    createDescriptorHeaps();
    createRenderTargets();

    gfx::ThrowIfFailed(
        m_device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT,
                                         IID_PPV_ARGS(&m_allocator)),
        "CreateCommandAllocator");
    gfx::ThrowIfFailed(
        m_device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, m_allocator.Get(), nullptr,
                                    IID_PPV_ARGS(&m_commandList)),
        "CreateCommandList");
    gfx::ThrowIfFailed(m_commandList->Close(), "CommandList::Close(initial)");

    gfx::ThrowIfFailed(
        m_device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&m_fence)), "CreateFence");

    m_fenceEvent = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    if (m_fenceEvent == nullptr) {
        throw std::runtime_error("CreateEventW(fence) failed");
    }
    m_initialized = true;
}

void D3D12Context::createDeviceAndQueue()
{
// debug layer
#if defined(_DEBUG)
    {
        gfx::ComPtr<ID3D12Debug> debug;
        if (SUCCEEDED(D3D12GetDebugInterface(IID_PPV_ARGS(&debug)))) {
            debug->EnableDebugLayer();
            OutputDebugStringW(L"[gfx] D3D12 debug layer enabled\n");
        }
    }
#endif

    // factory + adapter（Skia 的 GrD3DBackendContext 需要 adapter）
    gfx::ThrowIfFailed(
        CreateDXGIFactory2(0, IID_PPV_ARGS(&m_factory)), "CreateDXGIFactory2");
    gfx::ThrowIfFailed(
        m_factory->EnumAdapters1(0, &m_adapter), "EnumAdapters1");

    // create device
    gfx::ThrowIfFailed(
        D3D12CreateDevice(nullptr, D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&m_device)),
        "D3D12CreateDevice");

    // create command queue
    D3D12_COMMAND_QUEUE_DESC queueDesc{};
    queueDesc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
    gfx::ThrowIfFailed(
        m_device->CreateCommandQueue(&queueDesc, IID_PPV_ARGS(&m_queue)),
                       "CreateCommandQueue");
}

void D3D12Context::createSwapChain(void* nativeWindow)
{
    DXGI_SWAP_CHAIN_DESC1 desc{};       // DXGI_SWAP_CHAIN_DESC1 是一个结构体，用于描述交换链的属性和行为。它包含了交换链的宽度、高度、格式、缓冲区数量、使用方式等信息。通过设置这个结构体的各个字段，可以控制交换链的创建和配置，从而影响渲染输出的显示效果和性能。
    desc.Width  = m_width;
    desc.Height = m_height;
    desc.Format = kSwapChainFormat;
    desc.SampleDesc.Count = 1;
    desc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    desc.BufferCount = kFrameCount;     // kFrameCount = 2
    desc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
    desc.AlphaMode  = DXGI_ALPHA_MODE_IGNORE;
    desc.Scaling    = DXGI_SCALING_STRETCH;

    gfx::ComPtr<IDXGISwapChain1> swapChain1;

    // 踩坑点：D3D12 下第一个参数必须是**命令队列**，传 device 会得到
    // DXGI_ERROR_INVALID_CALL (0x887A0001)。
    // Q: 这里的factory 和 device的资源工厂有什么区别？
    // A: factory 是 DXGI 的资源工厂，主要负责创建交换链、适配器等与显示相关的资源。而 device 是 D3D12 的资源工厂，主要负责创建 GPU 资源，如缓冲区、纹理、着色器等。两者在功能上有所不同，但都用于管理和创建图形资源。
    // Q: 什么是DXGI？和D3D12的关系？
    // A: DXGI (DirectX Graphics Infrastructure) 是一个用于管理图形设备和交换链的接口层，它提供了与显示相关的功能，如创建交换链、处理全屏模式、管理适配器等。D3D12 是 Direct3D 12 的实现，负责 GPU 渲染和资源管理。DXGI 和 D3D12 是紧密相关的，DXGI 提供了 D3D12 所需的底层图形基础设施。
    // Q: 什么是Hwnd？
    // A: HWND 是 Windows 操作系统中的一个句柄类型，表示一个窗口的唯一标识符。它用于在 Windows API 中引用和操作窗口，例如创建、显示、隐藏、移动和销毁窗口。在图形编程中，HWND 通常用于与渲染上下文关联，以便将渲染输出显示在特定的窗口上。
    gfx::ThrowIfFailed(
        m_factory->CreateSwapChainForHwnd(m_queue.Get(), static_cast<HWND>(nativeWindow), &desc,
                                          nullptr, nullptr, &swapChain1),
        "CreateSwapChainForHwnd");

    // 把 swapChain1 升级成 IDXGISwapChain3，方便后续获取当前帧索引
    // Q: 为什么要升级成 IDXGISwapChain3？
    // A: IDXGISwapChain3 提供了更多的功能和改进，例如更好的性能和对多缓冲交换链的支持。通过升级到 IDXGISwapChain3，可以更方便地管理交换链的帧索引和其他高级功能，从而提高渲染效率和灵活性。
    // Q: 为什么不直接用 IDXGISwapChain3 声明swapChain1？
    gfx::ThrowIfFailed(
        swapChain1.As(&m_swapChain), 
        "IDXGISwapChain1::As(IDXGISwapChain3)");

    // 声明后台缓冲是 SDR sRGB。它只是"告诉 DWM 怎么解释"，不做转换。
    // Q: 什么是 DWM？
    // A: DWM (Desktop Window Manager) 是 Windows 的桌面窗口管理器，它负责管理窗口的显示、合成和渲染。DWM 使用硬件加速来提供平滑的窗口动画和视觉效果，同时处理窗口的透明度、阴影和其他图形特性。在使用 D3D12 渲染时，设置正确的颜色空间可以确保 DWM 正确解释渲染输出，从而实现预期的显示效果。
    UINT support = 0;
    if (SUCCEEDED(
        m_swapChain->CheckColorSpaceSupport(kSwapChainColorSpace, &support)) &&
        (support & DXGI_SWAP_CHAIN_COLOR_SPACE_SUPPORT_FLAG_PRESENT) != 0) {
        gfx::ThrowIfFailed(
            m_swapChain->SetColorSpace1(kSwapChainColorSpace), "SetColorSpace1");
    }

    // 获取当前帧索引，后续每次 present 之后都要更新 m_frameIndex
    m_frameIndex = m_swapChain->GetCurrentBackBufferIndex();
} // createSwapChain

// 创建 RTV 描述符堆，并计算每个 RTV 描述符的大小
void D3D12Context::createDescriptorHeaps()
{
    D3D12_DESCRIPTOR_HEAP_DESC desc{};
    desc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;     // RTV = Render Target View
    desc.NumDescriptors = kFrameCount;
    gfx::ThrowIfFailed(
        m_device->CreateDescriptorHeap(&desc, IID_PPV_ARGS(&m_rtvHeap)),
                       "CreateDescriptorHeap(RTV)");
    
    // 获取 RTV 描述符的大小，用于计算每个 RTV 描述符在堆中的偏移量
    m_rtvDescriptorSize =
        m_device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);

    // 获取 RTV 描述符堆的起始句柄，并计算每个 RTV 描述符的句柄
    // Q: 接下来遍历句柄即可？
    // A: 是的，接下来可以遍历句柄数组 m_rtvHandles，并为每个后台缓冲创建对应的 RTV 描述符。通过计算每个 RTV 描述符在堆中的偏移量，可以正确地访问和使用这些描述符，从而实现对渲染目标的管理和渲染操作。
    D3D12_CPU_DESCRIPTOR_HANDLE handle = m_rtvHeap->GetCPUDescriptorHandleForHeapStart();
    for (std::uint32_t i = 0; i < kFrameCount; ++i) {
        m_rtvHandles[i] = handle;
        handle.ptr += m_rtvDescriptorSize;
    }
}

// 创建后台缓冲的渲染目标视图（RTV），并存储在 m_renderTargets 数组中
// 把交换链自带的 N 张纹理，各自登记成"可以往里画的渲染目标"。
void D3D12Context::createRenderTargets()
{
    for (std::uint32_t i = 0; i < kFrameCount; ++i) {       // 遍历每个后台缓冲, frame count = 2
        m_renderTargets[i].Reset();
        gfx::ThrowIfFailed(
            m_swapChain->GetBuffer(i, IID_PPV_ARGS(&m_renderTargets[i])),
                           "SwapChain::GetBuffer");
        m_device->CreateRenderTargetView(m_renderTargets[i].Get(), nullptr, m_rtvHandles[i]);
    }
}

void D3D12Context::resize(std::uint32_t width, std::uint32_t height)
{
    if (!m_initialized || !m_swapChain) {
        return;
    }
    if (width == 0 || height == 0) {
        return;  // 最小化时会传 0，必须挡住
    }
    if (width == m_width && height == m_height) {
        return;
    }

    // ResizeBuffers 要求所有后台缓冲的引用都已释放，所以先等 GPU + 重置 ComPtr
    waitForGpu();
    for (auto& renderTarget : m_renderTargets) {
        renderTarget.Reset();       // Q: 为什么要 Reset()？
                                    // A: Reset() 是为了释放对后台缓冲的引用，以便在调用 ResizeBuffers 时不会出现资源冲突。ResizeBuffers 需要确保所有后台缓冲的引用都已释放，否则会导致 DXGI_ERROR_INVALID_CALL 错误。通过 Reset()，我们可以确保在调整窗口大小时，交换链能够正确地重新分配和配置后台缓冲。
                                    // Q: reset 做了什么？
                                    // A: reset 会将 ComPtr 内部的指针置为 nullptr，并释放对资源的引用计数，从而确保资源在不再使用时被正确释放。这样可以避免内存泄漏和资源冲突，确保在调整窗口大小时，交换链能够顺利地重新分配和配置后台缓冲。
    }

    gfx::ThrowIfFailed(
        m_swapChain->ResizeBuffers(kFrameCount, width, height, kSwapChainFormat, 0),
                       "ResizeBuffers");

    m_width = width;
    m_height = height;

    createRenderTargets();
    m_frameIndex = m_swapChain->GetCurrentBackBufferIndex();
}

/* ==================== frame management ==================== */
void D3D12Context::beginFrame()
{
    waitForGpu();

    gfx::ThrowIfFailed(m_allocator->Reset(), "CommandAllocator::Reset");
    gfx::ThrowIfFailed(m_commandList->Reset(m_allocator.Get(), nullptr), "CommandList::Reset");

    D3D12_RESOURCE_BARRIER barrier{};
    barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barrier.Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE;
    barrier.Transition.pResource = m_renderTargets[m_frameIndex].Get();
    barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_PRESENT;
    barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_RENDER_TARGET;
    m_commandList->ResourceBarrier(1, &barrier);
}

void D3D12Context::endFrame()
{
    D3D12_RESOURCE_BARRIER barrier{};
    barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barrier.Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE;
    barrier.Transition.pResource = m_renderTargets[m_frameIndex].Get();
    barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_RENDER_TARGET;
    barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_PRESENT;
    m_commandList->ResourceBarrier(1, &barrier);

    gfx::ThrowIfFailed(m_commandList->Close(), "CommandList::Close");
    ID3D12CommandList *lists[] = {m_commandList.Get()};
    m_queue->ExecuteCommandLists(1, lists);

    gfx::ThrowIfFailed(m_swapChain->Present(1, 0), "Present");
    m_frameIndex = m_swapChain->GetCurrentBackBufferIndex();
}

/* ==================== command management ==================== */
void D3D12Context::beginCommandList()
{
    gfx::ThrowIfFailed(m_allocator->Reset(), "CommandAllocator::Reset(upload)");
    gfx::ThrowIfFailed(m_commandList->Reset(m_allocator.Get(), nullptr),
                       "CommandList::Reset(upload)");
}

void D3D12Context::submitAndWait()
{
    gfx::ThrowIfFailed(m_commandList->Close(), "CommandList::Close");
    ID3D12CommandList *lists[] = {m_commandList.Get()};
    m_queue->ExecuteCommandLists(1, lists);
    waitForGpu();
}

/* ==================== Cpu and Gpu Synchronization ==================== */

void D3D12Context::waitForGpu() // cpu wait for gpu to finish all commands in the queue, then continue
{
    if (!m_queue || !m_fence || m_fenceEvent == nullptr) {
        return;
    }

    const std::uint64_t value = ++m_fenceValue;
    gfx::ThrowIfFailed(m_queue->Signal(m_fence.Get(), value), "Signal(fence)");
    gfx::ThrowIfFailed(m_fence->SetEventOnCompletion(value, m_fenceEvent),
                       "SetEventOnCompletion");
    WaitForSingleObjectEx(m_fenceEvent, INFINITE, FALSE);
}

} // namespace chroma::render