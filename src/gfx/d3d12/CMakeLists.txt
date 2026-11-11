# Windows 原生图形上下文：设备 / 队列 / 交换链 / RTV / 命令录制 / fence。

target_sources(chroma_gfx PRIVATE
    D3D12Context.hpp
    D3D12Context.cpp)

target_link_libraries(chroma_gfx PUBLIC d3d12 dxgi)
target_compile_definitions(chroma_gfx PUBLIC
    NOMINMAX WIN32_LEAN_AND_MEAN UNICODE _UNICODE)
