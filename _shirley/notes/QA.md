# QA

### Q：为什么选择离屏
# 一、结论先行

**离屏不是 Skia 的硬性要求，而是我们架构上的主动选择**：让 Skia 只往一张"格式固定、平台无关"的 RGBA16F 纹理里画，再由平台后端（D3D12/Metal）把这纹理拷到交换链后台缓冲并 present。

直接上屏（把 Skia 画布直接 wrap 到 swapchain backbuffer）**技术上完全可行**，能省掉这次拷贝；但代价是 Skia 层要直接感知平台/交换链细节，破坏当前"三后端同构 + 平台差异收口在 gfx"的分层。

# 二、代码里的证据

**离屏的创建**（[SkiaGpuBackendD3D12.cpp](src/render/skia/SkiaGpuBackendD3D12.cpp) `createSurface`）：

```cpp
constexpr DXGI_FORMAT kOffscreenFormat = DXGI_FORMAT_R16G16B16A16_FLOAT;
// DEFAULT heap + ALLOW_RENDER_TARGET 的专用纹理
CreateCommittedResource(..., D3D12_RESOURCE_STATE_RENDER_TARGET, ...);
// 包成 GrBackendRenderTarget，再 wrap 成 SkSurface（kRGBA_F16）
SkSurfaces::WrapBackendRenderTarget(gr, rt, ..., kRGBA_F16_SkColorType, ...);
```

**上屏的拷贝**（同文件 `copySurfaceToScreen`）：

```cpp
// ① offscreen: RENDER_TARGET -> COPY_SOURCE
// ② backbuffer: RENDER_TARGET -> COPY_DEST
CopyResource(back, offscreen);
// ③ 两个资源再切回 RENDER_TARGET
```

然后 `context->endFrame()` 负责 present。也就是说每帧固定多出：**4 个 barrier + 1 次全屏 CopyResource**。

# 三、为什么选离屏而不是直接上屏

## 1. 三后端"同构"的中间格式

手写 D3D12 / 手写 Metal / Skia 都把结果写进**同一种中间纹理**（线性 scRGB，RGBA16F）。
这样：

- 三后端可以做像素级/ΔE 对比（e2e 的核心前提）；
- 后续 readback、截图、叠图/差值（P1）都基于同一张中间纹理；
- 颜色计算与"怎么上屏"解耦。

如果直接画进 swapchain，中间结果就绑定在"每平台每帧的后台缓冲"上，e2e 想读回还得额外拷一次——等于把拷贝挪个位置，并没有省。

## 2. 平台色彩/显示差异收口在 gfx 层

- Windows：swapchain 是 `R16G16B16A16_FLOAT` + **scRGB colorspace**，靠 DXGI colorspace 告诉 DWM；
- macOS：`CAMetalLayer` 的 `colorspace` / EDR、Display-P3 输出。

这些"显示目标"决策目前都在 `gfx`/平台 backend 里。Skia 只负责产生"线性 scRGB 图像"。
如果让 Skia 直接 wrap backbuffer，Skia 层就必须知道：

- 当前平台的 swapchain 格式/颜色空间；
- 每帧 backbuffer 的旋转（FLIP model 有 2–3 张后台缓冲）；
- 资源状态屏障（Skia 与 gfx 谁负责 `PRESENT↔RENDER_TARGET`）。

这会把平台差异从 gfx 泄漏到 render/skia。

## 3. 交换链生命周期归 gfx 所有

[src/gfx/d3d12/AGENTS.md](src/gfx/d3d12/AGENTS.md) 的约定是：**交换链状态只能由 `D3D12Context` 的 `beginFrame/endFrame` 修改**。
当前分工是：

```
gfx::D3D12Context: 拥有 swapchain/backbuffer/状态/同步/present
SkiaGpuBackend:    拥有 offscreen 纹理 + Skia 画布
```

如果直接上屏，就要反过来：Skia 拿到当前 backbuffer 并 wrap 成 render target，gfx 只负责 present。两边都要改状态机，耦合和出错面都变大（尤其 resize、丢帧、HDR 开关切换）。

## 4. 实现复杂度：直接 wrap 的隐藏成本

Skia 确实支持直接渲染到交换链（`SkSurfaces::WrapBackendRenderTarget` / `WrapBackendTexture`），但需要：

- 每帧/每个 backbuffer 创建 `GrBackendRenderTarget`（或缓存 2–3 个）；
- swapchain resize/reset 时同步重建这些 RT；
- 与 gfx 协调"Ganesh flush 后、Present 前"的屏障和同步；
- 保证 Skia 的 color type（`kRGBA_F16`）与 swapchain 格式、colorspace 一致；
- D3D12 与 Metal 各写一套 wrap 逻辑（当前 `SkiaGpuBackend` 抽象就是为了避免这种分叉）。

离屏方案下，这些复杂度被压缩成 `createSurface`（resize 时重建一次）+ 一次 `CopyResource`。

## 5. 风险隔离与可测试性

- Skia 初始化/绘制失败不会破坏 swapchain 状态（最坏只是 offscreen 黑屏）；
- e2e 可以**无窗口离屏渲染**：只要有 `createSurface` + Ganesh context，不需要真实 HWND/drawable；
- 后续 supersampling、离屏截图、叠图差值都在同一套机制上扩展。

---

# 四、性能损耗（量化）

## 1. 额外显存（常驻）

RGBA16F 每像素 8 B：

| 分辨率 | offscreen 大小 | 说明 |
|---|---|---|
| 1920×1080 | **16.6 MB** | 常驻，直到 resize/销毁 |
| 2560×1440 | **29.5 MB** | |
| 3840×2160 | **66.4 MB** | 4K HDR 场景 |

（交换链本身的 2–3 张 RGBA16F backbuffer 是另一笔，两种方案都要。）

## 2. 额外显存带宽（每帧、主要成本）

`CopyResource` = 读 offscreen + 写 backbuffer = `2 × W × H × 8 B`：

| 分辨率 | 每帧拷贝流量 | 60 fps | 120 fps |
|---|---|---|---|
| 1080p | 33.2 MB | **≈ 2.0 GB/s** | ≈ 4.0 GB/s |
| 1440p | 59.0 MB | **≈ 3.5 GB/s** | ≈ 7.1 GB/s |
| 4K | 132.7 MB | **≈ 8.0 GB/s** | ≈ 15.9 GB/s |

对比：渲染本身写 offscreen 也要 `W×H×8`/帧（两种方案都有）；拷贝是**纯额外**开销。
在独显上 1080p/60 通常 <1–3% 帧时间；4K/120 或核显上会明显（带宽与 L2 争用）。

## 3. 额外 GPU pass / 延迟

- 每帧多 1 个 `CopyResource` + 4 个 barrier；
- 拷贝在 Skia flush 之后、present 之前，**串行**插入（同一队列 FIFO）；
- 典型耗时：几百 µs 量级（取决于分辨率与 GPU），在 60fps 的 16.7ms 预算里通常可接受，但它是"每帧固定税"；
- Present 必须等拷贝完成，不能省。

## 4. 显存/cache 压力

- offscreen 是 16F 大纹理，会挤占纹理缓存/L2；
- resize 时旧/新 offscreen 可能短暂并存（代码里先 `waitForGpu()` 再 `Reset()`，峰值可控但会有一次等待）；
- 拷贝与渲染交替读写同一显存区域，可能带来额外的 cache 失效。

## 5. CPU 侧

**基本没有额外 CPU 开销**：拷贝是 GPU 命令，CPU 只是录制 `CopyResource` + barrier（纳秒级）。
额外内存拷贝只发生在 GPU 上（不是 CPU `memcpy`）。

---

# 五、和手写 D3D12 后端的对比

| | 手写 D3D12 | Skia |
|---|---|---|
| 渲染目标 | **直接是 swapchain backbuffer** | offscreen RGBA16F |
| 上屏 | 直接 Present | CopyResource → backbuffer → Present |
| 额外拷贝 | 无 | 每帧 1 次全屏拷贝 |
| 平台耦合 | 后端自己处理色彩空间/present | 收口在 SkiaGpuBackend |
| e2e/readback | 需要额外 copy | offscreen 天然可读回 |

所以双视图里左右两边的"present 路径"本来就不完全对称：**左零拷贝，右有一次拷贝**。做 e2e 比较时比的是"同一输入、同一色彩数学下的像素结果"，不是比 present 路径。

---

# 六、什么时候值得改回"直接上屏"

建议到 **P4 性能阶段**再决策，触发条件例如：

- 4K/HDR/120Hz 下 CopyResource 占帧时间 > 5%；
- 或 frame time 分析显示 present 前的串行拷贝是瓶颈；
- 或核显/移动端带宽吃紧。

届时的两条路：

1. **直接 wrap 当前 backbuffer** 给 Skia（省拷贝，但引入平台耦合与状态管理）；
2. **保留离屏，但把结果作为"可复用中间纹理"**，把拷贝移到更合适的时机/队列（如独立 copy queue + fence，仍有同步成本）。

当前 P0/P1 阶段：色彩正确性和三后端可对比性优先于这点带宽，所以**离屏 + 拷贝是合理的默认选择**；等 P4 有真实 profiling 数据再优化，避免过早优化破坏分层。

一句话总结：**离屏买的是"三后端同构 + 平台差异收口 + 可测试性"，代价是每帧一次约 2–8 ––GB/s 的全屏拷贝（按分辨率/刷新率）和一块 16–66 MB 的常驻显存。**