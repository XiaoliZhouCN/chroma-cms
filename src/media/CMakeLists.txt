# 解码层：FFmpeg 负责把文件解成 RGBA16，不依赖 Qt / D3D12。
# 见 Docs（develop 分支）adr/0008-unified-ffmpeg-decoding.md

include(${PROJECT_SOURCE_DIR}/cmake/FindFFmpeg.cmake)

add_library(chroma_media STATIC
    ImageLoader.hpp
    ImageLoader.cpp)

target_include_directories(chroma_media PUBLIC "${PROJECT_SOURCE_DIR}/src")
target_compile_features(chroma_media PUBLIC cxx_std_20)
target_link_libraries(chroma_media PUBLIC FFmpeg::FFmpeg)

chroma_set_warnings(chroma_media)
