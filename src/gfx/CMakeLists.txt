# 平台层：原生图形上下文（设备 / 队列 / 交换链 / 同步）+ 共享工具。
#
#   d3d12/D3D12Context.*    Windows：设备/队列/交换链/RTV/命令录制/fence
#   metal/MetalContext.*    macOS：待补（与 D3D12Context 同形，不是同一接口）
#
# 抽象契约在 render/；这里只放"具体平台的原生资源和帧生命周期"。
# 这样 D3D12Renderer 与将来的 SkiaRenderer（Ganesh D3D）可以共用同一份上下文。

if(WIN32)
    add_library(chroma_gfx STATIC
        D3D12Common.hpp
        d3d12/D3D12Context.hpp
        d3d12/D3D12Context.cpp)
    target_link_libraries(chroma_gfx PUBLIC d3d12 dxgi)
    target_compile_definitions(chroma_gfx PUBLIC
        NOMINMAX WIN32_LEAN_AND_MEAN UNICODE _UNICODE)
elseif(APPLE)
    # MetalContext 落地前保持 INTERFACE（避免空静态库）。
    add_library(chroma_gfx INTERFACE)
    target_link_libraries(chroma_gfx INTERFACE
        "-framework Foundation"
        "-framework AppKit"
        "-framework Metal"
        "-framework QuartzCore"
        "-framework CoreGraphics")
else()
    add_library(chroma_gfx INTERFACE)
endif()

# 作用域跟着库类型走：INTERFACE 库只允许 INTERFACE 属性（用 PUBLIC 会报
# "target_include_directories may only set INTERFACE properties on INTERFACE targets"），
# 静态库才用 PUBLIC 把 include/标准传播给消费方。
# 这样 macOS 的 MetalContext 落地前（INTERFACE）与落地后（STATIC）都不用再改这一节。
get_target_property(_chroma_gfx_type chroma_gfx TYPE)
if(_chroma_gfx_type STREQUAL "INTERFACE_LIBRARY")
    set(_chroma_gfx_scope INTERFACE)
else()
    set(_chroma_gfx_scope PUBLIC)
endif()

target_include_directories(chroma_gfx ${_chroma_gfx_scope} "${PROJECT_SOURCE_DIR}/src")
target_compile_features(chroma_gfx ${_chroma_gfx_scope} cxx_std_20)

# chroma_set_warnings 内部是 PRIVATE 编译选项，只能对真实库目标调用。
if(NOT _chroma_gfx_type STREQUAL "INTERFACE_LIBRARY")
    chroma_set_warnings(chroma_gfx)
endif()
