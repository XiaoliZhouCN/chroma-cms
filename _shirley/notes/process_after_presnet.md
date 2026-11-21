# Process After Present

> `([])`标记内为输入/输出，名词
> `[]`标记内为操作，动词

``` mermaid
flowchart TD
    app_surface(["`应用给出带colorspace的texture/CAMetalLayer信息`"]) --> 
        system_transfer_cs["`系统将app给出的surface转换到合成空间需要的色彩空间`"] -->
        system_blend["`系统将若干转换好的surface窗口进行合成`"] -->
    system_frame(["`合成好的一张frame`"]) -->
        system_transfer_cs_for_monitor["`系统将frame再转换成显示器信号`"] -->   
    system_displayframe(["`转换到显示器信号空间的dispaly frame`"]) -->
        monitor_transfer_cs["`显示器/TCON 内部可能做 LUT、gamma、色调映射、局部调光，也可能基本直通；具体实现不可观测。`"] -->
    monitor_present(["`显示器显示的图像`"])
    
    system_blend --- win_DWM_blending("`DWM blending`")
    system_blend --- win_window_server_blending("`WindowServer / Core Animation 合成`")

    system_frame --- win_DWM_cccs("`**notes:** win 称为 cccs，当前win端获取的内容。色值和色彩空间都可读（HDR-scRGB， SDR-sRGB）。DWM 先把每个 app 从自己的色彩空间转换到 CCCS，然后混合`")
    system_frame --- mac_display_framebuffer("`**notes:** mac API 需要指定色彩空间（否则系统使用推荐色彩空间）获取色值，官方没有给出明确的合成空间的色彩空间信息。SCK 的 colorSpaceName 是“捕获输出空间”，如果不设置，输出使用显示器色彩空间（Apple 文档语义），不是“系统推荐合成空间”。`")

    system_displayframe --- win_wire_frame("`**notes:** 色彩空间信息可读，色值不可读。display kernel 再把 OS framebuffer 从 CCCS 转换成 wire format 色彩空间.wire format 色彩空间可以通过 DXGI_OUTPUT_DESC1 读取.不是 display ICC,不是 CCCS.Windows 的 display ICC/profile 是另一套色彩管理信息.`")
    system_displayframe --- mac_scanout("`**notes:** macOS 可以读显示器 ICC / EDR headroom，但没有 Windows DXGI_OUTPUT_DESC1 那样的 wire format 色彩空间 + 位深完整公开字段。实际 scanout 色彩空间不能像 Windows 一样可靠读取。")
```

可以概括为同一个通用模型：

应用带 colorspace 的 surface
  → 系统转换到合成空间
  → 系统在合成空间混合
  → 系统转换为显示输出
  → 显示器内部处理
  → 最终图像

Windows 可以把“合成空间”具体写成 CCCS（scRGB FP16）；
macOS 只能写成“内部合成空间，不公开”。

Windows 的两阶段色彩转换有官方文档；
macOS 的对应阶段没有公开规范，
不能把 Windows 的 CCCS 命名直接推广到 macOS。
