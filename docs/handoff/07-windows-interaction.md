# 07 · Windows 交互适配（A/D/Q/E/R，90° 矩阵 UV）

> 用途：macOS 侧已完成 M1–M4 + 交互；现在回到 Windows，把同一套交互接到 D3D12。
> 基线：`practice` @ `e545d91`（macOS 侧已在真机验证）。
>
> 这份文档是**给 Windows 侧 agent 的交接单**。先读 `AGENTS.md` 和 `01`–`04`，再看本文件。

---

## 1. 一句话结论

**app 层已经全部做完，Windows 只需要让 `D3D12Renderer` 实现 `IViewTransform`，并把 8 个 float 的矩阵参数接到 root constants + HLSL。**

不需要改：

- `ViewportWidget` 的按键逻辑；
- `IViewTransform` 接口；
- `RendererFactory`；
- `IRenderer` 接口。

需要改：

- `src/render/D3D12Renderer.hpp/.cpp`
- `shaders/fullscreen.hlsl`
- Windows 侧 root signature 的常量数量

---

## 2. 当前双平台状态

| 项 | Windows / D3D12 | macOS / Metal |
|---|---|---|
| 窗口 | Qt + `WA_NativeWindow` | Qt + `WA_NativeWindow` + 子 NSView |
| 输出色域 | **BT.709 / scRGB** | **Display-P3 线性** |
| 最后矩阵 | `XYZ→Rec.709` | `XYZ→Display-P3` |
| `outputScale` | **0.8** | **1.0** |
| EDR metadata | 由 DWM 处理 | 暂不设置 |
| 旋转/缩放 | **待适配** | 已实现 |
| 旋转方式 | **90° 步进矩阵** | 90° 步进矩阵 |
| 共享接口 | `IRenderer` | `IRenderer` + `IViewTransform` |

> **不要**把 macOS 的 Display-P3 / `outputScale=1.0` 直接搬到 Windows。  
> 两端色域是刻意不同的；本文件只适配**交互矩阵**，不动 Windows 的色彩数学。

---

## 3. 已经完成的共享部分（Windows 侧不用改）

### 3.1 `IViewTransform`

`src/render/IViewTransform.hpp`：

```cpp
class IViewTransform
{
public:
    virtual ~IViewTransform() = default;
    virtual void setQuarterTurns(int quarterTurns) = 0;  // 正 = 逆时针
    virtual void setZoom(float zoom) = 0;                // 1.0 = 原始
};
```

它是**可选接口**，没有改 `IRenderer`。  
`ViewportWidget` 用 `dynamic_cast<IViewTransform*>` 探测；没实现就按键无效果，不会崩。

### 3.2 `ViewportWidget` 按键已经写好

```cpp
case Qt::Key_A: m_quarterTurns = (m_quarterTurns + 1) & 3; break; // 逆时针 90°
case Qt::Key_D: m_quarterTurns = (m_quarterTurns + 3) & 3; break; // 顺时针 90°
case Qt::Key_Q: m_zoom = std::max(kMinZoom, m_zoom / kZoomStep); break;
case Qt::Key_E: m_zoom = std::min(kMaxZoom, m_zoom * kZoomStep); break;
case Qt::Key_R: m_quarterTurns = 0; m_zoom = 1.0f; break;
```

- 步长：旋转 90°，缩放 1.1×；
- 范围：`zoom ∈ [0.1, 8.0]`；
- 焦点：`showEvent()` 里 `setFocus()`；按键没反应时先点一下 viewport。

### 3.3 `RendererFactory`

Windows 走 `D3D12Renderer`，mac 走 `MetalRenderer`。  
`ViewportWidget` 只持有 `IRenderer`，所以 Windows 接上后交互自动生效。

### 3.4 媒体层 API

`ImageLoader::loadImage` 已改成：

```cpp
bool loadImage(const std::string& utf8Path, Image& out, std::string& error);
```

`main.cpp` 用 `imagePath.toStdString()`（UTF-8）。  
Windows 上 FFmpeg 的 `avformat_open_input` 接受 UTF-8，不需要 `WideCharToMultiByte`。

> Windows 构建如果还在用旧的 `std::wstring` 版本，会编译失败；按当前 `practice` 分支的代码走。

---

## 4. Windows 要做的 5 处改动

### 4.1 `D3D12Renderer.hpp`

**第 1 处：include 接口**

在：

```cpp
#include "render/IRenderer.hpp"
```

后面加：

```cpp
#include "render/IViewTransform.hpp"
```

**第 2 处：继承两个接口**

```cpp
class D3D12Renderer final : public IRenderer, public IViewTransform
```

**第 3 处：声明方法**

在 `setImage(...)` 后面加：

```cpp
    void setQuarterTurns(int quarterTurns) override;
    void setZoom(float zoom) override;
```

**第 4 处：成员状态**

在 `m_outputScale` 附近加：

```cpp
    int m_quarterTurns = 0;
    float m_zoom = 1.0f;
```

---

### 4.2 `D3D12Renderer.cpp`：实现两个 setter

在 `backendName()` 或 `setImage()` 附近加：

```cpp
void D3D12Renderer::setQuarterTurns(int quarterTurns)
{
    m_quarterTurns = ((quarterTurns % 4) + 4) % 4;
}

void D3D12Renderer::setZoom(float zoom)
{
    m_zoom = (zoom > 0.0f) ? zoom : 1.0f;
}
```

这两个函数只改状态；下一帧 `present()` 把矩阵传到 root constants。

---

### 4.3 root signature：4 个 DWORD → 8 个 DWORD

在 `createRootSignature()` 里找到：

```cpp
    // 根参数 1：4 个 32 位常量，映射到 b0
    ...
    params[1].Constants.Num32BitValues = 4;               // 4 个 DWORD = 16 字节
```

改成：

```cpp
    // 根参数 1：8 个 32 位常量，映射到 b0
    ...
    params[1].Constants.Num32BitValues = 8;               // 8 个 DWORD = 32 字节
```

**为什么**：

新的 `ShaderParams` 是：

```text
outputScale, aspect, m00, m01, m10, m11, reserved0, reserved1
```

正好 8 个 float = 32 字节。

---

### 4.4 `present()`：构建矩阵并传 8 个常量

找到：

```cpp
    // 根参数 1：outputScale 放在第 0 个 DWORD
    const float shaderParams[4] = {0.8f, 0.0f, 0.0f, 0.0f};        // m_outputScale
    m_commandList->SetGraphicsRoot32BitConstants(1, 4, shaderParams, 0);
```

替换为：

```cpp
    // 根参数 1：outputScale / aspect / 2×2 矩阵
    const int q = m_quarterTurns & 3;
    const float c = (q == 0) ? 1.0f : (q == 2) ? -1.0f : 0.0f;
    const float s = (q == 1) ? 1.0f : (q == 3) ? -1.0f : 0.0f;
    const float invZoom = (m_zoom > 0.0f) ? (1.0f / m_zoom) : 1.0f;

    float imageAspect = 1.0f;
    if (m_imageWidth > 0 && m_imageHeight > 0) {
        imageAspect = static_cast<float>(m_imageWidth) / static_cast<float>(m_imageHeight);
    }

    const float shaderParams[8] = {
        m_outputScale,                  // 0: outputScale（Windows 保持 0.8）
        imageAspect,                    // 1: imageWidth / imageHeight
        c * invZoom,                    // 2: m00
        -s * invZoom,                   // 3: m01
        s * invZoom,                    // 4: m10
        c * invZoom,                    // 5: m11
        0.0f,                           // 6: reserved
        0.0f                            // 7: reserved
    };
    m_commandList->SetGraphicsRoot32BitConstants(1, 8, shaderParams, 0);
```

**注意**：

- `m_outputScale` 是 Windows 现有的 0.8，**不要**改成 mac 的 1.0；
- 矩阵是“采样逆变换”，已经包含 `1/zoom`；
- `A` 对应 `q=1`，矩阵为 `[0,-1; 1,0]`，在屏幕上看是逆时针。

---

### 4.5 contain-fit：90°/270° 交换宽高比

找到：

```cpp
    const float imageAspect = static_cast<float>(m_imageWidth) / static_cast<float>(m_imageHeight);
    const float targetAspect = static_cast<float>(m_width) / static_cast<float>(m_height);
```

把它改成“有效宽高比”：

```cpp
    const float imageAspect = static_cast<float>(m_imageWidth) / static_cast<float>(m_imageHeight);
    const float effectiveAspect = ((m_quarterTurns & 1) != 0)
        ? (1.0f / imageAspect)
        : imageAspect;
    const float targetAspect = static_cast<float>(m_width) / static_cast<float>(m_height);
```

然后把后面的 `imageAspect`（viewport 计算）改成 `effectiveAspect`：

```cpp
    if (effectiveAspect > targetAspect) {
        viewHeight = static_cast<float>(m_width) / effectiveAspect;
    }
    else {
        viewWidth = static_cast<float>(m_height) * effectiveAspect;
    }
```

**为什么**：

- PTMR 是方图，`aspect=1`，交换后不变；
- 非方图旋转 90° 后，显示宽高比变成 `1/aspect`，不交换会拉伸/裁剪。

---

## 5. 改 `shaders/fullscreen.hlsl`

### 5.1 `cbuffer` 换成 8 个 float

找到：

```hlsl
cbuffer ShaderParams : register(b0)
{
    float outputScale;   // root constants[0]
    float reserved0;
    float reserved1;
    float reserved2;
};
```

替换为：

```hlsl
cbuffer ShaderParams : register(b0)
{
    float outputScale;   // root constants[0]
    float aspect;        // root constants[1]：imageWidth / imageHeight
    float m00;           // root constants[2..5]：采样逆变换矩阵，已含 1/zoom
    float m01;
    float m10;
    float m11;
    float reserved0;
    float reserved1;
};
```

### 5.2 加 `TransformUV`

在 `PqEotf` 后面加：

```hlsl
// 90° 步进的旋转 + 缩放，已经由 CPU 合成到 m00..m11。
float2 TransformUV(float2 uv)
{
    float2 p = (uv - 0.5) * float2(aspect, 1.0);
    p = float2(m00 * p.x + m01 * p.y,
               m10 * p.x + m11 * p.y);
    return p / float2(aspect, 1.0) + 0.5;
}
```

### 5.3 PS 里先变换 UV，再采样

找到：

```hlsl
    float3 code = g_texture.Sample(g_sampler, input.uv).rgb;   // 归一化 PQ 码值
```

替换为：

```hlsl
    const float2 uv = TransformUV(input.uv);

    // 旋转/缩小后落到图片外的像素直接丢弃，露出 clear color。
    if (uv.x < 0.0 || uv.x > 1.0 || uv.y < 0.0 || uv.y > 1.0)
    {
        discard;
    }

    float3 code = g_texture.Sample(g_sampler, uv).rgb;   // 归一化 PQ 码值
```

其余 PQ → BT.2020 → XYZ → BT.709 → `/80` 全部不变。

> HLSL 里用显式 `m00 * p.x + m01 * p.y`，不要用 `mul(float2x2, p)`，否则很容易把行/列顺序写反。  
> 这里的矩阵是 2×2，和 MSL 的数值完全一致；只是 HLSL 不需要转置写法。

---

## 6. 编译与验收

```powershell
cmake --preset windows-x64-debug -DQT_ROOT="D:/Applications/QtCreator/6.11.1/msvc2022_64"
cmake --build --preset windows-x64-debug

.\build\windows-x64-debug\Debug\ChromaCMS.exe
```

**操作：**

- 点一下 viewport 确保焦点；
- `A` 一次 → 逆时针 90°；
- `A` 两次 → 180°；
- `D` → 顺时针 90°；
- `Q/E` → 缩放；
- `R` → 同时复位旋转和缩放。

**预期：**

- 90° 时原图顶部灰 ramp 变成左侧竖列（逆时针）；
- 180° 时上下左右都反；
- 缩小后图片外是 clear color；
- 放大后图片被 viewport 裁掉；
- 非方图时 90° 旋转不拉伸。

**Windows 色彩预期：**

- 仍然走 BT.709 / scRGB；
- `outputScale = 0.8`；
- 观感和 mac 的 Display-P3 版本不同是预期的；
- 不要用 mac 截图做逐像素比较。

---

## 7. 常见错误

| 现象 | 原因 | 处理 |
|---|---|---|
| 按键完全没反应 | `D3D12Renderer` 没继承 `IViewTransform`，`dynamic_cast` 返回 nullptr | 检查 `class D3D12Renderer final : public IRenderer, public IViewTransform` |
| 按键有日志但画面不动 | `setQuarterTurns/setZoom` 没改成员，或 present 没传参 | 检查两个 setter 和 `SetGraphicsRoot32BitConstants` |
| 画面颜色/UV 全错 | root constants 还是 4 个 DWORD，但 HLSL cbuffer 已经是 8 个 float | `Num32BitValues = 8`，`SetGraphicsRoot32BitConstants(..., 8, ...)` |
| 旋转方向反了 | 矩阵符号/行列出错 | 用本文件给出的 `m00..m11` 和 `c/s`；A 的 q=1 应为 `[0,-1; 1,0]` |
| 非方图 90° 后拉伸 | 没交换 viewport 的 effectiveAspect | 按 4.5 用 `(m_quarterTurns & 1)` 交换 |
| 旋转后边缘被拉长 | 没 `discard`/`clip` 图片外 UV | PS 里加 `discard`（或 `clip`） |
| 改 shader 后画面不变 | POST_BUILD 只在 relink 时拷贝 | `LINK_DEPENDS` 在 VS 生成器可能不生效；手动把 `shaders\fullscreen.hlsl` 拷到 exe 旁 |
| 按键没焦点 | 窗口焦点在别的控件 | 点一下 viewport；`ViewportWidget::showEvent` 里已有 `setFocus` |
| Windows 编译不过 `IMG_...`/UTF-8 路径 | 媒体层 API 已改成 UTF-8 `std::string` | 确认 `main.cpp` 用 `toStdString()`，`ImageLoader` 不再用 `WideCharToMultiByte` |
| 想直接用 mac 的 P3 代码 | 两端色域刻意不同 | Windows 保持 `XYZ→Rec.709`、`outputScale=0.8` |

---

## 8. 验证证据（mac 侧）

mac 侧已用合成键盘事件验证：

- `A × 1` → 90°，原图顶部灰 ramp 变成左侧竖列；
- `A × 2` + `E × 2` → 180° + 1.21×；
- `R` → 旋转 0、缩放 1。

截图：

- `docs/handoff/images/macos-90.png`
- `docs/handoff/images/macos-180-zoom.png`
- `docs/handoff/images/macos-reset.png`

Windows 适配后，帧率/画面应和 `outputScale=0.8` 的旧 Windows 色彩管线一致，只是多了旋转/缩放。

---

## 9. 适配完成后的下一步

1. **M5 双平台一致性**
   - `src/gfx/ColorMath.hpp`：
     - `pqEotf`
     - `rec2020ToXyz`
     - `xyzToRec709`（Windows）
     - `xyzToDisplayP3`（macOS）
   - 单元测试：PQ 端点、白点、PTMR CSV nits、`outputScale`；
   - 双平台只比较到 XYZ，最后一步矩阵按平台分别断言。

2. **macOS M4.2（可选）**
   - `CAEDRMetadata` / tone mapping；
   - 当前 `outputScale=1.0` 高光逐通道裁剪，是已知缺口。

3. **交互增强（可选）**
   - 旋转/缩放后自动 fit；
   - 鼠标拖拽/滚轮；
   - 状态栏显示 `quarterTurns / zoom`。

---

## 10. 参考文件

| 文件 | 作用 |
|---|---|
| `src/render/IViewTransform.hpp` | 共享的可选接口 |
| `src/app/ViewportWidget.cpp` | 按键/焦点/90° 状态（已写完） |
| `src/render/MetalRenderer.mm` | mac 参考实现（矩阵构建、contain-fit） |
| `shaders/fullscreen.metal` | mac 参考 shader（矩阵 UV） |
| `src/render/D3D12Renderer.hpp/.cpp` | Windows 待改 |
| `shaders/fullscreen.hlsl` | Windows 待改 |
| `docs/handoff/04-dual-platform.md` | 双平台架构 |
