# 渲染层：契约层（本目录）+ 各后端实现（子目录）。
#
#   IRenderer.hpp / IViewTransform.hpp / RendererFactory.*   平台无关契约
#   d3d12/                                                    Windows 后端（手写）
#   metal/                                                    macOS 后端（手写）
#   skia/                                                     第三后端：Skia 桥接（Ganesh）
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

# ---- 第三后端：Skia（Ganesh） ----
# 独立 target 的原因：Skia 的 include/编译选项（/bigobj、SK_GANESH...）只作用于这一层，
# 不会传播到 app/ 或手写后端。预编译包是 Release(/MD)，必须用 windows-x64-skia 预设构建。
if(WIN32 AND CHROMA_WITH_SKIA)
    include(${PROJECT_SOURCE_DIR}/cmake/FindSkia.cmake)

    add_library(chroma_render_skia STATIC
        skia/SkiaRenderer.hpp
        skia/SkiaRenderer.cpp)

    target_link_libraries(chroma_render_skia PRIVATE Skia::Skia chroma_gfx)
    target_compile_definitions(chroma_render_skia PRIVATE CHROMA_HAS_SKIA=1)
    chroma_set_warnings(chroma_render_skia)

    target_link_libraries(chroma_render PRIVATE chroma_render_skia)
    target_compile_definitions(chroma_render PRIVATE CHROMA_HAS_SKIA=1)
    target_compile_definitions(chroma_render PRIVATE
        "CHROMA_DEFAULT_BACKEND=\"${CHROMA_DEFAULT_BACKEND}\"")

    message(STATUS "ChromaCMS: Skia 后端已启用（Release only）")
endif()

chroma_set_warnings(chroma_render)
