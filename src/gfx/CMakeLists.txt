# 平台层：只有头文件（D3D12Common.h），负责把 D3D12/DXGI 的公共依赖收敛到一处。
# 不产生编译产物，因此目录里没有 .cpp 是正常的。

add_library(chroma_gfx INTERFACE)

target_include_directories(chroma_gfx INTERFACE "${PROJECT_SOURCE_DIR}/src")
target_compile_features(chroma_gfx INTERFACE cxx_std_20)
target_link_libraries(chroma_gfx INTERFACE d3d12 dxgi)
