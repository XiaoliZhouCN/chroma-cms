#include "io/FileProbe.hpp"

#include <cstdio>
#include <string>

int main(int argc, char **argv)
{
    if (argc != 2) {
        std::fprintf(stderr, "用法: chroma-probe <file>\n");
        return 2;
    }

    const chroma::io::FileProbe probe = chroma::io::probeFile(argv[1]);
    if (probe.kind == chroma::io::FileKind::Unknown) {
        std::fprintf(stderr, "chroma-probe: 未识别的文件: %s\n", argv[1]);
        for (const std::string &note : probe.notes) {
            std::fprintf(stderr, "  note: %s\n", note.c_str());
        }
        return 1;
    }

    const std::string json = chroma::io::toJson(probe);
    std::fwrite(json.data(), 1, json.size(), stdout);
    return 0;
}
