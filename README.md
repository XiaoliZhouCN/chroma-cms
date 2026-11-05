# ChromaCMS · practice

> 从 `main` 清出来的**手写练习分支**：从零手写渲染器，不继承 `develop` 的任何实现。

## 这条分支是什么

- 目的：亲手写一遍 D3D12 渲染器——设备、交换链、命令队列、描述符堆、根签名、PSO、同步、呈现。
- 状态：**空**。这里没有任何实现代码，全部自己写。
- 与 `develop` 的关系：**没有共同祖先**（`git merge-base practice develop` 返回非 0）。
  所以只能按文件借用，**不要直接 merge 整条分支**。

## 当前目录（就这些）

| 文件 | 作用 |
|---|---|
| `.gitignore` | 忽略 `build/`、临时目录等 |
| `.gitattributes` | 统一换行 |
| `README.md` | 本文件 |

`develop` 上的 `Docs/`、`src/`、`assets/`、`tools/`、`external/` 都不在这里，是刻意清掉的。

## 需要材料时怎么取

从 `develop` 取单个文件/目录即可，不会破坏这条分支：

| 想要 | 命令 |
|---|---|
| PTMR 测试图 | `git checkout develop -- assets/test-images` |
| 测试图生成器（需 Python） | `git checkout develop -- tools/gen_ptmr.py` |
| FFmpeg（要解码图片时） | `git checkout develop -- tools/fetch-ffmpeg.ps1`，再 `pwsh tools/fetch-ffmpeg.ps1` |
| 色彩规范（写 EOTF 前必读） | `git show develop:Docs/COLOR_SPEC.md` |
| 参考实现（**写完再看**，别边写边抄） | `git show develop:src/render/D3D12Renderer.cpp` |

## 建议的练习路线

每一步都能上屏，出错时只看得到这一步的差异：

1. CMake + 空窗口（Win32 或 Qt 都行）
2. 设备 + 命令队列 + 交换链，清屏成纯色
3. 根签名 + PSO + 全屏三角形
4. 顶点缓冲 + 一个真三角形
5. 纹理上传 + 采样（此时再从 develop 取 PTMR 图）
6. fence 同步 + 双帧在飞
7. 按 `Docs/COLOR_SPEC.md` 把 EOTF 与色域矩阵写进像素着色器

## 规则

- 这条分支**不跑** `check-docs`，没有文档门禁，也不用写 ADR。
- 提交随意，能回滚就行；唯一要求是别把 `build/` 提交进来（已在 `.gitignore`）。

## 当前环境（已实测）

VS 2026 18.8（MSVC 19.51）· Windows SDK 10.0.26100 · CMake 4.2.1 · Qt 6.11.1 msvc2022_64
GPU：Intel UHD Graphics（D3D12 特性等级 12_1）· 显示器：8 bpc / SDR
