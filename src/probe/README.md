# src/probe · Stage B 色块探针

## 这个目录是什么
读取 ChromaCMS renderer 的 present 结果，按测试图色块中心采样并输出 CSV，用于验证我们自己的色彩管线。

Probe 读取的是：

```text
present 后的 renderer offscreen surface
```

不是 DWM / WindowServer 的 display buffer，也不是 Digital Color Meter 的屏幕最终值。

## 文件与职责
| 文件 | 职责 |
|---|---|
| `ProbeConfig.hpp/.cpp` | 读取 `card_config` 与坐标 JSON |
| `ProbeCsvWriter.hpp/.cpp` | 写 `output/card_color.csv` |
| `verify_probe.py` | 用 shader CPU oracle 校验完整性与数值 |
| `card_config/` | 测试图与坐标配置 |
| `output/` | 生成结果（不提交） |

## 依赖关系
- 允许：Qt6 Core、标准库。
- 禁止：Skia / Metal / D3D12 / app 头文件；renderer readback 由 app 层桥接。
- 被依赖：`ChromaCMS`。

## 输入
`card_config/17_ramp_config.json`：

```json
{
  "image": "/assets/test-images/PTMR_8ramps_17_2048x2048_10bit.png",
  "loc_config": "./assets/test-images/PTMR_8ramps_17_2048x2048_10bit.json"
}
```

## 输出
`output/card_color.csv`：

```text
block,r,g,b
gray-00,0,0,0
...
```

`r/g/b` 是 shader 输出的 raw linear F16 值，不包含 DWM/WindowServer 或显示器的后续映射。

## 构建与运行
```bash
cmake --build --preset macos-arm64-skia

CHROMA_PROBE=1 \
  build/macos-arm64-skia/ChromaCMS.app/Contents/MacOS/ChromaCMS

python3 src/probe/verify_probe.py
```

可选环境变量：

```text
CHROMA_PROBE_CONFIG  覆盖 card config 路径
CHROMA_PROBE_OUTPUT  覆盖 CSV 输出路径
```

## 验证
```bash
python3 src/probe/verify_probe.py
```

检查：

- 136 个 patch 全部存在
- block 名称顺序一致
- 与 CPU shader oracle 的最大绝对误差在 F16 量化范围内

## 相关文档
- [Probe 功能说明](../../docs/features/probe_card_capture.md)
- [架构设计](../../docs/ARCHITECTURE_DESIGN.md)
- [实现说明](../../docs/ARCHITECTURE_IMPLEMENTATION.md)
