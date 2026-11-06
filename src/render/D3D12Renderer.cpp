#include "render/D3D12Renderer.hpp"

#include <d3dcompiler.h>

#include <cstring>
#include <string>
#include <vector>

namespace chroma::render {
namespace {

constexpr DXGI_FORMAT kSwapChainFormat = DXGI_FORMAT_R10G10B10A2_UNORM;
constexpr DXGI_COLOR_SPACE_TYPE kSwapChainColorSpace = DXGI_COLOR_SPACE_RGB_FULL_G22_NONE_P709;     // rgb P709(SRGB), 2.2 gamma, full range

// 清屏色：深蓝灰。第一步只要"能看见一层稳定的颜色"就算成功。
constexpr float kClearColor[4] = {0.12f, 0.14f, 0.18f, 1.0f};

// 取 exe 所在目录：着色器随 exe 发布在 <exe目录>/shaders/ 下
std::wstring executableDirectory()
{
    wchar_t path[MAX_PATH] = {};
    const DWORD length = GetModuleFileNameW(nullptr, path, MAX_PATH);
    std::wstring full(path, length);
    const auto slash = full.find_last_of(L"\\/");
    if (slash != std::wstring::npos) {
        full.resize(slash);
    }
    return full;
}

std::string blobText(ID3DBlob* blob)
{
    if (blob == nullptr || blob->GetBufferSize() == 0) {
        return {};
    }
    return std::string(static_cast<const char*>(blob->GetBufferPointer()), blob->GetBufferSize());
}

// 运行时编译一个入口点；失败时把 D3DCompile 的错误文本一起抛出来
gfx::ComPtr<ID3DBlob> compileShader(const std::wstring& path, const char* entryPoint,
                                    const char* target)
{
    UINT flags = D3DCOMPILE_ENABLE_STRICTNESS;
#if defined(_DEBUG)
    flags |= D3DCOMPILE_DEBUG | D3DCOMPILE_SKIP_OPTIMIZATION;
#endif

    gfx::ComPtr<ID3DBlob> blob;
    gfx::ComPtr<ID3DBlob> errors;
    const HRESULT hr = D3DCompileFromFile(path.c_str(), nullptr, D3D_COMPILE_STANDARD_FILE_INCLUDE,
                                          entryPoint, target, flags, 0, &blob, &errors);
    if (FAILED(hr)) {
        throw std::runtime_error("编译着色器失败: " + std::string(entryPoint) + " @ " +
                                 blobText(errors.Get()));
    }
    return blob;
}

constexpr std::uint32_t kTextureSize = 256;

// 程序生成的测试纹理：棋盘格 + 四角标记，用来验证 UV 方向
std::vector<std::uint16_t> makeTestPixels()
{
    constexpr std::uint32_t kSize = kTextureSize;
    std::vector<std::uint16_t> pixels(static_cast<size_t>(kSize) * kSize * 4);

    for (std::uint32_t y = 0; y < kSize; ++y) {
        for (std::uint32_t x = 0; x < kSize; ++x) {
            const bool light = (((x / 32) + (y / 32)) & 1) != 0;
            const std::uint16_t base = light ? 0xFFFF : 0x3333;
            std::uint16_t r = base;
            std::uint16_t g = base;
            std::uint16_t b = base;

            // 四角标记：左上红、右上绿、左下蓝、右下白
            if (x < 32 && y < 32) {
                r = 0xFFFF; g = 0; b = 0;
            }
            else if (x >= kSize - 32 && y < 32) {
                r = 0; g = 0xFFFF; b = 0;
            }
            else if (x < 32 && y >= kSize - 32) {
                r = 0; g = 0; b = 0xFFFF;
            }
            else if (x >= kSize - 32 && y >= kSize - 32) {
                r = 0xFFFF; g = 0xFFFF; b = 0xFFFF;
            }

            const size_t index = (static_cast<size_t>(y) * kSize + x) * 4;
            pixels[index + 0] = r;
            pixels[index + 1] = g;
            pixels[index + 2] = b;
            pixels[index + 3] = 0xFFFF;
        }
    }
    return pixels;
}

} // namespace

D3D12Renderer::D3D12Renderer() = default;

D3D12Renderer::~D3D12Renderer()
{
    // 析构不允许抛异常：先等 GPU 空闲，再释放 fence 事件
    try {
        if (m_queue && m_fence && m_fenceEvent != nullptr) {
            waitForGpu();
        }
    }
    catch (...) {
        // 设备已丢失时忽略
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

/* ==================== initialize ==================== */
bool D3D12Renderer::initialize(void* nativeWindow, std::uint32_t width, std::uint32_t height)
{
    if (m_initialized) {
        return true;
    }

    m_width = width;
    m_height = height;

    // 创建设备、命令队列、交换链、RTV 描述符堆、渲染目标
    // Q: 
    createDeviceAndQueue();
    createSwapChain(nativeWindow);
    createDescriptorHeaps();
    createRenderTargets();
    createRootSignature();
    createPipelineState();

    // m_device: resource factory
    // create command allocator
    gfx::ThrowIfFailed(
        m_device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT,
                                         IID_PPV_ARGS(&m_allocator)),
        "CreateCommandAllocator");
    
    // create command list
    gfx::ThrowIfFailed(
        m_device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, m_allocator.Get(), nullptr,
                                    IID_PPV_ARGS(&m_commandList)),
        "CreateCommandList");

    // 命令列表创建出来是"打开"状态，先关掉，后面每次 present 再 Reset
    // Q: why?
    // A: 因为我们在初始化阶段只是创建了命令列表，但还没有录制任何命令。关闭命令列表可以确保它处于干净的状态，以便在后续的渲染过程中进行重置和录制新的命令。每次呈现时，我们会重置命令列表，以便重新录制渲染命令，从而确保每一帧的渲染都是独立的，不受之前帧的影响。
    gfx::ThrowIfFailed(
        m_commandList->Close(), "CommandList::Close(initial)");

    // create fence, setup fence event
    // Q: fence 是什么？
    // A: fence 是一种 GPU 同步机制，用于在 CPU 和 GPU 之间进行协调。它允许 CPU 等待 GPU 完成特定的任务，确保资源在使用前已经准备好。通过 fence，CPU 可以知道 GPU 是否已经完成了某些渲染操作，从而避免资源冲突和数据不一致的问题。
    gfx::ThrowIfFailed(
        m_device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&m_fence)), "CreateFence");

    m_fenceEvent = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    if (m_fenceEvent == nullptr) {
        throw std::runtime_error("CreateEventW(fence) failed");
    }

    // 第 4 步：SRV 描述符堆 + 上传测试纹理（用到命令列表，所以放在最后）
    createSrvHeap();

    m_initialized = true;
    return true;
}

void D3D12Renderer::createDeviceAndQueue()
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

    // create device
    // 传 nullptr = 用默认适配器（本机 Intel UHD，特性等级 12_1）
    gfx::ThrowIfFailed(
        D3D12CreateDevice(nullptr, D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&m_device)),
        "D3D12CreateDevice");

    // create command queue
    D3D12_COMMAND_QUEUE_DESC queueDesc{};
    queueDesc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
    gfx::ThrowIfFailed(
        m_device->CreateCommandQueue(&queueDesc, IID_PPV_ARGS(&m_queue)),
                       "CreateCommandQueue");

    gfx::ThrowIfFailed(
        CreateDXGIFactory2(0, IID_PPV_ARGS(&m_factory)), "CreateDXGIFactory2");
}

void D3D12Renderer::createSwapChain(void* nativeWindow)
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
}

// 创建 RTV 描述符堆，并计算每个 RTV 描述符的大小
void D3D12Renderer::createDescriptorHeaps()
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
void D3D12Renderer::createRenderTargets()
{
    for (std::uint32_t i = 0; i < kFrameCount; ++i) {       // 遍历每个后台缓冲, frame count = 2
        m_renderTargets[i].Reset();
        gfx::ThrowIfFailed(
            m_swapChain->GetBuffer(i, IID_PPV_ARGS(&m_renderTargets[i])),
                           "SwapChain::GetBuffer");
        m_device->CreateRenderTargetView(m_renderTargets[i].Get(), nullptr, m_rtvHandles[i]);
    }
}

/* ==================== 根签名 / PSO ==================== */
// Q. 为什么需要根签名？
// A. 
void D3D12Renderer::createRootSignature()
{
    // 第 4 步：一个 SRV 描述符表（t0）+ 一个静态采样器（s0）
    // srvRange: 描述符表的范围，包含一个 SRV 描述符，绑定到寄存器 t0
    // 1张表里连续N个描述符对应着色器的哪些槽位
    // 1个range里可以有多张表
    D3D12_DESCRIPTOR_RANGE srvRange{};
    srvRange.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
    srvRange.NumDescriptors = 1;
    srvRange.BaseShaderRegister = 0;
    srvRange.RegisterSpace = 0;
    srvRange.OffsetInDescriptorsFromTableStart = 0;

    // 根参数：一个描述符表，包含上面定义的 srvRange，绑定到寄存器 t0
    D3D12_ROOT_PARAMETER param{};
    param.ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    param.DescriptorTable.NumDescriptorRanges = 1;
    param.DescriptorTable.pDescriptorRanges = &srvRange;
    param.ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL; // 只在像素着色器可见（pixel shader，就是fragment shader）

    // 静态采样器：一个线性采样器，绑定到寄存器 s0
    // Q: 什么是静态采样器？
    // A: 静态采样器是一种在根签名中定义的采样器，它在创建根签名时就已经确定，并且在整个渲染过程中保持不变。静态采样器可以提高性能，因为它们不需要在每次渲染调用中重新绑定。与动态采样器不同，静态采样器在根签名中直接指定了采样器的属性，如过滤模式、寻址模式等，从而简化了渲染管线的配置。
    // Q: 还有动态采样器吗？
    // A: 是的，动态采样器是在渲染过程中可以更改的采样器。它们通常通过描述符表或根参数绑定到着色器中，可以在不同的渲染调用中使用不同的采样器设置。动态采样器提供了更大的灵活性，但可能会带来一定的性能开销，因为它们需要在每次渲染调用中重新绑定和配置。相比之下，静态采样器在性能上更优，因为它们在根签名中固定，不需要频繁更改。
    D3D12_STATIC_SAMPLER_DESC sampler{};        
    sampler.Filter   = D3D12_FILTER_MIN_MAG_MIP_LINEAR;
    sampler.AddressU = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    sampler.AddressV = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    sampler.AddressW = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    sampler.ComparisonFunc = D3D12_COMPARISON_FUNC_NEVER;       // shadow map 才用，这里不需要
    sampler.MaxLOD = D3D12_FLOAT32_MAX; // mip级别，默认0，这里设max
    sampler.ShaderRegister = 0;
    sampler.RegisterSpace = 0;
    sampler.ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

    // 签名描述：用C++结构体描述根签名的参数和静态采样器
    D3D12_ROOT_SIGNATURE_DESC desc{};
    desc.NumParameters = 1;
    desc.pParameters = &param;
    desc.NumStaticSamplers = 1;
    desc.pStaticSamplers = &sampler;
    desc.Flags = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;

    // 序列化：校验 + 编码成"签名字节码"，字节码才能交给 GPU
    gfx::ComPtr<ID3DBlob> signature;
    gfx::ComPtr<ID3DBlob> errors;
    const HRESULT hr =
        D3D12SerializeRootSignature(&desc, D3D_ROOT_SIGNATURE_VERSION_1, &signature, &errors);
    if (FAILED(hr)) {
        throw std::runtime_error("D3D12SerializeRootSignature failed: " + blobText(errors.Get()));
    }

    // 创建根签名：交给设备
    gfx::ThrowIfFailed(
        m_device->CreateRootSignature(0, signature->GetBufferPointer(), signature->GetBufferSize(),
                                      IID_PPV_ARGS(&m_rootSignature)),
        "CreateRootSignature");
}

void D3D12Renderer::createPipelineState()
{
    // 着色器在运行时从 exe 旁边的 shaders/ 目录加载（由 app 的 POST_BUILD 拷贝）
    const std::wstring path = executableDirectory() + L"\\shaders\\fullscreen.hlsl";

    gfx::ComPtr<ID3DBlob> vs = compileShader(path, "VSMain", "vs_5_1");
    gfx::ComPtr<ID3DBlob> ps = compileShader(path, "PSMain", "ps_5_1");

    D3D12_GRAPHICS_PIPELINE_STATE_DESC desc{};
    desc.pRootSignature = m_rootSignature.Get();
    desc.VS = {vs->GetBufferPointer(), vs->GetBufferSize()};
    desc.PS = {ps->GetBufferPointer(), ps->GetBufferSize()};

    // 光栅状态
    desc.RasterizerState.FillMode = D3D12_FILL_MODE_SOLID;      // 实心
    desc.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;       // 不做背面剔除，2D防止设置错误画面消失
    desc.RasterizerState.FrontCounterClockwise = FALSE;         // 不剔除，顶点顺/逆时针无意义
    desc.RasterizerState.DepthBias = D3D12_DEFAULT_DEPTH_BIAS;                          // 深度偏移，解决阴影贴图问题，不需要，使用默认 
    desc.RasterizerState.DepthBiasClamp = D3D12_DEFAULT_DEPTH_BIAS_CLAMP;
    desc.RasterizerState.SlopeScaledDepthBias = D3D12_DEFAULT_SLOPE_SCALED_DEPTH_BIAS;
    desc.RasterizerState.DepthClipEnable = TRUE;        // 裁掉0~1之外的几何体，一般都打开
    desc.RasterizerState.MultisampleEnable = FALSE;     // 关闭多重采样（Q: why？A: 解决几何体边缘锯齿，2D不需要）
    desc.RasterizerState.AntialiasedLineEnable = FALSE; // 关闭线段抗锯齿（Q: why？A: 线抗锯齿，不需要）
    desc.RasterizerState.ForcedSampleCount = 0;         // 强制光栅化成n个采样点，0不强制
    desc.RasterizerState.ConservativeRaster = D3D12_CONSERVATIVE_RASTERIZATION_MODE_OFF;        // 保守光栅化模式，OFF = 传统光栅化；保守光栅化：碰到像素就算覆盖，传统：覆盖像素中心才算

    // 混合状态 / 混合模式
    desc.BlendState.RenderTarget[0].BlendEnable = FALSE;        // 关闭混合，直接覆盖，下方src、dest、op等参数无效，但显示设置成合法值
    desc.BlendState.RenderTarget[0].LogicOpEnable = FALSE;      // 关闭逻辑操作，直接覆盖
    desc.BlendState.RenderTarget[0].SrcBlend = D3D12_BLEND_ONE;         // 源颜色 = 1 * src
    desc.BlendState.RenderTarget[0].DestBlend = D3D12_BLEND_ZERO;       // 目标颜色 = 0 * dest
    desc.BlendState.RenderTarget[0].BlendOp = D3D12_BLEND_OP_ADD;       // 混合操作 = src + dest
    desc.BlendState.RenderTarget[0].SrcBlendAlpha = D3D12_BLEND_ONE;    // 源 alpha = 1 * src
    desc.BlendState.RenderTarget[0].DestBlendAlpha = D3D12_BLEND_ZERO;  // 目标 alpha = 0 * dest
    desc.BlendState.RenderTarget[0].BlendOpAlpha = D3D12_BLEND_OP_ADD;  // 混合 alpha 操作 = src + dest
    desc.BlendState.RenderTarget[0].LogicOp = D3D12_LOGIC_OP_NOOP;      // 逻辑操作 = 无操作，逻辑操作只对8-bit有效，通常关闭
    desc.BlendState.RenderTarget[0].RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;   // 4通道可写

    desc.DepthStencilState.DepthEnable = FALSE;     // 深度模板2D不需要
    desc.DepthStencilState.StencilEnable = FALSE;   // 模板2D不需要

    desc.SampleMask = UINT_MAX;         // 全部采样点允许写入
    desc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;        // 三角形拓扑类型
    desc.NumRenderTargets = 1;                  // PSO的声明的RTV和交换链的RTV数量必须一致
    desc.RTVFormats[0] = kSwapChainFormat;      
    desc.SampleDesc.Count = 1;                  // 1个采样点，不开MSAA，必须和交换链的RTV数量一致

    // create PSO (pipline state object)
    gfx::ThrowIfFailed(
        m_device->CreateGraphicsPipelineState(&desc, IID_PPV_ARGS(&m_pipeline)),
                       "CreateGraphicsPipelineState");
}

/* ==================== 纹理 ==================== */

void D3D12Renderer::createSrvHeap()
{
    D3D12_DESCRIPTOR_HEAP_DESC desc{};
    desc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
    desc.NumDescriptors = 1;
    desc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;  // 着色器要能看见
    gfx::ThrowIfFailed(m_device->CreateDescriptorHeap(&desc, IID_PPV_ARGS(&m_srvHeap)),
                       "CreateDescriptorHeap(SRV)");
}

// 创建一个测试纹理，并上传到 GPU
// Q: 真实环境中上传纹理是否单独为一个函数？
// A: 是的，在真实环境中，上传纹理通常会单独封装为一个函数或类方法，以便在需要时可以方便地调用。这样可以提高代码的可维护性和可重用性，同时也有助于管理纹理资源的生命周期和上传过程中的错误处理。
void D3D12Renderer::createTextureFromImage(std::uint32_t width, std::uint32_t height,
                                           const std::uint16_t* rgba16)
{
    // 0. 生成像素，未来可以从文件加载
    const std::uint32_t size = width;
    // const std::vector<std::uint16_t> pixels = makeTestPixels();

    // 1. 创建目标纹理（DEFAULT 堆，GPU 专用）
    D3D12_RESOURCE_DESC textureDesc{};
    textureDesc.Dimension        = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    textureDesc.Width            = width;
    textureDesc.Height           = height;
    textureDesc.DepthOrArraySize = 1;
    textureDesc.MipLevels        = 1;
    textureDesc.Format           = DXGI_FORMAT_R16G16B16A16_UNORM;
    textureDesc.SampleDesc.Count = 1;
    textureDesc.Layout           = D3D12_TEXTURE_LAYOUT_UNKNOWN;
    textureDesc.Flags            = D3D12_RESOURCE_FLAG_NONE;

    D3D12_HEAP_PROPERTIES defaultHeap{};
    defaultHeap.Type = D3D12_HEAP_TYPE_DEFAULT;
    gfx::ThrowIfFailed(
        m_device->CreateCommittedResource(
            &defaultHeap, 
            D3D12_HEAP_FLAG_NONE,   // GPU 专用堆
            &textureDesc,
            D3D12_RESOURCE_STATE_COPY_DEST, nullptr,
            IID_PPV_ARGS(&m_texture)),
        "CreateCommittedResource(texture)");

    // 2. 算出上传布局（RowPitch 是 256 对齐后的行宽）
    // Q: 为什么要算上传布局？
    // A: 计算上传布局是为了确保在将纹理数据从 CPU 上传到 GPU 时，数据的内存布局符合 GPU 的要求。GPU 对纹理数据的存储方式有特定的对齐和排列要求，计算上传布局可以确保数据在 GPU 上正确显示，避免出现渲染错误或性能问题。通过获取上传布局信息，可以正确地设置上传缓冲区的大小和行宽，从而实现高效的数据传输和渲染。
    D3D12_PLACED_SUBRESOURCE_FOOTPRINT footprint{};
    UINT numRows = 0;
    UINT64 rowSize = 0;
    UINT64 totalBytes = 0;
    m_device->GetCopyableFootprints(&textureDesc, 0, 1, 0, &footprint, &numRows, &rowSize,
                                    &totalBytes);

    // 3. 上传堆缓冲（CPU 可写）
    D3D12_HEAP_PROPERTIES uploadHeap{};
    uploadHeap.Type = D3D12_HEAP_TYPE_UPLOAD;   // CPU 可写，GPU 可读，行主序

    D3D12_RESOURCE_DESC bufferDesc{};
    bufferDesc.Dimension        = D3D12_RESOURCE_DIMENSION_BUFFER;
    bufferDesc.Width            = totalBytes;
    bufferDesc.Height           = 1;
    bufferDesc.DepthOrArraySize = 1;
    bufferDesc.MipLevels        = 1;
    bufferDesc.Format           = DXGI_FORMAT_UNKNOWN;
    bufferDesc.SampleDesc.Count = 1;
    bufferDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR; // 上传缓冲必须是行主序

    gfx::ComPtr<ID3D12Resource> uploadBuffer;
    gfx::ThrowIfFailed(
        m_device->CreateCommittedResource(
            &uploadHeap, D3D12_HEAP_FLAG_NONE, &bufferDesc,
            D3D12_RESOURCE_STATE_GENERIC_READ, nullptr,
            IID_PPV_ARGS(&uploadBuffer)),
        "CreateCommittedResource(upload)");

    // 4. 逐行拷贝（必须按 footprint.RowPitch 走，不能按 width*bpp）
    // Q: 这里是否有零拷贝技术？
    // A: 在 D3D12 中，零拷贝技术通常是通过使用共享资源或直接映射 GPU 内存来实现的。然而，在这个例子中，我们使用了上传缓冲区（Upload Heap）来将纹理数据从 CPU 上传到 GPU，这涉及到一次内存拷贝操作。虽然这不是严格意义上的零拷贝，但它是 D3D12 中常用的纹理上传方法。要实现真正的零拷贝，需要使用更高级的技术，如使用共享资源或直接访问 GPU 内存，这可能需要特定的硬件支持和更复杂的资源管理。
    void* mapped = nullptr;
    gfx::ThrowIfFailed(
        uploadBuffer->Map(0, nullptr, &mapped), 
        "Map(upload)");

    auto* destination = static_cast<std::uint8_t*>(mapped) + footprint.Offset;
    // const auto* source = reinterpret_cast<const std::uint8_t*>(pixels.data());
    const auto* source = reinterpret_cast<const std::uint8_t*>(rgba16);
    const std::uint64_t sourceRowPitch = static_cast<std::uint64_t>(width) * 8;  // RGBA16 = 8 B/px, 行优先，用width*bpp计算每行字节数
    for (UINT row = 0; row < numRows; ++row) {
        std::memcpy(
            // dest = mapped + offset + row * rowPitch
            destination + static_cast<size_t>(row) * footprint.Footprint.RowPitch,
            // src = pixels + row * (width * 8)
            source + row * sourceRowPitch, static_cast<size_t>(rowSize)); // 只拷贝rowSize有效字节，其余留空
    }
    // Q: 拷贝完成，释放？
    // A: 是的，调用 Unmap 方法后，上传缓冲区的映射将被解除，CPU 不再可以直接访问该缓冲区。此时，数据已经被拷贝到 GPU 可读的上传缓冲区中，可以安全地释放映射。之后，GPU 可以从上传缓冲区读取数据并将其复制到目标纹理中。
    uploadBuffer->Unmap(0, nullptr);    

    // 5. 录制拷贝命令
    // reset command allocator and command list，准备新录制 command
    gfx::ThrowIfFailed(
        m_allocator->Reset(), "CommandAllocator::Reset(upload)");
    gfx::ThrowIfFailed(
        m_commandList->Reset(m_allocator.Get(), nullptr),
                       "CommandList::Reset(upload)");

    // 两个 D3D12_TEXTURE_COPY_LOCATION 分别描述源和目标纹理的位置和布局
    D3D12_TEXTURE_COPY_LOCATION sourceLocation{};
    sourceLocation.pResource = uploadBuffer.Get();
    sourceLocation.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
    sourceLocation.PlacedFootprint = footprint;

    D3D12_TEXTURE_COPY_LOCATION destinationLocation{};
    destinationLocation.pResource = m_texture.Get();
    destinationLocation.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
    destinationLocation.SubresourceIndex = 0;

    m_commandList->CopyTextureRegion(&destinationLocation, 0, 0, 0, &sourceLocation, nullptr);

    D3D12_RESOURCE_BARRIER barrier{};
    barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barrier.Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE;
    barrier.Transition.pResource = m_texture.Get();
    barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_DEST;
    barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
    m_commandList->ResourceBarrier(1, &barrier);

    gfx::ThrowIfFailed(m_commandList->Close(), "CommandList::Close(upload)"); // 录制完毕，关闭list

    ID3D12CommandList* lists[] = {m_commandList.Get()};
    m_queue->ExecuteCommandLists(1, lists);     // 执行command

    // 6. 等拷贝完成，uploadBuffer 才能安全释放
    waitForGpu();

    // 7. 建 SRV
    D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc{};
    srvDesc.Format = textureDesc.Format;
    srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
    srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    srvDesc.Texture2D.MipLevels = 1;
    m_device->CreateShaderResourceView(m_texture.Get(), &srvDesc,
                                       m_srvHeap->GetCPUDescriptorHandleForHeapStart());
}

// 设置输入图像，上传到 GPU 纹理， 更新 m_imageWidth 和 m_imageHeight
void D3D12Renderer::setImage(std::uint32_t width, std::uint32_t height,
                             const std::uint16_t* rgba16)
{
    if (!m_initialized || rgba16 == nullptr || width == 0 || height == 0) {
        return;
    }
    createTextureFromImage(width, height, rgba16);
    m_imageWidth = width;
    m_imageHeight = height;
}

/* ==================== clear screen and present ==================== */

void D3D12Renderer::present()
{
    if (!m_initialized) {
        return;
    }

    // 只有一个命令分配器，复用前必须确认 GPU 已经跑完上一帧。
    // 双帧在飞（每帧一个分配器 + 一个 fence 值）留到下一步优化。
    waitForGpu();

    gfx::ThrowIfFailed(
        m_allocator->Reset(), "CommandAllocator::Reset");
    gfx::ThrowIfFailed(
        m_commandList->Reset(m_allocator.Get(), nullptr), "CommandList::Reset");

    ID3D12Resource* backBuffer = m_renderTargets[m_frameIndex].Get();

    // 呈现态 -> 渲染目标态
    D3D12_RESOURCE_BARRIER toRenderTarget{};
    toRenderTarget.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    toRenderTarget.Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE;
    toRenderTarget.Transition.pResource = backBuffer;
    toRenderTarget.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    toRenderTarget.Transition.StateBefore = D3D12_RESOURCE_STATE_PRESENT;
    toRenderTarget.Transition.StateAfter = D3D12_RESOURCE_STATE_RENDER_TARGET;
    m_commandList->ResourceBarrier(1, &toRenderTarget);

    m_commandList->ClearRenderTargetView(m_rtvHandles[m_frameIndex], kClearColor, 0, nullptr);

    if (m_imageWidth == 0 || m_imageHeight == 0) {
        // 还没有图像：只清屏，不做 draw（避免绑定空 SRV）
        D3D12_RESOURCE_BARRIER back{};
        back.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        back.Transition.pResource = backBuffer;
        back.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
        back.Transition.StateBefore = D3D12_RESOURCE_STATE_RENDER_TARGET;
        back.Transition.StateAfter = D3D12_RESOURCE_STATE_PRESENT;
        m_commandList->ResourceBarrier(1, &back);

        // 直接关闭list+执行命令+presnet 一套流程
        gfx::ThrowIfFailed(m_commandList->Close(), "CommandList::Close");
        ID3D12CommandList* earlyLists[] = {m_commandList.Get()};
        m_queue->ExecuteCommandLists(1, earlyLists);
        gfx::ThrowIfFailed(m_swapChain->Present(1, 0), "Present");
        m_frameIndex = m_swapChain->GetCurrentBackBufferIndex();
        return;
    }

    // ---- 画全屏三角形 ----
    // ClearRenderTargetView 不需要 OMSetRenderTargets，但 DrawInstanced 需要。
    m_commandList->OMSetRenderTargets(1, &m_rtvHandles[m_frameIndex], FALSE, nullptr); // 输出到当前帧的渲染目标
    m_commandList->SetGraphicsRootSignature(m_rootSignature.Get());
    m_commandList->SetPipelineState(m_pipeline.Get());

    // 第 4 步：绑描述符堆 + 根参数（顺序不能反）
    ID3D12DescriptorHeap* heaps[] = {m_srvHeap.Get()};
    m_commandList->SetDescriptorHeaps(1, heaps);
    m_commandList->SetGraphicsRootDescriptorTable(
        0, m_srvHeap->GetGPUDescriptorHandleForHeapStart());

    // set view port and scissor rect
    // 按图片宽高比做 contain-fit：长边贴满，短边居中留边
    const float imageAspect = static_cast<float>(m_imageWidth) / static_cast<float>(m_imageHeight);
    const float targetAspect = static_cast<float>(m_width) / static_cast<float>(m_height);

    float viewWidth = static_cast<float>(m_width);
    float viewHeight = static_cast<float>(m_height);
    if (imageAspect > targetAspect) {
        viewHeight = static_cast<float>(m_width) / imageAspect;  // 上下留边
    }
    else {
        viewWidth = static_cast<float>(m_height) * imageAspect;  // 左右留边
    }

    const float viewLeft = (static_cast<float>(m_width) - viewWidth) * 0.5f;
    const float viewTop = (static_cast<float>(m_height) - viewHeight) * 0.5f;

D3D12_VIEWPORT viewport{};
    viewport.TopLeftX = viewLeft;
    viewport.TopLeftY = viewTop;
    viewport.Width = viewWidth;
    viewport.Height = viewHeight;
    viewport.MaxDepth = 1.0f;
    const D3D12_RECT scissor{static_cast<LONG>(viewLeft), static_cast<LONG>(viewTop),
                             static_cast<LONG>(viewLeft + viewWidth),
                             static_cast<LONG>(viewTop + viewHeight)};
    
    // 录制绘制命令
    m_commandList->RSSetViewports(1, &viewport);
    m_commandList->RSSetScissorRects(1, &scissor);
    m_commandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    m_commandList->DrawInstanced(3, 1, 0, 0);

    // 绘制完后：渲染目标态 -> 呈现态
    D3D12_RESOURCE_BARRIER toPresent = toRenderTarget;
    toPresent.Transition.StateBefore = D3D12_RESOURCE_STATE_RENDER_TARGET;
    toPresent.Transition.StateAfter = D3D12_RESOURCE_STATE_PRESENT;
    m_commandList->ResourceBarrier(1, &toPresent);

    gfx::ThrowIfFailed(m_commandList->Close(), "CommandList::Close");

    ID3D12CommandList* lists[] = {m_commandList.Get()};
    m_queue->ExecuteCommandLists(1, lists);

    // Present(1, 0) = 开垂直同步，等下一个刷新周期
    gfx::ThrowIfFailed(m_swapChain->Present(1, 0), "Present");

    m_frameIndex = m_swapChain->GetCurrentBackBufferIndex();
}

void D3D12Renderer::resize(std::uint32_t width, std::uint32_t height)
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

/* ==================== Cpu and Gpu Synchronization ==================== */

void D3D12Renderer::waitForGpu() // cpu wait for gpu to finish all commands in the queue, then continue
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