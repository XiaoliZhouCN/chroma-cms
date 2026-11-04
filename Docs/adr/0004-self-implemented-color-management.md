---
status: accepted
owner: XiaoliZhouCN
updated: 2026-11-04
---

# 0004. 自研色彩管理，禁用框架自带转换

- **状态**：accepted
- **日期**：2026-11-04
- **决策者**：XiaoliZhouCN

## 背景

Skia 有 `SkColorSpace`，Qt 有 `QColorSpace`，Windows 有 ICM/WCS，DXGI 有 `DXGI_COLOR_SPACE_TYPE`。它们都能"帮我们做颜色转换"。

但本项目的目标正是**学会色彩管理**。用框架的转换意味着：

- 只能看到输入和输出，看不到中间步骤；
- 出错时无法判断是自己的问题还是框架的默认行为；
- 双后端一致性测试会变成"比较两个框架的默认实现"，而不是"比较我们的数学"。

## 决策

1. 所有颜色转换由自研实现完成：
   - GPU 侧：HLSL（原生）与 SkSL（Skia），两套，逐式对齐 [../COLOR_SPEC.md](../COLOR_SPEC.md)；
   - CPU 侧：`src/color/` 的双精度参考实现，作为测试 oracle 与常量推导源。
2. **框架只允许"声明"，不允许"转换"**：
   - Skia 的 surface/纹理色彩空间只用于声明，且声明值必须与实际输出空间一致；
   - Qt 不使用 `QColorSpace` 做任何转换；
   - Windows ICM/WCS 在一期完全不使用（二期只用来**读取**显示 profile 数据，仍由我们做转换）。
3. 交换链色彩空间（`DXGI_COLOR_SPACE_TYPE`）只作为"声明"设置，不作为转换手段。
4. 合规检查：`SkColorSpace::MakeSRGB()` 等"会触发转换"的用法需在评审中显式排查。

## 后果

### 正面

- 管线的每一步都可观察、可抓帧、可单测；
- 双后端对比的意义成立（比的是同一套数学）；
- 学到的东西可迁移到任何图形 API。

### 负面 / 代价

- 必须自己处理所有边界情况：limited range、601/709 不匹配、sRGB 拐点、白点适应；
- 需要自建测试基础设施（CPU oracle、黄金图、抖动统计），否则错误不可见；
- 二期 ICC 解析要自己写（至少解析 `wtpt`/`rXYZ`/`gXYZ`/`bXYZ`/`rTRC` 等 tag）。

### 需要跟进

- [ ] M2：CPU oracle 与 SPEC §8 锚点先跑通，再写 HLSL。
- [ ] M3：像素探针验证 Skia 未插入隐式转换（[../COLOR_MANAGEMENT.md](../COLOR_MANAGEMENT.md) §4.4）。
- [ ] 评审清单已加入"有无框架自带色彩管理混入"一项（[../AGENT_WORKFLOW.md](../AGENT_WORKFLOW.md) §6）。

## 替代方案

| 方案 | 否决理由 |
|---|---|
| 直接用 `SkColorSpace` | 与学习目标冲突；两条后端会退化成比较框架默认行为 |
| 用 D3D12 的自动转换能力 | D3D12 本身不做色彩转换（仅声明），此方案本就不存在 |
| 只做 CPU 转换 | 性能不可接受，且学不到着色器实现 |
| 用 3D LUT 规避数学 | 学不到原理，且 LUT 精度与插值引入新变量 |

## 参考

- [../COLOR_MANAGEMENT.md](../COLOR_MANAGEMENT.md)
- [../COLOR_SPEC.md](../COLOR_SPEC.md)
- [0007](0007-swapchain-format-and-color-space.md)
