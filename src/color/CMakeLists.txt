# CPU 色彩管理核心（oracle）：纯 C++，不依赖 Qt / GPU / FFmpeg。
#
# 这里定义"什么是正确"：传递函数、primaries/白点、RGB<->XYZ 矩阵、白点适应、输出编码。
# GPU 后端与解码层都以本库为参考，误差用 ΔE 量化（见 docs/testing/00-index.md）。

add_library(chroma_color STATIC
    Types.hpp
    Transfer.hpp
    Transfer.cpp
    Primaries.hpp
    Primaries.cpp
    Matrix.hpp
    Matrix.cpp
    Output.hpp
    Output.cpp)

target_include_directories(chroma_color PUBLIC "${PROJECT_SOURCE_DIR}/src")
target_compile_features(chroma_color PUBLIC cxx_std_20)

chroma_set_warnings(chroma_color)
