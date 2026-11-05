#include "core/Version.hpp"

#include <cstdio>

int main()
{
    if (chroma::kVersion.empty()) {
        std::fprintf(stderr, "unit.smoke: kVersion 为空\n");
        return 1;
    }
    std::printf("unit.smoke ok (version %.*s)\n",
                static_cast<int>(chroma::kVersion.size()), chroma::kVersion.data());
    return 0;
}