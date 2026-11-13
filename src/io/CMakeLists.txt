# 容器/元数据探测（文件层，不做解码、不做色彩转换）。
#
# 纯 C++：不依赖 Qt / gfx / render / media / FFmpeg。
# 目标：回答"这个文件是什么容器、有没有帧/页、色彩元数据在哪里"。

add_library(chroma_io STATIC
    FileProbe.hpp
    FileProbe.cpp)

target_include_directories(chroma_io PUBLIC "${PROJECT_SOURCE_DIR}/src")
target_compile_features(chroma_io PUBLIC cxx_std_20)

chroma_set_warnings(chroma_io)
