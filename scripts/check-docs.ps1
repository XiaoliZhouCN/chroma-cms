#Requires -Version 5.1
<#
.SYNOPSIS
    ChromaCMS 文档门禁。校验结构、元数据、链接、ADR 台账。

.DESCRIPTION
    退出码 0 = 全部通过；1 = 有失败项。
    任何提交前都必须通过本脚本（见 AGENTS.md 硬规则 6）。

.EXAMPLE
    scripts\check-docs.cmd
    # 或（PowerShell 7 / CI）
    pwsh scripts/check-docs.ps1
#>
[CmdletBinding()]
param(
    [string]$Root = ''
)

# 兼容 Windows PowerShell 5.1：$PSScriptRoot 在 param 默认值求值时可能尚未赋值
if ([string]::IsNullOrWhiteSpace($Root)) {
    $scriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path
    $Root = (Resolve-Path (Join-Path $scriptDir '..')).Path
}
$Root = [System.IO.Path]::GetFullPath($Root)

$ErrorActionPreference = 'Stop'

# ---------------------------------------------------------------------------
# 基础设施
# ---------------------------------------------------------------------------

$script:Failures = [System.Collections.Generic.List[object]]::new()
$script:Checks   = 0

$ValidStatuses = @(
    'draft', 'proposed', 'active', 'accepted',
    'deprecated', 'superseded', 'rejected'
)

$ExcludedDirs = @('.git', 'node_modules', 'build', 'out', 'external', 'vcpkg_installed', 'temp_and_scratch', '.vs', '.idea', '.vscode')

function Add-Failure {
    param([string]$Rule, [string]$File, [string]$Message, [int]$Line = 0)
    $script:Failures.Add([pscustomobject]@{
        Rule    = $Rule
        File    = $File
        Line    = $Line
        Message = $Message
    })
}

function Get-RelTo {
    param([string]$Base, [string]$Path)
    $b = [System.IO.Path]::GetFullPath($Base)
    if (-not $b.EndsWith([string][System.IO.Path]::DirectorySeparatorChar)) {
        $b += [System.IO.Path]::DirectorySeparatorChar
    }
    $p = [System.IO.Path]::GetFullPath($Path)
    $rel = ([System.Uri]::new($b)).MakeRelativeUri([System.Uri]::new($p)).ToString()
    return ([System.Uri]::UnescapeDataString($rel)).Replace('\', '/')
}

function Get-RelPath {
    param([string]$Path)
    return Get-RelTo -Base $Root -Path $Path
}

function Test-Excluded {
    param([string]$FullName)
    $rel = Get-RelPath $FullName
    foreach ($d in $ExcludedDirs) {
        if ($rel -eq $d -or $rel.StartsWith("$d/")) { return $true }
    }
    return $false
}

# ---------------------------------------------------------------------------
# 收集文件
# ---------------------------------------------------------------------------

$AllMarkdown = Get-ChildItem -LiteralPath $Root -Recurse -File -Filter '*.md' -Force |
    Where-Object { -not (Test-Excluded $_.FullName) }

$DocsMarkdown = Get-ChildItem -LiteralPath (Join-Path $Root 'Docs') -Recurse -File -Filter '*.md' -Force |
    Where-Object { -not (Test-Excluded $_.FullName) }

$IndexPath = [System.IO.Path]::GetFullPath((Join-Path $Root 'Docs/INDEX.md'))
$IndexText = if (Test-Path -LiteralPath $IndexPath) { Get-Content -LiteralPath $IndexPath -Raw -Encoding UTF8 } else { '' }

Write-Host "check-docs: $($AllMarkdown.Count) 个 markdown 文件" -ForegroundColor Cyan
Write-Host ""

# ---------------------------------------------------------------------------
# D001 / D002 / D003 / D004 / D005  元数据与体量
# ---------------------------------------------------------------------------

foreach ($file in $DocsMarkdown) {
    $rel   = Get-RelPath $file.FullName
    $lines = @(Get-Content -LiteralPath $file.FullName -Encoding UTF8)
    $script:Checks++

    # AGENTS.md 是控制文件，不要求 front-matter
    $isControlFile = ($file.Name -eq 'AGENTS.md')

    if ($lines.Count -eq 0) {
        Add-Failure 'D001' $rel '空文件' 0
        continue
    }

    $bodyStart = 0
    $hasFrontMatter = ($lines[0].Trim() -eq '---')

    if ($hasFrontMatter) {
        $end = -1
        for ($i = 1; $i -lt $lines.Count; $i++) {
            if ($lines[$i].Trim() -eq '---') { $end = $i; break }
        }
        if ($end -lt 0) {
            Add-Failure 'D002' $rel 'front-matter 未闭合' 1
            $bodyStart = 0
        }
        else {
            $bodyStart = $end + 1
            $fields = @{}
            for ($i = 1; $i -lt $end; $i++) {
                if ($lines[$i] -match '^\s*([A-Za-z_]+)\s*:\s*(.*)$') {
                    $fields[$Matches[1]] = $Matches[2].Trim()
                }
            }

            if (-not $fields.ContainsKey('status'))  { Add-Failure 'D002' $rel 'front-matter 缺少 status' 1 }
            if (-not $fields.ContainsKey('owner'))   { Add-Failure 'D002' $rel 'front-matter 缺少 owner' 1 }
            if (-not $fields.ContainsKey('updated')) { Add-Failure 'D002' $rel 'front-matter 缺少 updated' 1 }

            if ($fields.ContainsKey('status') -and $fields['status'] -notin $ValidStatuses) {
                Add-Failure 'D003' $rel "status 非法：'$($fields['status'])" 1
            }
            if ($fields.ContainsKey('owner') -and [string]::IsNullOrWhiteSpace($fields['owner'])) {
                Add-Failure 'D004' $rel 'owner 为空' 1
            }
            if ($fields.ContainsKey('updated')) {
                $raw = $fields['updated']
                $parsed = [datetime]::MinValue
                if (-not [datetime]::TryParseExact($raw, 'yyyy-MM-dd', [cultureinfo]::InvariantCulture, [System.Globalization.DateTimeStyles]::None, [ref]$parsed)) {
                    Add-Failure 'D005' $rel "updated 不是 ISO 日期：'$raw'" 1
                }
                elseif ($parsed.Date -gt (Get-Date).Date) {
                    Add-Failure 'D005' $rel "updated 是未来日期：'$raw'" 1
                }
            }
        }
    }
    elseif (-not $isControlFile) {
        Add-Failure 'D002' $rel '缺少 front-matter（首行应为 ---）' 1
    }

    # D001 体量：不允许"只有标题"的占位文档
    $bodyLines = @($lines | Select-Object -Skip $bodyStart | Where-Object { $_.Trim() -ne '' })
    $bodyBytes = [System.Text.Encoding]::UTF8.GetByteCount(($bodyLines -join "`n"))
    if ($bodyLines.Count -lt 5 -or $bodyBytes -lt 200) {
        Add-Failure 'D001' $rel "内容过少（$($bodyLines.Count) 行 / $bodyBytes 字节），疑似占位文档"
    }
}

# ---------------------------------------------------------------------------
# D006 / D007  链接
# ---------------------------------------------------------------------------

foreach ($file in $AllMarkdown) {
    $rel   = Get-RelPath $file.FullName
    $lines = @(Get-Content -LiteralPath $file.FullName -Encoding UTF8)
    $inFence = $false

    for ($i = 0; $i -lt $lines.Count; $i++) {
        $line = $lines[$i]

        if ($line -match '^\s*(```|~~~)') { $inFence = -not $inFence; continue }
        if ($inFence) { continue }

        # 去掉行内代码，避免误报
        $scan = $line -replace '`[^`]*`', ''

        foreach ($m in [regex]::Matches($scan, '\[[^\]]*\]\(([^)]+)\)')) {
            $script:Checks++
            $target = $m.Groups[1].Value.Trim()

            if ($target -match '^(https?|mailto|tel):') { continue }
            if ($target.StartsWith('#')) { continue }

            if ($target -match '\\') {
                Add-Failure 'D007' $rel "链接使用了反斜杠：'$target'" ($i + 1)
                continue
            }
            if ($target -match '^[A-Za-z]:' -or $target.StartsWith('file:')) {
                Add-Failure 'D007' $rel "链接使用了绝对路径：'$target'" ($i + 1)
                continue
            }

            $clean = ($target -split '#')[0]
            if ([string]::IsNullOrWhiteSpace($clean)) { continue }
            $clean = [System.Uri]::UnescapeDataString($clean)

            $baseDir = Split-Path -Parent $file.FullName
            $resolved = [System.IO.Path]::GetFullPath((Join-Path $baseDir $clean))

            if (-not (Test-Path -LiteralPath $resolved)) {
                Add-Failure 'D006' $rel "链接目标不存在：'$target'" ($i + 1)
            }
        }
    }
}

# ---------------------------------------------------------------------------
# D008  索引登记（Docs 下每份文档必须被 INDEX.md 链接）
# ---------------------------------------------------------------------------

if ($IndexText -eq '') {
    Add-Failure 'D008' 'Docs/INDEX.md' '索引文件缺失或为空'
}
else {
    $docsDir = Join-Path $Root 'Docs'
    foreach ($file in $DocsMarkdown) {
        $script:Checks++
        if ($file.FullName -eq $IndexPath) { continue }

        $relFromDocs = Get-RelTo -Base $docsDir -Path $file.FullName
        if (-not $IndexText.Contains($relFromDocs)) {
            Add-Failure 'D008' (Get-RelPath $file.FullName) "未在 Docs/INDEX.md 登记（应出现 '$relFromDocs'）"
        }
    }
}

# ---------------------------------------------------------------------------
# D009 / D010  ADR 编号与必填章节
# ---------------------------------------------------------------------------

$AdrDir = Join-Path $Root 'Docs/adr'
if (Test-Path -LiteralPath $AdrDir) {
    $AdrFiles = Get-ChildItem -LiteralPath $AdrDir -File -Filter '*.md' -Force |
        Where-Object { $_.Name -match '^\d{4}-' } |
        Sort-Object Name

    $expected = 1
    foreach ($adr in $AdrFiles) {
        $script:Checks++
        $number = [int]$adr.Name.Substring(0, 4)
        if ($number -ne $expected) {
            Add-Failure 'D009' (Get-RelPath $adr.FullName) "ADR 编号不连续：期望 $('{0:D4}' -f $expected)，实际 $($adr.Name.Substring(0,4))"
        }
        $expected = $number + 1

        $text = Get-Content -LiteralPath $adr.FullName -Raw -Encoding UTF8
        foreach ($section in @('## 背景', '## 决策', '## 后果', '## 替代方案')) {
            $script:Checks++
            if (-not $text.Contains($section)) {
                Add-Failure 'D010' (Get-RelPath $adr.FullName) "缺少必备章节：'$section'"
            }
        }

        if ($IndexText -ne '' -and -not $IndexText.Contains("adr/$($adr.Name)")) {
            Add-Failure 'D010' (Get-RelPath $adr.FullName) "未在 Docs/INDEX.md 的 ADR 台账中登记"
        }
    }
}

# ---------------------------------------------------------------------------
# D011  命名规范
# ---------------------------------------------------------------------------

foreach ($file in $DocsMarkdown) {
    $script:Checks++
    $rel = Get-RelPath $file.FullName
    $name = $file.Name
    $dir  = Split-Path -Parent $rel

    if ($name -match '\s') {
        Add-Failure 'D011' $rel '文件名包含空格'
    }
    if ($name -match '[^\x20-\x7E]') {
        Add-Failure 'D011' $rel '文件名包含非 ASCII 字符'
    }

    switch -Wildcard ($dir) {
        'Docs/adr' {
            if ($name -notmatch '^(\d{4}-[a-z0-9-]+|TEMPLATE|AGENTS)\.md$') {
                Add-Failure 'D011' $rel 'ADR 目录文件名应为 NNNN-kebab-case.md / TEMPLATE.md / AGENTS.md'
            }
        }
        'Docs/templates' {
            if ($name -notmatch '^[A-Z][A-Z0-9_]*\.md$') {
                Add-Failure 'D011' $rel '模板目录文件名应为 UPPER_SNAKE.md'
            }
        }
        'Docs' {
            if ($name -notmatch '^[A-Z][A-Z0-9_]*\.md$') {
                Add-Failure 'D011' $rel '文档文件名应为 UPPER_SNAKE.md'
            }
        }
    }
}

# 目录名必须小写（Docs 子树）
foreach ($dir in (Get-ChildItem -LiteralPath (Join-Path $Root 'Docs') -Recurse -Directory -Force)) {
    $script:Checks++
    if ($dir.Name -cne $dir.Name.ToLowerInvariant()) {
        Add-Failure 'D011' (Get-RelPath $dir.FullName) '目录名必须全小写'
    }
}

# ---------------------------------------------------------------------------
# D012  全仓库不得存在空文件
# ---------------------------------------------------------------------------

foreach ($file in (Get-ChildItem -LiteralPath $Root -Recurse -File -Force | Where-Object { -not (Test-Excluded $_.FullName) })) {
    $script:Checks++
    if ($file.Length -eq 0) {
        Add-Failure 'D012' (Get-RelPath $file.FullName) '空文件（0 字节）'
    }
}

# ---------------------------------------------------------------------------
# 输出
# ---------------------------------------------------------------------------

Write-Host ""

if ($script:Failures.Count -eq 0) {
    Write-Host "✔ check-docs 通过（$($script:Checks) 项检查，$($AllMarkdown.Count) 个文件）" -ForegroundColor Green
    exit 0
}

$grouped = $script:Failures | Group-Object Rule | Sort-Object Name
foreach ($group in $grouped) {
    Write-Host "[$($group.Name)] $($group.Count) 项" -ForegroundColor Yellow
    foreach ($f in $group.Group) {
        $loc = if ($f.Line -gt 0) { "$($f.File):$($f.Line)" } else { $f.File }
        Write-Host "  - $loc  $($f.Message)" -ForegroundColor Red
    }
    Write-Host ""
}

Write-Host "✘ check-docs 失败：$($script:Failures.Count) 个问题（共 $($script:Checks) 项检查）" -ForegroundColor Red
exit 1
