---
extends: ../AGENTS.md
scope: tools
owner: agent
last_verified: 2026-11-13
---
# tools · AGENTS 本地扩展

## 继承
继承根 `AGENTS.md`。未在此声明的规则沿用父级；根 §1 硬规则不可覆写。

## 本地约定
- 工具只用于开发/测试，**不得**被 app 或库反向依赖。
- 每个工具一个子目录；工具之间不互相依赖。
- 检查类工具（如 doc-check）失败必须返回非 0，方便 ctest/CI 阻断。

## 验证入口
```powershell
cmake --build --preset windows-x64-debug --target doc_check
ctest --preset windows-x64-debug -R docs.structure
```

## 本目录专属坑
- doc_check 用 C++20 `std::filesystem`；路径一律用绝对路径，避免 ctest 工作目录差异。
