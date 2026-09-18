# tools/make-ico.ps1 —— 把一组方形 PNG 组装成多尺寸 .ico（PNG 压缩条目，Vista+ 原生支持）。
#
# 为什么要静态 .ico：任务管理器 / 资源管理器只读 exe 内嵌的图标资源，
# 而程序图标是 theme.h 里运行时 QPainter 绘制的 QIcon，两者不会互通。
# 所以先用 `awqtui.exe --emit-icon <dir>` 导出 PNG，再由本脚本合成 resources/app.ico，
# 最后由 resources/app.rc 内嵌进 awqtui.exe；aw-server.exe 复用同一个文件。
#
# 用法：just icon（推荐）或 pwsh tools/make-ico.ps1 -PngDir build/icon-src -Out resources/app.ico
param(
    [Parameter(Mandatory = $true)][string]$PngDir,
    [Parameter(Mandatory = $true)][string]$Out
)
$ErrorActionPreference = 'Stop'

$images = @()
foreach ($f in (Get-ChildItem -LiteralPath $PngDir -Filter 'icon_*.png' -File | Sort-Object Name)) {
    if ($f.Name -match '^icon_(\d+)\.png$') {
        $images += [pscustomobject]@{ Size = [int]$Matches[1]; Bytes = [IO.File]::ReadAllBytes($f.FullName) }
    }
}
if (-not $images.Count) { throw "no icon_<size>.png found in $PngDir" }
foreach ($img in $images) {
    if ($img.Size -lt 1 -or $img.Size -gt 256) { throw "unsupported icon size $($img.Size) in $PngDir" }
}
# 目录按尺寸升序，首个条目即资源管理器/任务管理器取用的默认图标
$images = @($images | Sort-Object Size)

$ms = New-Object IO.MemoryStream
$bw = New-Object IO.BinaryWriter($ms)
# ICONDIR：保留 0 + 类型 1(图标) + 条目数
$bw.Write([uint16]0)
$bw.Write([uint16]1)
$bw.Write([uint16]$images.Count)

# ICONDIRENTRY（每条 16 字节）；宽/高 0 表示 256
$offset = 6 + 16 * $images.Count
foreach ($img in $images) {
    $dim = if ($img.Size -ge 256) { 0 } else { $img.Size }
    $bw.Write([byte]$dim)          # bWidth
    $bw.Write([byte]$dim)          # bHeight
    $bw.Write([byte]0)             # bColorCount
    $bw.Write([byte]0)             # bReserved
    $bw.Write([uint16]1)           # wPlanes
    $bw.Write([uint16]32)          # wBitCount
    $bw.Write([uint32]$img.Bytes.Length)
    $bw.Write([uint32]$offset)
    $offset += $img.Bytes.Length
}
foreach ($img in $images) { $bw.Write($img.Bytes) }
$bw.Flush()

$outDir = Split-Path -Parent $Out
if ($outDir) { New-Item -ItemType Directory -Force -Path $outDir | Out-Null }
[IO.File]::WriteAllBytes($Out, $ms.ToArray())
$bw.Dispose()

$names = ($images | ForEach-Object { $_.Size }) -join ', '
Write-Host "[make-ico] $($images.Count) sizes ($names) -> $Out ($((Get-Item -LiteralPath $Out).Length) bytes)"
