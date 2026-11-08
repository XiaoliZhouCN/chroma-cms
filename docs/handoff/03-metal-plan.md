# 03 · Metal 移植方案（macOS）

> 目标：用**同一套架构、共享同一段色彩数学**，在 macOS 上从零手写一个 Metal 渲染器。
> 当前两端的最后色域矩阵不同（Windows 709 / macOS P3），所以**不要求逐像素一致**；
> 共享到 XYZ，最后一步按平台分别断言。


---

## 0. 实现状态（2026-11-09，`practice` @ `e545d91`）

macOS 侧已完成：M1 清屏 → M2 全屏三角形 → M3 纹理 → M4 PQ→Display-P3 → 90° 旋转/缩放交互。

**本文件下面的伪代码是“最初方案”，与当前实现有几处刻意差异：**

| 项 | 本文伪代码 | 当前实现 |
|---|---|---|
| 最终色域 | XYZ→BT.709 / scRGB | **XYZ→Display-P3 线性** |
| `outputScale` | 0.8 | **1.0** |
| `CAEDRMetadata` | 未提 | **暂不设置** |
| CAMetalLayer 挂载 | `view.layer = layer` | **子 NSView 承载 CAMetalLayer**（Qt 的 QContainerView 会替换 host.layer） |
| 交互 | 无 | **A/D 90° 旋转、Q/E 缩放、R 复位**；8×float 常量 + 2×2 矩阵 UV |
| shader 参数 | 4×float（outputScale + 3 reserved） | **8×float**（outputScale/aspect/m00..m11/reserved） |

> 读 §5/§6 的 MSL 时注意：示例里的 `kXYZToRec709` 是 Windows 路径；macOS 实际用 `kXYZToDisplayP3`。  
> 双平台一致性只要求共享到 XYZ；最后一步矩阵按平台不同。

## 1. 约束

| 项 | 值 |
|---|---|
| 语言 | C++20 + **Objective-C++**（`.mm`）；不引入 metal-cpp（理由见 §2） |
| 窗口 | Qt 6 Widgets，沿用 `ViewportWidget` 的 `WA_NativeWindow` 原生表面方案 |
| 图形 API | Metal 3（macOS 13+）；`CAMetalLayer` 提供可绘制表面 |
| 输出格式 | `MTLPixelFormatRGBA16Float` + `kCGColorSpaceExtendedLinearDisplayP3`（当前实现） |
| 着色器 | MSL 源码文件 `shaders/fullscreen.metal`，**运行时编译**（对应 D3DCompile） |
| 接口 | 实现既有的 `chroma::render::IRenderer`（不要改接口签名） |

## 2. 为什么用 Objective-C++ 而不是 metal-cpp

| 方案 | 优点 | 缺点 |
|---|---|---|
| **ObjC++ `.mm`**（推荐） | 直接写 Metal API，文档/示例一一对应；调试信息完整；零第三方依赖 | 需要 `enable_language(OBJCXX)`；头文件必须保持纯 C++（pimpl） |
| metal-cpp（Apple 官方 C++ 绑定） | 全程 C++，跨平台代码风格统一 | 引入大量第三方头文件；版本要与 SDK 对齐；仍需 ObjC++ 写 `CAMetalLayer` |

结论：**用 `.mm` + pimpl**。`MetalRenderer.hpp` 保持纯 C++（只有 `IREnderer` + 前置声明），
所有 `id<MTLDevice>` 之类的类型只出现在 `.mm` 里。

## 3. 文件清单（agent 负责建空文件，用户写内容）

| 文件 | 内容 | 说明 |
|---|---|---|
| `src/render/MetalRenderer.hpp` | 类声明 + `struct Impl;` + `std::unique_ptr<Impl>` | 纯 C++ |
| `src/render/MetalRenderer.mm` | 全部 Metal/AppKit 代码 | ObjC++ |
| `shaders/fullscreen.metal` | MSL：VS + PS + 色彩常量 | 运行时编译 |
| `src/render/RendererFactory.hpp` / `.cpp` | `createRenderer()` 双平台工厂 | 见 04 |
| `src/app/ViewportWidget.cpp` | 把 `D3D12Renderer` 换成工厂 | 用户改 |

## 4. D3D12 → Metal 映射表

| D3D12（Windows 现有实现） | Metal（macOS 目标） | 说明 |
|---|---|---|
| `ID3D12Device` | `id<MTLDevice>` | `MTLCreateSystemDefaultDevice()` |
| `ID3D12CommandQueue` | `id<MTLCommandQueue>` | 一个队列够用 |
| `IDXGISwapChain3` + 2 个后台缓冲 | `CAMetalLayer` + `nextDrawable` | Metal 由系统管理 drawable 轮转 |
| `CreateSwapChainForHwnd(hwnd)` | 子 NSView 的 `layer = CAMetalLayer` | `winId()` 在 macOS 上是 `NSView*`；不要改 host.layer |
| RTV 描述符堆 + `CreateRenderTargetView` | **不需要** | drawable.texture 直接当颜色附件 |
| `ID3D12RootSignature` | **不需要** | 资源按 index 绑定 |
| 根常量（`32BIT_CONSTANTS`） | `setFragmentBytes:length:atIndex:` | 1:1 对应，同样"内联在命令里" |
| SRV 描述符堆 | `setFragmentTexture:atIndex:` | 不需要堆/视图，直接给 texture |
| 静态采样器（根签名内） | `MTLSamplerState` + `setFragmentSamplerState:atIndex:` | `newSamplerStateWithDescriptor:` |
| `ID3D12PipelineState` | `id<MTLRenderPipelineState>` | `MTLRenderPipelineDescriptor` |
| HLSL + `D3DCompileFromFile` | MSL + `newLibraryWithSource:options:error:` | 都是运行时编译 |
| `ResourceBarrier(TRANSITION)` | **不需要** | Metal 自动跟踪资源依赖（tracked mode） |
| UPLOAD 堆缓冲 + `CopyTextureRegion` | `MTLBuffer`(shared) + blit `copyFromBuffer:...toTexture:` | 或 `replaceRegion`（仅非 private 纹理） |
| `GetCopyableFootprints` 的 RowPitch | `sourceBytesPerRow` | macOS 要求 256 字节对齐（2048×8=16384 天然满足） |
| `ID3D12Fence` + `WaitForSingleObject` | `[commandBuffer waitUntilCompleted]` | 先保持"全同步"，后续再上 semaphore |
| `Present(1,0)` | `[cb presentDrawable:]` + `[cb commit]` | 垂直同步由 `layer.displaySyncEnabled` 控制 |
| `IASetPrimitiveTopology` + `DrawInstanced` | `drawPrimitives:MTLPrimitiveTypeTriangle vertexStart:0 vertexCount:3` | 全屏三角形技巧完全一样 |
| `RSSetViewports` | `setViewport:` | contain-fit 数学一样 |
| `RSSetScissorRects` | `setScissorRect:` | — |
| `OMSetRenderTargets` | render pass 的 `colorAttachments[0].texture` | — |

> **最大的简化**：没有根签名、没有描述符堆、没有资源屏障。这三样是 D3D12 里最繁琐的部分，
> Metal 用"对象引用 + 自动依赖跟踪"替代了它们。

## 5. 核心流程（伪代码，交付时按此拆成逐处改法）

### 5.1 设备与图层（`initialize`）

```objc
// MetalRenderer.mm
id<MTLDevice> device = MTLCreateSystemDefaultDevice();
id<MTLCommandQueue> queue = [device newCommandQueue];

NSView* view = (__bridge NSView*)nativeWindow;   // Qt: QWidget::winId()
view.wantsLayer = YES;

CAMetalLayer* layer = [CAMetalLayer layer];
layer.device = device;
layer.pixelFormat = MTLPixelFormatRGBA16Float;
layer.framebufferOnly = YES;
layer.maximumDrawableCount = 2;
layer.displaySyncEnabled = YES;
layer.colorspace = CGColorSpaceCreateWithName(kCGColorSpaceExtendedLinearSRGB);
layer.wantsExtendedDynamicRangeContent = YES;   // HDR/EDR（SDR 屏自动回落）
// 注意：当前实现用子 NSView；下面是最初伪代码
view.layer = layer;

// 尺寸用**物理像素**（与 Windows 的 physicalWidth/Height 一致）
layer.drawableSize = CGSizeMake(width, height);
layer.contentsScale = view.window.backingScaleFactor;
```

### 5.2 管线（对应 `createPipelineState`）

```objc
NSString* src = [NSString stringWithContentsOfFile:shaderPath
                                          encoding:NSUTF8StringEncoding
                                             error:&err];
MTLCompileOptions* opts = [MTLCompileOptions new];
opts.fastMathEnabled = NO;        // ← 必须关：保证与 HLSL 数学一致

id<MTLLibrary> lib = [device newLibraryWithSource:src options:opts error:&err];
MTLRenderPipelineDescriptor* pd = [MTLRenderPipelineDescriptor new];
pd.vertexFunction   = [lib newFunctionWithName:@"VSMain"];
pd.fragmentFunction = [lib newFunctionWithName:@"PSMain"];
pd.colorAttachments[0].pixelFormat = layer.pixelFormat;   // RGBA16Float
pipeline = [device newRenderPipelineStateWithDescriptor:pd error:&err];
```

### 5.3 每帧（对应 `present`）

```objc
id<CAMetalDrawable> drawable = [layer nextDrawable];
if (drawable == nil) return;                       // 超时：跳过这一帧，不要崩

MTLRenderPassDescriptor* rpd = [MTLRenderPassDescriptor renderPassDescriptor];
rpd.colorAttachments[0].texture     = drawable.texture;
rpd.colorAttachments[0].loadAction  = MTLLoadActionClear;
rpd.colorAttachments[0].storeAction = MTLStoreActionStore;
rpd.colorAttachments[0].clearColor  = MTLClearColorMake(0.02, 0.02, 0.02, 1.0);  // 线性值

id<MTLCommandBuffer> cb = [queue commandBuffer];
id<MTLRenderCommandEncoder> enc = [cb renderCommandEncoderWithDescriptor:rpd];

[enc setRenderPipelineState:pipeline];
[enc setViewport:(MTLViewport){ viewLeft, viewTop, viewWidth, viewHeight, 0.0, 1.0 }];
[enc setScissorRect:(MTLScissorRect){ left, top, w, h }];
[enc setFragmentTexture:texture atIndex:0];
[enc setFragmentSamplerState:sampler atIndex:0];
ShaderParams params = { outputScale, 0, 0, 0 };
[enc setFragmentBytes:&params length:sizeof(params) atIndex:0];
[enc drawPrimitives:MTLPrimitiveTypeTriangle vertexStart:0 vertexCount:3];
[enc endEncoding];

[cb presentDrawable:drawable];
[cb commit];
[cb waitUntilCompleted];        // 暂时全同步，与 Windows 现有 waitForGpu 等价
```

### 5.4 纹理上传（对应 `createTextureFromImage`）

```objc
MTLTextureDescriptor* td = [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatRGBA16Unorm
                                                                              width:width
                                                                             height:height
                                                                          mipmapped:NO];
td.usage = MTLTextureUsageShaderRead;
td.storageMode = MTLStorageModePrivate;
texture = [device newTextureWithDescriptor:td];

const NSUInteger bytesPerRow = width * 8;    // RGBA16
id<MTLBuffer> staging = [device newBufferWithBytes:rgba16
                                            length:bytesPerRow * height
                                           options:MTLResourceStorageModeShared];
id<MTLCommandBuffer> cb = [queue commandBuffer];
id<MTLBlitCommandEncoder> blit = [cb blitCommandEncoder];
[blit copyFromBuffer:staging
        sourceOffset:0
   sourceBytesPerRow:bytesPerRow
 sourceBytesPerImage:bytesPerRow * height
          sourceSize:MTLSizeMake(width, height, 1)
           toTexture:texture
    destinationSlice:0
    destinationLevel:0
   destinationOrigin:MTLOriginMake(0, 0, 0)];
[blit endEncoding];
[cb commit];
[cb waitUntilCompleted];     // 对应 D3D12 上传后那次 waitForGpu
```

> 注意：macOS 上 `sourceBytesPerRow` 需要 **256 字节对齐**。2048×8 = 16384 天然满足；
> 换其它宽度时要自己 `roundUp(align)` 并调整 staging 大小。

## 6. MSL 着色器（`shaders/fullscreen.metal`）

> 与 `shaders/fullscreen.hlsl` **数学完全一致**，只是语法与矩阵存储顺序不同。

```metal
#include <metal_stdlib>
using namespace metal;

struct VsOut {
    float4 position [[position]];
    float2 uv;
};

vertex VsOut VSMain(uint vid [[vertex_id]])
{
    float2 uv = float2((vid << 1) & 2, vid & 2);

    VsOut o;
    o.position = float4(uv * float2(2.0, -2.0) + float2(-1.0, 1.0), 0.0, 1.0);
    o.uv = uv;
    return o;
}

struct ShaderParams {
    float outputScale;
    float reserved0;
    float reserved1;
    float reserved2;
};

constant float kPqPeakNits   = 10000.0;
constant float kScRgbRefNits = 80.0;

// 注意：MSL 的 float3x3 按**列**初始化，这里给的是 HLSL 行矩阵的转置写法
constant float3x3 kRec2020ToXYZ = float3x3(
    float3(0.636958, 0.262700, 0.000000),   // 第 0 列
    float3(0.144617, 0.677998, 0.028073),   // 第 1 列
    float3(0.168881, 0.059302, 1.060985));  // 第 2 列

constant float3x3 kXYZToRec709 = float3x3(
    float3( 3.240970, -0.969244,  0.055630),
    float3(-1.537383,  1.875968, -0.203977),
    float3(-0.498611,  0.041555,  1.056972));

float3 PqEotf(float3 code)
{
    const float m1 = 0.1593017578125;
    const float m2 = 78.84375;
    const float c1 = 0.8359375;
    const float c2 = 18.8515625;
    const float c3 = 18.6875;

    float3 p = pow(max(code, 0.0), 1.0 / m2);
    float3 num = max(p - c1, 0.0);
    float3 den = c2 - c3 * p;
    return pow(num / max(den, 1e-6), 1.0 / m1);
}

fragment float4 PSMain(VsOut in [[stage_in]],
                       texture2d<float> tex [[texture(0)]],
                       sampler smp [[sampler(0)]],
                       constant ShaderParams& params [[buffer(0)]])
{
    float3 code = tex.sample(smp, in.uv).rgb;

    float3 linear2020 = PqEotf(code);
    float3 xyz        = kRec2020ToXYZ * linear2020;
    float3 linear709  = kXYZToRec709 * xyz;
    linear709 = max(linear709, 0.0);

    float3 scRgb = (linear709 * kPqPeakNits / kScRgbRefNits) * params.outputScale;
    return float4(scRgb, 1.0);
}
```

### 矩阵顺序陷阱（最容易错）

- HLSL：`float3x3 m = { 行0, 行1, 行2 };` + `mul(m, v)`；
- MSL：`float3x3(col0, col1, col2)` + `m * v`。

所以**把 HLSL 行矩阵转置后按列写**。上面对照表：

| 数学矩阵 | HLSL 写法 | MSL 写法 |
|---|---|---|
| `[a b c; d e f; g h i]` | `{a,b,c}, {d,e,f}, {g,h,i}` | `float3(a,d,g), float3(b,e,h), float3(c,f,i)` |

## 7. 色彩空间 / HDR（EDR）等价关系

| Windows（现有） | macOS（目标） |
|---|---|
| 交换链 `R16G16B16A16_FLOAT` | drawable `MTLPixelFormatRGBA16Float` |
| `DXGI_COLOR_SPACE_RGB_FULL_G10_NONE_P709`（scRGB，线性，1.0=80 nits） | **当前实现**：`kCGColorSpaceExtendedLinearDisplayP3` + `wantsExtendedDynamicRangeContent = YES`；不设 `CAEDRMetadata` |
| SDR 屏由 DWM 转 sRGB 显示 | SDR 屏由系统转 sRGB 显示（EDR 自动回落） |
| HDR 屏由 DWM 按 SDR 白点映射 | HDR 屏由 EDR headroom 映射（`NSScreen.maximumPotentialExtendedDynamicRangeColorComponentValue` 可查） |
| 清屏色是线性值 | 同样：`MTLClearColorMake(0.02, 0.02, 0.02, 1.0)` |

> 关键：**不要**用默认的 `kCGColorSpaceSRGB` 或 `BGRA8Unorm`——那会把线性值当 sRGB 编码，
> 画面会明显变暗。这与 Windows 侧"不能用 G22 交换链"是同一个坑。

## 8. 路线图（每步都能上屏，出错只看这一步）

### M1 · 设备 + 图层 + 清屏
- 文件：`MetalRenderer.hpp/.mm`（空文件由 agent 建）
- 验收：窗口均匀显示 `(0.02,0.02,0.02)` 线性灰；日志 `[gfx] backend = Metal`
- 关键点：子 NSView + `CAMetalLayer`、`drawableSize`、`colorspace`、`nextDrawable` + clear

### M2 · 全屏三角形
- 文件：`shaders/fullscreen.metal`（VS + 纯色/渐变 PS）
- 验收：屏幕铺满渐变（和 Windows 第 3 步一致的观感）
- 关键点：运行时编译 MSL、`fastMathEnabled = NO`、`drawPrimitives`

### M3 · 纹理上传 + 采样
- 验收：显示棋盘格 + 四角标记（红/绿/蓝/白），方向正确 —— 对照 `images/expected-step4-texture.png`
- 关键点：`MTLPixelFormatRGBA16Unorm`、staging buffer、blit 拷贝、sampler（linear + clamp）

### M4 · 色彩管线 ✅
- 验收：显示 PTMR 图，颜色饱和、不再发灰
- 当前实现：PQ EOTF → BT.2020→XYZ → **XYZ→Display-P3** → `/80`；`outputScale=1.0`
- 注意：macOS 暂时没有 `CAEDRMetadata`，高光会逐通道裁剪；这不是 shader 数学错误

### M4.5 · 交互 ✅
- A/D 90° 旋转、Q/E 缩放、R 复位
- `IViewTransform` 可选接口；8×float 常量；旋转/缩放合成 2×2 矩阵
- 非方图 90°/270° 时 viewport 宽高比交换

### M5 · 双平台一致性（待做）
- 内容：CPU 参考实现 + 单元测试（见 04 §5）；两平台只比较到 XYZ
- 验收：PQ/XYZ 数学一致；最后矩阵按平台分别断言；截图不要求逐像素相同

## 9. macOS 专有坑

| 坑 | 现象 | 处理 |
|---|---|---|
| `nextDrawable` 返回 nil | 偶发空指针崩溃 | 判空后直接 return，跳过该帧 |
| Qt 重置 `view.layer` | 画面黑屏/闪烁 | **不要改 host.layer**；用子 NSView 承载 CAMetalLayer（Qt 6 会替换 host.layer） |
| `drawableSize` 用的是点而不是像素 | 画面模糊/只占左上角 | `drawableSize` 用**物理像素**；`contentsScale = backingScaleFactor` |
| 默认 colorspace 是 sRGB | 线性输出变暗 | 显式色彩空间；当前 mac 用 `kCGColorSpaceExtendedLinearDisplayP3` |
| 16F drawable 不可用 | 创建失败 | 检查设备支持（Apple 芯片/较新 Intel 都支持；否则回落 `BGRA8Unorm_srgb` + 做 OETF） |
| MSL 默认快速数学 | 与 HLSL 结果有细微差异 | macOS 15+ `MTLMathModeSafe`；更老系统 `fastMathEnabled = NO` |
| 应用打包后找不到 shader | `newLibraryWithSource` 报文件不存在 | 把 `shaders/` 拷到可执行文件同目录（bundle 里是 `Contents/MacOS/shaders`）；CMake 的 POST_BUILD 只在 relink 时拷贝 |
| `waitUntilCompleted` 在主线程 | 窗口卡顿 | 先接受（与 Windows 全同步一致），后续用信号量/双缓冲优化 |
| 截图黑屏 | 终端没有屏幕录制权限 | 系统设置授权后重试 |
