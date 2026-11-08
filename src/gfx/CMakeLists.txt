# 平台层。目前只有头文件，是 INTERFACE 库，不产生 .lib / .a 是正常的。
add_library(chroma_gfx INTERFACE)

target_include_directories(chroma_gfx INTERFACE "${PROJECT_SOURCE_DIR}/src")
target_compile_features(chroma_gfx INTERFACE cxx_std_20)

if(WIN32)
    target_link_libraries(chroma_gfx INTERFACE d3d12 dxgi)
elseif(APPLE)
    # .mm 里会用到的系统框架：NSView/NSWindow、CAMetalLayer、MTLDevice、CGColorSpace。
    target_link_libraries(chroma_gfx INTERFACE
        "-framework Foundation"
        "-framework AppKit"
        "-framework Metal"
        "-framework QuartzCore"
        "-framework CoreGraphics")
endif()
