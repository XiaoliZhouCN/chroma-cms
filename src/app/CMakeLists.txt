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
    chroma_core)

chroma_set_warnings(ChromaCMS)

# 第一步保留控制台窗口：日志直接可见，第二步接渲染时非常关键。
set_target_properties(ChromaCMS PROPERTIES OUTPUT_NAME "ChromaCMS")

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
