#!/usr/bin/env bash
# 注入 VC / Windows SDK 环境，使 cl / link / ninja / cmake / rc / mt / cargo 可用。
# 由 justfile 的各 recipe 通过 `source` 调用；读取以下环境变量覆盖（缺省走默认值并自动探测）：
#   VS_DIR  SDKROOT  VCVER  SDKVERSION  QT  VSWHERE
set -euo pipefail
export MSYS_NO_PATHCONV=1

vsdir="${VS_DIR:-C:/Program Files/Microsoft Visual Studio/2022/Community}"
sdkroot="${SDKROOT:-C:/Program Files (x86)/Windows Kits/10}"
vcver="${VCVER:-}"
sdkver="${SDKVERSION:-}"
qt="${QT:-C:/Qt/6.8.3/msvc2022_64}"
vswhere="${VSWHERE:-C:/Program Files (x86)/Microsoft Visual Studio/Installer/vswhere.exe}"

[ -x "$vswhere" ] && vsdir="$("$vswhere" -latest -property installationPath 2>/dev/null)"
# vswhere 返回反斜杠路径，MSYS bash 在 PATH 查找时无法 stat；统一转成正斜杠
# （保留 C:/ 盘符形式，cl.exe / cmake 等原生 Windows 程序同样能识别）。
vsdir="${vsdir//\\//}"
sdkroot="${sdkroot//\\//}"
[ -z "$vcver" ] && vcver="$(ls "$vsdir/VC/Tools/MSVC" 2>/dev/null | sort | tail -1)"
[ -z "$sdkver" ] && sdkver="$(ls "$sdkroot/Include" 2>/dev/null | grep -E '^10\.0\.[0-9]+\.[0-9]+$' | sort | tail -1)"
[ -z "$sdkver" ] && sdkver=10.0.26100.0

# 关键：MSYS bash 只在 PATH 中用 POSIX 形式（/c/...）才能 stat 到可执行文件；
# 而 cl.exe / rc.exe / cmake 等原生 Windows 程序需要 Windows 形式（C:/...）的 INCLUDE/LIB/rc 路径。
# 因此 PATH 用 cygpath -u 转出的 POSIX 路径，INCLUDE/LIB/rc/mt 保留 Windows 形式。
vsdir_p="$(cygpath -u "$vsdir")"
sdkroot_p="$(cygpath -u "$sdkroot")"
qt_p="$(cygpath -u "$qt")"

export INCLUDE="$vsdir/VC/Tools/MSVC/$vcver/include;$sdkroot/Include/$sdkver/ucrt;$sdkroot/Include/$sdkver/um;$sdkroot/Include/$sdkver/shared"
export LIB="$vsdir/VC/Tools/MSVC/$vcver/lib/x64;$sdkroot/Lib/$sdkver/um/x64;$sdkroot/Lib/$sdkver/ucrt/x64"
# 注意：MSYS bash 的 PATH 分隔符是冒号(:)，不是 Windows 的分号(;)；而 INCLUDE/LIB 由原生 cl.exe 读取，仍用分号(;)。
export PATH="$vsdir_p/VC/Tools/MSVC/$vcver/bin/Hostx64/x64:$sdkroot_p/bin/$sdkver/x64:$vsdir_p/Common7/IDE/CommonExtensions/Microsoft/CMake/CMake/bin:$vsdir_p/Common7/IDE/CommonExtensions/Microsoft/CMake/Ninja:$qt_p/bin:$HOME/.cargo/bin:$PATH"
echo "[vc-env] VS=$vsdir  VCVER=$vcver  SDK=$sdkver"
