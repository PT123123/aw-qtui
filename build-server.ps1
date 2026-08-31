# build-server.ps1 —— 单独构建 aw-inbox-rust（Windows 原生 MSVC）并部署到 build\server\
#
# 说明：
#   - aw-qtui 对 aw-inbox-rust 是「运行期进程 + REST 契约」依赖，编译期零链接。
#   - 源码默认取自 git submodule：
#       * vendor\aw-inbox                  —— 独立 crate（构建源，Cargo.toml 在仓库根）
#       * vendor\aw-server-rust\aw-inbox-rust —— 官方工作区成员（嵌套子模块，需先 init）
#   - 本脚本把服务端 crate 同步到本地 server-src\aw-inbox-rust，再用 Windows 原生 MSVC
#     工具链（vcvars64 + cargo）构建出 aw-inbox-rust.exe，产物放到 build\server\。
#   - aw-inbox-rust 是独立 crate（无 workspace 继承 / path 依赖），可脱离 aw-server-rust
#     工作区单独构建。
#
# Usage:
#   powershell -ExecutionPolicy Bypass -File build-server.ps1                       # 自动定位源码（submodule 优先）
#   powershell -ExecutionPolicy Bypass -File build-server.ps1 -ServerSrc D:\src\aw-inbox        # 独立 crate 根
#   powershell -ExecutionPolicy Bypass -File build-server.ps1 -ServerSrc D:\src\aw-server-rust  # workspace 根
#   powershell -ExecutionPolicy Bypass -File build-server.ps1 -Config Debug
#   powershell -ExecutionPolicy Bypass -File build-server.ps1 -SkipSync            # 复用已同步源码
#
# 环境变量：
#   AW_SERVER_SRC        服务端源码根目录（Windows/UNC 路径；可为独立 crate 根或含 aw-inbox-rust 的 workspace 根）
#   AW_SERVER_SRC_WSL    服务端源码根目录的 WSL Linux 路径（默认 /home/user/project/aw-android/aw-server-rust，仅兜底）
#
# 依赖：
#   - VS2022（含 C++ 桌面工作负载，提供 vcvars64 / cl.exe / link.exe）
#   - rustup + stable-x86_64-pc-windows-msvc（rusqlite bundled 的 C 代码由 cl.exe 编译）
param(
    [string]$ServerSrc,          # 服务端源码根目录（独立 aw-inbox crate 根，或含 aw-inbox-rust 的 workspace 根）
    [ValidateSet("Debug", "Release")]
    [string]$Config = "Release",
    [switch]$SkipSync            # 跳过源码同步（使用上次已同步的 server-src）
)
$ErrorActionPreference = "Stop"

$root   = Split-Path -Parent $MyInvocation.MyCommand.Path
$stage  = Join-Path $root "server-src"
$target = Join-Path $stage "target"          # CARGO_TARGET_DIR，与源码分离，跨同步增量复用
$build  = Join-Path $root "build"
$outDir = Join-Path $build "server"
$vs     = "C:\Program Files\Microsoft Visual Studio\2022\Community"
$vcvars = "$vs\VC\Auxiliary\Build\vcvars64.bat"

if (-not (Test-Path $vcvars)) { throw "vcvars64.bat 未找到: $vcvars（需要 VS2022 的 C++ 桌面工作负载）" }
if (-not (Get-Command cargo -ErrorAction SilentlyContinue)) { throw "cargo 不在 PATH（先安装 rustup: https://rustup.rs）" }

# ---------- 1) 定位服务端 crate 根目录 ----------
# 返回「含 aw-inbox-rust Cargo.toml 的 crate 根」。支持两种形态：
#   - 独立 crate：$Base 本身就有 Cargo.toml（如 vendor\aw-inbox）
#   - workspace 成员：$Base\aw-inbox-rust\Cargo.toml（如 vendor\aw-server-rust）
function Get-CrateRoot {
    param([string]$Base)
    if (-not $Base -or -not (Test-Path $Base)) { return $null }
    if (Test-Path (Join-Path $Base "Cargo.toml")) { return $Base }
    if (Test-Path (Join-Path $Base "aw-inbox-rust\Cargo.toml")) { return (Join-Path $Base "aw-inbox-rust") }
    return $null
}

function Resolve-CrateRoot {
    # 1) 显式参数
    if ($ServerSrc) {
        $r = Get-CrateRoot $ServerSrc
        if (-not $r) { throw "-ServerSrc 既不是独立 aw-inbox crate（含 Cargo.toml），也不含 aw-inbox-rust 子目录: $ServerSrc" }
        return $r
    }
    # 2) 环境变量
    if ($env:AW_SERVER_SRC) {
        $r = Get-CrateRoot $env:AW_SERVER_SRC
        if (-not $r) { throw "AW_SERVER_SRC 既不是独立 crate 也不含 aw-inbox-rust 子目录: $env:AW_SERVER_SRC" }
        return $r
    }
    # 3) git submodule（构建源优先 vendor\aw-inbox 独立 crate）
    $submodules = @(
        (Join-Path $root "vendor\aw-inbox"),
        (Join-Path $root "vendor\aw-server-rust\aw-inbox-rust")
    )
    foreach ($c in $submodules) {
        $r = Get-CrateRoot $c
        if ($r) { return $r }
    }
    # 4) submodule 已登记但未 checkout（空目录）→ 给出明确 git 命令
    foreach ($c in $submodules) {
        if (Test-Path $c) {
            throw "检测到 submodule 未初始化：$c 存在但为空。请先执行：`n  git submodule update --init --recursive`n（或改用 -ServerSrc / AW_SERVER_SRC 指定源码）"
        }
    }
    # 5) 兜底：WSL 工作区（旧版路径，注意含本地未提交修改风险）
    $distro = (wsl -l -q 2>$null | Where-Object { $_.Trim() } | Select-Object -First 1)
    if ($distro) {
        $distro = (($distro -replace '[^\x20-\x7E]', '').Trim() -replace '^\*\s*', '')
        $linuxPath = if ($env:AW_SERVER_SRC_WSL) { $env:AW_SERVER_SRC_WSL } else { "/home/user/project/aw-android/aw-server-rust" }
        $unc = (wsl -d $distro -- wslpath -w $linuxPath 2>$null).Trim()
        $r = Get-CrateRoot $unc
        if ($r) { return $r }
    }
    throw "未找到 aw-inbox-rust 源码。请先执行 git submodule update --init --recursive，或用 -ServerSrc 指定独立 crate / workspace 根目录，或设 AW_SERVER_SRC / AW_SERVER_SRC_WSL。"
}

$crateRoot = Resolve-CrateRoot
Write-Host "服务端 crate 根目录: $crateRoot" -ForegroundColor Cyan

# ---------- 2) 同步 aw-inbox-rust crate 到本地暂存（排除 target/.git） ----------
New-Item -ItemType Directory -Force -Path $stage | Out-Null
if (-not $SkipSync) {
    if (-not (Test-Path (Join-Path $crateRoot "Cargo.toml"))) { throw "crate 根缺少 Cargo.toml: $crateRoot" }
    Write-Host "同步 aw-inbox-rust 源码 → $stage\aw-inbox-rust ..."
    & robocopy $crateRoot (Join-Path $stage "aw-inbox-rust") /E /XD target .git node_modules /NFL /NDL /NJH /NJS | Out-Null
    if ($LASTEXITCODE -ge 8) { throw "robocopy 源码同步失败 (code=$LASTEXITCODE)" }
}

$manifest = Join-Path $stage "aw-inbox-rust\Cargo.toml"
if (-not (Test-Path $manifest)) { throw "暂存目录缺少 crate 清单: $manifest（先去掉 -SkipSync 重试）" }

# ---------- 3) vcvars + cargo 构建（临时 .bat 规避 PS 引号问题） ----------
$flag    = if ($Config -eq "Release") { "--release" } else { "" }
$profile = if ($Config -eq "Release") { "release" } else { "debug" }
$bat = Join-Path $env:TEMP "awinbox_build_$([guid]::NewGuid().ToString('N')).bat"
@"
@echo off
call "$vcvars" >nul 2>&1
if errorlevel 1 exit /b 1
cargo build $flag --manifest-path "$manifest"
if errorlevel 1 exit /b 1
"@ | Set-Content -Path $bat -Encoding ASCII
try {
    $env:CARGO_TARGET_DIR = $target
    & cmd /c $bat
    if ($LASTEXITCODE -ne 0) { throw "cargo 构建失败（见上方输出）" }
} finally {
    Remove-Item $bat -Force -ErrorAction SilentlyContinue
}

# ---------- 4) 部署产物 ----------
$exe = Join-Path $target "$profile\aw-inbox-rust.exe"
if (-not (Test-Path $exe)) { throw "构建产物缺失: $exe" }
New-Item -ItemType Directory -Force -Path $outDir | Out-Null
Copy-Item $exe $outDir -Force
$size = [math]::Round((Get-Item (Join-Path $outDir "aw-inbox-rust.exe")).Length / 1MB, 1)

Write-Host ""
Write-Host "Server build OK: $outDir\aw-inbox-rust.exe ($size MB)" -ForegroundColor Green
Write-Host "客户端侧当前为纯客户端模式，请自行启动服务端，或用 mock 联调："
Write-Host "  .\build\aw-inbox-rust.exe"
Write-Host "  .\build\awqtui.exe --url http://127.0.0.1:5600"
