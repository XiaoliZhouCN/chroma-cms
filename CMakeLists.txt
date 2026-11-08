cmake_minimum_required(VERSION 3.28)

project(ChromaCMS
    VERSION 0.1.0
    DESCRIPTION "ChromaCMS practice: hand-written D3D12 / Metal renderer"
    LANGUAGES CXX)

# macOS 的 Metal 后端用 Objective-C++（.mm）写，必须显式启用这个语言。
# Windows 侧不启用，避免 MSVC 生成器去找不存在的 OBJCXX 编译器。
if(APPLE)
    enable_language(OBJCXX)
    set(CMAKE_OBJCXX_STANDARD 20)
    set(CMAKE_OBJCXX_STANDARD_REQUIRED ON)
    set(CMAKE_OBJCXX_EXTENSIONS OFF)
endif()

# ---- 全局设置 ----
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# ---- 依赖 ----
include(cmake/QtBootstrap.cmake)
include(cmake/CompilerWarnings.cmake)

find_package(Qt6 6.8 REQUIRED COMPONENTS Core Gui Widgets)
qt_standard_project_setup()   # 打开 AUTOMOC/AUTOUIC，并设好 C++ 标准

message(STATUS "ChromaCMS: Qt ${Qt6_VERSION} @ ${QT_ROOT}")

# ---- 目标 ----
include(CTest)
add_subdirectory(src)

if(BUILD_TESTING)
    add_subdirectory(tests)
endif()