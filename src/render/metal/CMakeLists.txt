# macOS 手写 Metal 后端：MSL 编译 / PSO / texture / present。

target_sources(chroma_render PRIVATE
    MetalRenderer.hpp
    MetalRendererImpl.hpp
    MetalRenderer.cpp
    MetalRenderer.mm)

target_compile_options(chroma_render PRIVATE
    "$<$<COMPILE_LANGUAGE:OBJCXX>:-fobjc-arc>")
