# 一键收集 Chroma ColorPicker 的 HDR 验证信息。
#
# 用法：
#   cd _shirley\tools\colorpicker
#   powershell -NoProfile -ExecutionPolicy Bypass -File .\verify.ps1
#
# 输出：verification-YYYYMMDD-HHmmss.txt（当前目录）
# 说明：只读系统信息 + 调用 colorpicker --list/--dump，不修改任何系统设置。

param(
    [string]$Exe = (Join-Path $PSScriptRoot 'build\Debug\colorpicker.exe'),
    [string]$OutFile = (Join-Path $PSScriptRoot ('verification-' + (Get-Date -Format 'yyyyMMdd-HHmmss') + '.txt'))
)

$ErrorActionPreference = 'Stop'
$lines = New-Object System.Collections.Generic.List[string]

function Add-Line([string]$text) {
    $lines.Add($text) | Out-Null
    Write-Host $text
}

function Add-Section([string]$title) {
    Add-Line ''
    Add-Line ('=' * 70)
    Add-Line "== $title"
    Add-Line ('=' * 70)
}

Add-Line "Chroma ColorPicker verification report"
Add-Line ("Generated: " + (Get-Date -Format 'yyyy-MM-dd HH:mm:ss zzz'))
Add-Line ("Exe: " + $Exe)

Add-Section '1. Session / OS'
try {
    Add-Type -AssemblyName System.Windows.Forms
    Add-Line ("SESSIONNAME = " + $env:SESSIONNAME)
    Add-Line ("TerminalServerSession = " + [System.Windows.Forms.SystemInformation]::TerminalServerSession)
} catch {
    Add-Line ("会话查询失败: " + $_.Exception.Message)
}
$cv = Get-ItemProperty 'HKLM:\SOFTWARE\Microsoft\Windows NT\CurrentVersion'
Add-Line ("ProductName = " + $cv.ProductName)
Add-Line ("Build = " + $cv.CurrentBuild + "." + $cv.UBR)
Add-Line ("DisplayVersion = " + $cv.DisplayVersion)
$principal = New-Object Security.Principal.WindowsPrincipal([Security.Principal.WindowsIdentity]::GetCurrent())
Add-Line ("IsAdmin = " + $principal.IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator))
Get-Service -Name 'CaptureService*' -ErrorAction SilentlyContinue | ForEach-Object {
    Add-Line ("Service: " + $_.Name + " status=" + $_.Status + " start=" + $_.StartType)
}

Add-Section '2. GPU / current display mode'
Get-CimInstance Win32_VideoController | ForEach-Object {
    $gpuLine = "GPU: {0} | driver={1} | mode={2} | {3}x{4}@{5}Hz | bpp={6}" -f $_.Name, $_.DriverVersion, $_.VideoModeDescription, $_.CurrentHorizontalResolution, $_.CurrentVerticalResolution, $_.CurrentRefreshRate, $_.CurrentBitsPerPixel
    Add-Line $gpuLine
}

Add-Section '3. dxdiag HDR fields'
$dx = Join-Path $env:TEMP ("colorpicker-dxdiag-" + [guid]::NewGuid().ToString('N') + ".txt")
try {
    $dxArgs = @('/t', $dx)
    $dxProc = Start-Process -FilePath 'dxdiag.exe' -ArgumentList $dxArgs -PassThru -WindowStyle Hidden
    $dxProc.WaitForExit(30000) | Out-Null
    for ($i = 0; $i -lt 20 -and -not (Test-Path $dx); $i++) { Start-Sleep -Milliseconds 250 }
    if (Test-Path $dx) {
        $patterns = 'Card name|Current Mode|HDR Support|Display Color Space|Monitor Name|Monitor Capabilities|Advanced Color|Active Color Mode|Driver Version'
        Select-String -Path $dx -Pattern $patterns -Encoding UTF8 |
            ForEach-Object { Add-Line $_.Line.Trim() }
    } else {
        Add-Line 'dxdiag 未生成报告文件'
    }
} catch {
    Add-Line ("dxdiag 调用失败: " + $_.Exception.Message)
} finally {
    if (Test-Path $dx) { Remove-Item $dx -Force -ErrorAction SilentlyContinue }
}

Add-Section '4. colorpicker --list'
if (-not (Test-Path $Exe)) {
    Add-Line ("找不到可执行文件: " + $Exe)
    Add-Line '请先按 VERIFICATION.md §3.2 构建。'
} else {
    $listOutput = & $Exe --list 2>&1
    $listText = ($listOutput | Out-String)
    Add-Line $listText

    Add-Section '5. colorpicker --dump per monitor center'
    foreach ($line in $listOutput) {
        $text = [string]$line
        if ($text -match 'monitor=(?<name>\S+)') { $name = $Matches['name'] } else { $name = '?' }
        if ($text -match 'rect=\((?<x1>-?\d+),(?<y1>-?\d+)\)-\((?<x2>-?\d+),(?<y2>-?\d+)\)') {
            $x1 = [int]$Matches['x1']; $y1 = [int]$Matches['y1']
            $x2 = [int]$Matches['x2']; $y2 = [int]$Matches['y2']
            $cx = [int](($x1 + $x2) / 2)
            $cy = [int](($y1 + $y2) / 2)
            Add-Line ''
            Add-Line ("--- " + $name + " center (" + $cx + "," + $cy + ") ---")
            $dumpOutput = & $Exe --dump --x $cx --y $cy 2>&1
            Add-Line (($dumpOutput | Out-String))
        }
    }
}

Add-Section 'Done'
Add-Line ("Report saved to: " + $OutFile)
$lines | Set-Content -Path $OutFile -Encoding UTF8
Write-Host ''
Write-Host ("report: " + $OutFile)
