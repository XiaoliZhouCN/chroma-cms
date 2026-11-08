# AGENTS.md — ChromaCMS 协作指南（全平台通用）

> 本文件是**所有 AI 协作 agent 的唯一入口**。Windows（D3D12）与 macOS（Metal）两个方向都先读这里，
> 再按 `docs/handoff/` 下的文档顺序继续。

## 0. 项目是什么

ChromaCMS 是一个**手写渲染器**练习项目：用 Qt 做窗口，用自己的渲染器把一张 10-bit PQ（HDR）测试图
正确显示出来，并实现色彩管理（PQ EOTF → 色域转换 → 显示输出）。

- 当前工作分支：**`practice`**（手写练习分支，没有文档门禁、不用写 ADR）
- `develop`：完整参考实现（**与 practice 没有共同祖先**，只能按文件借用，不要 merge）
- `main`：空

## 1. 最重要的规则（违反即返工）

1. **渲染器实现代码由用户本人写**：`.cpp` / `.hpp` / `.mm` / `.hlsl` / `.metal`。
2. **agent 负责**：
   - 写 `CMakeLists.txt`、`cmake/*.cmake`、`CMakePresets.json`
   - 创建**空文件**（0 字节）作为占位
   - 给出**逐处改法**：改哪个文件、在哪一行之后插入、完整代码块
3. **不要在用户工作区做实验**。复制到 `temp_and_scratch/`（已 gitignore）里打补丁、构建、运行、验证，
   验证完删掉，只保留截图。
4. **交付前必须自验**：配置 + 构建 + 运行 + 截图（或日志）。不允许"应该可以"式交付。
5. **不自动提交**。用户明确说"commit"才提交；提交前构建必须是绿的。
6. **头文件一律 `.hpp`**。
7. 用中文交流；代码注释用中文；学习型注释沿用 `// Q: ...` / `// A: ...` 的问答风格。
8. 不要顺手重排/格式化用户的代码。

## 2. 先读哪些文档

| 顺序 | 文件 | 内容 |
|---|---|---|
| 1 | `docs/handoff/README.md` | 索引、当前进度、怎么开始 |
| 2 | `docs/handoff/01-project-state.md` | 已实现什么、代码地图、环境、验证证据 |
| 3 | `docs/handoff/02-collaboration-protocol.md` | 协作协议、交付格式、验证配方、用户偏好 |
| 4 | `docs/handoff/03-metal-plan.md` | **macOS/Metal 移植方案**（D3D12→Metal 映射、MSL、路线图） |
| 5 | `docs/handoff/04-dual-platform.md` | 双平台架构：渲染器工厂、CMake 条件、一致性测试 |
| 6 | `docs/handoff/05-macos-setup.md` | macOS 环境搭建与排错 |
| 7 | `docs/handoff/06-bootstrap-prompt.md` | 给新 agent 的启动提示词（用户可直接粘贴） |

## 3. 平台现状

| 平台 | 后端 | 进度 |
|---|---|---|
| Windows | 自写 D3D12 | 设备/交换链 → 三角形 → 纹理 → FFmpeg 解码 → PQ 色彩管线（单 pass，scRGB 16F 输出）✅ |
| macOS | 自写 Metal | **未开始**，从 M1（设备 + CAMetalLayer + 清屏）开始 |

两块屏的实际情况（Windows 侧已探测）：内屏 SDR/8bpc，外接 ASUS VG32AQL1A 通过 HDMI 2560×1440@60
且 **HDR 已启用**、8bpc。所以"同一套颜色输出在两块屏上表现不同"是预期现象。

## 4. 常用命令

Windows（PowerShell，注意执行策略）：

```powershell
cmake --preset windows-x64-debug -DQT_ROOT="D:/Applications/QtCreator/6.11.1/msvc2022_64"
cmake --build --preset windows-x64-debug
.\build\windows-x64-debug\Debug\ChromaCMS.exe
```

macOS（详见 05）：

```bash
export QT_ROOT=$(brew --prefix qt)
cmake --preset macos-arm64-debug
cmake --build --preset macos-arm64-debug
open build/macos-arm64-debug/ChromaCMS.app    # 或直接跑 .app/Contents/MacOS/ChromaCMS
```

## 5. 已踩过的坑（避免重复踩）

| 平台 | 坑 | 处理 |
|---|---|---|
| Windows | PowerShell 执行策略 AllSigned，`.ps1` 跑不了 | `powershell -NoProfile -ExecutionPolicy Bypass -File xxx.ps1` |
| Windows | `Start-Process` 启动 GUI app 会挂住工具调用 | 必须重定向 stdout/stderr 到文件 |
| Windows | 用 `Get-Content -Raw` + `Set-Content` 改中文文件会乱码 | 用 `[System.IO.File]::ReadAllText/WriteAllText` + UTF8 无 BOM |
| Windows | 无 `pwsh` 时的 Add-Type 只支持 FullLanguage | 当前会话是 danger-full-access，正常可用 |
| 通用 | 改着色器不用重编 exe（运行时编译，但 POST_BUILD 才会拷贝） | 改完 `Copy-Item shaders\* 到输出目录` 再跑 |
| 通用 | 空 `.cpp` 会让 AUTOMOC 警告 | 填上内容即消失，不影响构建 |
| macOS | 截图需要"屏幕录制"权限 | 系统设置 → 隐私与安全性 → 屏幕录制，给终端授权 |
| macOS | Qt 来自 Homebrew 时 `find_package(Qt6)` 需要前缀 | 设置 `QT_ROOT`（预设已用 `$penv{QT_ROOT}`） |

## 6. 交付时的自检清单

- [ ] 构建绿（完整配置 + 构建日志）
- [ ] 运行过，不是只编译
- [ ] 有截图或关键日志作为证据
- [ ] 没有修改用户的源码文件（`git status` 干净或只剩用户自己的改动）
- [ ] 改动清单里每处都有"在哪一行之后插入"的锚点
- [ ] 常见错误表写清楚（用户跑失败时能自查）
