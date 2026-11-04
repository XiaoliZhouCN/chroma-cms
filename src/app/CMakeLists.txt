qt_add_executable(ChromaCMS
    main.cpp
    MainWindow.h
    MainWindow.cpp
    ViewportWidget.h
    ViewportWidget.cpp)

target_link_libraries(ChromaCMS PRIVATE
    Qt6::Widgets
    chroma_core)

chroma_set_warnings(ChromaCMS)

# M0–M1 保留控制台窗口：日志（qInfo / qWarning）直接可见，便于调试。
# M6 之前改为 WIN32 子系统并接入文件日志。见 Docs/BUILD.md §7。
set_target_properties(ChromaCMS PROPERTIES
    OUTPUT_NAME "ChromaCMS")

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
