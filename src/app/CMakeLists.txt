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

# 着色器在运行时由 D3Compile 从文件加载，必须随可执行文件发布。
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
