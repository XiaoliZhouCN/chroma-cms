qt_add_executable(ChromaCMS
    main.cpp
    MainWindow.hpp
    MainWindow.cpp
    ViewportWidget.hpp
    ViewportWidget.cpp
    )

target_link_libraries(ChromaCMS PRIVATE
    Qt6::Widgets
    chroma_render
    chroma_media
    chroma_core)

chroma_set_warnings(ChromaCMS)

# 第一步保留控制台窗口：日志直接可见，第二步接渲染时非常关键。
set_target_properties(ChromaCMS PROPERTIES OUTPUT_NAME "ChromaCMS")
target_compile_definitions(ChromaCMS PRIVATE CHROMA_SOURCE_DIR="${PROJECT_SOURCE_DIR}")

# 让 shader 文件成为链接依赖：只改 shader 也会触发重新链接，
# 从而让下面的 POST_BUILD 拷贝执行；否则 .app 里会一直是旧 shader。
set_property(TARGET ChromaCMS APPEND PROPERTY LINK_DEPENDS
    "${PROJECT_SOURCE_DIR}/shaders/fullscreen.hlsl"
    "${PROJECT_SOURCE_DIR}/shaders/fullscreen.metal"
    "${PROJECT_SOURCE_DIR}/shaders/fullscreen.sksl")

if(APPLE)
    # macOS 生成标准 .app，并把产物放到 build/<preset>/ 根目录，
    # 与 docs/handoff/05-macos-setup.md 里的 open .../ChromaCMS.app 保持一致。
    set_target_properties(ChromaCMS PROPERTIES
        MACOSX_BUNDLE TRUE
        MACOSX_BUNDLE_GUI_IDENTIFIER "com.chromacms.practice"
        MACOSX_BUNDLE_BUNDLE_VERSION "${PROJECT_VERSION}"
        MACOSX_BUNDLE_SHORT_VERSION_STRING "${PROJECT_VERSION}"
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}")
endif()

# 着色器在运行时编译（Windows 用 D3DCompile，macOS 用 Metal 运行时编译器），
# 必须随可执行文件发布。macOS 的 .app 里会落到 Contents/MacOS/shaders。
add_custom_command(TARGET ChromaCMS POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
            "${PROJECT_SOURCE_DIR}/shaders"
            "$<TARGET_FILE_DIR:ChromaCMS>/shaders"
    COMMENT "复制 shaders 到输出目录")

# FFmpeg 以 DLL 形式分发（LGPL shared），仅 Windows 需要随可执行文件发布。
# macOS 用 Homebrew 的 .dylib，由 dyld 正常解析，不需要拷贝。
if(WIN32 AND DEFINED FFMPEG_DLL_DIR AND EXISTS "${FFMPEG_DLL_DIR}")
    file(GLOB _chroma_ffmpeg_dlls "${FFMPEG_DLL_DIR}/*.dll")
    add_custom_command(TARGET ChromaCMS POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different ${_chroma_ffmpeg_dlls}
                "$<TARGET_FILE_DIR:ChromaCMS>"
        COMMENT "复制 FFmpeg DLL 到输出目录")
endif()

# 把 Qt 运行时 DLL 拷到输出目录，保证双击和 ctest 都能直接启动。
if(CHROMA_QT_ROOT AND EXISTS "${CHROMA_QT_ROOT}/bin/windeployqt.exe")
    add_custom_command(TARGET ChromaCMS POST_BUILD
        COMMAND "${CHROMA_QT_ROOT}/bin/windeployqt.exe"
                --no-translations
                --no-system-d3d-compiler
                --no-opengl-sw
                --no-compiler-runtime
                --verbose 0
                "$<TARGET_FILE:ChromaCMS>"
        COMMENT "windeployqt: 部署 Qt 运行时")
endif()
