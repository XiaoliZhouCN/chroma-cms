# 测试手册（docs/testing）

> 测试是"色彩管理理论实践"的一部分：没有断言的知识不算掌握。
> 方向与阶段见 [`../ROADMAP.md`](../ROADMAP.md)；每篇文章的实验见 [`../color/00-index.md`](../color/00-index.md)。

## 0. 为什么这么测

色彩管理错误往往**肉眼可见但不精确**（偏色、发灰、过曝），也可能**肉眼不可见但数值错**（矩阵系数、传递函数边界）。
所以本仓库的测试分三层，分别回答三个问题：

| 层 | 回答的问题 | 失败意味着 |
|---|---|---|
| unit | 数学/解析本身对不对？ | 公式、常量、解析逻辑错 |
| integration | 一组输入经过完整链路后结果对不对？ | 管线接线、元数据优先级、转换顺序错 |
| e2e | 三个后端 + 系统链路给出一致结果吗？ | 后端实现不一致、GPU 精度/状态问题 |

## 1. 三层测试

### 1.1 unit（`tests/unit/`）

对象：`src/color`、`src/io` 等纯 CPU 逻辑。

- 传递函数：EOTF/OETF 往返、边界（0/1/码值 1/1023）、分段连续性；
- 矩阵：白点映射为 (1,1,1)、已知色度点、矩阵可逆性；
- 适应：D65↔D50 往返、Bradford 不变量；
- 元数据解析：字段精确值、缺省与优先级；
- 量化：位深步长、舍入/抖动统计。

判据：`double` 计算绝对误差 **1e-6 ~ 1e-5**（按量级选）；解析字段精确匹配。

### 1.2 integration（`tests/integration/`）

对象：文件 → 解码 → Frame → 色彩描述 → 转换 → 输出像素。

- 输入：`tests/corpus/` 的小样例 + `pattern-gen` 生成的合成图；
- 参考：CPU oracle 输出（`tests/golden/`）；
- 判据：SDR `ΔE2000 < 1.0`；HDR `ΔE ITP < 1.0`；量化/抖动场景另定阈值并写进用例。

### 1.3 e2e（`tests/e2e/`）

对象：三个后端（手写 D3D12 / 手写 Metal / Skia）与双视图。

- 渲染到离屏 RGBA16F → readback → 与 CPU oracle 比；
- 后端之间互比（同输入、同目标描述）；
- 系统链路（P3）：DWM/WindowServer 改变数值的情况只做"文档化 + 可复现实验"，断言写清假设。

判据：与 oracle 的差异在阈值内；后端间差异在阈值内。截图只作为人工证据，不作为唯一判据。

## 2. 测试资产

| 资产 | 位置 | 规则 |
|---|---|---|
| 小样例 | `tests/corpus/` | 单文件 < 5 MB，总量 < 10 MB；入库 |
| 大样例 | 下载脚本 `tools/fetch-corpus` | 校验 SHA-256；不入库 |
| 合成图 | `tools/pattern-gen` | 确定性种子；可重现 |
| 参考输出 | `tests/golden/` | 与生成脚本/版本绑定；更新需说明原因 |
| 指标输出 | `tests/` 日志 | 失败时打印实际 ΔE/误差，便于定位 |

## 3. 怎么写一个测试

### 3.1 命名与注册

- 文件名：`<主题>_test.cpp`（unit）、`<场景>_test.cpp`（integration/e2e）；
- CMake：`add_test(NAME <name> COMMAND <target>)`，统一由 ctest 收集；
- 一个测试文件对应一个"可解释的主题"，不要把所有断言堆进 smoke。

### 3.2 断言风格

```cpp
// unit：数值误差
expectNear("pq(0.5).r", actual.r, expected, 1e-6);

// integration：感知误差
expectDeltaE2000("srgb->p3 sample", actual, expected, /*max=*/1.0);

// e2e：后端一致性
expectBackendsAgree("spinning pattern", hand_written, skia, /*max=*/1.0);
```

- 失败信息必须包含**用例名 + 实际值 + 期望值 + 阈值**；
- 浮点比较禁止用 `==`（除了解析整数/枚举）；
- 随机/抖动测试固定 seed。

### 3.3 数据驱动

优先"一个测试 + 多组数据"（表驱动），数据放在测试文件内或 `tests/data/`：

```cpp
struct TransferCase { double code; double expectedLinear; };
for (const auto &c : cases) { ... }
```

## 4. golden 更新流程

1. 明确"为什么参考要变"（规范变化/实现修正），写进提交信息；
2. 生成新 golden 的脚本必须与测试共用同一套 CPU oracle；
3. 更新后必须同时在 Windows 与 macOS 跑通（双平台验收门槛）；
4. 不允许"为了让测试通过而调阈值"——阈值调整要单独说明并评审。

## 5. 反模式

| 反模式 | 问题 |
|---|---|
| 只看截图 | 截图受系统色彩管理影响，不能证明数值正确 |
| 阈值随手放大 | 掩盖真实回归 |
| 测试依赖本机 ICC/显示器 | 不可复现；系统状态应作为显式参数 |
| 一个 smoke 承担所有断言 | 失败无法定位 |
| 用 GPU 输出当参考 | 参考必须来自 CPU oracle |

## 6. 阶段测试清单

| 阶段 | 必须新增 |
|---|---|
| P0 | 现有 color math 迁入 `src/color` 后的 unit 测试；容器探测测试（`unit.container_probe`） |
| P1 | 每种格式的元数据解析 unit；SDR/HDR/双层各自的 integration；双视图 e2e |
| P2 | 视频首帧/连续帧 integration；四轨统一 ΔE 报告 |
| P3 | 系统显示链路实验记录（非断言类）；环境参数化测试 |
| P4 | benchmark 基线 + CI 脚本 |
