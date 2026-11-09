#include "render/d3d12/D3D12Renderer.hpp"

#include <d3dcompiler.h>

#include <cstring>
#include <string>
#include <vector>

namespace chroma::render
{
namespace
{

// 清屏色：深蓝灰。第一步只要"能看见一层稳定的颜色"就算成功。
constexpr float kClearColor[4] = {0.02f, 0.02f, 0.02f, 1.0f};

// 取 exe 所在目录：着色器随 exe 发布在 <exe目录>/shaders/ 下
std::wstring executableDirectory()
{
    wchar_t path[MAX_PATH] = {};
    const DWORD length = GetModuleFileNameW(nullptr, path, MAX_PATH);
    std::wstring full(path, length);
    const auto slash = full.find_last_of(L"\\/");
    if (slash != std::wstring::npos)
    {
        full.resize(slash);
    }
    return full;
}

std::string blobText(ID3DBlob *blob)
{
    if (blob == nullptr || blob->GetBufferSize() == 0)
    {
        return {};
    }
    return std::string(static_cast<const char *>(blob->GetBufferPointer()), blob->GetBufferSize());
}

// 运行时编译一个入口点；失败时把 D3DCompile 的错误文本一起抛出来
gfx::ComPtr<ID3DBlob> compileShader(const std::wstring &path, const char *entryPoint,
                                    const char *target)
{
    UINT flags = D3DCOMPILE_ENABLE_STRICTNESS;
#if defined(_DEBUG)
    flags |= D3DCOMPILE_DEBUG | D3DCOMPILE_SKIP_OPTIMIZATION;
#endif

    gfx::ComPtr<ID3DBlob> blob;
    gfx::ComPtr<ID3DBlob> errors;
    const HRESULT hr = D3DCompileFromFile(path.c_str(), nullptr, D3D_COMPILE_STANDARD_FILE_INCLUDE,
                                            entryPoint, target, flags, 0, &blob, &errors);
    if (FAILED(hr))
    {
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

    for (std::uint32_t y = 0; y < kSize; ++y)
    {
        for (std::uint32_t x = 0; x < kSize; ++x)
        {
            const bool light = (((x / 32) + (y / 32)) & 1) != 0;
            const std::uint16_t base = light ? 0xFFFF : 0x3333;
            std::uint16_t r = base;
            std::uint16_t g = base;
            std::uint16_t b = base;

            // 四角标记：左上红、右上绿、左下蓝、右下白
            if (x < 32 && y < 32)
            {
                r = 0xFFFF;
                g = 0;
                b = 0;
            }
            else if (x >= kSize - 32 && y < 32)
            {
                r = 0;
                g = 0xFFFF;
                b = 0;
            }
            else if (x < 32 && y >= kSize - 32)
            {
                r = 0;
                g = 0;
                b = 0xFFFF;
            }
            else if (x >= kSize - 32 && y >= kSize - 32)
            {
                r = 0xFFFF;
                g = 0xFFFF;
                b = 0xFFFF;
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

D3D12Renderer::~D3D12Renderer() = default;

const char *D3D12Renderer::backendName() const
{
    return "D3D12 (native)";
}

void D3D12Renderer::setQuarterTurns(int quarterTurns)
{
    m_quarterTurns = ((quarterTurns % 4) + 4) % 4;
}

void D3D12Renderer::setZoom(float zoom)
{
    m_zoom = (zoom > 0.0f) ? zoom : 1.0f;
}

/* ==================== initialize ==================== */
bool D3D12Renderer::initialize(void *nativeWindow, std::uint32_t width, std::uint32_t height)
{
    if (m_initialized)
    {
        return true;
    }

    m_context = std::make_unique<gfx::D3D12Context>();
    m_context->initialize(nativeWindow, width, height);

    createRootSignature();
    createPipelineState();
    createSrvHeap();

    m_initialized = true;
    return true;
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
    sampler.Filter = D3D12_FILTER_MIN_MAG_MIP_LINEAR;
    sampler.AddressU = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    sampler.AddressV = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    sampler.AddressW = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    sampler.ComparisonFunc = D3D12_COMPARISON_FUNC_NEVER; // shadow map 才用，这里不需要
    sampler.MaxLOD = D3D12_FLOAT32_MAX;                   // mip级别，默认0，这里设max
    sampler.ShaderRegister = 0;
    sampler.RegisterSpace = 0;
    sampler.ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

    // 根参数 1：8 个 32 位常量，映射到 b0
    D3D12_ROOT_PARAMETER params[2] = {};
    params[0] = param; // 原 SRV 表
    params[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
    params[1].Constants.ShaderRegister = 0; // b0
    params[1].Constants.RegisterSpace = 0;
    params[1].Constants.Num32BitValues = 8; // 8 个 DWORD = 16 字节
    params[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

    // 签名描述：用C++结构体描述根签名的参数和静态采样器
    D3D12_ROOT_SIGNATURE_DESC desc{};
    desc.NumParameters = 2;
    desc.pParameters = params;
    desc.NumStaticSamplers = 1;
    desc.pStaticSamplers = &sampler;
    desc.Flags = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;

    // 序列化：校验 + 编码成"签名字节码"，字节码才能交给 GPU
    gfx::ComPtr<ID3DBlob> signature;
    gfx::ComPtr<ID3DBlob> errors;
    const HRESULT hr =
        D3D12SerializeRootSignature(&desc, D3D_ROOT_SIGNATURE_VERSION_1, &signature, &errors);
    if (FAILED(hr))
    {
        throw std::runtime_error("D3D12SerializeRootSignature failed: " + blobText(errors.Get()));
    }

    // 创建根签名：交给设备
    gfx::ThrowIfFailed(
        m_context->device()->CreateRootSignature(0, signature->GetBufferPointer(), signature->GetBufferSize(),
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
    desc.RasterizerState.FillMode = D3D12_FILL_MODE_SOLID;     // 实心
    desc.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;      // 不做背面剔除，2D防止设置错误画面消失
    desc.RasterizerState.FrontCounterClockwise = FALSE;        // 不剔除，顶点顺/逆时针无意义
    desc.RasterizerState.DepthBias = D3D12_DEFAULT_DEPTH_BIAS; // 深度偏移，解决阴影贴图问题，不需要，使用默认
    desc.RasterizerState.DepthBiasClamp = D3D12_DEFAULT_DEPTH_BIAS_CLAMP;
    desc.RasterizerState.SlopeScaledDepthBias = D3D12_DEFAULT_SLOPE_SCALED_DEPTH_BIAS;
    desc.RasterizerState.DepthClipEnable = TRUE;                                         // 裁掉0~1之外的几何体，一般都打开
    desc.RasterizerState.MultisampleEnable = FALSE;                                      // 关闭多重采样（Q: why？A: 解决几何体边缘锯齿，2D不需要）
    desc.RasterizerState.AntialiasedLineEnable = FALSE;                                  // 关闭线段抗锯齿（Q: why？A: 线抗锯齿，不需要）
    desc.RasterizerState.ForcedSampleCount = 0;                                          // 强制光栅化成n个采样点，0不强制
    desc.RasterizerState.ConservativeRaster = D3D12_CONSERVATIVE_RASTERIZATION_MODE_OFF; // 保守光栅化模式，OFF = 传统光栅化；保守光栅化：碰到像素就算覆盖，传统：覆盖像素中心才算

    // 混合状态 / 混合模式
    desc.BlendState.RenderTarget[0].BlendEnable = FALSE;                                  // 关闭混合，直接覆盖，下方src、dest、op等参数无效，但显示设置成合法值
    desc.BlendState.RenderTarget[0].LogicOpEnable = FALSE;                                // 关闭逻辑操作，直接覆盖
    desc.BlendState.RenderTarget[0].SrcBlend = D3D12_BLEND_ONE;                           // 源颜色 = 1 * src
    desc.BlendState.RenderTarget[0].DestBlend = D3D12_BLEND_ZERO;                         // 目标颜色 = 0 * dest
    desc.BlendState.RenderTarget[0].BlendOp = D3D12_BLEND_OP_ADD;                         // 混合操作 = src + dest
    desc.BlendState.RenderTarget[0].SrcBlendAlpha = D3D12_BLEND_ONE;                      // 源 alpha = 1 * src
    desc.BlendState.RenderTarget[0].DestBlendAlpha = D3D12_BLEND_ZERO;                    // 目标 alpha = 0 * dest
    desc.BlendState.RenderTarget[0].BlendOpAlpha = D3D12_BLEND_OP_ADD;                    // 混合 alpha 操作 = src + dest
    desc.BlendState.RenderTarget[0].LogicOp = D3D12_LOGIC_OP_NOOP;                        // 逻辑操作 = 无操作，逻辑操作只对8-bit有效，通常关闭
    desc.BlendState.RenderTarget[0].RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL; // 4通道可写

    desc.DepthStencilState.DepthEnable = FALSE;   // 深度模板2D不需要
    desc.DepthStencilState.StencilEnable = FALSE; // 模板2D不需要

    desc.SampleMask = UINT_MAX;                                          // 全部采样点允许写入
    desc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE; // 三角形拓扑类型
    desc.NumRenderTargets = 1;                                           // PSO的声明的RTV和交换链的RTV数量必须一致
    desc.RTVFormats[0] = m_context->swapChainFormat();
    desc.SampleDesc.Count = 1; // 1个采样点，不开MSAA，必须和交换链的RTV数量一致

    // create PSO (pipline state object)
    gfx::ThrowIfFailed(
        m_context->device()->CreateGraphicsPipelineState(&desc, IID_PPV_ARGS(&m_pipeline)),
        "CreateGraphicsPipelineState");
}

/* ==================== 纹理 ==================== */

// SRV(Shader Resource View， 着色器资源摄图)
// 1个描述符，告诉硬件“着色器怎么读这块资源”
void D3D12Renderer::createSrvHeap()
{
    D3D12_DESCRIPTOR_HEAP_DESC desc{};
    desc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
    desc.NumDescriptors = 1;
    desc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE; // 着色器要能看见
    gfx::ThrowIfFailed(m_context->device()->CreateDescriptorHeap(&desc, IID_PPV_ARGS(&m_srvHeap)),
                        "CreateDescriptorHeap(SRV)");
}

// 创建一个测试纹理，并上传到 GPU
// Q: 真实环境中上传纹理是否单独为一个函数？
// A: 是的，在真实环境中，上传纹理通常会单独封装为一个函数或类方法，以便在需要时可以方便地调用。这样可以提高代码的可维护性和可重用性，同时也有助于管理纹理资源的生命周期和上传过程中的错误处理。
void D3D12Renderer::createTextureFromImage(std::uint32_t width, std::uint32_t height,
                                            const std::uint16_t *rgba16)
{
    // 1. 创建目标纹理（DEFAULT 堆，GPU 专用）
    // 1.1 ID3D12Resource（资源），描述数据长什么样
    D3D12_RESOURCE_DESC textureDesc{};
    textureDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    textureDesc.Width = width;
    textureDesc.Height = height;
    textureDesc.DepthOrArraySize = 1;
    textureDesc.MipLevels = 1;
    textureDesc.Format = DXGI_FORMAT_R16G16B16A16_UNORM;
    textureDesc.SampleDesc.Count = 1;
    textureDesc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
    textureDesc.Flags = D3D12_RESOURCE_FLAG_NONE;

    // 1.2 Heap（堆）,描述内存在哪、多大
    D3D12_HEAP_PROPERTIES defaultHeap{};
    defaultHeap.Type = D3D12_HEAP_TYPE_DEFAULT;
    gfx::ThrowIfFailed(
        m_context->device()->CreateCommittedResource( // committe, placed, reserved 三种绑定方式（ID3D12Resource - Heap 的绑定）
            &defaultHeap,
            D3D12_HEAP_FLAG_NONE, // GPU 专用堆
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
    m_context->device()->GetCopyableFootprints(&textureDesc, 0, 1, 0, &footprint, &numRows, &rowSize,
                                                &totalBytes);

    // 3. 上传堆缓冲（CPU 可写）
    D3D12_HEAP_PROPERTIES uploadHeap{};
    uploadHeap.Type = D3D12_HEAP_TYPE_UPLOAD; // CPU 可写，GPU 可读，行主序

    D3D12_RESOURCE_DESC bufferDesc{};
    bufferDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    bufferDesc.Width = totalBytes;
    bufferDesc.Height = 1;
    bufferDesc.DepthOrArraySize = 1;
    bufferDesc.MipLevels = 1;
    bufferDesc.Format = DXGI_FORMAT_UNKNOWN;
    bufferDesc.SampleDesc.Count = 1;
    bufferDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR; // 上传缓冲必须是行主序

    gfx::ComPtr<ID3D12Resource> uploadBuffer;
    gfx::ThrowIfFailed(
        m_context->device()->CreateCommittedResource(
            &uploadHeap, D3D12_HEAP_FLAG_NONE, &bufferDesc,
            D3D12_RESOURCE_STATE_GENERIC_READ, nullptr,
            IID_PPV_ARGS(&uploadBuffer)),
        "CreateCommittedResource(upload)");

    // 4. 逐行拷贝（必须按 footprint.RowPitch 走，不能按 width*bpp）
    // Q: 这里是否有零拷贝技术？
    // A: 在 D3D12 中，零拷贝技术通常是通过使用共享资源或直接映射 GPU 内存来实现的。然而，在这个例子中，我们使用了上传缓冲区（Upload Heap）来将纹理数据从 CPU 上传到 GPU，这涉及到一次内存拷贝操作。虽然这不是严格意义上的零拷贝，但它是 D3D12 中常用的纹理上传方法。要实现真正的零拷贝，需要使用更高级的技术，如使用共享资源或直接访问 GPU 内存，这可能需要特定的硬件支持和更复杂的资源管理。
    void *mapped = nullptr;
    gfx::ThrowIfFailed(
        uploadBuffer->Map(0, nullptr, &mapped),
        "Map(upload)");

    auto *destination = static_cast<std::uint8_t *>(mapped) + footprint.Offset;
    const auto *source = reinterpret_cast<const std::uint8_t *>(rgba16);
    const std::uint64_t sourceRowPitch = static_cast<std::uint64_t>(width) * 8; // RGBA16 = 8 B/px, 行优先，用width*bpp计算每行字节数
    for (UINT row = 0; row < numRows; ++row)
    {
        std::memcpy(
            destination + static_cast<size_t>(row) * footprint.Footprint.RowPitch,
            source + row * sourceRowPitch, static_cast<size_t>(rowSize)); // 只拷贝rowSize有效字节，其余留空
    }
    // Q: 拷贝完成，释放？
    // A: 是的，调用 Unmap 方法后，上传缓冲区的映射将被解除，CPU 不再可以直接访问该缓冲区。此时，数据已经被拷贝到 GPU 可读的上传缓冲区中，可以安全地释放映射。之后，GPU 可以从上传缓冲区读取数据并将其复制到目标纹理中。
    uploadBuffer->Unmap(0, nullptr);

    // 5. 录制拷贝命令
    // reset command allocator and command list，准备新录制 command
    m_context->beginCommandList();
    ID3D12GraphicsCommandList *commandList = m_context->commandList();

    // 两个 D3D12_TEXTURE_COPY_LOCATION 分别描述源和目标纹理的位置和布局
    D3D12_TEXTURE_COPY_LOCATION sourceLocation{};
    sourceLocation.pResource = uploadBuffer.Get();
    sourceLocation.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
    sourceLocation.PlacedFootprint = footprint;

    D3D12_TEXTURE_COPY_LOCATION destinationLocation{};
    destinationLocation.pResource = m_texture.Get();
    destinationLocation.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
    destinationLocation.SubresourceIndex = 0;

    commandList->CopyTextureRegion(&destinationLocation, 0, 0, 0, &sourceLocation, nullptr);

    D3D12_RESOURCE_BARRIER barrier{};
    barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barrier.Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE;
    barrier.Transition.pResource = m_texture.Get();
    barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_DEST;
    barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
    commandList->ResourceBarrier(1, &barrier);

    // 6. 提交并等拷贝完成（uploadBuffer 才能安全释放）
    m_context->submitAndWait();

    // 7. 建 SRV
    D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc{};
    srvDesc.Format = textureDesc.Format;
    srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
    srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    srvDesc.Texture2D.MipLevels = 1;
    m_context->device()->CreateShaderResourceView(m_texture.Get(), &srvDesc,
                                                    m_srvHeap->GetCPUDescriptorHandleForHeapStart());
}

// 设置输入图像，上传到 GPU 纹理， 更新 m_imageWidth 和 m_imageHeight
void D3D12Renderer::setImage(std::uint32_t width, std::uint32_t height,
                                const std::uint16_t *rgba16)
{
    if (!m_initialized || rgba16 == nullptr || width == 0 || height == 0)
    {
        return;
    }
    createTextureFromImage(width, height, rgba16);
    m_imageWidth = width;
    m_imageHeight = height;
}

/* ==================== clear screen and present ==================== */

void D3D12Renderer::present()
{
    if (!m_initialized)
    {
        return;
    }

    m_context->beginFrame();

    ID3D12GraphicsCommandList *commandList = m_context->commandList();
    const D3D12_CPU_DESCRIPTOR_HANDLE rtv = m_context->currentRtv();

    commandList->ClearRenderTargetView(rtv, kClearColor, 0, nullptr);

    // 没有图像：只清屏，直接交给 endFrame 呈现（原来的 early - return 分支删掉） if (m_imageWidth > 0 && m_imageHeight > 0)
    {
        commandList->OMSetRenderTargets(1, &rtv, FALSE, nullptr);
        commandList->SetGraphicsRootSignature(m_rootSignature.Get());
        commandList->SetPipelineState(m_pipeline.Get());

        ID3D12DescriptorHeap *heaps[] = {m_srvHeap.Get()};
        commandList->SetDescriptorHeaps(1, heaps);
        commandList->SetGraphicsRootDescriptorTable(
            0, m_srvHeap->GetGPUDescriptorHandleForHeapStart());

        // 矩阵参数：原 shaderParams 逻辑不变
        const int q = m_quarterTurns & 3;
        const float c = (q == 0) ? 1.0f : (q == 2) ? -1.0f : 0.0f;
        const float s = (q == 1) ? 1.0f : (q == 3) ? -1.0f : 0.0f;
        const float invZoom = (m_zoom > 0.0f) ? (1.0f / m_zoom) : 1.0f;

        float imageAspect = 1.0f;
        if (m_imageWidth > 0 && m_imageHeight > 0) {
            imageAspect = static_cast<float>(m_imageWidth) / static_cast<float>(m_imageHeight);
        }

        const float shaderParams[8] = {
            m_outputScale, imageAspect,
            c * invZoom, -s * invZoom,
            s * invZoom, c * invZoom,
            0.0f, 0.0f};
        commandList->SetGraphicsRoot32BitConstants(1, 8, shaderParams, 0);

        // contain-fit：尺寸改从 context 取
        const std::uint32_t width = m_context->width();
        const std::uint32_t height = m_context->height();
        const float effectiveAspect =
            ((m_quarterTurns & 1) != 0) ? (1.0f / imageAspect) : imageAspect;
        const float targetAspect = static_cast<float>(width) / static_cast<float>(height);

        float viewWidth = static_cast<float>(width);
        float viewHeight = static_cast<float>(height);
        if (effectiveAspect > targetAspect) {
            viewHeight = static_cast<float>(width) / effectiveAspect;
        }
        else {
            viewWidth = static_cast<float>(height) * effectiveAspect;
        }

        const float viewLeft = (static_cast<float>(width) - viewWidth) * 0.5f;
        const float viewTop = (static_cast<float>(height) - viewHeight) * 0.5f;

        D3D12_VIEWPORT viewport{};
        viewport.TopLeftX = viewLeft;
        viewport.TopLeftY = viewTop;
        viewport.Width = viewWidth;
        viewport.Height = viewHeight;
        viewport.MaxDepth = 1.0f;
        const D3D12_RECT scissor{static_cast<LONG>(viewLeft), static_cast<LONG>(viewTop),
                                 static_cast<LONG>(viewLeft + viewWidth),
                                 static_cast<LONG>(viewTop + viewHeight)};

        commandList->RSSetViewports(1, &viewport);
        commandList->RSSetScissorRects(1, &scissor);
        commandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        commandList->DrawInstanced(3, 1, 0, 0);
    }

    m_context->endFrame();
}
void D3D12Renderer::resize(std::uint32_t width, std::uint32_t height)
{
    if (m_context) {
        m_context->resize(width, height);
    }
}
} // namespace chroma::render