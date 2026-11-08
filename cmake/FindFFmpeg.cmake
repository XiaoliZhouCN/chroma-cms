# 定位 FFmpeg 开发包，提供 imported target FFmpeg::FFmpeg。
#
# 搜索顺序：
#   1. -DFFMPEG_ROOT=<dir>
#   2. <repo>/external/ffmpeg      （Windows: tools/fetch-ffmpeg.ps1 的安装位置）
#   3. Homebrew 的 ffmpeg          （macOS: brew install ffmpeg）
#
# 期望布局（Windows）：
#   <root>/include/libavcodec/avcodec.h
#   <root>/lib/avcodec.lib          （MSVC 导入库）
#   <root>/bin/avcodec-*.dll        （运行时，由 app 的 POST_BUILD 拷贝）
# macOS 布局相同，只是库是 .dylib，且没有 DLL 需要拷贝。

set(FFMPEG_ROOT "" CACHE PATH "FFmpeg 开发包根目录")

if(NOT FFMPEG_ROOT)
    set(_chroma_ffmpeg_candidates
        "${PROJECT_SOURCE_DIR}/external/ffmpeg"
        "${PROJECT_SOURCE_DIR}/third_party/ffmpeg")

    if(APPLE)
        list(APPEND _chroma_ffmpeg_candidates
            "/opt/homebrew/opt/ffmpeg"
            "/usr/local/opt/ffmpeg")
        if(DEFINED ENV{HOMEBREW_PREFIX})
            list(APPEND _chroma_ffmpeg_candidates
                "$ENV{HOMEBREW_PREFIX}/opt/ffmpeg")
        endif()
    endif()

    foreach(_candidate IN LISTS _chroma_ffmpeg_candidates)
        if(EXISTS "${_candidate}/include/libavcodec/avcodec.h")
            set(FFMPEG_ROOT "${_candidate}")
            break()
        endif()
    endforeach()
endif()

if(NOT FFMPEG_ROOT)
    if(APPLE)
        message(FATAL_ERROR
            "未找到 FFmpeg。请先安装（brew install ffmpeg），"
            "或传入 -DFFMPEG_ROOT=$(brew --prefix ffmpeg)")
    else()
        message(FATAL_ERROR
            "未找到 FFmpeg。请先运行 pwsh tools/fetch-ffmpeg.ps1，"
            "或传入 -DFFMPEG_ROOT=<包含 include/ 与 lib/ 的目录>")
    endif()
endif()

foreach(_header IN ITEMS
        "include/libavcodec/avcodec.h"
        "include/libavformat/avformat.h"
        "include/libavutil/avutil.h"
        "include/libavutil/pixfmt.h"
        "include/libswscale/swscale.h")
    if(NOT EXISTS "${FFMPEG_ROOT}/${_header}")
        message(FATAL_ERROR "FFmpeg 头文件缺失: ${FFMPEG_ROOT}/${_header}")
    endif()
endforeach()

set(_ffmpeg_libraries "")
foreach(_name IN ITEMS avformat avcodec avutil swscale)
    find_library(FFMPEG_${_name}_LIBRARY NAMES ${_name}
                 PATHS "${FFMPEG_ROOT}/lib" NO_DEFAULT_PATH)
    if(NOT FFMPEG_${_name}_LIBRARY)
        message(FATAL_ERROR "FFmpeg 导入库缺失: ${_name}")
    endif()
    list(APPEND _ffmpeg_libraries "${FFMPEG_${_name}_LIBRARY}")
endforeach()

# 只有 Windows 需要把 DLL 拷到 exe 旁边；macOS 由 Homebrew 的 dylib 提供。
if(WIN32)
    set(FFMPEG_DLL_DIR "${FFMPEG_ROOT}/bin" CACHE INTERNAL "FFmpeg 运行时 DLL 目录")
endif()

add_library(FFmpeg::FFmpeg INTERFACE IMPORTED)
set_target_properties(FFmpeg::FFmpeg PROPERTIES
    INTERFACE_INCLUDE_DIRECTORIES "${FFMPEG_ROOT}/include"
    INTERFACE_LINK_LIBRARIES "${_ffmpeg_libraries}")

message(STATUS "ChromaCMS: FFmpeg = ${FFMPEG_ROOT}")
