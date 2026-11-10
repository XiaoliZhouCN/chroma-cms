# legacy · 两代渲染器存档（只做记录，不参与编译）

这里保存的是 ChromaCMS 渲染层的**两代实现**，用来对照"把 gfx context 抽出去"前后长什么样：

| 代号 | 是什么 | 来源 |
|---|---|---|
| `api_only/` | **第 1 代**：一个 `Renderer` 类里直接调完 D3D12 / Metal 的所有 API（设备、交换链、命令、fence、纹理、present 全在一个类里） | `practice` @ **`42518f8`**（`[win][image] sync interaction`） |
| `api_context/` | **第 2 代**：拆成 `gfx/*Context`（原生资源 + 帧生命周期 + 同步）+ `renderer/*Renderer`（根签名/PSO/纹理/常量/Draw） | 当前工作区（HEAD `822b976` + 一处未提交的小修复） |

两代文件都与来源**逐字节一致**（含当时的全部注释、`// Q: ... // A: ...` 学习笔记、被注释掉的旧成员）。
**不参与编译**，只作为阅读/对照记录。

---

## 1. 目录与文件

```text
legacy/
  README.md
  api_only/                                  ← 第 1 代：未拆 context
    d3d12/
      D3D12Renderer.hpp     86 行   3,322 B
      D3D12Renderer.cpp    777 行  38,372 B
    metal/
      MetalRenderer.hpp     38 行   1,272 B
      MetalRenderer.mm     496 行  20,831 B
  api_context/                               ← 第 2 代：已拆 context
    gfx/
      d3d12/
        D3D12Context.hpp    76 行   2,455 B
        D3D12Context.cpp   300 行  14,065 B
      metal/
        MetalContext.hpp    52 行   1,772 B
        MetalContext.mm    290 行  10,978 B
    renderer/
      d3d12/
        D3D12Renderer.hpp   88 行   3,452 B
        D3D12Renderer.cpp  521 行  24,190 B
      metal/
        MetalRenderer.hpp   38 行   1,272 B
        MetalRenderer.mm   422 行  17,600 B
```

合计：`api_only` 4 个文件 **1,397 行**；`api_context` 8 个文件 **1,787 行**
（拆分后总行数反而多了——多出来的部分是 context 的访问器、职责边界和注释；收益是结构，不是行数）。

> `api_context/` 里保存的是当时的**工作区**状态：
> 其中 `renderer/d3d12/D3D12Renderer.cpp` 含一个**未提交**的小修复（"无图不 Draw"守卫，见 §4），
> 所以它和 HEAD `822b976` 的同名文件不完全一样。`api_only/` 的 4 个文件则严格等于 `42518f8` 的 blob。

---

## 2. 两代对照：同一个程序，拆前 vs 拆后

| 维度 | `api_only`（第 1 代） | `api_context`（第 2 代） |
|---|---|---|
| 形态 | 每个平台一个类、一个 `.cpp` | 每个平台 4 个文件、2 层（`gfx` + `renderer`） |
| 谁管设备/交换链/命令/fence | `Renderer` 自己 | `gfx::D3D12Context` / `gfx::MetalContext` |
| 谁管根签名/PSO/着色器/纹理/常量/Draw | `Renderer` | `render/<api>/…Renderer` |
| 帧边界 | 散在 `present()` 里 | 显式的 `beginFrame()` / `endFrame()` |
| 纹理上传 | `present()` 的同类代码里自己 Reset/Close/Execute/Wait | 复用 context 的 `beginCommandList()` / `submitAndWait()` |
| 复用性 | 只能自己用 | context 可给第三个后端（SkiaRenderer）复用 |
| 学习价值 | 一次看完整条 D3D12/Metal 流水 | 看清"哪些是平台账、哪些是画法" |

一句话：**第 1 代回答"一次绘制要调哪些 API"，第 2 代回答"这些 API 里哪些属于帧/平台、哪些属于画面"。**

---

## 3. `api_only/`（第 1 代）里看什么

### 3.1 D3D12（`D3D12Renderer.cpp` 777 行）

| 主题 | 行号 |
|---|---|
| `initialize()`：设备 → 交换链 → RTV 堆 → 后台缓冲 → 根签名 → PSO → allocator/list → fence/event → SRV 堆 | `:139`（子函数 `createDeviceAndQueue` `:193`、`createSwapChain` `:223`、`createDescriptorHeaps` `:275`、`createRenderTargets` `:300`、`createRootSignature` `:314`、`createPipelineState` `:383`、`createSrvHeap` `:440`） |
| 纹理上传：DEFAULT 纹理 + `GetCopyableFootprints` + UPLOAD 缓冲 + `CopyTextureRegion` + barrier + 等 GPU + SRV | `createTextureFromImage` `:453-581`，入口 `setImage` `:584` |
| 每帧：等 GPU → Reset → barrier → clear → 绑定 → 常量 → viewport/scissor → draw → barrier → Present | `present()` `:597-728` |
| 8 个 float 的矩阵参数（`outputScale/aspect/m00..m11`） | `:657-677`（`shaderParams[8]` + `SetGraphicsRoot32BitConstants`） |
| contain-fit 的 viewport/scissor（90° 用 `1/aspect`） | `:679-711`（`D3D12_VIEWPORT` 在 `:697`） |
| 交换链重建 | `resize()` `:730-760` |
| fence 等待 | `waitForGpu()` `:764-775` |

### 3.2 Metal（`MetalRenderer.mm` 496 行）

| 主题 | 行号 |
|---|---|
| `initialize()`：device/queue、MSL 运行时编译、PSO、sampler、子 NSView + CAMetalLayer + colorspace/EDR | `:91-240` |
| 纹理上传：RGBA16Unorm 私有纹理 + 256 对齐 staging + blit + `waitUntilCompleted` | `setImage` `:403-494` |
| 每帧：nextDrawable（判空）→ render pass/encoder → 绑定 → 常量 → viewport/scissor → draw → present + commit + wait | `present()` `:267-385` |
| 8 个 float 参数（`setFragmentBytes`，32 B） | `:316-330` |
| contain-fit（用 drawable 宽高，90° 交换宽高比） | `:332-370` |
| 析构：`removeFromSuperview`（其余交给 ARC） | `:84-89` |

### 3.3 读这里时的两个提醒

1. **`makeTestPixels()`（`:64-113`）是第 4 步"程序生成棋盘格"的遗留**，第 5 步换成 FFmpeg 解码后就**不再被调用**。
   看到它不要以为这个版本"没有读 PNG"——PNG 链路在 `_shirley/notes/ai_recording.md` §1（解码层 + 上传层分开说明）。
2. **include 保留旧路径**（如 `#include "render/D3D12Renderer.hpp"`），在当前仓库已经不存在；这是历史记录，不需要修正、也不要拿去编译。
   两代里 **Metal 的 `.hpp` 完全一样**（pimpl 把 ObjC 类型关在 `.mm` 里），差异只在 `.mm`。

---

## 4. `api_context/`（第 2 代）里看什么

### 4.1 `gfx/`：原生资源 + 帧生命周期 + 同步

| 文件 | 关键入口 |
|---|---|
| `gfx/d3d12/D3D12Context.cpp`（300 行） | `initialize` `:35`、`createSwapChain` `:100`、`resize` `:188`、`beginFrame` `:221`（wait + Reset + PRESENT→RT barrier）、`endFrame` `:238`（RT→PRESENT + Close + Execute + Present + frameIndex）、`beginCommandList` `:258`、`submitAndWait` `:265`、`waitForGpu` `:275`、访问器 `:289-298` |
| `gfx/metal/MetalContext.mm`（290 行） | `initialize` `:76`（device/queue/子 view/layer/色彩空间）、`resize` `:152`、`beginFrame` `:190`（nextDrawable，nil 返回 false）、`endFrame` `:216`（presentDrawable+commit+wait）、`beginCommandList` `:239`、`submitAndWait` `:258`、访问器 `:281-288` |

### 4.2 `renderer/`：画什么

| 文件 | 关键入口 |
|---|---|
| `renderer/d3d12/D3D12Renderer.cpp`（521 行） | `initialize` `:137`（context 初始化 + 根签名/PSO/SRV 堆）、`createRootSignature` `:158`、`createPipelineState` `:228`、`createTextureFromImage` `:298`、`setImage` `:420`、`present` `:434`、`resize` `:515` |
| `renderer/metal/MetalRenderer.mm`（422 行） | `initialize` `:91`、`present` `:202`（context 取 drawable/cb，自建 pass/encoder）、`setImage` `:331` |

> `renderer/d3d12/D3D12Renderer.cpp:449` 是那份**工作区未提交**的守卫：
> `if (m_imageWidth > 0 && m_imageHeight > 0)` —— 无图时只清屏、不绑空 SRV。HEAD 里这个 `if` 还是被注释掉的状态。

### 4.3 与第 1 代的对应（哪些代码搬走了）

| 第 1 代 `api_only` 里的内容 | 第 2 代去哪里 |
|---|---|
| 设备/命令队列/工厂/交换链/RTV 堆/后台缓冲/命令分配器/命令列表/fence/event/resize/Present/frameIndex | `gfx/<api>/*Context` |
| 每帧开头的 wait + Reset + PRESENT→RT barrier、结尾的 RT→PRESENT + Execute + Present | `gfx/<api>/*Context::beginFrame/endFrame` |
| 上传用的 Reset/Close/Execute/Wait | `gfx/<api>/*Context::beginCommandList/submitAndWait` |
| 根签名/PSO/着色器编译/采样器 | `render/<api>/*Renderer` |
| 纹理资源与上传内容（SRV/blit/staging/上传 barrier） | `render/<api>/*Renderer::setImage` |
| 8 个常量、clear color、contain-fit、viewport/scissor、Draw | `render/<api>/*Renderer::present` |

归属判据一句话：**换平台必须重写的 → `gfx`；换画面才改的 → `renderer`。**
更完整的"为什么这样切"见 `docs/handoff/08-current-progress.md` §3/§6。

---

## 5. 来源与完整性

| 目录 | 来源 | 校验方式 | 结果 |
|---|---|---|---|
| `api_only/` | `42518f8` 的 `src/render/D3D12Renderer.{hpp,cpp}`、`src/render/MetalRenderer.{hpp,mm}` | 逐文件 `git hash-object` ↔ `git rev-parse 42518f8:<path>` | 4/4 一致 |
| `api_context/` | 当前工作区（HEAD `822b976` + 未提交守卫修复）的 `src/gfx/{d3d12,metal}/…Context.*` 与 `src/render/{d3d12,metal}/…Renderer.*` | 与工作区文件做 SHA-256 对比 | 8/8 一致 |

- 两代都保留 LF 行尾与原始注释；`api_only/` 的 4 个文件在 2026-11-10 按 `42518f8` 恢复。
- 当前仓库的 `shaders/fullscreen.{hlsl,metal}` 与 `42518f8` 的 git blob **完全一致**（已校验），
  所以两代共用同一份 shader，矩阵/色彩数学不变。

---

## 6. 为什么不参与编译

- `legacy/` 不在 `src/` 下；顶层 `CMakeLists.txt` 只 `add_subdirectory(src)` / `tests`；
  `cmake/`、`CMakePresets.json`、各 `CMakeLists.txt` 里都**没有** `legacy` 引用。
- 本目录**没有** `CMakeLists.txt`，不会被任何 target 或 IDE 工程当作源加入。
- 旧 include 路径保留原样（`api_only` 的 `render/D3D12Renderer.hpp` 等），不需要能编过。
- 如果 IDE 把 `legacy/` 扫进索引，忽略即可。

---

## 7. 完整程序的其他部分在哪

本目录**只存 renderer / context 层**（完整程序快照 `program-42518f8/` 已从本目录移除）。要看全链路：

| 内容 | 在哪 |
|---|---|
| PNG 文件 → FFmpeg 解码 → RGBA16 → `setImage` → 纹理上传 → 色彩管理 → 上屏 | `_shirley/notes/ai_recording.md`（按"图片文件输入 + 色彩管理"整理，逐行标注） |
| 着色器（矩阵/色彩数学真正使用的地方） | 当前 `shaders/fullscreen.hlsl` / `.metal`（与 `42518f8` 相同） |
| 窗口层（`showEvent` / `resizeEvent` / `paintEvent` / 按键） | 当前 `src/app/ViewportWidget.*`、`src/app/main.cpp`（两代都没变过） |
| 媒体层（FFmpeg 解码） | 当前 `src/media/ImageLoader.*`（两代都没变过） |
| 双平台逐项对照 | `_shirley/notes/pipeline_comparison.md` |
| 重构方案/交接 | `docs/handoff/08-current-progress.md` |

> 想恢复完整程序快照：可以从 git 重新导出，例如
> `git archive --format=zip -o legacy-program.zip 42518f8`（`42518f8` 就是第 1 代的运行时状态）。
> 注意 `external/ffmpeg` 与 `assets/test-images/` 不入库，需要另行准备。

---

## 8. 建议阅读顺序

1. **先通读一遍第 1 代**（选一个平台即可）：按 §3 的 `initialize → setImage → present` 顺序，理解一个 renderer 每帧到底调了哪些 API；
2. **再看另一个平台的同一段**（或直接看 `pipeline_comparison.md`），体会 D3D12 的"三本账" vs Metal 的"对象生命周期"；
3. **再看第 2 代**：对照 §4 的行号，确认哪些代码搬进了 `gfx`、`renderer` 只剩什么；
4. 最后回到 `docs/handoff/08-current-progress.md` 和 `_shirley/notes/ai_recording.md`，把"为什么这样拆"和"两条数据链路"串起来。

---

## 9. 一页速记

> **`api_only`（第 1 代）**：一个类 = 三本账（资源状态/描述符/帧同步）+ 画法，全在一个 `.cpp` 里。
> **`api_context`（第 2 代）**：`gfx` 管"帧与原生资源"，`renderer` 管"画什么"；
> 判据：**换平台要重写的 → gfx；换画面要改的 → renderer**。
> 两代都只做记录、不参与编译；注释（含 Q/A）保持当时原样。
