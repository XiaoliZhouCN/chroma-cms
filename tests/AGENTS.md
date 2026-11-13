---
extends: ../AGENTS.md
scope: tests
owner: agent
last_verified: 2026-11-13
---
# tests · AGENTS 本地扩展

## 继承
继承根 `AGENTS.md`。未在此声明的规则沿用父级。

## 本地约定
- 三层测试边界：unit 只测纯逻辑；integration 走文件→帧→像素；e2e 才涉及 GPU/双视图。
- 判据：unit 绝对误差 1e-6~1e-5；integration SDR `ΔE2000 < 1.0`、HDR `ΔE ITP < 1.0`；e2e 后端间/对 oracle 在阈值内。
- 参考必须来自 CPU oracle（`src/color`），不得用 GPU 输出当参考。
- 阈值调整必须单独说明原因；禁止"调阈值让测试过"。
- 测试不得依赖本机 ICC/显示器/系统色彩设置；环境相关项必须是显式参数。

## 验证入口
```powershell
ctest --preset windows-x64-debug --output-on-failure
```

## 本目录专属坑
- `docs.structure` 也是测试：新增目录忘记写 AGENTS/README 会让 ctest 失败。
- 测试资产：小样本入库（<10MB 总量），大文件走下载脚本 + 哈希校验。
