# 定位预编译 Skia（Ganesh），提供 imported target Skia::Skia。
#
# 搜索顺序：
#   1. -DSKIA_ROOT=<dir>
#   2. Windows: <repo>/external/skia        （D3D12 后端）
#      macOS:   <repo>/external/skia-mac    （Metal 后端）
#
# 期望布局（预编译包）：
#   <root>/include/...               公共头（include/core、include/gpu/ganesh/...）
#   <root>/modules/skcms/skcms.h     被 SkColorSpace.h 等公共头引用
#   <root>/lib/...                   静态库（Windows: skia.lib；macOS: libskia.a）
#
# 平台约束：
#   * Windows 包是 is_debug=false + /MD（Release），消费方必须用 Release，
#     否则链接期报 LNK2038（_ITERATOR_DEBUG_LEVEL）。
#   * macOS 包用 GN 构建（见 external/skia-mac/args.gn）：arm64 + Metal，
#     skia_use_gl/vulkan/dawn=false，只链接系统框架。
#
# 用法：
#   include(${PROJECT_SOURCE_DIR}/cmake/FindSkia.cmake)
#   target_link_libraries(mytarget PRIVATE Skia::Skia)

if(TARGET Skia::Skia)
    return()
endif()

set(SKIA_ROOT "" CACHE PATH "预编译 Skia 根目录（包含 include/ 与 lib/）")

if(NOT SKIA_ROOT)
    if(APPLE)
        set(_chroma_skia_candidates
            "${PROJECT_SOURCE_DIR}/external/skia-mac"
            "${PROJECT_SOURCE_DIR}/third_party/skia/prebuilt-mac")
    else()
        set(_chroma_skia_candidates
            "${PROJECT_SOURCE_DIR}/external/skia"
            "${PROJECT_SOURCE_DIR}/third_party/skia/prebuilt")
    endif()

    foreach(_candidate IN LISTS _chroma_skia_candidates)
        if(EXISTS "${_candidate}/include/core/SkGraphics.h")
            set(SKIA_ROOT "${_candidate}")
            break()
        endif()
    endforeach()
endif()

if(NOT SKIA_ROOT)
    if(APPLE)
        message(FATAL_ERROR
            "未找到 macOS 预编译 Skia。请把 Metal 版预编译包放到 "
            "<repo>/external/skia-mac（包含 include/ 与 lib/），"
            "或传入 -DSKIA_ROOT=<dir>")
    else()
        message(FATAL_ERROR
            "未找到预编译 Skia。请把预编译包放到 <repo>/external/skia"
            "（包含 include/ 与 lib/），或传入 -DSKIA_ROOT=<dir>")
    endif()
endif()

# 里程碑（Skia 版本号），两端共用。
if(NOT EXISTS "${SKIA_ROOT}/include/core/SkMilestone.h")
    message(FATAL_ERROR "Skia 预编译包不完整，缺少: ${SKIA_ROOT}/include/core/SkMilestone.h")
endif()
file(READ "${SKIA_ROOT}/include/core/SkMilestone.h" _skia_milestone_header)
if(_skia_milestone_header MATCHES "#define SK_MILESTONE ([0-9]+)")
    set(SKIA_MILESTONE "${CMAKE_MATCH_1}")
else()
    set(SKIA_MILESTONE "unknown")
endif()

if(APPLE)
    # ---- macOS：Ganesh Metal ----
    foreach(_required IN ITEMS
            "include/core/SkGraphics.h"
            "include/core/SkSurface.h"
            "include/gpu/ganesh/GrDirectContext.h"
            "include/gpu/ganesh/GrBackendSurface.h"
            "include/gpu/ganesh/SkSurfaceGanesh.h"
            "include/gpu/ganesh/mtl/GrMtlBackendContext.h"
            "include/gpu/ganesh/mtl/GrMtlBackendSurface.h"
            "include/gpu/ganesh/mtl/GrMtlDirectContext.h"
            "include/gpu/ganesh/mtl/GrMtlTypes.h"
            "modules/skcms/skcms.h"
            "lib/libskia.a"
            "lib/libskcms.a")
        if(NOT EXISTS "${SKIA_ROOT}/${_required}")
            message(FATAL_ERROR "Skia 预编译包不完整，缺少: ${SKIA_ROOT}/${_required}")
        endif()
    endforeach()

    add_library(Skia::Skia INTERFACE IMPORTED)
    set_target_properties(Skia::Skia PROPERTIES
        INTERFACE_INCLUDE_DIRECTORIES "${SKIA_ROOT};${SKIA_ROOT}/include"
        INTERFACE_COMPILE_DEFINITIONS "SK_GANESH;SK_METAL"
        INTERFACE_COMPILE_OPTIONS
            "$<$<COMPILE_LANGUAGE:OBJCXX>:-fobjc-arc>")
    set_property(TARGET Skia::Skia PROPERTY INTERFACE_LINK_LIBRARIES
        "${SKIA_ROOT}/lib/libskia.a"
        "${SKIA_ROOT}/lib/libskcms.a"
        "-framework Foundation"
        "-framework AppKit"
        "-framework Metal"
        "-framework MetalKit"
        "-framework QuartzCore"
        "-framework CoreGraphics"
        "-framework CoreText"
        "-framework IOSurface"
        "-framework CoreVideo"
        "-framework CoreFoundation")

    set(SKIA_RELEASE_ONLY ON)
    message(STATUS
        "ChromaCMS: Skia (Metal) = ${SKIA_ROOT} (M${SKIA_MILESTONE}, 仅 Release 配置可用)")
else()
    # ---- Windows：Ganesh D3D12 ----
    foreach(_required IN ITEMS
            "include/core/SkGraphics.h"
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
    message(STATUS
        "ChromaCMS: Skia (D3D12) = ${SKIA_ROOT} (M${SKIA_MILESTONE}, 仅 Release 配置可用)")
endif()
