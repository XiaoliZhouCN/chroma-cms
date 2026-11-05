#pragma once

// D3D12 公共头：只放类型别名与最小工具，不放业务语义。
// 全项目唯一允许直接 include <d3d12.h> / <dxgi*.h> 的地方就是这一层。

#include <windows.h>

#include <d3d12.h>
#include <dxgi1_6.h>
#include <wrl/client.h>

#include <cstdio>
#include <stdexcept>

namespace chroma::gfx {

template <typename T>
using ComPtr = Microsoft::WRL::ComPtr<T>;

/// 把 HRESULT 失败转成带上下文的异常。只允许在初始化 / 呈现路径抛，由 ViewportWidget 兜住。
inline void ThrowIfFailed(HRESULT hr, const char* what)
{
    if (SUCCEEDED(hr)) {
        return;
    }
    char buffer[256] = {};
    std::snprintf(buffer, sizeof(buffer), "%s failed (hr=0x%08lX)", what,
                  static_cast<unsigned long>(hr));
    throw std::runtime_error(buffer);
}

} // namespace chroma::gfx