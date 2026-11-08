set(_chroma_render_sources
    IRenderer.hpp
    IViewTransform.hpp
    RendererFactory.hpp
    RendererFactory.cpp)

if(WIN32)
    list(APPEND _chroma_render_sources
        D3D12Renderer.hpp
        D3D12Renderer.cpp)
elseif(APPLE)
    list(APPEND _chroma_render_sources
        MetalRenderer.hpp
        MetalRenderer.mm)
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
