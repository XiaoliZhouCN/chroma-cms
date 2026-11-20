#include "core/Platform.hpp"
#include "core/Types.hpp"
#include "ui/MainWindow.hpp"

#include <QApplication>

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <thread>
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

void printOptionalDouble(const char *key, const std::optional<double> &value, bool last = false)
{
    if (value) {
        std::printf("  \"%s\": %.1f%s\n", key, *value, last ? "" : ",");
    }
    else {
        std::printf("  \"%s\": null%s\n", key, last ? "" : ",");
    }
}

int runList()
{
    const std::unique_ptr<cp::IDisplayProbe> probe = cp::createDisplayProbe();
    const std::vector<cp::MonitorInfo> monitors = probe->enumerate();
    for (const cp::MonitorInfo &monitor : monitors) {
        std::unique_ptr<cp::ICaptureSession> capture = cp::createCaptureSession();
        const bool ok = capture->initialize(monitor, 0);
        if (ok) {
            capture->captureFrame();
        }
        const cp::DisplayInfo &display = capture->display();
        const cp::CaptureFormat format = capture->format();
        std::printf("monitor=%s rect=(%d,%d)-(%d,%d) init=%s backend=%s format=%s hdr=%s "
                    "color_space=%s advanced_supported=%s advanced_enabled=%s "
                    "wide_enforced=%s force_disabled=%s encoding=%s cfg_bits=%u "
                    "desc1_bits=%u sdr_white=%.1f max_lum=%.1f name=%s adapter=%s\n",
                    monitor.id.c_str(), monitor.bounds.left, monitor.bounds.top,
                    monitor.bounds.right, monitor.bounds.bottom, ok ? "ok" : "fail",
                    capture->backendName(), format.name.c_str(), display.hdr ? "on" : "off",
                    display.colorSpace.c_str(),
                    display.advancedColorSupported ? "on" : "off",
                    display.advancedColorEnabled ? "on" : "off",
                    display.wideColorEnforced ? "on" : "off",
                    display.advancedColorForceDisabled ? "on" : "off",
                    display.colorEncoding.c_str(), display.configBitsPerColor,
                    display.bitsPerColor, display.sdrWhiteNits.value_or(0.0),
                    display.maxLuminance.value_or(0.0), display.friendlyName.c_str(),
                    display.adapterName.c_str());
        capture->release();
    }
    return 0;
}

int runDump(const Options &options)
{
    cp::prepareProcessForDump();

    const std::unique_ptr<cp::IDisplayProbe> probe = cp::createDisplayProbe();
    cp::PointI cursor = probe->cursorPosition();
    if (options.hasPoint) {
        cursor.x = options.x;
        cursor.y = options.y;
    }

    const cp::MonitorInfo monitor = probe->monitorAt(cursor);
    if (monitor.id.empty()) {
        std::fprintf(stderr, "找不到显示器（坐标 %d,%d）\n", cursor.x, cursor.y);
        return 1;
    }

    std::unique_ptr<cp::ICaptureSession> capture = cp::createCaptureSession();
    if (!capture->initialize(monitor, 0)) {
        std::fprintf(stderr, "捕获初始化失败（monitor=%s）\n", monitor.id.c_str());
        return 1;
    }
    capture->captureFrame();

    const int localX = cursor.x - monitor.bounds.left;
    const int localY = cursor.y - monitor.bounds.top;
    cp::ColorSample sample;
    for (int attempt = 0; attempt < 40; ++attempt) {
        sample = capture->sampleRect(localX, localY, 1, 1);
        if (sample.ok && (sample.rgba[0] + sample.rgba[1] + sample.rgba[2]) > 0.01f) {
            break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
        capture->captureFrame();
    }
    if (!sample.ok) {
        std::fprintf(stderr, "采样失败（frame=%s，local=%d,%d）\n",
                     capture->format().name.c_str(), localX, localY);
        return 1;
    }

    const cp::DisplayInfo &display = capture->display();
    std::printf("{\n");
    std::printf("  \"backend\": \"%s\",\n", capture->backendName());
    std::printf("  \"capture_format\": \"%s\",\n", sample.format.name.c_str());
    std::printf("  \"hdr\": %s,\n", display.hdr ? "true" : "false");
    std::printf("  \"color_space\": \"%s\",\n", display.colorSpace.c_str());
    std::printf("  \"advanced_color_supported\": %s,\n",
                display.advancedColorSupported ? "true" : "false");
    std::printf("  \"advanced_color_enabled\": %s,\n",
                display.advancedColorEnabled ? "true" : "false");
    std::printf("  \"wide_color_enforced\": %s,\n",
                display.wideColorEnforced ? "true" : "false");
    std::printf("  \"advanced_color_force_disabled\": %s,\n",
                display.advancedColorForceDisabled ? "true" : "false");
    std::printf("  \"color_encoding\": \"%s\",\n", display.colorEncoding.c_str());
    std::printf("  \"config_bits_per_color\": %u,\n", display.configBitsPerColor);
    std::printf("  \"remote_session\": %s,\n", display.remoteSession ? "true" : "false");
    std::printf("  \"adapter\": \"%s\",\n", display.adapterName.c_str());
    if (display.sdrWhiteLevelRaw) {
        std::printf("  \"sdr_white_level\": %u,\n", *display.sdrWhiteLevelRaw);
    }
    else {
        std::printf("  \"sdr_white_level\": null,\n");
    }
    printOptionalDouble("sdr_white_nits", display.sdrWhiteNits);
    printOptionalDouble("max_luminance_nits", display.maxLuminance);
    std::printf("  \"bits_per_color\": %u,\n", display.bitsPerColor);
    std::printf("  \"monitor\": \"%s\",\n", display.gdiName.c_str());
    std::printf("  \"screen_x\": %d,\n", cursor.x);
    std::printf("  \"screen_y\": %d,\n", cursor.y);
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
