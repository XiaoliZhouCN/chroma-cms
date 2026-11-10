# ChromaCMS 渲染笔记：图片文件输入 + 色彩管理

> 这份笔记按程序的**两条数据链路**组织：
> **① 图片文件输入**（PNG 文件 → CPU 像素 → GPU 纹理）和 **② 色彩管理**（纹理里的 PQ 码值 → 显示器能用的线性值），
> 最后再看它们如何汇合成一帧，以及重构后每一步归 `gfx` 还是 `render`。
>
> 代码坐标以当前 `practice` @ **`822b976`**（两个平台都已抽出 context）为准；
> 重构前的完整程序另存于 `legacy/program-42518f8/`（`1575472` 之前的最后一版，两个 renderer 都还是单文件）。
> 工作区另有一处未提交修改：`src/render/d3d12/D3D12Renderer.cpp` 的"无图不 Draw"守卫已恢复。

---

## 0. 总览：两条链路 + 一个底座

```text
                        assets/test-images/PTMR_....png
                                   │
        ┌──────────────────────────┴──────────────────────────┐
        │  ① 图片文件输入                                       │
        │  FFmpeg 解码（media/ImageLoader.cpp）                  │
        │      → RGBA16_UNORM, 行优先, 8 B/px                  │
        │  ViewportWidget::ensureRenderer → renderer->setImage │
        │  GPU 纹理上传（D3D12 / Metal 各一套）                   │
        └──────────────────────────┬──────────────────────────┘
                                   ▼
                           GPU 纹理（16-bit/通道）
                                   │
        ┌──────────────────────────┴──────────────────────────┐
        │  ② 色彩管理（shader PS，单 pass）                     │
        │  采样 PQ 码值 → PQ EOTF → BT.2020 线性                 │
        │  → CIE XYZ → 平台矩阵（Rec.709 / Display-P3）          │
        │  → 裁负 → ×10000 nits → ÷80 → ×outputScale           │
        │  输出：16F 线性值（不做 OETF）                          │
        └──────────────────────────┬──────────────────────────┘
                                   ▼
                  交换链 / drawable（R16G16B16A16_FLOAT）
                     D3D12: scRGB（G10_NONE_P709）
                     Metal: ExtendedLinearDisplayP3 + EDR
                                   │
                                   ▼
                        DWM / WindowServer 显示映射 → 屏幕
```

**底座**（两条链路都依赖，但不属于任何一条）：

- app 层：Qt 事件（`showEvent` / `resizeEvent` / `paintEvent` / `keyPressEvent`）；
- `gfx` 层：设备、交换链 / CAMetalLayer、命令、fence、`Present`；
- 同步模型：全同步（D3D12 等 fence，Metal 等 `waitUntilCompleted`）。

| 链路 | 输入 | 输出 | 主要文件 |
|---|---|---|---|
| ① 图片文件输入 | PNG 文件 | GPU 纹理 | `app/main.cpp`、`media/ImageLoader.*`、`app/ViewportWidget.cpp`、`render/*/…Renderer::setImage` |
| ② 色彩管理 | 纹理中的 PQ 码值 | 16F 线性像素 | `shaders/fullscreen.{hlsl,metal}`、renderer 里的 8 个常量、`gfx/*Context` 的色彩空间声明 |
| 底座 | Qt 窗口 / 原生表面 | 每帧 Present | `app/ViewportWidget.*`、`gfx/d3d12/D3D12Context.*`、`gfx/metal/MetalContext.*` |

---

## 1. ① 图片文件输入方案

### 1.1 目标与契约

**目标**：把磁盘上的 PNG 变成 GPU 上可采样的纹理，且**跨平台一致**。

约定：

| 项 | 约定 |
|---|---|
| 解码层输出 | `media::Image{ width, height, std::vector<uint16_t> rgba16 }`（`src/media/ImageLoader.hpp:10-14`） |
| 像素格式 | RGBA16_UNORM，行优先，每像素 8 字节 |
| 生命周期 | `setImage()` 返回后 CPU 数据即可释放（上传内部已等 GPU 完成） |
| 分层 | 解码层不碰 GPU；renderer 不读文件。**PNG 永远不进 renderer** |

### 1.2 文件从哪来

- 图片路径：`src/app/main.cpp:15-17`，用编译期宏拼出
  `CHROMA_SOURCE_DIR "/assets/test-images/PTMR_9-16-9-full_2048x2048_10bit.png"`；
  `CHROMA_SOURCE_DIR` 由 `src/app/CMakeLists.txt` 的 `target_compile_definitions` 注入。
- `assets/` 被 `.gitignore` 排除，测试图用 `tools/gen_ptmr.py` 生成（PTMR 色带 + `cICP [9,16,0,1]`）。
- 解码用 FFmpeg：`cmake/FindFFmpeg.cmake` 找到 `external/ffmpeg`（或 Homebrew），产出 `FFmpeg::FFmpeg`；
  Windows 的 DLL 由 `src/app/CMakeLists.txt` 的 POST_BUILD 拷到 exe 旁边。

### 1.3 FFmpeg 解码（`src/media/ImageLoader.cpp`）

```text
loadImage(utf8Path, out, error)            // :25
  1. avformat_open_input                    // :39 打开容器（PNG 也有"容器"层）
  2. 找流 + avcodec_find_decoder            // :57
  3. avcodec_alloc_context3 + open2         // :63 / :69
  4. send_packet / receive_frame            // :88-97（只取第一帧）
  5. sws_getContext(..., AV_PIX_FMT_RGBA64LE, SWS_POINT)   // :113
  6. sws_scale → out.rgba16（stride = width*8）            // :123
  7. 识别并还原 10-bit 左移 6 位的存储约定                    // :125-145
     若三个 RGB 通道低 6 位全为 0：
        code10 = value16 >> 6
        value16 = (code10 * 65535 + 511) / 1023
  8. 失败路径：out = Image{}；释放 avframe/packet/codec/format（:150-168）
```

要点：

- **为什么是 `RGBA64LE`**：FFmpeg 把"每通道 16-bit"的像素格式叫 `RGBA64`（64 = 4×16），`LE` 是字节序。
  `sws_scale` 在这里只做**像素布局转换**（`SWS_POINT`，不做色彩转换）。
- **为什么要第 7 步**：PTMR 图是 10-bit PQ 码值，解码器常把 10-bit 值左移 6 位放进 16-bit 容器。
  不还原就相当于整体压暗（最大值只有 1023/65535）。识别方式是"低 6 位全 0"这一特征。
- 解码层的错误只写 `error` 字符串；渲染层完全不知道 FFmpeg 的存在。

### 1.4 跨层交接（`src/app/ViewportWidget.cpp`）

```text
showEvent()                                 // :93
  → ensureRenderer()                        // :58
      1. createRenderer()                   // :66  RendererFactory（平台在编译期选定）
      2. renderer->initialize(winId(), 物理宽, 物理高)   // :71
      3. m_renderer = std::move(renderer)   // 初始化成功才接管（异常安全）
      4. if (m_image) m_renderer->setImage(...)        // :77  ← 输入链路在这里进入 GPU
      5. applyViewTransform()               // dynamic_cast<IViewTransform*> 设旋转/缩放
      6. 打日志 + emit rendererReady
  → setFocus()、update()
```

- `winId()`：Windows 是 `HWND`，macOS 是 `NSView*`；尺寸传**物理像素**（`width() * devicePixelRatioF()`）。
- 图片加载失败时 `m_image` 为空：renderer 照样初始化，`present()` 只清屏（见 §4 的守卫）。
- app 只调 5 个接口：`initialize / resize / setImage / present / backendName`（`src/render/IRenderer.hpp`），
  另有可选的 `IViewTransform`。

### 1.5 GPU 纹理上传（两平台各一套）

**共同模型**：CPU 缓冲（紧排/对齐）→ GPU 纹理（私有内存）→ 记录一次拷贝 → **提交并等 GPU** → 之后才能采样。

**D3D12**（`src/render/d3d12/D3D12Renderer.cpp` `createTextureFromImage`，约 :298-417）

```text
1. 目标纹理：CreateCommittedResource(DEFAULT 堆, R16G16B16A16_UNORM,
                                     初始状态 COPY_DEST)
2. GetCopyableFootprints：RowPitch（256 对齐）、每行有效字节 rowSize、总字节
3. 上传缓冲：CreateCommittedResource(UPLOAD 堆, ROW_MAJOR, GENERIC_READ)
4. Map → 按 footprint.RowPitch 逐行 memcpy → Unmap
5. context->beginCommandList()            // allocator/list Reset（重构前在 renderer 里）
   CopyTextureRegion(upload → texture)
   ResourceBarrier(COPY_DEST → PIXEL_SHADER_RESOURCE)
   context->submitAndWait()               // Close + Execute + waitForGpu
6. CreateShaderResourceView(texture → srvHeap)
```

**Metal**（`src/render/metal/MetalRenderer.mm` `setImage`，:331-420）

```text
1. MTLTextureDescriptor：RGBA16Unorm、usage=ShaderRead、storageMode=Private
2. sourceRowBytes = width*8；bytesPerRow = roundUp(256)
3. staging：2048 宽走整块 memcpy，否则逐行拷到对齐行宽
4. [device newBufferWithBytes:... options:Shared] → stagingBuffer
5. context->beginCommandList()            // 新建 command buffer
   blit = [commandBuffer blitCommandEncoder]
   [blit copyFromBuffer:... sourceBytesPerRow:bytesPerRow ... toTexture:]
   [blit endEncoding]
   context->submitAndWait()               // commit + waitUntilCompleted
6. m_impl->texture = texture（ARC 强引用，旧纹理自动释放）
```

**对照**

| 步骤 | D3D12 | Metal |
|---|---|---|
| GPU 纹理 | DEFAULT 堆资源，初始 `COPY_DEST` | `storageMode = Private` 纹理 |
| CPU 侧缓冲 | UPLOAD 堆资源，`Map/Unmap` | `MTLBuffer`（Shared）+ staging 数组 |
| 行对齐 | `GetCopyableFootprints` 的 256 RowPitch | `sourceBytesPerRow` 256 对齐 |
| 拷贝命令 | `CopyTextureRegion` | blit `copyFromBuffer:...toTexture:` |
| 状态切换 | barrier `COPY_DEST → PIXEL_SHADER_RESOURCE` | **不需要**（驱动自动跟踪） |
| 同步 | `submitAndWait`（fence + Win32 event） | `submitAndWait`（`waitUntilCompleted`） |
| 采样入口 | SRV（描述符堆） | `setFragmentTexture` 直接给对象 |

> **Q: 为什么上传完必须等 GPU？**
> A: `uploadBuffer` / `stagingBuffer` 是 CPU 侧临时资源，函数返回就释放；
> 不等拷贝完成就释放，GPU 可能读到已回收内存（闪纹理/花屏）。
> 这也是 `submitAndWait` 名字的由来——它是"一次性命令"的收尾。
>
> **Q: 重构后 renderer 还碰命令列表吗？**
> A: 碰，但只"录制"；`beginCommandList()/submitAndWait()/commandList()/commandBuffer()` 由 context 提供，
> Reset/Close/Execute/wait 这些"台账"动作不归 renderer（见 §5）。

### 1.6 输入链路的常见错误

| 现象 | 原因 | 处理 |
|---|---|---|
| 纹理花屏/撕裂 | 上传缓冲在 GPU 拷完前释放 | 上传后必须等 GPU（`submitAndWait`） |
| 图片整体偏暗 | 10-bit 左移 6 位没还原 | 检查 `ImageLoader.cpp` 的低 6 位检测 |
| 每行错位/斜纹 | 没按 256 对齐的行宽拷贝 | D3D12 用 footprint.RowPitch；Metal 用 256 对齐 `bytesPerRow` |
| 纹理方向反了 | 行序/UV 方向 | 对照 `docs/handoff/images/expected-step4-texture.png`（四角标记） |
| Windows 运行缺 avcodec DLL | POST_BUILD 没跑/被跳过 | 手动拷 `external/ffmpeg/bin/*.dll` 到 exe 旁 |
| 无图时黑屏/报错 | 没走 clear-only 分支 | 见 §4 的 `m_imageWidth > 0` 守卫 |

---

## 2. ② 色彩管理方案

### 2.1 目标与总体选择

**目标**：纹理里存的是 **PQ（ST.2084）编码的码值**，不是线性光；显示器/合成器需要线性值，
所以必须在"采样之后、呈现之前"完成：**PQ 解码 → 线性 RGB → 色域转换 → 显示参考缩放**。

本项目的选择（刻意简单）：

- **单 pass**：全部在像素着色器里做，不引入中间纹理/多 pass；
- **不做 OETF**：输出到 **16F 线性**交换链 / drawable，由系统做最终显示映射；
- **共享到 XYZ**：两端共用 PQ EOTF 与 BT.2020→XYZ，最后一步矩阵按平台不同；
- **不要求两端逐像素一致**：Windows 走 Rec.709/scRGB（`outputScale=0.8`），macOS 走 Display-P3（`1.0`）。

### 2.2 参数通道：CPU 如何把 8 个 float 交给 shader

shader 端（两端一致）：

```text
struct ShaderParams {          // 8 × float = 32 字节
    float outputScale;         // [0] 显示缩放（Windows 0.8 / mac 1.0）
    float aspect;              // [1] imageWidth / imageHeight
    float m00, m01, m10, m11;  // [2..5] 采样逆变换矩阵（含 1/zoom）
    float reserved0, reserved1;// [6..7] 对齐/预留
};
```

| 项 | D3D12 | Metal |
|---|---|---|
| 声明 | 根签名根参数 1：`32BIT_CONSTANTS`，`Num32BitValues = 8`（`D3D12Renderer::createRootSignature`） | `constant ShaderParams& params [[buffer(0)]]` |
| 每帧写入 | `SetGraphicsRoot32BitConstants(1, 8, shaderParams, 0)`（`present()` :471-476） | `[encoder setFragmentBytes:&params length:32 atIndex:0]`（`present()` :263-267） |
| 布局约束 | 8×4 = **32 字节**；寄存器 b0 | 必须和 MSL 的 struct 逐字段一致（16 字节对齐规则内） |

> **Q: 为什么把"色彩"和"几何"塞进同一组常量？**
> A: 两者都是"每帧很小、每帧都变"的参数；塞进同一次根常量/`setFragmentBytes` 可以少一套绑定，
> 也保证 shader 只需一个 `ShaderParams`。`aspect`/矩阵属于采样域变换（§2.5），`outputScale` 属于色彩域。

### 2.3 数学链（shader 里逐行）

以 `shaders/fullscreen.hlsl:76-99`（MSL 对应 `fullscreen.metal:80-101`）为准：

```text
code            = texture.Sample(...).rgb          // 归一化 PQ 码值（0..1）
linear2020      = PqEotf(code)                     // 相对亮度，1.0 = 10000 nits
xyz             = kRec2020ToXYZ * linear2020       // CIE XYZ (D65)
linearPlatform  = kXYZTo<平台> * xyz                // Windows: Rec.709；mac: Display-P3
linearPlatform  = max(linearPlatform, 0)           // 裁掉色域外负值
nits            = linearPlatform * 10000
scaled          = (nits / 80) * outputScale        // scRGB：1.0 == 80 nits
输出 float4(scaled, 1)                              // 16F 线性，无 OETF
```

**PQ EOTF（ST.2084）常量**（两端必须一致）：

| 名称 | 值 | 来历 |
|---|---|---|
| m1 | 0.1593017578125 | 2610/16384 |
| m2 | 78.84375 | 2523/4096 × 128 |
| c1 | 0.8359375 | 3424/4096 |
| c2 | 18.8515625 | 2413/4096 × 32 |
| c3 | 18.6875 | 2392/4096 × 32 |

**矩阵**：

| 变换 | Windows | macOS |
|---|---|---|
| BT.2020 线性 → XYZ (D65) | `0.636958 0.144617 0.168881 / 0.262700 0.677998 0.059302 / 0.000000 0.028073 1.060985` | 同左 |
| XYZ → 平台线性 RGB | Rec.709：`3.240970 -1.537383 -0.498611 / -0.969244 1.875968 0.041555 / 0.055630 -0.203977 1.056972` | Display-P3：`2.4934969119 -0.9313836179 -0.4027107845 / -0.8294889696 1.7626640603 0.0236246858 / 0.0358458302 -0.0761723893 0.9568845240` |

编写矩阵时最常见的坑：

```text
HLSL：float3x3 m = { 行0, 行1, 行2 };  mul(m, v)   // 行主序书写
MSL ：float3x3(col0, col1, col2);      m * v      // 按列初始化 → 把 HLSL 行矩阵转置后按列写
```

**为什么 `÷80`**：scRGB 的 1.0 定义为 80 nits（SDR 白点）；`×10000` 是把相对亮度换成 nits，
再除以 80 得到"以 scRGB 为单位"的线性值。`outputScale` 是平台/观感缩放：

- Windows：**0.8**（压低一点，给 HDR 屏高光留 headroom；旧实测 1.0 → 均亮度 94.7，0.0 → 31.2）；
- macOS：**1.0**（当前 EDR 策略，见 §2.4 已知缺口）。

**为什么不写 OETF**：交换链/drawable 已声明为"线性扩展色域"（§2.4），系统负责把它映射到屏幕；
若再写一次 sRGB OETF，等于编码两次，画面会明显发灰/过亮。

### 2.4 输出端声明：色彩管理的另一半

色彩管理不只是 shader 数学，还要**告诉系统"这块缓冲里是线性值、是什么色域"**。这步在 `gfx` 层：

| 项 | D3D12 | Metal |
|---|---|---|
| 缓冲格式 | `DXGI_FORMAT_R16G16B16A16_FLOAT`（`D3D12Context.cpp:12`） | `MTLPixelFormatRGBA16Float`（`MetalContext.mm:104`） |
| 色彩空间 | `DXGI_COLOR_SPACE_RGB_FULL_G10_NONE_P709`（scRGB，线性，1.0=80nits），`SetColorSpace1`（`D3D12Context.cpp:141-144`） | `layer.colorspace = kCGColorSpaceExtendedLinearDisplayP3`（`MetalContext.mm:115-120`） |
| HDR/EDR | 由 DWM 处理 | `layer.wantsExtendedDynamicRangeContent = YES`（:121）；**暂不设 `CAEDRMetadata`** |
| 含义 | 只声明解释方式，**不做转换** | 同左 |

> **Q: 为什么 D3D12 声明成"P709/G10"却输出 Rec.709 线性？**
> A: `G10` = gamma 1.0（线性），`P709` 是红/绿/蓝原色定义；这就是 scRGB 的交换链声明，
> 告诉 DWM"按线性、Rec.709 原色解释"。数值超过 1.0 表示超过 SDR 白点（HDR 高光）。
>
> **Q: mac 上高光为什么容易泛白/偏色？**
> A: 当前 `outputScale=1.0` 且没有 `CAEDRMetadata`，超过屏幕 EDR headroom 的部分会逐通道裁剪；
> 这是已知情况，不是 shader 数学错。M4.2 再做 EDR/tone mapping。

### 2.5 采样域变换（交互矩阵）与色彩链路的关系

旋转/缩放发生在**采样阶段**，不是颜色变换，但它和色彩共用同一组 8 个常量：

```text
CPU（present）：q = quarterTurns % 4
  c = q==0 ? 1 : q==2 ? -1 : 0；s = q==1 ? 1 : q==3 ? -1 : 0
  invZoom = 1/zoom
  params = { outputScale, aspect,
             c*invZoom, -s*invZoom,      // m00, m01
             s*invZoom,  c*invZoom,      // m10, m11
             0, 0 }
GPU（PS 采样前）：TransformUV
  p = (uv - 0.5) * (aspect, 1)
  p = (m00*p.x + m01*p.y, m10*p.x + m11*p.y)
  uv' = p / (aspect, 1) + 0.5
  越界 → discard（D3D12）/ discard_fragment（Metal），露出 clear color
```

- **A 一次（q=1）** 对应矩阵 `[0,-1; 1,0]`，屏幕上看是逆时针 90°；
- **contain-fit** 在 CPU 算 viewport/scissor（`D3D12Renderer::present` :478-510 / `MetalRenderer::present` :269-305），
  90°/270° 时用 `1/aspect` 交换宽高比（`effectiveAspect`）；
- 矩阵在 CPU 合成而不是 shader 里算 `cos/sin`：每帧只有 8 个 float，且方便两端共用同一段逻辑。

### 2.6 验收与已知差异

| 验收点 | 期望 |
|---|---|
| 色带顺序/朝向 | PTMR：灰/红/绿/蓝/青/黄/品/灰，左暗右亮，方向正确 |
| 灰阶 | 单调递增，暗部能分层 |
| 高光 | 不整体泛白（Windows）；mac 高光逐通道裁剪是已知缺口 |
| 定量 | `assets/test-images/*.csv` 的 `code10` → 反算 nits，和显示值对照（M5 再做 CPU ColorMath 单测） |
| 跨平台 | **不逐像素比较**；只比色带顺序/朝向/灰阶趋势，数学共享到 XYZ |

---

## 3. 两条链路如何合成一帧

### 3.1 初始化（一次性）

| 阶段 | D3D12（`D3D12Renderer::initialize` / `D3D12Context::initialize`） | Metal（`MetalRenderer::initialize` / `MetalContext::initialize`） |
|---|---|---|
| 设备/队列 | `D3D12CreateDevice` + `CreateCommandQueue` + `DXGIFactory` | `MTLCreateSystemDefaultDevice` + `newCommandQueue` |
| 表面 | `CreateSwapChainForHwnd`（2 缓冲、flip-discard、16F、scRGB 色彩空间） | 子 `NSView` + `CAMetalLayer`（16F、Display-P3 线性、EDR） |
| 帧资源 | allocator + command list + fence + event | 每帧新建 command buffer（无 allocator/fence） |
| 管线 | 根签名 + PSO（运行时编译 HLSL） | PSO（运行时编译 MSL）+ sampler |
| 纹理 | SRV 堆（纹理在 `setImage` 时创建） | texture（`setImage` 时创建） |

### 3.2 每帧（`paintEvent → present`）

**D3D12**（`D3D12Renderer::present` :434-514）：

```text
m_context->beginFrame()        // waitForGpu + allocator/list Reset + barrier PRESENT→RT
rtv = currentRtv(); ClearRenderTargetView(rtv, clearColor)
if (有图片) {                  // ← 无图只清屏（工作区刚修复的守卫 :448-449）
    OMSetRenderTargets；SetRootSignature/PSO；SetDescriptorHeaps + 根描述符表
    SetGraphicsRoot32BitConstants(1, 8, params)      // ② 色彩/几何参数
    contain-fit → viewport/scissor；DrawInstanced(3,1,0,0)  // 全屏三角形，PS 里做 ②
}
m_context->endFrame()          // barrier RT→PRESENT + Close + Execute + Present + 更新 frameIndex
```

**Metal**（`MetalRenderer::present` :202-313）：

```text
if (!m_context->beginFrame()) return;     // nextDrawable == nil → 跳过该帧
drawable/texture = currentDrawable()      // renderer 自建 render pass + encoder
pass.loadAction = Clear; clearColor = kClearColor
if (texture == nil) { endEncoding; m_context->endFrame(); return; }   // 无图只清屏
setRenderPipelineState / setFragmentTexture / setFragmentSamplerState
setFragmentBytes(&params, 32, 0)          // ② 色彩/几何参数
contain-fit → setViewport / setScissorRect
drawPrimitives(Triangle, 0, 3)            // PS 里做 ②
endEncoding
m_context->endFrame()                     // presentDrawable + commit + waitUntilCompleted
```

### 3.3 为什么全同步

- D3D12 只有一个 allocator + 一个 command list：**复用前必须确认 GPU 跑完**，所以 `beginFrame()` 开头 `waitForGpu()`；
- Metal 每帧新建 command buffer，本可以不等；但 texture 上传依赖 staging 生命周期，且项目刻意保持"简单同步模型"，
  所以 `endFrame()` 里也 `waitUntilCompleted`。

以后要优化：D3D12 上 per-backbuffer fence + 双 allocator；Metal 上 `MTLSharedEvent` + 半同步。

### 3.4 resize

- D3D12：`context->resize()` → `waitForGpu` → 释放后台缓冲引用 → `ResizeBuffers` → 重建 RTV（**RTV 句柄会变**）→ 更新 frameIndex；
- Metal：`context->resize()` → `metalView.frame = host.bounds`（point）→ `layer.drawableSize = 物理像素` → 更新 contentsScale。

---

## 4. 重构后归属：两条链路各在哪一层

### 4.1 分层

```text
app/     Qt 事件、窗口、按键；只认识 IRenderer / IViewTransform
media/   FFmpeg 解码 → RGBA16；不碰 GPU
render/  契约（IRenderer/IViewTransform/RendererFactory）+ 各后端"画什么"
gfx/     各平台原生上下文：设备/交换链（layer）/命令/fence/present/resize
```

### 4.2 两条链路逐项归属

| 链路步骤 | 归属 | 说明 |
|---|---|---|
| 读 PNG、FFmpeg 解码、10-bit 还原 | `media` | 纯 CPU、与平台无关 |
| 图片路径、加载时机 | `app/main.cpp`、`ViewportWidget::ensureRenderer` | 事件驱动 |
| 纹理资源、SRV/staging、拷贝录制、上传 barrier | `render/<api>` | 这是"这张纹理自己的资源与状态" |
| 上传用的 Reset/Close/Execute/wait | `gfx`（`beginCommandList`/`submitAndWait`/`commandList`/`commandBuffer`） | 帧/命令基础设施 |
| 8 个常量、矩阵合成、contain-fit、Draw | `render/<api>` | 着色器契约 + 绘制策略 |
| PQ EOTF、色域矩阵、÷80、outputScale | `shaders/fullscreen.{hlsl,metal}` | 单 pass，平台各一份但数学相同 |
| 16F 格式 + 色彩空间声明（scRGB / DisplayP3、EDR） | `gfx`（`D3D12Context::createSwapChain`、`MetalContext::initialize`） | 平台呈现机制的一部分 |
| 每帧 barrier/Present/frameIndex | `gfx`（`beginFrame`/`endFrame`） | 只有 context 知道后台缓冲状态 |
| 设备/队列/交换链/命令/fence/resize | `gfx` | 进程级/帧级生命周期 |

### 4.3 判据（以后加后端/加功能直接用）

- 换平台必须重写？→ **gfx**
- 换"画什么/怎么画"才改？→ **renderer**
- 只是"数据格式/文件解析"？→ **media**
- 和窗口/交换链/呈现/同步绑定？→ **gfx**
- 和着色器/管线/描述符/资源内容绑定？→ **renderer**
- 第二个后端（如 SkiaRenderer）也要用且与画面无关？→ **gfx**

一句话：**"换平台要重写的 → gfx；换画面要改的 → renderer；数据怎么来的 → media；事件怎么来 → app"。**

### 4.4 MetalContext 实际落地（`822b976`）

| 进 `gfx/metal/MetalContext` | 留在 `render/metal/MetalRenderer` |
|---|---|
| device / queue / 子 NSView / CAMetalLayer（格式/色彩空间/EDR/drawableSize） | MSL 编译、PSO、sampler |
| `nextDrawable` + nil 判空 → `beginFrame() -> bool` | render pass / render encoder（clear color 留在 renderer） |
| command buffer 生命周期、`presentDrawable`、`commit`、`waitUntilCompleted` | 8 个常量、viewport/scissor、Draw、contain-fit |
| `resize`（frame/drawableSize/contentsScale） | 纹理与上传内容（staging/256 对齐/blit 录制） |
| `void*` 不透明句柄访问器（头文件保持纯 C++） | 交互状态、`outputScale`、图像宽高 |

与 D3D12 的 3 处刻意不同（Metal 语义决定）：

1. `beginFrame()` 返回 `bool`：`nextDrawable` 可能 nil；
2. 没有 `currentRtv()`，改成 `currentDrawable()`：`drawable.texture` 直接当颜色附件；
3. `commandBuffer()` 代替 `commandList()`，且 **pass/encoder 由 renderer 建**：
   这样 clear color 留在 renderer（与 D3D12 `kClearColor` 对偶），将来 SkiaRenderer 复用 context 也不会被绑死。

### 4.5 重构后已知问题

| 问题 | 状态 |
|---|---|
| `present()` 无图也 Draw（绑定空 SRV） | 工作区已修复：`D3D12Renderer.cpp` 恢复 `if (m_imageWidth > 0 && m_imageHeight > 0)` 守卫（未提交） |
| `createRootSignature` 注释 "8 个 DWORD = 16 字节" | 实际 32 字节；仅注释错，建议改 |
| `D3D12Context.cpp` 结尾命名空间注释写成 `chroma::render`；`<d3dcompiler.h>` 无用 include | 仅注释/冗余，建议清 |
| 全同步（单 fence、单 allocator） | 旧设计，未变；优化项 |
| macOS EDR / tone mapping | `CAEDRMetadata` 未做；高光裁剪已知 |

---

## 5. 平台差异速查

| 概念 | Windows / D3D12 | macOS / Metal |
|---|---|---|
| 原生表面 | `HWND` + `CreateSwapChainForHwnd` | 子 `NSView` + `CAMetalLayer`（不改 host.layer） |
| 颜色声明 | 16F + `SetColorSpace1(G10_NONE_P709)`（scRGB） | 16F + `ExtendedLinearDisplayP3` + `wantsEDR` |
| 后台缓冲 | 自己 `GetBuffer(i)` + RTV | `nextDrawable` + `drawable.texture` |
| 帧索引 | `GetCurrentBackBufferIndex` 维护 | 不存在 |
| 着色器 | `D3DCompileFromFile` → `ID3DBlob` | `newLibraryWithSource` → `MTLLibrary` |
| 管线 | 根签名 + PSO | 只有 PSO |
| 绑定 | 描述符堆 → 表 → 根参数 | 对象 + index 直接设 |
| 参数 | `SetGraphicsRoot32BitConstants` | `setFragmentBytes` |
| 纹理上传 | DEFAULT + UPLOAD + `CopyTextureRegion` + barrier | Private + Shared staging + blit |
| 状态切换 | 手写 barrier | 无（自动跟踪） |
| 每帧命令 | allocator/list 复用，必须等 GPU | 每帧新 command buffer |
| 同步 | fence + Win32 event | `waitUntilCompleted` |
| 呈现 | `Present(1,0)` + 更新 frameIndex | `presentDrawable` + `commit` |
| 失败点 | `HRESULT`（`ThrowIfFailed`） | `nextDrawable == nil`、`NSError*`、`cb.status == Error` |
| 色域/缩放 | Rec.709 / `outputScale=0.8` | Display-P3 / `1.0` |

**净差异**：D3D12 多维护三本账——①资源状态（barrier）②描述符（视图→堆→根签名）③帧与同步（allocator+fence）；
Metal 把这些交给"对象引用 + 驱动自动跟踪 + 每帧新 command buffer"，复杂度落在 drawable/对象生命周期。

---

## 6. 代码索引与阅读顺序

| 顺序 | 读什么 | 看什么 |
|---|---|---|
| 1 | `src/app/main.cpp` + `src/media/ImageLoader.cpp` | ① 文件 → RGBA16 |
| 2 | `src/app/ViewportWidget.cpp` | 事件驱动、`setImage` 交接 |
| 3 | `src/render/d3d12/D3D12Renderer.cpp` 或 `src/render/metal/MetalRenderer.mm` | ① 纹理上传 + 每帧绘制 |
| 4 | `shaders/fullscreen.hlsl` / `fullscreen.metal` | ② 色彩数学 + 交互矩阵 |
| 5 | `src/gfx/d3d12/D3D12Context.cpp` / `src/gfx/metal/MetalContext.mm` | 底座：设备/交换链/命令/fence/present |
| 6 | `legacy/program-42518f8/` | 重构前"两个裸 renderer"（含全部注释） |
| 7 | `_shirley/notes/pipeline_comparison.md` | D3D12 ↔ Metal 逐项对照 |
| 8 | `docs/handoff/08-current-progress.md` | 重构方案与 macOS 交接 |

---

## 7. 一页速记

> **链路 ①（图片输入）**：PNG → FFmpeg `RGBA64LE` → 10-bit 左移还原 → `Image{rgba16}` →
> `setImage` → 纹理（D3D12：DEFAULT+UPLOAD+barrier；Metal：Private+blit）→ **必须等 GPU**。
>
> **链路 ②（色彩管理）**：采样 PQ 码值 → `PqEotf` → BT.2020→XYZ → 平台矩阵（709 / P3）→ 裁负 →
> ×10000 nits → ÷80 → ×`outputScale` → 16F 线性输出；**不写 OETF**。
> 另一半在底座：16F + scRGB / ExtendedLinearP3 + EDR 的声明。
>
> **汇合**：`beginFrame` → clear →（有图才）绑管线/纹理 → 8 个常量 → contain-fit → Draw →
> `endFrame`（barrier+Present / presentDrawable+wait）。
>
> **归属**：数据格式 → `media`；画什么 → `render`；平台资源与帧 → `gfx`；事件 → `app`。
> 判据：**换平台要重写的 → gfx；换画面要改的 → renderer。**
