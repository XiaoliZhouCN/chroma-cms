cmake_minimum_required(VERSION 3.24)

if(APPLE)
    set(CMAKE_OSX_DEPLOYMENT_TARGET "13.0" CACHE STRING "Minimum supported macOS version")
endif()

project(chroma_colorpicker LANGUAGES CXX)

if(APPLE)
    enable_language(OBJCXX)
endif()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)

# Qt6：通过 -DCMAKE_PREFIX_PATH=<Qt6 目录> 指定。
find_package(Qt6 REQUIRED COMPONENTS Widgets)

set(CHROMA_CORE_SOURCES
    src/core/Types.hpp
    src/core/CaptureSession.hpp
    src/core/DisplayProbe.hpp
    src/core/Platform.hpp)

set(CHROMA_UI_SOURCES
    src/ui/MainWindow.cpp
    src/ui/MainWindow.hpp
    src/ui/HdrPreview.hpp)

set(CHROMA_APP_SOURCES
    src/app/main.cpp)

add_executable(colorpicker
    ${CHROMA_CORE_SOURCES}
    ${CHROMA_UI_SOURCES}
    ${CHROMA_APP_SOURCES})

target_include_directories(colorpicker PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${CMAKE_CURRENT_SOURCE_DIR}/src/core
    ${CMAKE_CURRENT_SOURCE_DIR}/src/ui
    ${CMAKE_CURRENT_SOURCE_DIR}/src/app)

target_link_libraries(colorpicker PRIVATE Qt6::Widgets)

if(WIN32)
    target_sources(colorpicker PRIVATE
        src/platform/windows/Capture.cpp
        src/platform/windows/Capture.hpp
        src/platform/windows/WgcCapture.cpp
        src/platform/windows/WgcCapture.hpp
        src/platform/windows/HdrSwatch.cpp
        src/platform/windows/HdrSwatch.hpp
        src/platform/windows/WindowsConversions.cpp
        src/platform/windows/WindowsConversions.hpp
        src/platform/windows/WindowsDisplayProbe.cpp
        src/platform/windows/WindowsDisplayProbe.hpp
        src/platform/windows/WindowsCaptureSession.cpp
        src/platform/windows/WindowsCaptureSession.hpp
        src/platform/windows/WindowsHdrPreview.cpp
        src/platform/windows/WindowsHdrPreview.hpp
        src/platform/windows/WindowsPlatform.cpp)

    target_include_directories(colorpicker PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src/platform/windows)

    target_link_libraries(colorpicker PRIVATE
        d3d11
        dxgi
        user32
        gdi32)

    target_compile_definitions(colorpicker PRIVATE
        NOMINMAX
        WIN32_LEAN_AND_MEAN)

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
elseif(APPLE)
    target_sources(colorpicker PRIVATE
        src/platform/macos/MacPlatform.mm
        src/platform/macos/MacDisplayProbe.hpp
        src/platform/macos/MacDisplayProbe.mm
        src/platform/macos/MacCaptureSession.hpp
        src/platform/macos/MacCaptureSession.mm
        src/platform/macos/MacHdrPreview.cpp)

    target_include_directories(colorpicker PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src/platform/macos)

    target_link_libraries(colorpicker PRIVATE
        "-framework AppKit"
        "-framework Foundation"
        "-framework CoreGraphics"
        "-framework CoreMedia"
        "-framework CoreVideo"
        "-framework ApplicationServices"
        "-framework ScreenCaptureKit"
        "-framework ColorSync")

    set_target_properties(colorpicker PROPERTIES
        MACOSX_BUNDLE TRUE
        MACOSX_BUNDLE_BUNDLE_NAME "ChromaColorPicker")

    message(STATUS "colorpicker: macOS ScreenCaptureKit (SDR + HDR stream)")
else()
    message(FATAL_ERROR "colorpicker: unsupported platform")
endif()

if(MSVC)
    target_compile_options(colorpicker PRIVATE /utf-8 /W4)
elseif(APPLE)
    target_compile_options(colorpicker PRIVATE -Wall -Wextra)
endif()
