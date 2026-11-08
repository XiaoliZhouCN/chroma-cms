# 02 · 协作协议（Collaboration Protocol）

> 这份文档比技术方案更重要。**用户是来"练手写渲染器"的，不是来验收一个能跑的成品。**
> 所以 agent 的价值在于：把工程脚手架和排查成本接过去，把"写代码"留给用户。

## 1. 分工（硬性）

| 谁 | 做什么 |
|---|---|
| **用户** | 写所有实现代码：`.cpp` / `.hpp` / `.mm` / `.hlsl` / `.metal`；决定技术路线；跑验证；提 commit |
| **agent** | 写 `CMakeLists.txt`、`cmake/*.cmake`、`CMakePresets.json`；创建**空文件**；给出**逐处改法 + 完整代码块**；在副本里预先验证 |

即使用户说"你直接帮我写完"，也先确认一次——本项目的默认规则是 **agent 不写实现**。

## 2. 一次交付应该长什么样

按这个顺序写，用户照着做能一次通过：

1. **一句话目标**：这一步要达成什么（可验收）。
2. **改动清单表**：文件 × 改什么 × 新增/修改。
3. **逐处改法**：每处都给出
   - 锚点：「找到 `xxx` 这一行，在**后面**插入」
   - 完整代码块（可整段复制）
   - 一句话解释「为什么」
4. **构建与验收**：命令 + 预期日志 + 预期画面（附截图更好）。
5. **常见错误对照表**：现象 → 原因 → 处理。
6. **我验证过的证据**：在副本里构建/运行/截图的结果。

## 3. 验证配方（交付前 agent 自己做一遍）

### Windows

```powershell
# 1) 复制到临时目录（不碰用户工作区）
$dst = 'temp_and_scratch\<step>'
Copy-Item 'CMakeLists.txt','CMakePresets.json' $dst -Force
Copy-Item 'cmake','src','tests','shaders' $dst -Recurse -Force

# 2) 打补丁（用 .NET API，避免中文乱码）
$p = Join-Path $dst 'src\render\D3D12Renderer.cpp'
$t = [System.IO.File]::ReadAllText((Resolve-Path $p), [System.Text.Encoding]::UTF8)
$t = $t.Replace('<old>', '<new>')
[System.IO.File]::WriteAllText((Resolve-Path $p), $t, (New-Object System.Text.UTF8Encoding($false)))

# 3) 配置 + 构建（FFmpeg 用绝对路径）
cmake -S $dst -B "$dst\build" -G "Visual Studio 18 2026" -A x64 `
      -DQT_ROOT="D:/Applications/QtCreator/6.11.1/msvc2022_64" `
      -DFFMPEG_ROOT="D:/Repositories/Projects/ChromaCMS/external/ffmpeg"
cmake --build "$dst\build" --config Debug

# 4) 运行 + 截图（必须重定向 stdio，否则工具调用会挂）
$proc = Start-Process -FilePath "$dst\build\Debug\ChromaCMS.exe" -PassThru `
        -RedirectStandardOutput 'o.log' -RedirectStandardError 'e.log'
Start-Sleep -Seconds 4
# System.Drawing CopyFromScreen 截图 → 保存 PNG
# 5) Kill + 读图 + 删除副本（保留截图到 temp_and_scratch/）
```

### macOS

```bash
# 1) 复制副本（排除 build/assets/external）
rsync -a --exclude build --exclude .git --exclude temp_and_scratch \
      ./ temp_and_scratch/stepM1/

# 2) 打补丁（用 python3 或 perl -0pi，注意保留 UTF-8）
python3 - <<'PY'
p = 'temp_and_scratch/stepM1/src/render/...'
s = open(p, encoding='utf-8').read()
s = s.replace('<old>', '<new>')
open(p, 'w', encoding='utf-8').write(s)
PY

# 3) 配置 + 构建
export QT_ROOT=$(brew --prefix qt)
cmake -S temp_and_scratch/stepM1 -B temp_and_scratch/stepM1/build -G Ninja \
      -DCMAKE_BUILD_TYPE=Debug -DCMAKE_PREFIX_PATH="$QT_ROOT"
cmake --build temp_and_scratch/stepM1/build

# 4) 运行 + 截图
open temp_and_scratch/stepM1/build/ChromaCMS.app &
sleep 4
screencapture -x temp_and_scratch/stepM1.png

# 5) 关闭 + 检查日志 + 清理
pkill -f ChromaCMS || true
rm -rf temp_and_scratch/stepM1
```

> macOS 截图前需要在「系统设置 → 隐私与安全性 → 屏幕录制」里给终端权限，否则 `screencapture` 出来是黑图。

## 4. 用户偏好

| 项 | 约定 |
|---|---|
| 头文件后缀 | **`.hpp`**（不是 `.h`） |
| 大括号 | Allman（`{` 独占一行） |
| 成员声明 | 尽量对齐（`gfx::ComPtr<ID3D12Device>  m_device;`） |
| 注释 | 中文；学习型注释用 `// Q: 问题` / `// A: 回答` |
| 命名 | 成员 `m_` 前缀；常量 `k` 前缀；类型 PascalCase |
| 不要 | 重排/格式化用户已有的代码；不要顺手改注释 |
| 依赖 | 新增第三方依赖前先问 |
| 删除 | 任何删除文件/大段代码前先问 |

## 5. 用户的提问方式与期望回答

用户经常问「X 是干什么的」「为什么这样写」，并且会追问细节。回答应：

- **先给结论**（一句话），再展开；
- 多用**对照表**（D3D11 vs D3D12、视口 vs 裁剪、根常量 vs CBV …）；
- 讲清**为什么**，而不只是"怎么做"；
- 指出**常见坑**和"不这样写会怎样"；
- 需要时给可运行的**最小示例**。

## 6. 提交规则

- 用户说 commit 才提交；
- 提交前：构建绿 + 工作区检查（`git status`）确认没有把 `assets/`、`external/`、`build/` 加进去；
- 提交信息风格参考历史：`feat(render): ...`、`[color] update color management`；
- 中文正文可以，但首行尽量英文或简短。

## 7. 笔记约定

用户自己在 `_shirley/notes/` 维护学习笔记（例如 `pipeline.md`，是带 Q/A 的函数调用链梳理）。
agent **可以**在用户允许后追加内容，但默认不主动改这些笔记。

## 8. 什么时候必须停下来问用户

1. 要改用户的**实现代码**（`.cpp/.hpp`）；
2. 要新增第三方依赖或下载大文件；
3. 要改架构（如 IRenderer 接口签名、分层）；
4. 要删除/重命名已有文件；
5. 用户的两块显示器 / HDR 状态会影响结论（例如需要"把窗口拖到副屏"才能验证）。
