cmake_minimum_required(VERSION 3.24)

project(chroma_colorpicker LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)

# Qt6：通过 -DCMAKE_PREFIX_PATH=<Qt6 msvc 目录> 指定。
find_package(Qt6 REQUIRED COMPONENTS Widgets)

add_executable(colorpicker
    src/main.cpp
    src/MainWindow.cpp
    src/MainWindow.hpp
    src/Capture.cpp
    src/Capture.hpp)

target_link_libraries(colorpicker PRIVATE
    Qt6::Widgets
    d3d11
    dxgi
    user32
    gdi32)

target_compile_definitions(colorpicker PRIVATE
    NOMINMAX
    WIN32_LEAN_AND_MEAN)

if(MSVC)
    target_compile_options(colorpicker PRIVATE /utf-8 /W4)
endif()
