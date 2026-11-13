# 下载大样本 / 外部测试资产（当前列表为空，后续按需求登记）。
#
# 条目格式：@{ Url = "..."; Sha256 = "..."; Dest = "tests/corpus/external/<name>" }
# 规则：只下载明确登记的 URL；下载后校验 SHA-256；失败不覆盖已有文件。

$ErrorActionPreference = 'Stop'
Set-Location (Join-Path $PSScriptRoot '..\..')

$samples = @(
    # 示例（未启用）：
    # @{ Url = 'https://example.invalid/sample.heic'; Sha256 = '<sha256>'; Dest = 'tests/corpus/external/sample.heic' }
)

if ($samples.Count -eq 0) {
    Write-Host 'fetch-corpus: 当前没有登记的外部样本（列表为空）。'
    Write-Host '如需 HEIF/AVIF/视频等较大样本，请在 tools/scripts/fetch-corpus.ps1 里登记 URL + SHA-256。'
    exit 0
}

foreach ($sample in $samples) {
    $dest = Resolve-Path (Split-Path $sample.Dest -Parent) -ErrorAction SilentlyContinue
    if (-not $dest) {
        New-Item -ItemType Directory -Force -Path (Split-Path $sample.Dest -Parent) | Out-Null
    }
    Write-Host ("fetch-corpus: 下载 " + $sample.Url)
    Invoke-WebRequest -Uri $sample.Url -OutFile $sample.Dest
    $hash = (Get-FileHash -Algorithm SHA256 -Path $sample.Dest).Hash.ToLowerInvariant()
    if ($hash -ne $sample.Sha256.ToLowerInvariant()) {
        Remove-Item $sample.Dest -Force
        throw ("SHA-256 不匹配: " + $sample.Dest)
    }
    Write-Host ("fetch-corpus: ok " + $sample.Dest)
}
