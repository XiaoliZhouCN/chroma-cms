// M0 占位用例：验证「单元测试可执行文件 + ctest 注册」这条链路是通的。
// M2 起由 src/color 的传输函数与矩阵测试取代，见 Docs/TESTING.md §2。

#include "core/Version.h"

#include <cstdio>

int main()
{
    if (chroma::kVersion.empty()) {
        std::fprintf(stderr, "unit.smoke: kVersion 为空\n");
        return 1;
    }

    std::printf("unit.smoke ok (version %.*s)\n",
                static_cast<int>(chroma::kVersion.size()),
                chroma::kVersion.data());
    return 0;
}
