#pragma once

// D3D12 公共头：只放类型别名与最小工具，不放业务语义。
// 注意（踩过）：D3D12 的 CreateSwapChainForHwnd 第一个参数必须是 ID3D12CommandQueue，
// 传 ID3D12Device 会返回 DXGI_ERROR_INVALID_CALL(0x887A0001)。见 Docs/BUILD.md §8。

#include <windows.h>

#include <d3d12.h>
#include <dxgi1_6.h>
#include <wrl/client.h>

#include <cstdio>
#include <stdexcept>
#include <string>

namespace chroma::gfx {

template <typename T>
using ComPtr = Microsoft::WRL::ComPtr<T>;

/// 把 HRESULT 失败转成带上下文的异常。渲染初始化路径允许抛异常（见 CONVENTIONS §3）。
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
