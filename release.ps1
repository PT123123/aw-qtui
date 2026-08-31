# release.ps1 —— 联合构建 + 打包发布（awqtui.exe + Qt DLL/plugins + aw-inbox-rust.exe）
#
# Usage:
#   powershell -ExecutionPolicy Bypass -File release.ps1                 # 完整构建（客户端+服务端）并打包
#   powershell -ExecutionPolicy Bypass -File release.ps1 -Version 0.2.0  # 指定版本号（默认读 CMakeLists.txt）
#   powershell -ExecutionPolicy Bypass -File release.ps1 -SkipBuild      # 复用已有 build/（须含 server 产物）
#   powershell -ExecutionPolicy Bypass -File release.ps1 -ServerSrc D:\src\aw-server-rust
#
# 产物：
#   dist\aw-qtui-<ver>-win64\awqtui.exe
#   dist\aw-qtui-<ver>-win64\aw-inbox-rust.exe
#   dist\aw-qtui-<ver>-win64.zip
param(
    [string]$Version,        # 版本号，缺省从 CMakeLists.txt 的 project(aw-qtui VERSION ...) 读取
    [switch]$SkipBuild,      # 跳过构建，直接打包已有 build/
    [string]$ServerSrc       # 服务端源码根目录（可选，透传给 build.ps1 / build-server.ps1）
)
$ErrorActionPreference = "Stop"

$root  = Split-Path -Parent $MyInvocation.MyCommand.Path
$build = Join-Path $root "build"

# ---------- 1) 版本号 ----------
if (-not $Version) {
    $m = Select-String -Path (Join-Path $root "CMakeLists.txt") -Pattern 'project\(aw-qtui\s+VERSION\s+([0-9]+\.[0-9]+\.[0-9]+)'
    if ($m) { $Version = $m.Matches[0].Groups[1].Value }
}
if (-not $Version) { $Version = "0.1.0" }
$ver = $Version.TrimStart('v')
Write-Host "版本号: $ver" -ForegroundColor Cyan

# ---------- 2) 构建（可选） ----------
if (-not $SkipBuild) {
    if ($ServerSrc) {
        & "$root\build.ps1" -WithServer -ServerSrc $ServerSrc
    } else {
        & "$root\build.ps1" -WithServer
    }
    if ($LASTEXITCODE -ne 0) { throw "构建失败（见上方输出）" }
}

# ---------- 3) 校验产物 ----------
foreach ($rel in @("awqtui.exe", "server\aw-inbox-rust.exe")) {
    $p = Join-Path $build $rel
    if (-not (Test-Path $p)) {
        throw "产物缺失: $p`n请先执行: powershell -ExecutionPolicy Bypass -File build.ps1 -WithServer（或 release.ps1 -SkipBuild 复用已有产物）"
    }
}

# ---------- 4) 组装发布目录 ----------
$dist = Join-Path $root "dist\aw-qtui-$ver-win64"
if (Test-Path $dist) { Remove-Item $dist -Recurse -Force }
New-Item -ItemType Directory -Force -Path $dist | Out-Null

# 只拷贝运行所需内容：exe + Qt DLL/plugins + server\，剔除 CMake/Ninja 中间产物
$exclude = @('CMakeCache.txt', 'CMakeFiles', 'cmake_install.cmake', 'build.ninja',
             'CTestTestfile.cmake', 'Makefile', 'awqtui_autogen', 'server-src')
Get-ChildItem $build |
    Where-Object { $_.Name -notin $exclude -and $_.Name -notlike '*.obj' -and
                   $_.Name -notlike '*.ilk' -and $_.Name -notlike '*.pdb' -and
                   $_.Name -notlike '.ninja*' } |
    Copy-Item -Destination $dist -Recurse -Force

Copy-Item (Join-Path $root "README.md") $dist -Force

# ---------- 5) 打包 zip ----------
$zip = Join-Path $root "dist\aw-qtui-$ver-win64.zip"
if (Test-Path $zip) { Remove-Item $zip -Force }
Compress-Archive -Path (Join-Path $dist "*") -DestinationPath $zip -CompressionLevel Optimal

$exeSize    = [math]::Round((Get-Item (Join-Path $dist "awqtui.exe")).Length / 1MB, 1)
$srvSize    = [math]::Round((Get-Item (Join-Path $dist "server\aw-inbox-rust.exe")).Length / 1MB, 1)
$zipSize    = [math]::Round((Get-Item $zip).Length / 1MB, 1)
$distSizeMB = [math]::Round(((Get-ChildItem $dist -Recurse -File | Measure-Object Length -Sum).Sum) / 1MB, 1)

Write-Host ""
Write-Host "Release OK: $dist" -ForegroundColor Green
Write-Host "  awqtui.exe          $exeSize MB"
Write-Host "  aw-inbox-rust.exe   $srvSize MB"
Write-Host "  目录合计            $distSizeMB MB"
Write-Host "  zip                 $zip ($zipSize MB)"
