# build.ps1 —— aw-qtui 一键构建（MSVC 2022 + Ninja + Qt 6.8）
# 单独构建客户端（默认）：仅编译 awqtui.exe 并部署 Qt DLL。
# 联合构建（-WithServer）：在客户端之外，一并构建并部署 aw-inbox-rust.exe（sidecar 服务端）。
#
# Usage:
#   powershell -ExecutionPolicy Bypass -File build.ps1                      # 仅客户端
#   powershell -ExecutionPolicy Bypass -File build.ps1 -WithServer          # 客户端 + 服务端
#   powershell -ExecutionPolicy Bypass -File build.ps1 -WithServer -ServerSrc D:\src\aw-server-rust
#   powershell -ExecutionPolicy Bypass -File build.ps1 -Config Debug
#
# 说明：-WithServer 会把产物部署到 build\server\aw-inbox-rust.exe；客户端侧的
#       「运行时自动 spawn」属后续里程碑（M2），当前仍需自行启动服务端或用 mock 联调。
param(
    [switch]$WithServer,      # 联合构建：一并构建并部署 aw-inbox-rust.exe
    [string]$ServerSrc,       # 服务端源码根目录（可选，传给 build-server.ps1）
    [ValidateSet("Debug", "Release")]
    [string]$Config = "Release"
)
$ErrorActionPreference = "Stop"

$vs    = "C:\Program Files\Microsoft Visual Studio\2022\Community"
$cmake = "$vs\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe"
$ninja = "$vs\Common7\IDE\CommonExtensions\Microsoft\CMake\Ninja\ninja.exe"
$qt    = "C:\Qt\6.8.3\msvc2022_64"
$root  = Split-Path -Parent $MyInvocation.MyCommand.Path
$build = Join-Path $root "build"
$cfg   = $Config.ToLower()

if (-not (Test-Path $qt))    { throw "Qt not found: $qt (install via: aqt install-qt windows desktop 6.8.3 win64_msvc2022_64 -O C:\Qt)" }
if (-not (Test-Path $cmake)) { throw "CMake not found: $cmake" }

$env:Qt6_DIR = "$qt\lib\cmake\Qt6"
$env:Path    = "$qt\bin;" + $env:Path

# vcvars64 must run in the same cmd process as cmake; write a temp bat to avoid PS quoting hell
$bat = Join-Path $env:TEMP "awqtui_build_$([guid]::NewGuid().ToString('N')).bat"
@"
@echo off
call "$vs\VC\Auxiliary\Build\vcvars64.bat" >nul 2>&1
if errorlevel 1 exit /b 1
"$cmake" -S "$root" -B "$build" -G Ninja -DCMAKE_BUILD_TYPE=$cfg -DCMAKE_MAKE_PROGRAM="$ninja" -DQt6_DIR="$env:Qt6_DIR"
if errorlevel 1 exit /b 1
"$cmake" --build "$build" --config $cfg
if errorlevel 1 exit /b 1
"@ | Set-Content -Path $bat -Encoding ASCII

try {
    & cmd /c $bat
    if ($LASTEXITCODE -ne 0) { throw "Build failed (see output above)" }
} finally {
    Remove-Item $bat -Force -ErrorAction SilentlyContinue
}

# Deploy Qt DLLs next to the exe
& "$qt\bin\windeployqt.exe" --release --no-translations --no-system-d3d-compiler --no-opengl-sw (Join-Path $build "awqtui.exe") | Out-Null

Write-Host ""
Write-Host "Client build OK: $build\awqtui.exe" -ForegroundColor Green

# 联合构建：服务端 sidecar
if ($WithServer) {
    Write-Host "==> 联合构建：构建 aw-inbox-rust.exe ..." -ForegroundColor Yellow
    $args2 = @("-Config", $Config)
    if ($ServerSrc) { $args2 += @("-ServerSrc", $ServerSrc) }
    & powershell -NoProfile -ExecutionPolicy Bypass -File (Join-Path $root "build-server.ps1") @args2
    if ($LASTEXITCODE -ne 0) { throw "Server build failed (see output above)" }
    Write-Host "Combined build OK: $build\awqtui.exe + $build\server\aw-inbox-rust.exe" -ForegroundColor Green
}

Write-Host ""
Write-Host "Run:     & `"$build\awqtui.exe`" --url http://127.0.0.1:5600"
if ($WithServer) {
    Write-Host "Server:  & `"$build\server\aw-inbox-rust.exe`"   # 当前需自行启动，spawn 属 M2 规划"
}
