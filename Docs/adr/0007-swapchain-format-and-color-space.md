---
status: proposed
owner: XiaoliZhouCN
updated: 2026-11-04
---

# 0007. 交换链格式与色彩空间

- **状态**：**proposed**（尚未拍板，M1 前必须定）
- **日期**：2026-11-04
- **决策者**：XiaoliZhouCN

## 背景

Pass 2 的输出必须是交换链后台缓冲。格式与声明的色彩空间共同决定：

1. 可用的量化位深（直接影响色带与抖动策略）；
2. DWM 如何解释这些像素（声明错 = 整体偏色）；
3. 二期 HDR 是否需要换格式。

关键事实（须在 M1 实测确认）：

- `DXGI_COLOR_SPACE_TYPE` 只是**声明**，不做转换（见 [0004](0004-self-implemented-color-management.md)）；
- 每个格式支持哪些色彩空间必须用 `IDXGISwapChain3::CheckColorSpaceSupport` 查询，不能假定；
- 10-bit UNORM 格式在 SDR 与 HDR10 两种场景下是**同一个格式、不同的色彩空间声明**（`RGB_FULL_G22_NONE_P709` vs `RGB_FULL_G2084_NONE_P2020`）。

## 决策（倾向，未定稿）

一期采用 **方案 A**：

- 格式：`DXGI_FORMAT_R10G10B10A2_UNORM`
- 色彩空间：`DXGI_COLOR_SPACE_RGB_FULL_G22_NONE_P709`
- 呈现：`DXGI_SWAP_EFFECT_FLIP_DISCARD`，2–3 个缓冲，`Present(1, 0)`
- 降级链：`R10G10B10A2_UNORM` → `R8G8B8A8_UNORM`（8-bit，抖动幅度降为 ±0.5 LSB）

理由：10-bit 让"抖动到底有没有用"这件事可被肉眼与像素双重验证；SDR 语义与一期范围一致；且二期切 HDR 时**格式不变，只改色彩空间声明**，Pass 2 的编码函数从 sRGB 换成 PQ 即可。

## 后果

### 正面

- 位深余量足够，色带问题在一期就能解决而不是遮掩过去；
- 一期到二期的切换面很小（只换 OETF 与色彩空间声明）；
- 格式支持广泛，降级路径清晰。

### 负面 / 代价

- 10-bit 路径下列宽/显示设置可能影响实际有效位深（需读 `GetDesc1` 判断）；
- A2 的 2 位 alpha 语义要明确（恒为 1）；
- 若目标机器只有 8-bit 输出链路，10-bit 交换链可能被驱动降级，需记录日志。

### 需要跟进

- [ ] M1 实测 `CheckColorSpaceSupport` 结果并记录到本文。
- [ ] M1 实测 `IDXGIOutput6::GetDesc1` 的 `BitsPerColor`，判断实际位深。
- [ ] 定稿后把状态改为 `accepted` 并同步 [../COLOR_SPEC.md](../COLOR_SPEC.md) §6。

## 替代方案

| 方案 | 格式 | 优点 | 否决/保留理由 |
|---|---|---|---|
| **A（倾向）** | `R10G10B10A2_UNORM` + `G22_NONE_P709` | 10-bit、SDR 语义明确、二期可复用格式 | — |
| B | `R16G16B16A16_FLOAT` + `RGB_FULL_G10_NONE_P709`（scRGB） | 线性输出、超范围值可表达、HDR 友好 | 80 nits 参考语义与 DWM 合成规则更绕；一期会把"线性 vs 编码"的教学顺序打乱；留作二期备选 |
| C | `B8G8R8A8_UNORM` + `G22_NONE_P709` | 兼容性最好 | 8-bit 必须靠抖动，掩盖色彩管线本身的精度问题；仅作降级 |

## 实测（2026-11-04，本机）

| 项 | 结果 |
|---|---|
| 适配器 | Intel UHD Graphics，D3D12 特性等级 **12_1** |
| 输出 | `\\.\DISPLAY1`，`BitsPerColor = 8`，当前 `RGB_FULL_G22_NONE_P709`，峰值 270 nits |
| `R10G10B10A2_UNORM` 交换链 | 创建成功 |
| ↳ `RGB_FULL_G22_NONE_P709` | **SUPPORTED** |
| ↳ `RGB_FULL_G2084_NONE_P2020` | **SUPPORTED** |
| ↳ `RGB_FULL_G10_NONE_P709` | NOT SUPPORTED |
| `R8G8B8A8_UNORM` / `B8G8R8A8_UNORM` | 创建成功，SDR 与 HDR10 均 SUPPORTED |
| `R16G16B16A16_FLOAT` | 创建成功，**仅** `RGB_FULL_G10_NONE_P709`（scRGB）SUPPORTED |

结论：

1. **方案 A 在本机可行**，`R10G10B10A2_UNORM` + `RGB_FULL_G22_NONE_P709` 是有效组合。
2. `BitsPerColor = 8` 说明**输出链路是 8-bit**：交换链的 10-bit 价值在于量化前的中间精度与抖动效果，**不要在本项目里声称"10-bit 显示"**。
3. HDR10 色彩空间声明是 SUPPORTED，但当前系统 HDR 关闭、输出 8bpc。二期必须先读系统 HDR 状态与 `GetDesc1`，不能只看 `CheckColorSpaceSupport`。
4. 踩坑：`CreateSwapChainForHwnd` 在 D3D12 下第一个参数必须传 **`ID3D12CommandQueue`**，传 `ID3D12Device` 会得到 `DXGI_ERROR_INVALID_CALL (0x887A0001)`。

状态变更条件：M1 完成实现并跑通后改为 `accepted`；若届时发现 8-bit 输出链路导致抖动不可验证，则回退到方案 C 并另写 ADR。

## 参考

- `IDXGISwapChain3::SetColorSpace1` / `CheckColorSpaceSupport`
- `IDXGIOutput6::GetDesc1`（`ColorSpace`、`BitsPerColor`）
- [../ARCHITECTURE.md](../ARCHITECTURE.md) §5
- [../COLOR_SPEC.md](../COLOR_SPEC.md) §6
