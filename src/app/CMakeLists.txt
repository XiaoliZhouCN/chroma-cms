qt_add_executable(ChromaCMS
    main.cpp
    MainWindow.h
    MainWindow.cpp
    ViewportWidget.h
    ViewportWidget.cpp)

target_link_libraries(ChromaCMS PRIVATE
    Qt6::Widgets
    chroma_render
    chroma_core)

chroma_set_warnings(ChromaCMS)

# 默认测试图放在源码树里，开发期直接用绝对路径定位（本机开发工具，可接受）。
# 换机器时需要重新 configure。
target_compile_definitions(ChromaCMS PRIVATE CHROMA_SOURCE_DIR="${PROJECT_SOURCE_DIR}")

# M0–M1 保留控制台窗口：日志（qInfo / qWarning）直接可见，便于调试。
# M6 之前改为 WIN32 子系统并接入文件日志。见 Docs/BUILD.md §6。
set_target_properties(ChromaCMS PROPERTIES
    OUTPUT_NAME "ChromaCMS")

# 着色器在运行时由 D3DCompile 从文件加载，必须随可执行文件发布。
add_custom_command(TARGET ChromaCMS POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
            "${PROJECT_SOURCE_DIR}/shaders"
            "$<TARGET_FILE_DIR:ChromaCMS>/shaders"
    COMMENT "复制 shaders 到输出目录")

# FFmpeg 以 DLL 形式分发（LGPL shared），必须随可执行文件发布。
if(DEFINED FFMPEG_DLL_DIR AND EXISTS "${FFMPEG_DLL_DIR}")
    file(GLOB _chroma_ffmpeg_dlls "${FFMPEG_DLL_DIR}/*.dll")
    add_custom_command(TARGET ChromaCMS POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different ${_chroma_ffmpeg_dlls}
                "$<TARGET_FILE_DIR:ChromaCMS>"
        COMMENT "复制 FFmpeg DLL 到输出目录")
endif()

# 把 Qt 运行时 DLL 拷到输出目录，保证 ctest 与直接双击都能启动。
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
