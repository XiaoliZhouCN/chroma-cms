add_library(chroma_render STATIC
    IRenderer.hpp
    D3D12Renderer.hpp
    D3D12Renderer.cpp)

target_link_libraries(chroma_render PUBLIC
    chroma_gfx)

chroma_set_warnings(chroma_render)