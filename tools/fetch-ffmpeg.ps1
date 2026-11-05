# 下载并解压 FFmpeg（BtbN 的 win64-lgpl-shared 预编译包）到 external/ffmpeg。
#
# 为什么用预编译：源码自建需要 MSYS2 + nasm，且 make 在受限环境下不稳定。
# 许可是 LGPL v3（shipping 前需复核），详见 Docs/BUILD.md §3.1。
#
# 用法：
#   pwsh tools/fetch-ffmpeg.ps1
#   pwsh tools/fetch-ffmpeg.ps1 -Url <自定义 zip 地址>

[CmdletBinding()]
param(
    [string]$Url = 'https://github.com/BtbN/FFmpeg-Builds/releases/download/latest/ffmpeg-n9.0-latest-win64-lgpl-shared-9.0.zip',
    [string]$Root = (Split-Path -Parent $PSScriptRoot)
)

$ErrorActionPreference = 'Stop'

$dest = Join-Path $Root 'external/ffmpeg'
$zip = Join-Path $env:TEMP 'chroma-ffmpeg.zip'
$extract = Join-Path $env:TEMP 'chroma-ffmpeg-extract'

if ((Test-Path (Join-Path $dest 'include/libavcodec/avcodec.h')) -and -not $PSBoundParameters.ContainsKey('Url')) {
    Write-Host "FFmpeg 已存在: $dest（删除该目录可重新下载）"
    exit 0
}

Write-Host "下载 FFmpeg: $Url"
[Net.ServicePointManager]::SecurityProtocol = [Net.SecurityProtocolType]::Tls12
Invoke-WebRequest -Uri $Url -OutFile $zip -UseBasicParsing

if (Test-Path $extract) { Remove-Item $extract -Recurse -Force }
Expand-Archive -Path $zip -DestinationPath $extract -Force

$inner = Get-ChildItem $extract -Directory | Select-Object -First 1
New-Item -ItemType Directory -Force -Path $dest | Out-Null

Copy-Item (Join-Path $inner.FullName 'include') $dest -Recurse -Force
Copy-Item (Join-Path $inner.FullName 'lib') $dest -Recurse -Force
New-Item -ItemType Directory -Force -Path (Join-Path $dest 'bin') | Out-Null
Copy-Item (Join-Path $inner.FullName 'bin/*.dll') (Join-Path $dest 'bin') -Force
Copy-Item (Join-Path $inner.FullName 'bin/ffprobe.exe') (Join-Path $dest 'bin') -Force
Copy-Item (Join-Path $inner.FullName 'LICENSE.txt') $dest -Force

Remove-Item $zip -Force
Remove-Item $extract -Recurse -Force

Write-Host "完成: $dest"
