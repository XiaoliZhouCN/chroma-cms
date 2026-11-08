# Pipeline

``` mermaid

```

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

D3D12Renderer::createDeviceAndQueue()

- D3D12CreateDevice()
- m_device->CreateCommandQueue()
- CreateDXGIFactory2()
