# 定位预编译 Skia（Ganesh + D3D12 后端），提供 imported target Skia::Skia。
#
# 搜索顺序：
#   1. -DSKIA_ROOT=<dir>
#   2. <repo>/external/skia         （本仓库约定；gitignore，不随仓库分发）
#
# 期望布局（预编译包）：
#   <root>/include/...               公共头（include/core、include/gpu/ganesh/...）
#   <root>/modules/skcms/skcms.h     被 SkColorSpace.h 等公共头引用
#   <root>/lib/skia.lib + 依赖库     来自 Skia out/ 目录
#
# 重要约束（来自 Skia 的 GN 构建参数）：
#   * 预编译包是 is_debug=false + /MD（动态 CRT，Release），
#     消费方必须使用 **Release** 配置，否则链接期报 LNK2038（_ITERATOR_DEBUG_LEVEL）。
#   * 消费方必须定义 SK_GANESH、SK_DIRECT3D、SK_GL（GN 的 public_defines）。
#   * Skia 模板实例化产生的 .obj 节数可能超过 MSVC 默认上限，需要 /bigobj。
#   * 头文件含非 ASCII 注释，需要 /utf-8。
#
# 用法：
#   include(${PROJECT_SOURCE_DIR}/cmake/FindSkia.cmake)
#   target_link_libraries(mytarget PRIVATE Skia::Skia)

set(SKIA_ROOT "" CACHE PATH "预编译 Skia 根目录（包含 include/ 与 lib/）")

if(NOT SKIA_ROOT)
    set(_chroma_skia_candidates
        "${PROJECT_SOURCE_DIR}/external/skia"
        "${PROJECT_SOURCE_DIR}/third_party/skia/prebuilt")
    foreach(_candidate IN LISTS _chroma_skia_candidates)
        if(EXISTS "${_candidate}/include/core/SkGraphics.h")
            set(SKIA_ROOT "${_candidate}")
            break()
        endif()
    endforeach()
endif()

if(NOT SKIA_ROOT)
    message(FATAL_ERROR
        "未找到预编译 Skia。请把预编译包放到 <repo>/external/skia"
        "（包含 include/ 与 lib/），或传入 -DSKIA_ROOT=<dir>")
endif()

foreach(_required IN ITEMS
        "include/core/SkGraphics.h"
        "include/core/SkMilestone.h"
        "include/core/SkSurface.h"
        "include/gpu/ganesh/GrDirectContext.h"
        "include/gpu/ganesh/d3d/GrD3DBackendContext.h"
        "include/gpu/ganesh/d3d/GrD3DDirectContext.h"
        "modules/skcms/skcms.h"
        "lib/skia.lib")
    if(NOT EXISTS "${SKIA_ROOT}/${_required}")
        message(FATAL_ERROR "Skia 预编译包不完整，缺少: ${SKIA_ROOT}/${_required}")
    endif()
endforeach()

# 里程碑（Skia 版本号）
file(READ "${SKIA_ROOT}/include/core/SkMilestone.h" _skia_milestone_header)
if(_skia_milestone_header MATCHES "#define SK_MILESTONE ([0-9]+)")
    set(SKIA_MILESTONE "${CMAKE_MATCH_1}")
else()
    set(SKIA_MILESTONE "unknown")
endif()

# 依赖库：skia.lib 依赖下方这些静态库；缺一个就会 LNK2019。
set(_chroma_skia_lib_names
    skia
    d3d12allocator
    skcms
    harfbuzz
    expat
    libjpeg
    libjpeg12
    libjpeg16
    libpng
    libwebp
    libwebp_sse41
    wuffs
    zlib
    spirv_cross)

set(_chroma_skia_libs "")
foreach(_name IN LISTS _chroma_skia_lib_names)
    find_library(CHROMA_SKIA_${_name}_LIBRARY
                 NAMES ${_name}
                 PATHS "${SKIA_ROOT}/lib"
                 NO_DEFAULT_PATH)
    if(NOT CHROMA_SKIA_${_name}_LIBRARY)
        message(FATAL_ERROR "Skia 依赖库缺失: ${SKIA_ROOT}/lib/${_name}.lib")
    endif()
    list(APPEND _chroma_skia_libs "${CHROMA_SKIA_${_name}_LIBRARY}")
endforeach()

add_library(Skia::Skia INTERFACE IMPORTED)
set_target_properties(Skia::Skia PROPERTIES
    INTERFACE_INCLUDE_DIRECTORIES "${SKIA_ROOT};${SKIA_ROOT}/include"
    INTERFACE_COMPILE_DEFINITIONS "SK_GANESH;SK_DIRECT3D;SK_GL"
    INTERFACE_LINK_LIBRARIES "${_chroma_skia_libs};d3d12;dxgi;d3dcompiler")

if(MSVC)
    set_property(TARGET Skia::Skia APPEND PROPERTY
        INTERFACE_COMPILE_OPTIONS "/utf-8;/bigobj")
endif()

set(SKIA_RELEASE_ONLY ON)
message(STATUS "ChromaCMS: Skia = ${SKIA_ROOT} (M${SKIA_MILESTONE}, 仅 Release 配置可用)")
