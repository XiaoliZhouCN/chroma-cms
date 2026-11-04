---
status: active
owner: XiaoliZhouCN
updated: 2026-11-04
---

# 测试与验证

> 本项目的"正确"是有定义的：见 [COLOR_SPEC.md](COLOR_SPEC.md) §7 的容差表。测试就是把这句话变成可执行的门禁。

## 1. 测试金字塔

```
        ┌──────────────────────────┐
   手工 │ 目视比对 / 色卡 / PIX 抓帧 │  低频，人判断
        ├──────────────────────────┤
   一致性│ 双后端同帧比对（1 LSB）    │  M3 起每次提交
        ├──────────────────────────┤
   黄金图│ 渲染结果 vs 受控期望       │  每次提交
        ├──────────────────────────┤
   单元  │ 传输函数 / 矩阵 / 归一化   │  每次提交，最快
        └──────────────────────────┘
```

原则：**能用单元测试证明的，不靠看图**。看图只用于"看起来对不对"这类无法量化的判断。

## 2. 单元测试（`tests/color/`）

CPU 参考实现是唯一的 oracle，全部用双精度。

| 测试 | 内容 | 判据 |
|---|---|---|
| `transfer_roundtrip` | sRGB / gamma2.2 编解码往返 | 误差 ≤ 1e-9 |
| `transfer_anchors` | [COLOR_SPEC.md](COLOR_SPEC.md) §8 的 A1–A6 | 容差见 SPEC §7 |
| `primaries_matrix` | 由原色推导的矩阵 vs 独立推导 | 逐元素 ≤ 1e-6 |
| `white_point` | `M * (1,1,1)` == D65 XYZ | ≤ 1e-4 |
| `yuv_anchors` | limited/full 归一化与 YUV→RGB | 与解析解一致 |
| `cat_bradford` | D65↔D50 往返 | ≤ 1e-9 |
| `quantize_dither` | 抖动后均值无偏、边界不越界 | 统计 + 边界断言 |

命名：`TEST(ColorMath, SrgbDecodeHalf)`。一个测试只断言一件事。

## 3. 黄金图测试（`tests/golden/`）

**输入受控**：`assets/test-images/` 只放合成图（渐变、色卡、单通道阶梯），不放版权不明的照片。

**参考输出**：由 CPU 参考实现生成 16-bit PNG，不是"某次跑出来的截图"。生成命令必须写在测试文件头注释里，保证可复现。

**比对**：允许逐像素容差，指标与阈值按 SPEC §7；超限时输出差异热力图到 `build/test-artifacts/`。

**禁止**：用 GPU 输出反向"更新"黄金图来让测试变绿。更新黄金图必须走 [AGENTS.md](../AGENTS.md) 的"需人工确认"档位。

## 4. 双后端一致性测试（核心）

```
同一帧输入
  → D3D12Renderer.present(frame)  → 读回  → A
  → SkiaD3D12Renderer.present(frame) → 读回 → B
  → 断言 max|A-B| ≤ 1 LSB
```

要点：

1. 两条后端共用同一个 `ID3D12Device`（见 [ARCHITECTURE.md](ARCHITECTURE.md) §4），避免设备差异污染结论；
2. 读回用同一种格式（16F 读回再量化），避免量化阶段差异；
3. 失败时必须落盘：两帧原始数据 + 差异图 + 当前 `ColorMetadata`，否则没法排查。

**已知允许的差异**：浮点求值顺序不同导致的末位差异。容差就是为它设的，不要为了"完全相等"去改数学顺序。

## 5. 手工验证（每次里程碑）

| 项 | 方法 | 通过标准 |
|---|---|---|
| 灰阶无断层 | 8-bit 0–255 渐变铺满窗口 | 无可见色带 |
| 肤色正确 | ColorChecker 参考图 | 与参考图目视一致 |
| 黑位/白位 | 全黑、全白、limited range 视频 | 无发灰/过曝 |
| 交换链声明 | 启动日志打印格式与色彩空间 | 与 ADR-0007 一致 |
| 后端切换 | 运行时切换 D3D12 / Skia | 无闪烁、无泄漏、画面不跳 |
| 宽色域 | P3 测试图 | 不出现整体偏色 |

GPU 诊断工具：PIX（抓帧、逐级看 Pass 1 RT）、GPUView（帧率/呈现节奏）。捕获文件放 `instruments/artifacts/`，不入库。

## 6. CI 边界（诚实说明）

GitHub Actions 的 runner 没有可用的 D3D12 设备，因此：

| 检查 | CI | 本地 |
|---|---|---|
| 构建 | ✅ windows-latest | ✅ |
| 单元测试（CPU） | ✅ | ✅ |
| 文档校验 | ✅ | ✅ |
| 黄金图（GPU） | ❌ 跳过 | ✅ 必须跑 |
| 双后端一致性 | ❌ 跳过 | ✅ 必须跑 |

CI 绿 **不代表**渲染正确。因此 [AGENTS.md](../AGENTS.md) 的 DoD 要求本地跑一致性测试，不能只看 CI。

## 7. 缺陷记录

发现渲染/颜色缺陷时，在 `Docs/COLOR_MANAGEMENT.md` §5 的陷阱表补一行，并在 `tests/` 补一个回归用例。
**只修代码不补用例**，同一个坑会再踩一次。
