# 项目 CMake 启动：定位 Qt6 + 统一编译告警策略。
#
# Qt 定位规则：优先 -DQT_ROOT=...，否则扫描常见安装位置取版本号最大的一个。
# 规则见 develop:Docs/BUILD.md §3.2。

if(NOT QT_ROOT)
    set(_chroma_qt_globs
        "C:/Qt/6.*/msvc*_64"
        "D:/Qt/6.*/msvc*_64"
        "D:/Applications/QtCreator/6.*/msvc*_64"
        "/opt/homebrew/opt/qt"
        "/opt/homebrew/opt/qtbase"
        "/usr/local/opt/qt"
        "/usr/local/opt/qtbase"
        "$ENV{HOME}/Qt/6.*/macos")

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
    # 没显式设置时不要直接 FATAL：预设可能已经通过 CMAKE_PREFIX_PATH
    # 或 Qt6_DIR 指定了 Qt，交给 find_package(Qt6) 给出最终结论。
    message(STATUS "ChromaCMS: QT_ROOT 未设置，交给 find_package(Qt6) 查找")
endif()

# 供 app 做 windeployqt 使用（仅 Windows 分支会用到）
set(CHROMA_QT_ROOT "${QT_ROOT}" CACHE INTERNAL "Resolved Qt root")

# 统一编译告警策略。/WX 暂不启用：Qt 头文件在 /W4 下仍有少量告警。
function(chroma_set_warnings target)
    if(MSVC)
        target_compile_options(${target} PRIVATE
            /W4
            /permissive-
            /Zc:__cplusplus
            /utf-8          # 必须：否则中文字符串字面量会报“文本后缀无效”
            /EHsc)
    else()
        target_compile_options(${target} PRIVATE -Wall -Wextra -Wpedantic)
    endif()
endfunction()
