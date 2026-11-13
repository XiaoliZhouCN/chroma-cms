---
extends: <父目录>/AGENTS.md
scope: <相对仓库根的目录路径>
owner: agent
last_verified: <YYYY-MM-DD>
---
# <目录> · AGENTS 本地扩展

## 继承
继承 `<父目录>/AGENTS.md` 与上级 `AGENTS.md`。未在此声明的规则沿用父级；
根 §1 硬规则不可覆写。

## 本地约定
- <本目录特有的约束/边界，只写 delta>

（若没有本地扩展，删除上面这条并写：）
（无本地扩展：完全继承父目录约定。本文件用于消除"规则缺失"的歧义。）

## 验证入口
```powershell
cmake --build --preset windows-x64-debug
ctest --preset windows-x64-debug
```

## 本目录专属坑
- <只属于本目录的坑>
