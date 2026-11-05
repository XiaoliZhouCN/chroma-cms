include(${PROJECT_SOURCE_DIR}/cmake/FindFFmpeg.cmake)

add_library(chroma_media STATIC
    ImageData.h
    ImageLoader.h
    ImageLoader.cpp)

target_link_libraries(chroma_media PUBLIC
    chroma_gfx
    FFmpeg::FFmpeg)

chroma_set_warnings(chroma_media)
