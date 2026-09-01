# release.ps1 —— 联合构建 + 打包发布（awqtui.exe + Qt DLL/plugins + aw-inbox-rust.exe）
#
# Usage:
#   powershell -ExecutionPolicy Bypass -File release.ps1                 # 完整构建（客户端+服务端）并打包，版本默认 +0.01 自动递增
#   powershell -ExecutionPolicy Bypass -File release.ps1 -Version 0.2.0  # 指定版本号（默认在 CMakeLists.txt 当前版本上 patch +0.01）
#   powershell -ExecutionPolicy Bypass -File release.ps1 -SkipBuild      # 复用已有 build/（默认仍须含 server 产物）
#   powershell -ExecutionPolicy Bypass -File release.ps1 -SkipServer     # 显式声明「不带服务端」，发布纯客户端包
#   powershell -ExecutionPolicy Bypass -File release.ps1 -ServerSrc D:\src\aw-server-rust
#
# 默认行为：release 必带 aw-inbox-rust.exe（联合构建）。只有显式传 -SkipServer 才允许发布不含服务端的包；
# 缺省（未声称不带）而服务端产物缺失时直接报错，绝不静默产出不带服务端的发布包。
#
# 版本号：默认在 CMakeLists.txt 当前版本上 patch +0.01（0.1.0 → 0.1.1），打包成功后写回 CMakeLists.txt，
#         下次 release 继续 +0.01（版本不重复）；显式传 -Version 时按给定版本，不自动加、也不写回。
#
# 产物：
#   dist\aw-qtui-<ver>-win64\awqtui.exe
#   dist\aw-qtui-<ver>-win64\aw-inbox-rust.exe      （-SkipServer 时不包含）
#   dist\aw-qtui-<ver>-win64.zip
param(
    [string]$Version,        # 版本号；缺省读取 CMakeLists.txt 当前版本并 patch +0.01 自动递增（打包成功后写回）
    [switch]$SkipBuild,      # 跳过构建，直接打包已有 build/
    [switch]$SkipServer,     # 显式声明「不带服务端」：默认必带，只有传此开关才允许发布纯客户端包
    [string]$ServerSrc       # 服务端源码根目录（可选，透传给 build.ps1 / build-server.ps1）
)
$ErrorActionPreference = "Stop"

$root  = Split-Path -Parent $MyInvocation.MyCommand.Path
$build = Join-Path $root "build"
# 默认必带服务端；仅显式 -SkipServer 才允许不带
$bundleServer = -not $SkipServer

# ---------- 1) 版本号 ----------
# 默认：在 CMakeLists.txt 当前版本上 patch +0.01（0.1.0 → 0.1.1），打包成功后写回 CMakeLists.txt，
# 保证下次 release 继续 +0.01；显式传 -Version 时不自动加、也不写回。
function Bump-PatchVersion {
    param([string]$v)
    $parts = $v -split '\.'
    if ($parts.Count -ne 3) { throw "无法解析版本号（需 X.Y.Z）: $v" }
    $patch = [int]$parts[2] + 1
    return "$($parts[0]).$($parts[1]).$patch"
}

$cmakePath = Join-Path $root "CMakeLists.txt"
$bumpBase  = $null
$bumpLine  = $null
$bumped    = $false
if (-not $Version) {
    $m = Select-String -Path $cmakePath -Pattern 'project\(aw-qtui\s+VERSION\s+([0-9]+\.[0-9]+\.[0-9]+)' | Select-Object -First 1
    if ($m) {
        $bumpBase = $m.Matches[0].Groups[1].Value
        $bumpLine = $m.Line
        $Version  = Bump-PatchVersion $bumpBase
        $bumped   = $true
    }
}
if (-not $Version) { $Version = "0.1.0" }
$ver = $Version.TrimStart('v')
if ($bumped) {
    Write-Host "版本号: $bumpBase → $ver（打包成功后写回 CMakeLists.txt）" -ForegroundColor Cyan
} else {
    Write-Host "版本号: $ver" -ForegroundColor Cyan
}

# ---------- 2) 构建（可选） ----------
if (-not $SkipBuild) {
    if ($bundleServer) {
        # 默认：联合构建（客户端 + 服务端）
        if ($ServerSrc) {
            & "$root\build.ps1" -WithServer -ServerSrc $ServerSrc
        } else {
            & "$root\build.ps1" -WithServer
        }
    } else {
        # 显式 -SkipServer：只构建客户端
        & "$root\build.ps1"
    }
    if ($LASTEXITCODE -ne 0) { throw "构建失败（见上方输出）" }
}

# ---------- 3) 校验产物 ----------
# 默认必带服务端；服务端产物缺失即报错（含 -SkipBuild 复用场景）。
# 只有显式 -SkipServer 才放行「不带服务端」。
$required = @("awqtui.exe")
if ($bundleServer) { $required += "server\aw-inbox-rust.exe" }
foreach ($rel in $required) {
    $p = Join-Path $build $rel
    if (-not (Test-Path $p)) {
        if ($rel -eq "server\aw-inbox-rust.exe") {
            throw "产物缺失: $p`n默认 release 必须包含服务端（aw-inbox-rust.exe）。`n若确实要发布不带服务端的包，请显式传 -SkipServer。`n正常构建请先执行: powershell -ExecutionPolicy Bypass -File build.ps1 -WithServer"
        }
        throw "产物缺失: $p`n请先执行: powershell -ExecutionPolicy Bypass -File build.ps1（或 release.ps1 -SkipBuild 复用已有产物）"
    }
}

# ---------- 4) 组装发布目录 ----------
$dist = Join-Path $root "dist\aw-qtui-$ver-win64"
if (Test-Path $dist) { Remove-Item $dist -Recurse -Force }
New-Item -ItemType Directory -Force -Path $dist | Out-Null

# 只拷贝运行所需内容：exe + Qt DLL/plugins + server\（除非显式 -SkipServer），剔除 CMake/Ninja 中间产物
$exclude = @('CMakeCache.txt', 'CMakeFiles', 'cmake_install.cmake', 'build.ninja',
             'CTestTestfile.cmake', 'Makefile', 'awqtui_autogen', 'server-src')
if ($SkipServer) { $exclude += 'server' }
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
$srvSize    = 0
if ($bundleServer) { $srvSize = [math]::Round((Get-Item (Join-Path $dist "server\aw-inbox-rust.exe")).Length / 1MB, 1) }
$zipSize    = [math]::Round((Get-Item $zip).Length / 1MB, 1)
$distSizeMB = [math]::Round(((Get-ChildItem $dist -Recurse -File | Measure-Object Length -Sum).Sum) / 1MB, 1)

Write-Host ""
Write-Host "Release OK: $dist" -ForegroundColor Green
Write-Host "  awqtui.exe          $exeSize MB"
if ($bundleServer) {
    Write-Host "  aw-inbox-rust.exe   $srvSize MB"
} else {
    Write-Host "  server              (未包含 - 显式 -SkipServer)"
}
Write-Host "  目录合计            $distSizeMB MB"
Write-Host "  zip                 $zip ($zipSize MB)"

# ---------- 6) 写回版本号（仅默认自动 +0.01 时；打包失败不会走到这里，不消耗版本号） ----------
if ($bumped) {
    $utf8 = New-Object System.Text.UTF8Encoding($false)
    $content = [System.IO.File]::ReadAllText($cmakePath, $utf8)
    $newLine = $bumpLine -replace [regex]::Escape($bumpBase), $ver
    $content = $content.Replace($bumpLine, $newLine)
    [System.IO.File]::WriteAllText($cmakePath, $content, $utf8)
    Write-Host ""
    Write-Host "已写回 CMakeLists.txt: VERSION $bumpBase → $ver（下次 release 将基于 $ver 继续 +0.01）" -ForegroundColor Yellow
}
