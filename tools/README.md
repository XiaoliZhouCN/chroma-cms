# tools

> 与应用代码解耦的离线工具。只依赖 Python 标准库，不需要虚拟环境。

## gen_ptmr.py

PTMR HDR 测试色块图生成器。规格与存储约定写在文件头注释里（单一事实来源就在脚本内）。

```powershell
# 默认输出到 tools/out/
python tools/gen_ptmr.py

# 生成仓库内使用的主测试图
python tools/gen_ptmr.py --out assets/test-images/PTMR_9-16-9-full_2048x2048_10bit.png --cicp-matrix 0 --preview

# 自检（脚本默认会跑）：重新解析产物，逐 bar 校验 10-bit 码
python tools/gen_ptmr.py --no-csv
```

关键参数：

| 参数 | 作用 |
|---|---|
| `--domain code\|nits` | 等距采样发生在 PQ 码域（视觉均匀）还是 nits 域 |
| `--storage shift\|normalize` | 10-bit 码放进 16-bit 通道的方式；`shift` 时低 6 位恒为 0 |
| `--cicp-matrix 0\|9` | PNG 的 cICP matrix 字段；PNG 是 RGB，语义上应为 0 |
| `--preview` | 额外输出 8-bit 预览图 |

产物说明见 [../assets/README.md](../assets/README.md)。

## fetch-ffmpeg.ps1

下载并解压 FFmpeg（BtbN `win64-lgpl-shared`）到 `external/ffmpeg`。

```powershell
pwsh tools/fetch-ffmpeg.ps1
pwsh tools/fetch-ffmpeg.ps1 -Url <自定义 zip 地址>
```

产物布局：`include/` + `lib/` + `bin/*.dll` + `LICENSE.txt`；`external/` 已在 `.gitignore` 中。
CMake 通过 `cmake/FindFFmpeg.cmake` 找它，详见 [../Docs/BUILD.md](../Docs/BUILD.md) §3.1。

## 约定

1. 工具的输出**不直接入库**，只有被文档引用、被应用实际使用的那一份进 `assets/`。
2. 工具改变产物字节时，必须同步更新 `assets/README.md` 里的 SHA-256。
3. 新增工具请遵循同一风格：单文件、标准库、`--out` 可控、默认自检。
