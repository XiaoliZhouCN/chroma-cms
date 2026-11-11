# 平台层：原生图形上下文（设备 / 队列 / 交换链 / 同步）+ 共享工具。
#
#   d3d12/    Windows：D3D12Context
#   metal/    macOS：MetalContext
#
# 抽象契约在 render/；这里只放具体平台的原生资源和帧生命周期。

if(WIN32)
    add_library(chroma_gfx STATIC
        ColorMath.hpp
        D3D12Common.hpp)
elseif(APPLE)
    add_library(chroma_gfx STATIC
        ColorMath.hpp)
else()
    add_library(chroma_gfx INTERFACE)
endif()

# 作用域跟着库类型走：INTERFACE 库只能设 INTERFACE 属性；
# 静态库才使用 PUBLIC 把 include / C++ 标准传播给消费方。
get_target_property(_chroma_gfx_type chroma_gfx TYPE)
if(_chroma_gfx_type STREQUAL "INTERFACE_LIBRARY")
    set(_chroma_gfx_scope INTERFACE)
else()
    set(_chroma_gfx_scope PUBLIC)
endif()

target_include_directories(chroma_gfx ${_chroma_gfx_scope} "${PROJECT_SOURCE_DIR}/src")
target_compile_features(chroma_gfx ${_chroma_gfx_scope} cxx_std_20)

if(NOT _chroma_gfx_type STREQUAL "INTERFACE_LIBRARY")
    chroma_set_warnings(chroma_gfx)
endif()

if(WIN32)
    add_subdirectory(d3d12)
elseif(APPLE)
    add_subdirectory(metal)
endif()
