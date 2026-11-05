#pragma once

#include <string_view>

namespace chroma {

inline constexpr std::string_view kAppName = "ChromaCMS";

// 由 CMake 通过 chroma_core 的 INTERFACE 定义注入（见 src/core/CMakeLists.txt）。
inline constexpr std::string_view kVersion = CHROMA_VERSION;

} // namespace chroma