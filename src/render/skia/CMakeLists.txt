# Skia（Ganesh）后端：平台无关 SkiaRenderer + 平台 GPU backend。
# 独立 target，避免 Skia include / 编译选项传播到手写后端和 app。

if(NOT CHROMA_WITH_SKIA)
    return()
endif()

if(NOT (WIN32 OR APPLE))
    return()
endif()

include(${PROJECT_SOURCE_DIR}/cmake/FindSkia.cmake)

add_library(chroma_render_skia STATIC
    SkiaRenderer.hpp
    SkiaRenderer.cpp
    SkiaGpuBackend.hpp)

target_link_libraries(chroma_render_skia PRIVATE Skia::Skia chroma_gfx)
target_compile_definitions(chroma_render_skia PRIVATE CHROMA_HAS_SKIA=1)

if(WIN32)
    target_sources(chroma_render_skia PRIVATE
        SkiaGpuBackendD3D12.cpp)
    target_link_libraries(chroma_render_skia PRIVATE d3d12 dxgi)
elseif(APPLE)
    target_sources(chroma_render_skia PRIVATE
        SkiaGpuBackendMetal.mm)
    target_compile_options(chroma_render_skia PRIVATE
        "$<$<COMPILE_LANGUAGE:OBJCXX>:-fobjc-arc>")
endif()

chroma_set_warnings(chroma_render_skia)

message(STATUS "ChromaCMS: Skia 后端已启用（Release only）")
