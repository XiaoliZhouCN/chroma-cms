---
status: active
owner: XiaoliZhouCN
updated: 2026-11-04
---

# 工程约定

> 规则要可执行。本文件只写**能被检查或被一眼判定**的规则；需要论证的选择写在 ADR 里。

## 1. 工具链

| 项 | 取值 | 备注 |
|---|---|---|
| 语言标准 | C++20 | 不用 C++23 特性（MSVC 支持不稳） |
| 编译器 | MSVC 19.5x（VS 2026 18.8） | 选项 `/W4 /permissive- /Zc:__cplusplus /utf-8`；`/WX` 待收敛后启用 |
| 构建 | CMake 4.2 + CMake Presets（生成器 `Visual Studio 18 2026`） | 见 [BUILD.md](BUILD.md) |
| 依赖 | 暂不引入包管理器；M4 引入 FFmpeg 时再定 | 不手拷 DLL |
| 平台 | Windows x64 only | 显式拒绝其它平台 |

## 2. 目录与文件命名

| 类别 | 规则 | 例 |
|---|---|---|
| C++ 源文件 | `PascalCase.h` / `PascalCase.cpp`，与主类同名 | `D3D12Renderer.cpp` |
| 着色器 | `kebab_case.hlsl` / `kebab_case.sksl`，两套同名 | `yuv-to-rgb.hlsl` ↔ `yuv-to-rgb.sksl` |
| 文档（入口） | `UPPER_SNAKE.md` | `COLOR_SPEC.md` |
| ADR | `NNNN-kebab-case.md`，四位连续编号 | `0003-dual-render-backend.md` |
| 目录 | 全小写 kebab-case | `src/render/`、`Docs/adr/` |

禁止：文件名含空格、非 ASCII 字符、大写目录名。

## 3. C++ 规则

1. **所有权**：COM 用 `Microsoft::WRL::ComPtr`；其余用 `std::unique_ptr`；禁止裸 `new/delete`。
2. **命名**：类型 `PascalCase`；函数与变量 `camelCase`；成员变量 `m_camelCase`；常量 `kPascalCase`；命名空间 `chroma::render` 等。
3. **头文件**：`#pragma once`；头文件必须自包含；前向声明优先于 include。
4. **错误**：D3D12 调用统一 `THROW_IF_FAILED`（`wil`），异常只在初始化路径穿过边界；UI 槽函数内禁止抛异常。
5. **不用 Qt 的类型做核心数据**（`QString`/`QImage` 只在 `src/app/`）。
6. **禁止**在渲染路径里做堆分配（`present()` 内零 `new`）。

## 4. 着色器规则

1. 入口点固定为 `VSMain` / `PSMain`。
2. 常量缓冲统一 `cbuffer FrameConstants : register(b0)`，16 字节对齐，成员顺序与 CPU 侧 `FrameConstants` 结构体**逐字段对应**。
3. 常量**不得在着色器里硬编码**：矩阵、白点、传输函数参数由 `scripts/gen-color-constants` 从 [COLOR_SPEC.md](COLOR_SPEC.md) 的 CPU 参考实现导出为 `generated/color_constants.h` 与两份着色器 include。
4. `float` 用于颜色数学；`half` 只用于纯纹理采样中间值，且必须在两条后端上保持一致精度行为。
5. HLSL 与 SKSL 的**表达式顺序必须一致**（顺序不同会导致末位差异，破坏一致性测试）。

## 5. 分支与提交

| 项 | 规则 |
|---|---|
| 分支 | `develop` 日常；`feat/<slug>`、`fix/<slug>`、`docs/<slug>`、`chore/<slug>` 从 `develop` 切出 |
| 合并 | 特性分支先 `rebase develop`，再 `--ff-only` 合入（**保持线性历史，禁止 merge commit**） |
| `main` | 永远为空，不接收提交 |
| 提交信息 | Conventional Commits：`<type>(<scope>): <subject>` |
| type | `feat` `fix` `docs` `refactor` `perf` `test` `build` `chore` |
| scope | `app` `render` `gfx` `color` `media` `shaders` `build` `docs` `repo` |
| 主题 | 祈使句、英文、≤ 72 字符、句末不加句号 |
| 正文 | 说**为什么**，不说"改了什么"（diff 已经说了） |

例：`fix(shaders): align skSL sRGB decode with hlsl order to fix 1-LSB drift`

## 6. 文档规则

1. 每份 `Docs/**/*.md` 必须有 front-matter：

   ```yaml
   ---
   status: active      # draft | active | deprecated | superseded
   owner: XiaoliZhouCN
   updated: 2026-11-04 # ISO 日期，内容变更时更新
   ---
   ```

2. 链接只用**相对路径 + 正斜杠**，禁止 `\`、禁止绝对路径、禁止 `file://`。
3. 每份文档必须能被 [INDEX.md](INDEX.md) 或其它文档链接到（无孤儿）。
4. 中文正文，英文标识符；术语首次出现给英文原文。
5. 单文件超过 300 行考虑拆分；拆分后必须在 INDEX 登记。

## 7. 资源

| 资源 | 位置 | 规则 |
|---|---|---|
| 测试图 | `assets/test-images/` | 单文件 < 2 MB；来源与许可证写在 `assets/README.md` |
| 黄金图 | `tests/golden/` | 必须是可复现生成，附生成命令 |
| GPU 捕获 | `instruments/artifacts/`（gitignore） | 不入库 |
| 编译产物 | `build/`、`out/` | 不入库 |

## 8. 日志

- 统一宏 `CHROMA_LOG_TRACE/INFO/WARN/ERROR`，默认输出到 `OutputDebugString` 与 `stderr`。
- 渲染路径内的日志必须可在 Release 下整体编译掉（`#if CHROMA_LOG_ENABLED`）。
- 每条日志带子系统标签（`[color]`、`[gfx]`、`[media]`）。

## 9. 脚本

1. PowerShell 脚本必须保存为 **UTF-8 with BOM**。
   原因：Windows PowerShell 5.1 对无 BOM 的 `.ps1` 按系统 ANSI 代码页解码，文件里的中文字符串会导致**语法错误**（现象是莫名其妙的 "Unexpected token"）。
2. 对外命令统一走 `scripts\check-docs.cmd`：本机执行策略为 `AllSigned`，直接调 `.ps1` 会被拒绝。
3. 脚本必须**退出码即结论**（0 通过 / 非 0 失败），不得只打印警告后返回 0。
4. 脚本不得依赖 PowerShell 7 专有语法，除非同时提供 5.1 兼容分支（CI 用 pwsh，本机是 5.1）。

## 10. 外部依赖

1. **依赖本体不入库**：一律放 `external/`（gitignore），仓库只保存"如何得到它"的脚本。
2. 每个外部依赖必须有对应的 `tools/fetch-*.ps1`，从零可复原；脚本里的版本/URL 是唯一事实来源。
3. 通过 `cmake/Find*.cmake` 暴露 imported target，业务代码不得直接写死 include/lib 路径。
4. **C 库头文件必须包 `extern "C"`**（FFmpeg 就是典型：不包会得到一堆 C++ 修饰名的 `LNK2019`）。
5. 引入或升级依赖 → 先写 ADR，再改脚本。


