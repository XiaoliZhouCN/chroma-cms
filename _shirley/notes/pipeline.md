# Pipeline

``` mermaid

```

## review 思路
- 渲染在做什么
  数据(2D/3D) --> 显示器 image
- 渲染器要解决什么问题
  CPU 向 GPU 发出绘制命令 DrawCall，这里可以拆解成两个部分：Draw 和 Call，即绘制和命令
  1. 绘制-即数据传输
    1.1 传输手段
    CPU --buffer--> GPU --buffer--> CPU
    - 数据放在哪，谁(CPU、GPU)怎么读，
    1.2 传输效率
    1.3 同步/异步，
  2. 执行命令传输

## classes and functions

### ViewportWidget

ViewportWidget::showEvent()

- ensureRenderer()
  - m_renderer: renderer->initialize()
    - create device and queue, swapchain, description heaps, render targtes, root signature, pipeline state;
    - create command allocator and list;
    - create fence, set up fence event;
    - create srv heap
  - m_renderer->setImage()

- update(): 提交绘制请求，多次提交会合并

ViewportWidget::resizeEvent()

- m_renderer-reseize()
- update()

ViewportWidget::paintEvent()

- m_renderer->present()

### D3D12Renderer

D3D12Renderer::initialize()

- createDeviceAndQueue()
- createSwapChain(nativeWindow)
- createDescriptorHeaps()
- createRenderTargets()
- createRootSignature()
- createPipelineState()
- create command allocator and list
  - m_device->CreateCommandAllocator()
  - m_device->CreateCommandList()
  - m_commandList->Close()
- create fence, set up fence event
  - m_device->CreateFence()
  - m_fenceEvent = CreateEventW()
- create srv heap
  - createSrvHeap()
    > Q: SRV 是什么？
    > A: SRV(Shader Resource View， 着色器资源视图): 1个描述符，告诉硬件“着色器怎么读这块资源”

D3D12Renderer::setImage()
- createTextureFromImage()
  - create target texture
    - create D3D12 Resource(资源): 描述数据长什么样
    - create D3D12 Heap Properties(堆): 描述内存在哪，多大
  - 计算上传布局(RowPitch)
    > Q: 为什么要算上传布局？
    > A: 计算上传布局是为了确保在将纹理数据从 CPU 上传到 GPU 时，数据的内存布局符合 GPU 的要求。GPU 对纹理数据的存储方式有特定的对齐和排列要求，计算上传布局可以确保数据在 GPU 上正确显示，避免出现渲染错误或性能问题。通过获取上传布局信息，可以正确地设置上传缓冲区的大小和行宽，从而实现高效的数据传输和渲染。
  - uploadBuffer
    -  m_device->CreateCommittedResource()
    > 上传堆缓冲,  CPU 可写，GPU 可读，行主序
    > 解决的是CPU、GPU数据传输问题
  - 逐行拷贝
    > Q: 零拷贝技术
    > A：A: 在 D3D12 中，零拷贝技术通常是通过使用共享资源或直接映射 GPU 内存来实现的。然而，在这个例子中，我们使用了上传缓冲区（Upload Heap）来将纹理数据从 CPU 上传到 GPU，这涉及到一次内存拷贝操作。虽然这不是严格意义上的零拷贝，但它是 D3D12 中常用的纹理上传方法。要实现真正的零拷贝，需要使用更高级的技术，如使用共享资源或直接访问 GPU 内存，这可能需要特定的硬件支持和更复杂的资源管理。
  - 录制拷贝命令
    - 两个 D3D12_TEXTURE_COPY_LOCATION 分别描述源和目标纹理的位置和布局
    - m_commandList->CopyTextureRegion()
    - m_commandList->ResourceBarrier()
      > Q: RESOURCE_BARRIER 是什么？作用是什么？
      > A: 是告诉 GPU"这个资源的用途/状态马上要变了"的命令。它是 D3D12 显式同步机制的核心。改资源用法（写→读、渲染→显示）之前，必须插一个 barrier 声明"从状态 A 转到状态 B"，GPU 才知道要做哪些缓存维护工作。
    - waitForGpu()
      > 等拷贝完成，uploadBuffer 才能安全释放
    -  m_device->CreateShaderResourceView()
      > 根据图片信息，建 SRV


D3D12Renderer::createDeviceAndQueue()

- D3D12CreateDevice()
  > Q: device 的作用是什么？
- m_device->CreateCommandQueue()
- CreateDXGIFactory2()
  > Q: factory 的作用是什么？
  > Q: D3D12 和 DXGI 分别是什么？二者什么关系？
  > Q: device 和 factory 的区别是什么？二者什么关系？


D3D12Renderer::waitForGpu()