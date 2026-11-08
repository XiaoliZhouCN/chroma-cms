# 05 · macOS 环境搭建与排错

## 1. 前置要求

| 项 | 要求 |
|---|---|
| macOS | 13 Ventura 及以上（Metal 3；EDR 支持更早） |
| 芯片 | Apple Silicon 或 Intel 均可（`uname -m` 看架构） |
| Xcode | 15+，或至少 Command Line Tools（提供 clang、SDK、Metal 工具链） |
| Qt | 6.8+（与 Windows 侧一致；Homebrew 的 `qt` 即可） |
| CMake | 3.28+（预设用到 `condition`，需要 3.21+） |
| Ninja | 建议装（预设用 Ninja） |
| FFmpeg | Homebrew `ffmpeg`（解码测试图用） |

## 2. 安装

```bash
# 1) 编译工具链
xcode-select --install
xcodebuild -version
sw_vers
uname -m

# 2) 依赖
brew install cmake ninja qt ffmpeg python3

# 3) 确认 Qt 版本（需要 6.8+）
brew --prefix qt
$(brew --prefix qt)/bin/qmake --version
```

## 3. 拿到代码

远端是 SSH 别名（`git@github-shirley:...`），如果 Mac 上没有配置这个别名，
用下面任一方式：

```bash
# 方式 A：在 ~/.ssh/config 里加别名
Host github-shirley
  HostName github.com
  User git

# 方式 B：直接用真实地址
git clone git@github.com:XiaoliZhouCN/chroma-cms.git

cd chroma-cms
git checkout practice
```

测试图与 FFmpeg 都不在仓库里（被 gitignore），需要自己补：

```bash
# 测试图（Python 生成，需要 numpy / pillow 时先装）
python3 -m pip install --user numpy pillow
mkdir -p assets/test-images
python3 tools/gen_ptmr.py \
  --out assets/test-images/PTMR_9-16-9-full_2048x2048_10bit.png \
  --cicp-matrix 0 --preview
```

FFmpeg 用 Homebrew 的即可，配置时传 `-DFFMPEG_ROOT=$(brew --prefix ffmpeg)`，
或直接让 `FindFFmpeg.cmake` 的候选路径命中（`/opt/homebrew/opt/ffmpeg`）。

## 4. 构建与运行

```bash
export QT_ROOT=$(brew --prefix qt)

cmake --preset macos-arm64-debug
cmake --build --preset macos-arm64-debug

# 直接运行（开发阶段最方便，能看日志）
./build/macos-arm64-debug/ChromaCMS.app/Contents/MacOS/ChromaCMS

# 或用 open（看不到 stdout，需要看日志时用上面的方式）
open build/macos-arm64-debug/ChromaCMS.app
```

> `.app` 里自带 `Contents/MacOS/shaders/`（CMake POST_BUILD 拷贝的），
> 所以 `executableDirectory() + "/shaders/fullscreen.metal"` 能找到着色器。

## 5. 验证 Metal 环境

```bash
# GPU 与显示信息
system_profiler SPDisplaysDataType

# 是否支持 EDR（HDR）——看 max EDR 值
# 可用一小段 Swift / ObjC 读 NSScreen.maximumPotentialExtendedDynamicColorComponentValue
```

调试层（开发期建议常开）：

```bash
MTL_DEBUG_LAYER=1 MTL_SHADER_VALIDATION=1 \
  ./build/macos-arm64-debug/ChromaCMS.app/Contents/MacOS/ChromaCMS
```

## 6. 截图（验证用）

```bash
# 全屏截图
screencapture -x /tmp/chroma.png

# 或者截取最前面的窗口
screencapture -x -o -l"$(osascript -e 'tell app "System Events" to id of window 1 of process "ChromaCMS"')" /tmp/chroma-win.png
```

> 第一次使用需要在「系统设置 → 隐私与安全性 → 屏幕录制」里给**终端/IDE**授权，
> 否则截出来是全黑或桌面背景。

## 7. 常见错误

| 现象 | 原因 | 处理 |
|---|---|---|
| `Could not find a package configuration file for Qt6` | 没设置 Qt 前缀 | `export QT_ROOT=$(brew --prefix qt)`，再重新 configure（预设里用 `$penv{QT_ROOT}`） |
| `qt.qpa.plugin: Could not find the Qt platform plugin "cocoa"` | 直接跑了裸二进制但 Qt 框架不在路径 | 在 `build/.../ChromaCMS.app` 内运行，或 `export DYLD_FRAMEWORK_PATH=$QT_ROOT/lib` |
| `dyld: Library not loaded: @rpath/QtWidgets.framework/...` | 同上 | 同上；发布时用 `macdeployqt` |
| `FFmpeg: 打不开文件` / 链接找不到 `avcodec` | FFmpeg 路径没被找到 | `-DFFMPEG_ROOT=$(brew --prefix ffmpeg)` 重新 configure |
| `CMAKE_OBJCXX_COMPILER not set` | 顶层没 `enable_language(OBJCXX)` | 按 04 §3.1 改 CMakeLists |
| `framework not found Metal` | 平台库分支没写对 | 检查 `src/gfx/CMakeLists.txt` 的 APPLE 分支 |
| 窗口黑屏但无报错 | `CAMetalLayer` 没挂上 / 没设置 colorspace | 检查 `view.wantsLayer`、`view.layer`、`layer.colorspace` |
| 画面整体偏暗 | 用了默认 sRGB colorspace 但输出线性值 | 显式 `kCGColorSpaceExtendedLinearSRGB` |
| 画面只有左上角一块 | `drawableSize` 没按物理像素设置 | `drawableSize` = 物理像素；`contentsScale` = backingScaleFactor |
| 偶发崩溃在 `nextDrawable` | drawable 超时返回 nil | 判空跳过该帧 |
| 首次绘制前 layer 被 Qt 重置 | Qt 在显示流程里重建了 layer | 在 `showEvent` 之后再设置/重设 layer |
| 截图全黑 | 无屏幕录制权限 | 系统设置授权后重试 |

## 8. 建议的第一次运行检查清单

- [ ] `cmake --preset macos-arm64-debug` 成功（打印 Qt 版本、FFmpeg 路径）
- [ ] 构建成功，产物是 `ChromaCMS.app`
- [ ] `Contents/MacOS/shaders/` 里有着色器文件
- [ ] 运行后窗口出现，控制台打印 `[gfx] backend = Metal`
- [ ] 截图能看到画面（不是黑图）
