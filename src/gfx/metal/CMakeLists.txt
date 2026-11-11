# macOS 原生图形上下文：device / queue / CAMetalLayer / drawable / command buffer。

target_sources(chroma_gfx PRIVATE
    MetalContext.hpp
    MetalContext.mm)

target_link_libraries(chroma_gfx PUBLIC
    "-framework Foundation"
    "-framework AppKit"
    "-framework Metal"
    "-framework QuartzCore"
    "-framework CoreGraphics")

target_compile_options(chroma_gfx PRIVATE
    "$<$<COMPILE_LANGUAGE:OBJCXX>:-fobjc-arc>")
