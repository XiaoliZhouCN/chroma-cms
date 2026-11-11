# Windows 手写 D3D12 后端：根签名 / PSO / HLSL 编译 / SRV / Draw。

target_sources(chroma_render PRIVATE
    D3D12Renderer.hpp
    D3D12Renderer.cpp)

target_link_libraries(chroma_render PUBLIC d3dcompiler)
