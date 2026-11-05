# 定位 Qt6。
#
# 优先使用显式传入的 -DQT_ROOT=<path>。
# 否则扫描常见安装位置，取版本号最大的一个（Natural 排序，6.11 > 6.9）。
#
# 规则来源：Docs/BUILD.md §3.2。本机路径不写进 CMakePresets.json。

if(NOT QT_ROOT)
    set(_chroma_qt_globs
        "C:/Qt/6.*/msvc*_64"
        "D:/Qt/6.*/msvc*_64"
        "D:/Applications/QtCreator/6.*/msvc*_64")

    set(_chroma_qt_candidates "")
    foreach(_glob IN LISTS _chroma_qt_globs)
        file(GLOB _hits "${_glob}")
        list(APPEND _chroma_qt_candidates ${_hits})
    endforeach()

    if(_chroma_qt_candidates)
        list(SORT _chroma_qt_candidates COMPARE NATURAL ORDER ASCENDING)
        list(GET _chroma_qt_candidates -1 QT_ROOT)
    endif()
endif()

if(QT_ROOT)
    list(PREPEND CMAKE_PREFIX_PATH "${QT_ROOT}")
    message(STATUS "ChromaCMS: QT_ROOT = ${QT_ROOT}")
else()
    message(WARNING "ChromaCMS: 未找到 Qt6 安装。请显式传入 -DQT_ROOT=<Qt msvc*_64 目录>")
endif()

# 供 src/app 做 windeployqt 使用
set(CHROMA_QT_ROOT "${QT_ROOT}" CACHE INTERNAL "Resolved Qt root")
