# tests/corpus · 测试样本

## 这个目录是什么
小样本测试资产（入库，总量 < 10 MB）：供 `unit.container_probe` 与后续 integration 测试使用。

## 文件与用途
| 路径 | 用途 |
|---|---|
| `png/rgba8-plain.png` | 无色彩元数据的 PNG（默认假设路径） |
| `png/rgb8-srgb.png` | 带 sRGB chunk 的 PNG |
| `png/rgba16.png` | 16-bit RGBA PNG（位深路径） |
| `jpeg/rgb8-no-icc.jpg` | 无 ICC 的 JPEG（默认假设路径） |
| `tiff/rgb8-single.tif` | 单页 TIFF |
| `tiff/rgb8-multipage.tif` | 双页 TIFF（帧/页语义） |
| `gif/static.gif` / `gif/animated.gif` | 静态/动画 GIF（帧计数） |
| `webp/rgb8.webp` | 静态 WebP |
| `exr/rgb-half.exr` | 最小 half EXR（线性/浮点路径） |

## 重新生成
样本由脚本确定性生成（开发依赖 Pillow；构建/测试不需要）：

```powershell
python tools/scripts/gen-corpus.py
```

## 大文件
不放在仓库：用 `tools/scripts/fetch-corpus.ps1` 下载并校验 SHA-256（当前外部样本列表为空，后续在
`docs/color/` 文章里登记 URL）。

## 相关文档
`docs/color/01-container-and-frame-basics.md`、`docs/testing/00-index.md`
