# vcenv.ps1 —— 注入 VC / Windows SDK 环境，使 cl / link / ninja / cmake / rc / mt / cargo 可用。
#
# 由 justfile 的 recipe 通过 dot-source 调用（必须在同一进程内，环境变量才会保留）：
#     . '<justfile_directory>/tools/vcenv.ps1'
#
# 读取以下环境变量覆盖（缺省走默认值并自动探测）：
#     VS_DIR  SDKROOT  VCVER  SDKVERSION  QT  VSWHERE
#
# 说明：本脚本是原 tools/vcenv.sh 的 PowerShell 7 等价实现。PowerShell 天生使用 Windows
#       原生路径与分号(;)分隔的 PATH，因此不再需要 MSYS / cygpath 那套路径形式转换：
#         - 原脚本 PATH 用 POSIX 形式(/c/...) 只为让 MSYS bash 能 stat 到可执行文件；
#         - INCLUDE/LIB 本来就要求 Windows 形式，这里直接用。
#       另外 dot-source 后 $vsDir / $vcVer / $sdkRoot / $sdkVer / $qtRoot 会留在调用方
#       作用域内，recipe 可直接引用（PowerShell 变量名大小写不敏感，$sdkroot 亦可）。

$ErrorActionPreference = 'Stop'

$vsDir = if ($env:VS_DIR) { $env:VS_DIR } else { 'C:\Program Files\Microsoft Visual Studio\2022\Community' }
$sdkRoot = if ($env:SDKROOT) { $env:SDKROOT } else { 'C:\Program Files (x86)\Windows Kits\10' }
$vcVer = if ($env:VCVER) { $env:VCVER } else { '' }
$sdkVer = if ($env:SDKVERSION) { $env:SDKVERSION } else { '' }
$qtRoot = if ($env:QT) { $env:QT } else { 'C:\Qt\6.8.3\msvc2022_64' }
$vsWhere = if ($env:VSWHERE) { $env:VSWHERE } else { 'C:\Program Files (x86)\Microsoft Visual Studio\Installer\vswhere.exe' }

# vswhere 自动探测 VS 安装路径（存在才探测，避免缺 vswhere 时整条 recipe 失败）
if (Test-Path -LiteralPath $vsWhere) {
    $detected = & $vsWhere -latest -property installationPath
    if ($LASTEXITCODE -eq 0 -and $detected) {
        $vsDir = ($detected | Select-Object -First 1).Trim()
    }
}

# MSVC 工具集版本：取 VC\Tools\MSVC 下字典序最大的目录
if (-not $vcVer) {
    $msvcRoot = Join-Path $vsDir 'VC\Tools\MSVC'
    if (Test-Path -LiteralPath $msvcRoot) {
        $vcVer = Get-ChildItem -LiteralPath $msvcRoot -Directory |
            Sort-Object -Property Name | Select-Object -Last 1 -ExpandProperty Name
    }
}
if (-not $vcVer) {
    throw "[vc-env] 未找到 MSVC 工具集：$vsDir\VC\Tools\MSVC（可用 VS_DIR / VCVER 覆盖）"
}

# Windows SDK 版本：取 Include 下形如 10.0.x.y 的最大目录，探测不到则退回已知版本
if (-not $sdkVer) {
    $sdkIncludeRoot = Join-Path $sdkRoot 'Include'
    if (Test-Path -LiteralPath $sdkIncludeRoot) {
        $sdkVer = Get-ChildItem -LiteralPath $sdkIncludeRoot -Directory |
            Where-Object { $_.Name -match '^10\.0\.\d+\.\d+$' } |
            Sort-Object -Property Name | Select-Object -Last 1 -ExpandProperty Name
    }
}
if (-not $sdkVer) {
    $sdkVer = '10.0.26100.0'
}

# INCLUDE / LIB：原生 cl.exe / link.exe 读取，分号分隔的 Windows 路径
$env:INCLUDE = "$vsDir\VC\Tools\MSVC\$vcVer\include;$sdkRoot\Include\$sdkVer\ucrt;$sdkRoot\Include\$sdkVer\um;$sdkRoot\Include\$sdkVer\shared"
$env:LIB = "$vsDir\VC\Tools\MSVC\$vcVer\lib\x64;$sdkRoot\Lib\$sdkVer\um\x64;$sdkRoot\Lib\$sdkVer\ucrt\x64"

# PATH：cl / ninja / cmake / rc / mt / windeployqt / cargo 的查找路径（PowerShell 用分号）
$env:PATH = @(
    "$vsDir\VC\Tools\MSVC\$vcVer\bin\Hostx64\x64"
    "$sdkRoot\bin\$sdkVer\x64"
    "$vsDir\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin"
    "$vsDir\Common7\IDE\CommonExtensions\Microsoft\CMake\Ninja"
    "$qtRoot\bin"
    "$env:USERPROFILE\.cargo\bin"
    $env:PATH
) -join ';'

Write-Host "[vc-env] VS=$vsDir  VCVER=$vcVer  SDK=$sdkVer"
