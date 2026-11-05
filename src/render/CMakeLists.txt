add_library(chroma_render STATIC
    IRenderer.h
    D3D12Renderer.h
    D3D12Renderer.cpp)

target_link_libraries(chroma_render PUBLIC
    chroma_gfx
    chroma_media
    d3dcompiler)

chroma_set_warnings(chroma_render)
