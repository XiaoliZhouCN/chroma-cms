# 渲染层：契约层（本目录）+ 各后端实现（子目录）。
#
#   d3d12/    Windows 手写后端
#   metal/    macOS 手写后端
#   skia/     Skia 第三后端

add_library(chroma_render STATIC
    IRenderer.hpp
    IViewTransform.hpp
    RendererFactory.hpp
    RendererFactory.cpp)

target_link_libraries(chroma_render PUBLIC chroma_gfx)

if(WIN32)
    add_subdirectory(d3d12)
elseif(APPLE)
    add_subdirectory(metal)
endif()

add_subdirectory(skia)

if(TARGET chroma_render_skia)
    target_link_libraries(chroma_render PRIVATE chroma_render_skia)
    target_compile_definitions(chroma_render PRIVATE CHROMA_HAS_SKIA=1)
    target_compile_definitions(chroma_render PRIVATE
        "CHROMA_DEFAULT_BACKEND=\"${CHROMA_DEFAULT_BACKEND}\"")
endif()

chroma_set_warnings(chroma_render)
