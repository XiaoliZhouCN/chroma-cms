# 渲染层：契约层（本目录）+ 各后端实现（子目录）。
#
#   IRenderer.hpp / IViewTransform.hpp / RendererFactory.*   平台无关契约
#   d3d12/                                                    Windows 后端
#   metal/                                                    macOS 后端
#   skia/                                                     （待加，第三后端）
#
# 每个后端只依赖 chroma_gfx（原生上下文层），互相之间不引用。

set(_chroma_render_sources
    IRenderer.hpp
    IViewTransform.hpp
    RendererFactory.hpp
    RendererFactory.cpp)

if(WIN32)
    list(APPEND _chroma_render_sources
        d3d12/D3D12Renderer.hpp
        d3d12/D3D12Renderer.cpp)
elseif(APPLE)
    list(APPEND _chroma_render_sources
        metal/MetalRenderer.hpp
        metal/MetalRenderer.mm)
endif()

add_library(chroma_render STATIC ${_chroma_render_sources})

target_link_libraries(chroma_render PUBLIC chroma_gfx)

if(WIN32)
    target_link_libraries(chroma_render PUBLIC d3dcompiler)
endif()

if(APPLE)
    # MetalRenderer.mm 用 ARC 管理 id<MTL...>/CAMetalLayer 的生命周期；
    # 只对 OBJCXX 开，不影响纯 C++ 文件。
    target_compile_options(chroma_render PRIVATE
        "$<$<COMPILE_LANGUAGE:OBJCXX>:-fobjc-arc>")
endif()

chroma_set_warnings(chroma_render)
