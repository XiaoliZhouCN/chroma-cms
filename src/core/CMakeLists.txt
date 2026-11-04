# 无渲染依赖的基础层：版本信息、未来的通用工具。
# 不链接 Qt，不链接 D3D12 —— 保证 core 可以被任何模块与测试使用。

add_library(chroma_core INTERFACE)

target_include_directories(chroma_core INTERFACE "${PROJECT_SOURCE_DIR}/src")
target_compile_features(chroma_core INTERFACE cxx_std_20)

target_compile_definitions(chroma_core INTERFACE
    CHROMA_VERSION="${PROJECT_VERSION}"
    NOMINMAX
    WIN32_LEAN_AND_MEAN
    UNICODE
    _UNICODE)
