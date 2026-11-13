# <目录> · 开发说明

## 这个目录是什么
<一句话说明职责与边界>

## 文件与职责
| 文件/子目录 | 职责 |
|---|---|
| `<file>` | <职责> |

## 依赖关系
- 允许：<可依赖的模块/库>
- 禁止：<明确不允许的依赖>
- 被依赖：<谁依赖本目录>

## 构建与测试
- target：`<target 名>`
- 测试：`<ctest 名/运行方式>`

```powershell
cmake --build --preset windows-x64-debug --target <target>
ctest --preset windows-x64-debug -R <test>
```

## 怎么加新东西
1. <新文件放哪、需要注册什么>
2. <需要同步更新的文档/测试>

## 相关文档
<链接到 ROADMAP / AGENTS / 相邻 README>
