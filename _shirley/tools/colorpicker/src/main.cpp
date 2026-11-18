#include "Capture.hpp"
#include "MainWindow.hpp"

#include <QApplication>

#include <windows.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

namespace
{
struct Options
{
    bool dump = false;
    bool list = false;
    bool hasPoint = false;
    int x = 0;
    int y = 0;
};

bool parseArgs(int argc, char **argv, Options &options)
{
    for (int index = 1; index < argc; ++index) {
        const char *arg = argv[index];
        if (std::strcmp(arg, "--dump") == 0) {
            options.dump = true;
        }
        else if (std::strcmp(arg, "--list") == 0) {
            options.list = true;
        }
        else if (std::strcmp(arg, "--x") == 0 && index + 1 < argc) {
            options.x = std::atoi(argv[++index]);
            options.hasPoint = true;
        }
        else if (std::strcmp(arg, "--y") == 0 && index + 1 < argc) {
            options.y = std::atoi(argv[++index]);
            options.hasPoint = true;
        }
        else if (std::strcmp(arg, "--help") == 0) {
            std::printf("用法:\n"
                        "  colorpicker                 打开取色窗口\n"
                        "  colorpicker --dump          取当前鼠标位置并输出 JSON\n"
                        "  colorpicker --dump --x 800 --y 450\n"
                        "  colorpicker --list          列出显示器与捕获状态\n");
            return false;
        }
    }
    return true;
}

int runList()
{
    const std::vector<cp::MonitorInfo> monitors = cp::enumerateMonitors();
    for (const cp::MonitorInfo &monitor : monitors) {
        cp::ScreenCapture capture;
        const bool ok = capture.initialize(monitor.handle);
        if (ok) {
            capture.captureFrame();
        }
        const cp::DisplayState &display = capture.display();
        std::printf("monitor=%ws rect=(%ld,%ld)-(%ld,%ld) init=%s format=%s hdr=%s "
                    "color_space=%s advanced_supported=%s advanced_enabled=%s "
                    "wide_enforced=%s force_disabled=%s encoding=%s cfg_bits=%u "
                    "desc1_bits=%u sdr_white=%.1f max_lum=%.1f name=%ws adapter=%ws\n",
                    monitor.gdiName.c_str(),
                    static_cast<long>(monitor.rect.left), static_cast<long>(monitor.rect.top),
                    static_cast<long>(monitor.rect.right), static_cast<long>(monitor.rect.bottom),
                    ok ? "ok" : "fail",
                    cp::formatToString(capture.frameFormat()).c_str(),
                    display.hdr ? "on" : "off",
                    cp::colorSpaceToString(display.colorSpace).c_str(),
                    display.advancedColorSupported ? "on" : "off",
                    display.advancedColorEnabled ? "on" : "off",
                    display.wideColorEnforced ? "on" : "off",
                    display.advancedColorForceDisabled ? "on" : "off",
                    cp::colorEncodingToString(display.colorEncoding).c_str(),
                    display.configBitsPerColor, display.bitsPerColor,
                    display.sdrWhiteNits, display.maxLuminance,
                    display.friendlyName.c_str(), display.adapterName.c_str());
        capture.release();
    }
    return 0;
}

int runDump(const Options &options)
{
    POINT cursor{};
    if (!GetCursorPos(&cursor)) {
        std::fprintf(stderr, "GetCursorPos 失败\n");
        return 2;
    }
    if (options.hasPoint) {
        cursor.x = options.x;
        cursor.y = options.y;
    }

    const cp::MonitorInfo monitor = cp::monitorFromPoint(cursor);
    cp::ScreenCapture capture;
    if (!capture.initialize(monitor.handle)) {
        std::fprintf(stderr, "捕获初始化失败（monitor=%ws）\n", monitor.gdiName.c_str());
        return 1;
    }
    capture.captureFrame();

    const int localX = cursor.x - monitor.rect.left;
    const int localY = cursor.y - monitor.rect.top;
    cp::ScreenCapture::Sample sample;
    for (int attempt = 0; attempt < 40; ++attempt) {
        sample = capture.sampleRect(localX, localY, 1, 1);
        if (sample.ok && (sample.rgba[0] + sample.rgba[1] + sample.rgba[2]) > 0.01f) {
            break;
        }
        Sleep(50);
        capture.captureFrame();
    }
    if (!sample.ok) {
        std::fprintf(stderr, "采样失败（frame=%s，local=%d,%d）\n",
                     cp::formatToString(capture.frameFormat()).c_str(), localX, localY);
        return 1;
    }

    const cp::DisplayState &display = capture.display();
    std::printf("{\n");
    std::printf("  \"capture_format\": \"%s\",\n",
                cp::formatToString(sample.format).c_str());
    std::printf("  \"hdr\": %s,\n", display.hdr ? "true" : "false");
    std::printf("  \"color_space\": \"%s\",\n",
                cp::colorSpaceToString(display.colorSpace).c_str());
    std::printf("  \"advanced_color_supported\": %s,\n",
                display.advancedColorSupported ? "true" : "false");
    std::printf("  \"advanced_color_enabled\": %s,\n",
                display.advancedColorEnabled ? "true" : "false");
    std::printf("  \"wide_color_enforced\": %s,\n",
                display.wideColorEnforced ? "true" : "false");
    std::printf("  \"advanced_color_force_disabled\": %s,\n",
                display.advancedColorForceDisabled ? "true" : "false");
    std::printf("  \"color_encoding\": \"%s\",\n",
                cp::colorEncodingToString(display.colorEncoding).c_str());
    std::printf("  \"config_bits_per_color\": %u,\n", display.configBitsPerColor);
    std::printf("  \"remote_session\": %s,\n",
                GetSystemMetrics(SM_REMOTESESSION) != 0 ? "true" : "false");
    std::printf("  \"adapter\": \"%ws\",\n", display.adapterName.c_str());
    std::printf("  \"sdr_white_nits\": %.1f,\n", display.sdrWhiteNits);
    std::printf("  \"max_luminance_nits\": %.1f,\n", display.maxLuminance);
    std::printf("  \"bits_per_color\": %u,\n", display.bitsPerColor);
    std::printf("  \"monitor\": \"%ws\",\n", display.gdiName.c_str());
    std::printf("  \"screen_x\": %ld,\n", static_cast<long>(cursor.x));
    std::printf("  \"screen_y\": %ld,\n", static_cast<long>(cursor.y));
    std::printf("  \"r\": %.6f,\n", sample.rgba[0]);
    std::printf("  \"g\": %.6f,\n", sample.rgba[1]);
    std::printf("  \"b\": %.6f,\n", sample.rgba[2]);
    std::printf("  \"a\": %.6f\n", sample.rgba[3]);
    std::printf("}\n");
    return 0;
}
} // namespace

int main(int argc, char **argv)
{
    SetProcessDPIAware(); // 使用物理像素坐标（与 DXGI 捕获尺寸一致）
    Options options;
    if (!parseArgs(argc, argv, options)) {
        return 0;
    }
    if (options.list) {
        return runList();
    }
    if (options.dump) {
        return runDump(options);
    }

    QApplication application(argc, argv);
    QApplication::setApplicationName(QStringLiteral("ChromaColorPicker"));
    cp::MainWindow window;
    window.show();
    return QApplication::exec();
}
