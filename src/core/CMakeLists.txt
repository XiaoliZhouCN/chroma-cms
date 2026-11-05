# 无平台依赖的基础层。第一步只有版本号，后面放通用工具、CPU 参考实现等。

add_library(chroma_core INTERFACE)

target_include_directories(chroma_core INTERFACE "${PROJECT_SOURCE_DIR}/src")
target_compile_features(chroma_core INTERFACE cxx_std_20)

target_compile_definitions(chroma_core INTERFACE
    CHROMA_VERSION="${PROJECT_VERSION}"
    NOMINMAX
    WIN32_LEAN_AND_MEAN
    UNICODE
    _UNICODE)