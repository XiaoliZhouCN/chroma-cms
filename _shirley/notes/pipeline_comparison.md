两边代码我都读过（`D3D12Renderer.cpp` / `MetalRenderer.mm`），按"生命周期 → 相同点 → 不同点 → 逐帧对照 → 心智模型"整理。

## 一句话总览

> **Metal 是"把 D3D12 的三本台账合并成一本"的版本。**
> D3D12 要维护：①资源状态账（barrier）②描述符账（视图→堆→根签名）③帧/同步账（allocator + fence）；
> Metal 只剩：对象引用 + command buffer 生命周期，其余交给驱动自动跟踪。

## 一、生命周期并排

| 阶段 | D3D12 | Metal |
|---|---|---|
| 设备 | `D3D12CreateDevice` | `MTLCreateSystemDefaultDevice` |
| 队列 | `CreateCommandQueue(DIRECT)` | `[device newCommandQueue]` |
| 表面 | `CreateSwapChainForHwnd`（DXGI） | `CAMetalLayer` + 挂到 `NSView` 上 |
| 颜色声明 | `SetColorSpace1` + 交换链格式 | `layer.pixelFormat` + `layer.colorspace` |
| 后台缓冲 | `GetBuffer(i)` → 自建 RTV | `nextDrawable` → `drawable.texture`（直接用） |
| 着色器 | `D3DCompileFromFile` → `ID3DBlob` | `newLibraryWithSource` → `MTLLibrary` |
| 管线 | `D3D12_GRAPHICS_PIPELINE_STATE_DESC` → PSO | `MTLRenderPipelineDescriptor` → `MTLRenderPipelineState` |
| 绑定规则 | **根签名** + 描述符堆 + SRV 描述符 | **无**（按 index 直接绑对象） |
| 参数 | `SetGraphicsRoot32BitConstants` | `setFragmentBytes:length:atIndex:` |
| 纹理 | 资源 + SRV + `CopyTextureRegion` | `MTLTexture` + blit `copyFromBuffer` |
| 每帧录制 | `Reset(allocator)` → `Reset(list)` → 录制 → `Close` | `[queue commandBuffer]` → encoder → `endEncoding` |
| 提交 | `ExecuteCommandLists` | `[commandBuffer commit]` |
| 呈现 | `Present(1,0)` + `GetCurrentBackBufferIndex` | `presentDrawable` + `commit` + `waitUntilCompleted` |
| 同步 | `ID3D12Fence` + Win32 Event | `waitUntilCompleted`（或 `MTLSharedEvent`） |
| 销毁 | `ComPtr` 引用计数 | ARC 引用计数 |

## 二、相同的部分（几乎可以逐个对应）

| 概念 | D3D12 | Metal | 项目里的体现 |
|---|---|---|---|
| 设备/队列 | `ID3D12Device` / `ID3D12CommandQueue` | `MTLDevice` / `MTLCommandQueue` | 都是一次创建、全局复用 |
| 管线对象 | PSO（不可变） | `MTLRenderPipelineState`（不可变） | 都是"创建一次，每帧 `set` 一下" |
| 着色器运行时编译 | `D3DCompileFromFile` | `newLibraryWithSource` | 两边都从 `exe/shaders/` 读源码编译 |
| 全屏三角形 | `SV_VertexID` 现算 3 个顶点 | `[[vertex_id]]`，同公式 | 顶点代码几乎逐字相同 |
| 视口/裁剪 | `RSSetViewports` / `RSSetScissorRects` | `setViewport` / `setScissorRect` | contain-fit 数学一样，左上角原点 |
| 坐标系 | NDC y 向上、z∈[0,1] | 同 | 所以 UV 变换矩阵两端完全一致 |
| 越界处理 | `discard` | `discard_fragment()` | 旋转/缩小后图片外的像素丢给 clear color |
| 参数传递 | 根常量（内联在命令流） | `setFragmentBytes`（内联） | 一次 8 个 float |
| 纹理上传 | staging buffer → `CopyTextureRegion` → 等 GPU | staging buffer → blit `copyFromBuffer` → 等 GPU | 都是"CPU 缓冲 → 私有纹理 → 同步" |
| 行对齐 | `RowPitch` 256 对齐 | `sourceBytesPerRow` 256 对齐 | 2048 宽天然满足 |
| 全同步模型 | `waitForGpu()`（fence） | `waitUntilCompleted` | 都是"提交完就阻塞到 GPU 做完" |
| 交互 | 根常量矩阵 | `setFragmentBytes` 矩阵 | 矩阵构建代码两端相同 |

## 三、不同的部分（重点）

### 3.1 绑定模型：6 层 vs 1 层

```
D3D12：资源 → 视图(SRV) → 描述符 → 描述符堆 → 根签名声明 → 命令(SetGraphicsRoot*)
Metal： 资源对象 ────────────────────────────────→ 命令(setFragmentTexture:atIndex:)
```

| | D3D12 | Metal |
|---|---|---|
| 你写多少 | 一堆堆/视图/表/签名代码 | 一行 `setFragmentTexture` |
| 灵活性 | 高（同资源多视图、TYPELESS 重解释、bindless） | 低（一个对象一种解释） |
| 出错点 | 参数编号/可见性/堆没绑 | 基本只剩 index 写错 |
| 性能模型 | 根参数内联 + 表指针，可精确控制 | 驱动决定 |

**这是两边代码量差异的最大来源**：`createRootSignature()` + `createDescriptorHeaps()` + `createSrvHeap()` + 每帧三次 `SetGraphicsRoot*`，在 Metal 里被压缩成"建 PSO 时什么都不用声明 + 每帧三行 set"。

### 3.2 同步：显式 barrier vs 自动跟踪

| 场景 | D3D12 | Metal |
|---|---|---|
| 后台缓冲 呈现→渲染 | 手写 `TRANSITION` barrier | 无（render pass 自动） |
| 纹理 拷贝→采样 | 手写 `COPY_DEST→PIXEL_SHADER_RESOURCE` | 无（依赖自动跟踪） |
| RT→SRV 读 | 手写 barrier | 无 |
| CPU 等 GPU | fence + event | `waitUntilCompleted` / `MTLSharedEvent` |
| 资源被复用 | 状态机要自己维护 | 驱动按依赖跟踪（tracked 模式） |

> 结果：D3D12 每一步都可能因为"忘了 barrier / StateBefore 写错"而黑屏或报错；Metal 里**没有 barrier 这个概念**，但代价是你失去了"精确调度"的手段（要优化得切 untracked + 手动 fence）。

### 3.3 交换链：DXGI vs CAMetalLayer

| | D3D12 | Metal |
|---|---|---|
| 表面对象 | `IDXGISwapChain3` | `CAMetalLayer` |
| 后台缓冲 | 2 张 `ID3D12Resource`，自己建 RTV | drawable 池，`nextDrawable` 取，`drawable.texture` 直接用 |
| 帧索引 | `GetCurrentBackBufferIndex()` | 无（每次 nextDrawable） |
| 尺寸 | `ResizeBuffers` + 重建 RTV | 改 `layer.drawableSize` + `metalView.frame` |
| vsync | `Present(1, 0)` | `layer.displaySyncEnabled = YES` |
| 色彩空间 | `SetColorSpace1(G10…)` | `layer.colorspace = …`（还要 `wantsExtendedDynamicRangeContent`） |
| 呈现 | `Present` | `presentDrawable` 然后 `commit` |
| 失败/耗尽 | `Present` 返回错误码 | `nextDrawable` 返回 **nil**（必须判空） |

**结构差异**：D3D12 的交换链是"你的资源"，要自己建视图；Metal 的 drawable 是"系统借给你的纹理"，用完还回去。

### 3.4 命令生命周期

| | D3D12 | Metal |
|---|---|---|
| 命令内存 | `CommandAllocator` 显式管理，**GPU 用完才能 Reset** | command buffer 由队列新建，底层自动管理 |
| 录制单元 | `ID3D12GraphicsCommandList` 可复用，`Reset` 后重录 | encoder 一次性，`endEncoding` 即结束 |
| 复用安全 | 必须 `waitForGpu()` 后才能 Reset allocator | 每帧新 buffer，天然安全 |
| 并行录制 | 多 list 多 allocator | 多 command buffer |

所以 Windows 的 `present()` 开头那行 `waitForGpu()` 是**为了 allocator 能安全复用**；Metal 对应代码里没有等，因为它每帧新建 command buffer，不需要等——两边的"等待"动机不同（Metal 里 `waitUntilCompleted` 是为了**保持全同步模型**和**上传，必须等**）。

### 3.5 视图与格式

| | D3D12 | Metal |
|---|---|---|
| 读的视图 | SRV（带格式/mip/分量映射） | 纹理本身（`usage` 声明可读） |
| 写的视图 | RTV / DSV | render pass 的 attachment |
| 重解释 | TYPELESS + 不同视图 | `newTextureViewWithPixelFormat:`（用途不同） |

Metal 没有"同一资源同时是 RTV 和 SRV"这种状态切换——**靠 render pass 的 attachment 语义**。

### 3.6 窗口集成

| | D3D12 | Metal（本项目做法） |
|---|---|---|
| 宿主 | `HWND`（Qt `winId()`） | `NSView*`（Qt `winId()`） |
| 绑定方式 | `CreateSwapChainForHwnd(hwnd)` | 新建子 `NSView` → `wantsLayer` → 挂 `CAMetalLayer` → `addSubview` |
| 尺寸单位 | 物理像素 | 逻辑 point（`frame`）+ 物理像素（`drawableSize`） |
| Retina/DPR | `devicePixelRatio` 自己乘 | `layer.contentsScale = backingScaleFactor` |

### 3.7 错误模型

| | D3D12 | Metal |
|---|---|---|
| 返回 | `HRESULT` + `ThrowIfFailed` | 返回 nil + `NSError**` |
| 调试层 | `D3D12GetDebugInterface` + `EnableDebugLayer` | 环境变量 `MTL_DEBUG_LAYER=1` |
| 命令失败 | 设备移除原因 | `commandBuffer.status == Error` + `error` |

### 3.8 着色器工具链

| | D3D12 | Metal |
|---|---|---|
| 源码 | HLSL | MSL |
| 编译 | `D3DCompileFromFile(entry, "vs_5_1")` | `newLibraryWithSource` → `newFunctionWithName` |
| 产物 | `ID3DBlob` | `MTLLibrary`（注释里你已经类比成 ID3DBlob，很准） |
| 数学一致性 | `D3DCOMPILE_ENABLE_STRICTNESS` | **`mathMode = MTLMathModeSafe` / `fastMathEnabled = NO`**（否则和 HLSL 有细微差异） |
| 矩阵 | 行主序书写 | **列主序初始化**（移植时最常见的坑） |

### 3.9 资源内存

| | D3D12 | Metal |
|---|---|---|
| GPU 专用 | `D3D12_HEAP_TYPE_DEFAULT` | `MTLStorageModePrivate` |
| CPU 可写 | `UPLOAD` 堆 | `MTLResourceStorageModeShared` |
| 分配粒度 | committed / placed / reserved | `newTexture` / `newBuffer` / heap（可选） |
| 行对齐 | 256 | 256 |

## 四、逐帧流程并排（把两边的 `present()` 对齐）

| 步 | D3D12 `present()` | Metal `present()` |
|---|---|---|
| 1 | `waitForGpu()`（allocator 复用） | —（新 command buffer） |
| 2 | `Reset(allocator)` + `Reset(list)` | `[queue commandBuffer]` |
| 3 | barrier：PRESENT→RENDER_TARGET | —（render pass） |
| 4 | `ClearRenderTargetView` | `pass.colorAttachments[0].clearColor` |
| 5 | `OMSetRenderTargets` | `pass.colorAttachments[0].texture = drawable.texture` |
| 6 | `SetGraphicsRootSignature` + `SetPipelineState` | `setRenderPipelineState` |
| 7 | `SetDescriptorHeaps` + `SetGraphicsRootDescriptorTable` | `setFragmentTexture` / `setFragmentSamplerState` |
| 8 | `SetGraphicsRoot32BitConstants(1, 8, …)` | `setFragmentBytes(&params, sizeof, 0)` |
| 9 | `RSSetViewports` / `RSSetScissorRects` | `setViewport` / `setScissorRect` |
| 10 | `IASetPrimitiveTopology` + `DrawInstanced(3,1,0,0)` | `drawPrimitives(Triangle, 0, 3)` |
| 11 | barrier：RENDER_TARGET→PRESENT | — |
| 12 | `Close()` + `ExecuteCommandLists` | `endEncoding` + `commit` |
| 13 | `Present(1,0)` + `GetCurrentBackBufferIndex` | `presentDrawable` + `waitUntilCompleted` |

**净差异**：D3D12 多了第 1、3、5、6(根签名)、7(堆)、11、13(索引) —— 全是"台账"；Metal 多了第 2 的 drawable 获取和判空。

## 五、心智模型（记这四句）

1. **D3D12 管"状态"**：资源状态要手写 barrier，绑定时要经过"视图→堆→根签名"三跳。
2. **Metal 管"对象"**：资源就是对象，绑定时直接给对象 + 索引；依赖由驱动跟踪。
3. **D3D12 的帧资源要复用**（allocator/list），所以必须 fence 等；**Metal 每帧新 command buffer**，不需要等（等是为了同步语义）。
4. **PSO 的概念两边一致**（不可变、创建一次、每帧 set），差别只在"创建时要不要顺带声明根签名"。

## 六、移植时最容易踩的 5 个坑（都来自你项目的真实代码）

| 坑 | 说明 |
|---|---|
| `nextDrawable` 返回 nil | D3D12 的 `GetBuffer` 不会失败，Metal 会；必须判空跳过该帧 |
| 以为要建 RTV | Metal 不需要，`drawable.texture` 直接当颜色附件 |
| 找 barrier 找不到 | Metal 没有；但要注意 untracked 资源才需要手动 fence |
| `setFragmentBytes` 布局 | 必须和 MSL 的 `struct ShaderParams` 内存布局逐字段一致（16 字节对齐规则） |
| MSL 矩阵列主序 | HLSL 行矩阵到 MSL 要按列写，否则旋转/色域矩阵方向反 |

---

要不要我把这份整理落成 `docs/handoff/08-d3d12-vs-metal.md`（给 mac 侧 agent 当参考），并顺手把"根常量 ↔ setFragmentBytes"的对照补进 07？