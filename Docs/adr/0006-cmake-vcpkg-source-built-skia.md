---
status: accepted
owner: XiaoliZhouCN
updated: 2026-11-04
---

# 0006. CMake + vcpkg + 源码构建 Skia

- **状态**：accepted
- **日期**：2026-11-04
- **决策者**：XiaoliZhouCN

## 背景

需要同时满足：

- D3D12 后端可用的 Skia（官方预编译包不提供 `skia_use_direct3d`）；
- Qt 6.8 与 FFmpeg 依赖；
- Agent 可重复执行的构建流程（不能依赖"我机器上装过什么"）。

## 决策

1. **CMake ≥ 3.28 + Ninja + CMake Presets** 作为构建入口，preset 名 `windows-x64-debug` / `windows-x64-release`；
2. **vcpkg manifest 模式**管理 Qt 与 FFmpeg，`vcpkg.json` 入库，`vcpkg_installed/` 不入库；
3. **Skia 源码构建**，产物放在仓库外 `external/skia/out/<config>`，通过 `-DSKIA_DIR` 注入；
4. 构建 Skia 的 GN 参数写成 `scripts/build-skia.ps1`，入库——参数是知识，不能只存在于某人终端历史里；
5. 本机绝对路径一律放 `CMakeUserPresets.json`（gitignore），不入库。

## 后果

### 正面

- 依赖版本可复现，Agent 能照着脚本从零构建；
- Skia 构建参数被文档化，D3D12 后端是否开启可审计；
- 不入库二进制，仓库保持轻量。

### 负面 / 代价

- **首次 Skia 构建耗时约 1–3 小时**，是 M0 的主要时间成本；
- 需要 Python 与 depot_tools，环境门槛高于纯 vcpkg 项目；
- CI 上必须缓存 Skia 产物，否则流水线不可用（本期 CI 只跑文档与 CPU 测试，暂不构建 Skia）；
- vcpkg 与 Skia 的 CRT 链接方式（`/MD` vs `/MT`）必须一致，否则符号冲突。

### 需要跟进

- [ ] **【待验证】** GN 参数：`skia_use_direct3d`、是否需要 `skia_use_dawn`、是否需要额外 image codec 开关。
- [ ] **【待验证】** 产物形态（`skia.lib` / `skia.dll`）与链接方式。
- [ ] **【待验证】** 与 vcpkg 依赖的 CRT 一致性（统一 `/MD`）。
- [ ] 若 vcpkg 的 Skia 端口能可靠开启 D3D12 后端，需新建 ADR 简化流程。

## 替代方案

| 方案 | 否决理由 |
|---|---|
| 官方预编译 Skia | 不含 D3D12 后端，直接否掉核心需求 |
| vcpkg 的 `skia` 端口 | 默认不开 D3D12 后端，特性开关不确定，等于把风险藏起来 |
| 手工 MSBuild 工程 | 不可重复、不可版本化，Agent 无法从零复现 |
| Bazel / gn 全量接管 | 与 Qt/vcpkg 生态冲突，学习成本偏离目标 |

## 参考

- [../BUILD.md](../BUILD.md) §3
- Skia `gn` 构建文档
