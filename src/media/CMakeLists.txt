add_library(chroma_media STATIC
    ImageSource.h
    ImageSource.cpp)

target_link_libraries(chroma_media PUBLIC
    chroma_gfx
    windowscodecs
    ole32)

chroma_set_warnings(chroma_media)
