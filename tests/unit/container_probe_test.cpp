// 容器/元数据探测：覆盖 tests/corpus 的小样本 + 截断/缺失文件的健壮性。

#include "TestSupport.hpp"

#include "io/FileProbe.hpp"

#include <fstream>
#include <string>
#include <vector>

using namespace chroma::io;
using namespace chroma::testing;

namespace
{
std::vector<std::uint8_t> readAll(const std::string &path)
{
    std::ifstream file(path, std::ios::binary);
    return std::vector<std::uint8_t>((std::istreambuf_iterator<char>(file)),
                                     std::istreambuf_iterator<char>());
}

std::string corpusPath(const char *relative)
{
    return std::string(CHROMA_SOURCE_DIR) + "/tests/corpus/" + relative;
}

void expectKind(const char *name, const FileProbe &probe, FileKind kind)
{
    expectTrue((std::string(name) + ".kind").c_str(), probe.kind == kind);
}

bool containsNote(const FileProbe &probe, const std::string &needle)
{
    for (const std::string &note : probe.notes) {
        if (note.find(needle) != std::string::npos) {
            return true;
        }
    }
    return false;
}
} // namespace

int main()
{
    // ---- PNG：8-bit RGBA、无色彩元数据 ----
    {
        const FileProbe probe = probeFile(corpusPath("png/rgba8-plain.png"));
        expectKind("png.plain", probe, FileKind::Png);
        expectNear("png.plain.width", probe.width, 64, 0);
        expectNear("png.plain.height", probe.height, 64, 0);
        expectNear("png.plain.bits", probe.bitsPerChannel, 8, 0);
        expectTrue("png.plain.pixel", probe.pixelFormatHint == "rgba8");
        expectTrue("png.plain.no_icc", !probe.hasIcc);
        expectTrue("png.plain.default_note", containsNote(probe, "默认假设"));
    }

    // ---- PNG：带 sRGB chunk ----
    {
        const FileProbe probe = probeFile(corpusPath("png/rgb8-srgb.png"));
        expectKind("png.srgb", probe, FileKind::Png);
        expectNear("png.srgb.bits", probe.bitsPerChannel, 8, 0);
        expectTrue("png.srgb.has_srgb", probe.hasSrgbChunk);
        expectTrue("png.srgb.pixel", probe.pixelFormatHint == "rgb8");
    }

    // ---- PNG：16-bit RGBA ----
    {
        const FileProbe probe = probeFile(corpusPath("png/rgba16.png"));
        expectKind("png.rgba16", probe, FileKind::Png);
        expectNear("png.rgba16.bits", probe.bitsPerChannel, 16, 0);
        expectTrue("png.rgba16.pixel", probe.pixelFormatHint == "rgba16");
    }

    // ---- JPEG：无 ICC，默认按 sRGB 解释 ----
    {
        const FileProbe probe = probeFile(corpusPath("jpeg/rgb8-no-icc.jpg"));
        expectKind("jpeg", probe, FileKind::Jpeg);
        expectNear("jpeg.width", probe.width, 64, 0);
        expectNear("jpeg.height", probe.height, 64, 0);
        expectNear("jpeg.bits", probe.bitsPerChannel, 8, 0);
        expectTrue("jpeg.no_icc", !probe.hasIcc);
        expectTrue("jpeg.default_note", containsNote(probe, "默认假设"));
    }

    // ---- TIFF：单页与多页 ----
    {
        const FileProbe single = probeFile(corpusPath("tiff/rgb8-single.tif"));
        expectKind("tiff.single", single, FileKind::Tiff);
        expectNear("tiff.single.width", single.width, 64, 0);
        expectNear("tiff.single.frames", single.frames, 1, 0);

        const FileProbe multi = probeFile(corpusPath("tiff/rgb8-multipage.tif"));
        expectKind("tiff.multi", multi, FileKind::Tiff);
        expectTrue("tiff.multi.pages", multi.frames >= 2);
    }

    // ---- GIF：静态与动画 ----
    {
        const FileProbe still = probeFile(corpusPath("gif/static.gif"));
        expectKind("gif.static", still, FileKind::Gif);
        expectNear("gif.static.width", still.width, 64, 0);

        const FileProbe animated = probeFile(corpusPath("gif/animated.gif"));
        expectKind("gif.animated", animated, FileKind::Gif);
        expectTrue("gif.animated.frames", animated.frames >= 2);
    }

    // ---- WebP ----
    {
        const FileProbe probe = probeFile(corpusPath("webp/rgb8.webp"));
        expectKind("webp", probe, FileKind::WebP);
        expectNear("webp.width", probe.width, 64, 0);
        expectNear("webp.height", probe.height, 64, 0);
    }

    // ---- EXR：half 线性 ----
    {
        const FileProbe probe = probeFile(corpusPath("exr/rgb-half.exr"));
        expectKind("exr", probe, FileKind::Exr);
        expectNear("exr.width", probe.width, 8, 0);
        expectNear("exr.height", probe.height, 8, 0);
        expectNear("exr.bits", probe.bitsPerChannel, 16, 0);
        expectTrue("exr.pixel", probe.pixelFormatHint == "half");
    }

    // ---- 健壮性：截断的 PNG 不能崩溃 ----
    {
        const std::vector<std::uint8_t> bytes = readAll(corpusPath("png/rgba8-plain.png"));
        expectTrue("truncated.nonempty", !bytes.empty());
        const std::size_t keep = std::min<std::size_t>(bytes.size(), 20);
        const FileProbe probe = probeBytes(bytes.data(), keep);
        expectKind("truncated.kind", probe, FileKind::Png);
    }

    // ---- 健壮性：空字节 / 不存在的文件 ----
    {
        const FileProbe empty = probeBytes(nullptr, 0);
        expectKind("empty.kind", empty, FileKind::Unknown);

        const FileProbe missing = probeFile(corpusPath("does-not-exist.png"));
        expectKind("missing.kind", missing, FileKind::Unknown);
        expectTrue("missing.note", containsNote(missing, "无法打开"));
    }

    return finish("unit.container_probe");
}
