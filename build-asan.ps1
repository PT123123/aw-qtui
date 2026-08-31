param([string]$Config = "Release")
$ErrorActionPreference = "Stop"
$vs    = "C:\Program Files\Microsoft Visual Studio\2022\Community"
$cmake = "$vs\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe"
$ninja = "$vs\Common7\IDE\CommonExtensions\Microsoft\CMake\Ninja\ninja.exe"
$qt    = "C:\Qt\6.8.3\msvc2022_64"
$root  = Split-Path -Parent $MyInvocation.MyCommand.Path
$build = Join-Path $root "build-asan"
$cfg   = $Config.ToLower()
$env:Qt6_DIR = "$qt\lib\cmake\Qt6"
$env:Path    = "$qt\bin;" + $env:Path
$bat = Join-Path $env:TEMP "awqtui_asan_$([guid]::NewGuid().ToString('N')).bat"
@"
@echo off
call "$vs\VC\Auxiliary\Build\vcvars64.bat" >nul 2>&1
if errorlevel 1 exit /b 1
"$cmake" -S "$root" -B "$build" -G Ninja -DCMAKE_BUILD_TYPE=$cfg -DCMAKE_MAKE_PROGRAM="$ninja" -DQt6_DIR="$env:Qt6_DIR" -DCMAKE_CXX_FLAGS="/fsanitize=address /Zi" -DCMAKE_EXE_LINKER_FLAGS="/fsanitize=address"
if errorlevel 1 exit /b 1
"$cmake" --build "$build" --config $cfg
if errorlevel 1 exit /b 1
"@ | Set-Content -Path $bat -Encoding ASCII
try {
    & cmd /c $bat
    if ($LASTEXITCODE -ne 0) { throw "ASan build failed" }
} finally {
    Remove-Item $bat -Force -ErrorAction SilentlyContinue
}
& "$qt\bin\windeployqt.exe" --release --no-translations --no-system-d3d-compiler --no-opengl-sw (Join-Path $build "awqtui.exe") | Out-Null
Write-Host ""
Write-Host "ASan build OK: $build\awqtui.exe"
