# Pipeline In Mini Insight

## 1. 2个与渲染相关的 Entrance

### 1.1 初始化Renderer 和 Image 信息

> Entrance： `ViewportWidget::showEvent()`

```mermaid
---
id: 1c21b5b2-4c64-4180-adc3-96067f671414
---
flowchart TD
showEvent_entrance["`showEvent()`"]
showEvent_exit["`showEvent()`"]
showEvent_entrance --- showEvent_exit

ensureRenderer["`ensureRenderer()`"]
ensureRenderer_note["`**note:** 初始化 renderer，创建并懒拷贝纹理`"]
ensureRenderer --- ensureRenderer_note

update["`update()`"]
update_note["`**note:** 标记更新，下一次给ViewportWidget执行updateEvent？`"]
update --- update_note

showEvent_entrance --> ensureRenderer --> update ---> showEvent_exit
```

### 1.2 绘制

```mermaid
flowchart TD
painEvent_entrance["`painEvent()`"]
painEvent_exit["`painEvent()`"]
painEvent_entrance --- painEvent_exit

renderer_present["`renderer->present()`"]
renderer_present_note["`**note:** `"]
renderer_present --- renderer_present_note

painEvent_entrance -->renderer_present ---> painEvent_exit
```

## `ensureRenderer` 的初始化操作
`src\app\ViewportWidget.cpp, ViewportWidget::ensureRenderer()`

```mermaid
flowchart TD
ensureRenderer_entrance["`ensureRenderer()`"]
ensureRenderer_exit["`ensureRenderer()`"]
ensureRenderer_entrance --- ensureRenderer_exit

renderer_initialize["`renderer->initialize()`"]
renderer_initialize_note["`**note:** 1. rednerer 初始化；2. 创建ganish context；3. 编译shader`"]
renderer_initialize --- renderer_initialize_note

renderer_setImage["`renderer->setImage()`"]
renderer_setImage_note["`**note:** 1. 创建对应格式纹理；2. CPU 图像懒上传；`"]
renderer_setImage --- renderer_setImage_note

ensureRenderer_entrance --> renderer_initialize --> renderer_setImage ---> ensureRenderer_exit
```

### `renderer->initialize()`

```mermaid
flowchart TD
renderer_initialize_entrance["`renderer->initialize()`"]
renderer_initialize_exit["`renderer->initialize()`"]
renderer_initialize_entrance --- renderer_initialize_exit

backend_initialize["`backend->initialize()`"]
backend_initialize_note["`**note:** 1. 创建对应gpu backend；2. 初始化backend；`"]
backend_initialize --- backend_initialize_note

backend_makeGaneshContext["`backend->initialize()`"]
backend_makeGaneshContext_note["`**note:** 创建ganish contex；`"]
backend_makeGaneshContext --- backend_makeGaneshContext_note

complie_shader["`// complie shader`"]
complie_shader_note["`**note:** 读取sksl文件并编译；`"]
complie_shader --- complie_shader_note

renderer_initialize_entrance --> 
    backend_initialize --> 
    backend_makeGaneshContext --> 
    complie_shader --> 
renderer_initialize_exit
```

### `renderer->setImage()`

```mermaid
flowchart TD
renderer_setImage_entrance["`renderer->initialize()`"]
renderer_setImage_exit["`renderer->initialize()`"]
renderer_setImage_entrance --- renderer_setImage_exit

make_image_info["`info = SkImageInfo::Make()`"]
make_image_info_note["`**note:** 创建需要格式的纹理，width, hight, kR16G16B16A16_unorm_SkColorType, kOpaque_SkAlphaType`"]
make_image_info --- make_image_info_note

raster_from_pixmap_copy["`拷贝纹理，懒上传`"]
raster_from_pixmap_copy_note["`**note:** 1. pixmap()像素映射到纹理；\n 2. 拷贝纹理，获取CPU raster 图像，懒上传，即绘制时才真正发生上传`"]
raster_from_pixmap_copy --- raster_from_pixmap_copy_note

state_update_to_impl["`状态保存至impl`"]
state_update_to_impl_note["`**note:** impl是保存后端、图像状态的结构体，用于解耦renderer和context`"]
state_update_to_impl --- state_update_to_impl_note

renderer_setImage_entrance --> make_image_info --> raster_from_pixmap_copy --> state_update_to_impl --> renderer_setImage_exit
```

**notes**
- ① 为什么用 RasterFromPixmapCopy（拷贝）而不是 RasterFromPixmap（不拷贝）
    不拷贝版本要求 rgba16 内存在 SkImage 生命周期内一直有效——耦合 app 的 m_image；
    拷贝版本立刻复制 32 MB，SkImage 自持内存，m_image 之后释放也不影响绘制；
    代价是一次 CPU 拷贝（约 32 MB，一次性，可接受）.
- ② GPU 纹理什么时候创建/上传？ 
    RasterFromPixmapCopy 得到的是 CPU raster 图像，不是 GPU 纹理。第一次 drawFrame() 里：
    ``` cpp
    sk_sp<SkShader> imageShader = m_impl->image->makeShader(...);   // 引用 raster 图像
    ... image.eval(...) ...                                        // shader 采样
    m_impl->gr->flushAndSubmit();                                  // Ganesh 在绘制时把 raster 图像上传成 GPU 纹理
    ```
    Ganesh 会在首次使用时把 CPU 图像上传成 GPU 纹理并缓存（GrDirectContext 内部按图像唯一性缓存），后续帧直接复用。所以是懒上传（lazy upload）。
    - 懒上传
        懒上传（lazy upload / deferred instantiation）：创建一个 SkImage 时，Skia 只拿到"CPU 像素 + 格式描述"，还不存在 GPU 纹理。直到某次 GPU 绘制真的需要采样这张图、并且这帧命令被提交（flush）时，Ganesh 才：

        - 创建 GPU 纹理；
        - 把 CPU 像素上传进去；
        - 把纹理放进 GrDirectContext 的资源缓存。
        如果这张图从来没被画出来（窗口没显示、被裁剪、只创建没使用），就一次都不会上传。

        > 所以"懒"指的是：上传时机推迟到"真正要用"的那一刻，而不是"创建图像对象"或"调用 setImage"的那一刻。
    - 缓存复用
        静态图使用同一个 SkImage 对象，首次上传一次，之后复用；
        视频每帧更新同一张 GPU 纹理，复用同一个纹理句柄，每帧只更新内容（仍是 CPU→GPU 带宽，但不重新分配/注册缓存）
        > 视频每帧内容都变，必须有"每帧的数据搬运"。但可以避免"每帧新建对象 + 重新分配纹理"的额外开销。
    - 视频链路的三种可选做法（P2 设计）
        | 方案 | 做法 | 优点 | 缺点 |
        | --- | --- | --- | --- |
        | A. 每帧新 SkImage| 解码 → RasterFromPixmapCopy → draw | 实现最简单 | 每帧新纹理+上传+缓存 churn，最慢 |
        | B. 持久纹理更新 |	创建一次 GPU 纹理；每帧 updateBackendTexture 或 writePixels 更新 |	无分配、缓存稳定 |	需要管理纹理句柄/同步 |
        | C. GPU 上直接画 |	解码帧包成 GPU 纹理（或 YUV pixmaps），画到持久 SkSurface |	最低带宽（YUV 不用转 RGBA） |	需要 YUV/色彩空间处理（正好是 P2/P3 的学习内容） |

## `paintEvent()` 的渲染操作

`src\app\ViewportWidget.cpp, ViewportWidget::paintEvent()`
> 重写qt的`paintEvent()`，直接调用`render->present()`。

### `render->present()`
```mermaid
flowchart TD
render_present_entrance["`render->present()`"]
render_present_exit["`render->present()`"]
render_present_entrance --- render_present_exit

backend_beginFrame["`backend->beginFrame()`"]
backend_beginFrame_note["`**note:** 开始一帧，win取backbuffer/RTV，mac取nextdrawble`"]

drawFrame["`drawFrame()`"]
drawFrame_note["`**note:** 录制绘制命令。1.获取离屏canvas； 2. 准备shader 参数并创建shader； 3. 绘制内容clip到允许绘制范围（contain-fit）； 4. 绘制；`"]

grContext_flushAndSubmit["`gr->flushAndSubmit()`"]
grContext_flushAndSubmit_note["`**note:** 上传命令`"]

backend_endFramee["`backend->endFrame()`"]
backend_endFramee_note["`**note:** 结束本帧。 1. 拷贝离屏纹理； 2. 交换链present，更新下一张backbuffer。nextdrawable`"]

render_present_entrance --> 
    backend_beginFrame --> 
    drawFrame --> 
    grContext_flushAndSubmit --> 
    backend_endFramee-->  
render_present_exit

backend_beginFrame --- backend_beginFrame_note
drawFrame --- drawFrame_note
grContext_flushAndSubmit --- grContext_flushAndSubmit_note
backend_endFramee --- backend_endFramee_note
```

**notes**
- flushAndSubmit() 确实不"执行"命令，但它也不只是"记录命令列表"：Ganesh 在这一次调用里完成了"资源实例化 + CPU 侧上传准备 + 命令录制 + 提交"，GPU 执行是 submit 之后异步发生的。
| 阶段 | 发生时间 | CPU 做什么 | GPU 做什么 |
|---|---|---|---|
| ① Canvas 录制 | `drawRect` 等调用时 | 记录到 `SkRecord`（还不存在 GPU 命令） | 无 |
| ② **flush** | `flushAndSubmit()` 前半段 | 把 canvas ops 转成 GrOps；准备（onPrepare）；**首次用到图片时：实例化 lazy proxy → 创建 GPU 纹理对象 → 分配 upload/staging buffer → `memcpy` 像素 → 录制"buffer→texture"拷贝命令** | 无（还没提交） |
| ③ **submit** | `flushAndSubmit()` 后半段 | 把后端命令缓冲区提交：D3D12 `ExecuteCommandLists` / Metal `commit` | **真正执行**：GPU 执行 copy/upload、draw |
| ④ 异步完成 | submit 返回之后 | 可以继续下一帧 | GPU 在后台跑；默认**不等待** |
所以准确说法是：

纹理上传的"触发与 CPU 侧工作"发生在 flush；GPU 侧的 copy 执行发生在 submit 之后、异步完成。

#### 