cmake_minimum_required(VERSION 3.28)

project(ChromaCMS
    VERSION 0.1.0
    DESCRIPTION "Windows image/video viewer: native D3D12 + Skia(D3D12), hand-written colour management"
    LANGUAGES CXX)

# ---------------------------------------------------------------------------
# 全局设置
# ---------------------------------------------------------------------------
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Skia 后端不进入 M0–M2 关键路径。默认关闭，M3 打开。
# 见 Docs/ROADMAP.md 与 Docs/adr/0006-cmake-vcpkg-source-built-skia.md。
option(CHROMA_WITH_SKIA "构建 Skia(D3D12) 渲染后端（M3 起启用）" OFF)

# ---------------------------------------------------------------------------
# 依赖
# ---------------------------------------------------------------------------
include(cmake/QtBootstrap.cmake)
include(cmake/CompilerWarnings.cmake)

find_package(Qt6 6.8 REQUIRED COMPONENTS Core Gui Widgets)
qt_standard_project_setup()

message(STATUS "ChromaCMS: Qt ${Qt6_VERSION} @ ${QT_ROOT}")
if(CHROMA_WITH_SKIA)
    message(STATUS "ChromaCMS: Skia backend = ENABLED (skeleton only)")
else()
    message(STATUS "ChromaCMS: Skia backend = disabled (default until M3)")
endif()

# ---------------------------------------------------------------------------
# 目标
# ---------------------------------------------------------------------------
include(CTest)

add_subdirectory(src)
add_subdirectory(tests)
