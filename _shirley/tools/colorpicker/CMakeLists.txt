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
    src/Capture.hpp
    src/WgcCapture.cpp
    src/WgcCapture.hpp
    src/HdrSwatch.cpp
    src/HdrSwatch.hpp)

target_link_libraries(colorpicker PRIVATE
    Qt6::Widgets
    d3d11
    dxgi
    user32
    gdi32)

# WGC（Windows.Graphics.Capture）依赖 Windows SDK 自带的 C++/WinRT 头；
# 仅 MSVC 构建启用；MinGW 构建保持 Duplication 后端。
if(MSVC)
    file(GLOB _chroma_cppwinrt_dirs
        "C:/Program Files (x86)/Windows Kits/10/Include/*/cppwinrt")
    if(_chroma_cppwinrt_dirs)
        list(SORT _chroma_cppwinrt_dirs)
        list(GET _chroma_cppwinrt_dirs -1 _chroma_cppwinrt_dir)
        if(EXISTS "${_chroma_cppwinrt_dir}/winrt/Windows.Graphics.Capture.h")
            target_include_directories(colorpicker PRIVATE "${_chroma_cppwinrt_dir}")
            target_compile_definitions(colorpicker PRIVATE CHROMA_HAS_WGC=1)
            target_link_libraries(colorpicker PRIVATE windowsapp)
            message(STATUS "colorpicker: WGC enabled (${_chroma_cppwinrt_dir})")
        endif()
    endif()
endif()

target_compile_definitions(colorpicker PRIVATE
    NOMINMAX
    WIN32_LEAN_AND_MEAN)

if(MSVC)
    target_compile_options(colorpicker PRIVATE /utf-8 /W4)
endif()
