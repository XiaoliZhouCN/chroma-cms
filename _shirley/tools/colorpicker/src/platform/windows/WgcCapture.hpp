#pragma once

#include <windows.h>
#include <d3d11.h>
#include <dxgi.h>

#include <cstdint>
#include <memory>
#include <vector>

namespace cp::win
{
/// Windows.Graphics.Capture（WGC）FP16 捕获后端。
///
/// 仅在 MSVC + CHROMA_HAS_WGC 下实现；其它编译器下 initialize() 返回 false，
/// 由调用方回退到 Desktop Duplication。
class WgcCapture
{
public:
    struct Sample
    {
        bool ok = false;
        DXGI_FORMAT format = DXGI_FORMAT_UNKNOWN;
        int x = 0;
        int y = 0;
        int width = 0;
        int height = 0;
        std::vector<float> rgba;
    };

    WgcCapture();
    ~WgcCapture();

    bool initialize(HMONITOR monitor, HWND owner);
    void close();

    bool captureFrame();
    Sample sampleRect(int x, int y, int width, int height);

    DXGI_FORMAT frameFormat() const;
    int width() const;
    int height() const;

private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;
};
} // namespace cp::win
