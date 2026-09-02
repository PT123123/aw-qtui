# justfile —— aw-qtui 任务编排器（最顶层入口）
#
# 运行（Windows Terminal / PowerShell 直接敲 just；无需 Developer Command Prompt）：
#   just                显示帮助
#   just release        构建 Release 客户端 + 服务端 + 部署 + 发通知
#   just debug          构建 Debug 客户端 + 服务端
#   just build          仅构建 Release 客户端（不带服务端）
#   just build-dbg      仅构建 Debug 客户端
#   just server         构建并部署 aw-server.exe（完整 /api/0 + /inbox + /todo）到 build/server/
#   just dist           打包 dist/aw-qtui-<ver>-win64.zip（版本 +0.01，写回 CMakeLists）
#   just install        把已部署的 build/ 拷贝到安装目录（默认 %LOCALAPPDATA%/Programs/aw-qtui）
#   just asan           AddressSanitizer 诊断构建
#   just selftest       编译并运行 TodoStore 自测
#   just run            运行 build/awqtui.exe
#   just notify         发送 Windows Toast 通知
#   just clean          清理 build / build-dbg / build-asan / server-src
#   just clean-all      额外清理 dist/
#
# 设计：本 justfile 只做「任务编排」，真正的编译引擎是 cmake -G Ninja（ninja 调 cl），
#       服务端是 cargo。VC / Windows SDK 环境由 tools/vcenv.sh 注入（不依赖 Developer Prompt）。
#       recipe 全部以 Git bash 解释，命令中的中文仅出现在注释；执行语句保持 ASCII。
#       覆盖「变量」用 just VAR=... recipe（如 just QT="C:/Qt/6.8.3/msvc2022_64" release）。
#       覆盖「recipe 参数」用位置参数（本版本 just 不解析 name=value 命名参数）：
#         just build Debug build-dbg / just dist 0.1.1 / just run 8080 / just install C:/path
#   QT=  VS_DIR=  SDKROOT=  VSWHERE=  VCVER=  SDKVERSION=  SERVER=  BUILD=  DBG=

# ---------- 变量（export 的会进入 recipe 环境，供 tools/vcenv.sh 读取） ----------
export QT        := "C:/Qt/6.8.3/msvc2022_64"
export VS_DIR    := "C:/Program Files/Microsoft Visual Studio/2022/Community"
export SDKROOT   := "C:/Program Files (x86)/Windows Kits/10"
export VSWHERE   := "C:/Program Files (x86)/Microsoft Visual Studio/Installer/vswhere.exe"
export VCVER     := ""
export SDKVERSION := ""

BUILD := "build"
DBG   := "build-dbg"
CFG   := "Release"
SERVER := "1"

VCENV := "tools/vcenv.sh"

# ---------- 帮助（默认目标） ----------
#[default]
help:
    @echo "aw-qtui build & task targets (just):"
    @echo "  just release       Release client + server + deploy + notify (default: just)"
    @echo "  just debug         Debug client + server"
    @echo "  just build         Release client only"
    @echo "  just build-dbg     Debug client only"
    @echo "  just server        build & deploy aw-server.exe (full /api/0 + /inbox + /todo) to build/server/"
    @echo "  just server-aw-server  alias of server (backward compat)"
    @echo "  just deploy        deploy Qt runtimes (windeployqt)"
    @echo "  just dist          package dist/aw-qtui-<ver>-win64.zip (bump +0.01)"
    @echo "  just install       copy deployed build/ into install dir"
    @echo "  just asan          AddressSanitizer build"
    @echo "  just selftest      compile & run TodoStore self-test"
    @echo "  just run           run build/awqtui.exe"
    @echo "  just notify        send Windows Toast notification"
    @echo "  just clean         clean build / build-dbg / build-asan / server-src"
    @echo "  just clean-all     also clean dist/"
    @echo "overrides: QT= VS_DIR= SDKROOT= VSWHERE= VCVER= SDKVERSION= SERVER= BUILD= DBG="

# ---------- 客户端：cmake -G Ninja + cmake --build ----------
build cfg="Release" builddir="build":
    #!C:/Progra~1/Git/bin/bash.exe
    . "{{VCENV}}"
    cmake -S . -B {{builddir}} -G Ninja -DCMAKE_BUILD_TYPE={{cfg}} -DQt6_DIR={{QT}}/lib/cmake/Qt6 -DCMAKE_RC_COMPILER="$sdkroot/bin/$sdkver/x64/rc.exe" -DCMAKE_MT="$sdkroot/bin/$sdkver/x64/mt.exe"
    cmake --build {{builddir}} --config {{cfg}}

build-dbg: (build "Debug" "build-dbg")

# ---------- 部署 Qt 运行库 ----------
deploy:
    "{{QT}}/bin/windeployqt.exe" --release --no-translations --no-system-d3d-compiler --no-opengl-sw {{BUILD}}/awqtui.exe

deploy-dbg:
    "{{QT}}/bin/windeployqt.exe" --debug --no-translations --no-system-d3d-compiler --no-opengl-sw {{DBG}}/awqtui.exe

# ---------- 服务端：cargo 编 aw-server workspace（aw-server.exe：/api/0 + /inbox + /todo）并部署 ----------
server:
    #!C:/Progra~1/Git/bin/bash.exe
    . "{{VCENV}}"
    WS="vendor/aw-server-rust"
    [ -f "$WS/Cargo.toml" ] || { echo "aw-server-rust workspace not found: run 'git submodule update --init vendor/aw-server-rust'"; exit 1; }
    echo "[server] workspace: $WS"
    cargo build --release -p aw-server --manifest-path "$WS/Cargo.toml"
    SRC="$WS/target/release/aw-server.exe"
    [ -f "$SRC" ] || { echo "build artifact missing: $SRC"; exit 1; }
    DST="{{BUILD}}/server/aw-server.exe"
    mkdir -p "{{BUILD}}/server"
    mv -f "$DST" "$DST.bak" 2>/dev/null || true
    cp -f "$SRC" "$DST"
    rm -f "$DST.bak" 2>/dev/null || true
    echo "[server] deployed $DST"

# ---------- 服务端（完整）：与 server 等价（backward compat 别名） ----------
server-aw-server: server

# ---------- 聚合：release / debug ----------
release:
    just build
    just deploy
    if [ -n "{{SERVER}}" ]; then just server; fi
    just notify "aw-qtui" "release build complete"

debug:
    just build-dbg
    just deploy-dbg
    if [ -n "{{SERVER}}" ]; then just server; fi
    just notify "aw-qtui" "debug build complete"

# ---------- 打包发布 ----------
# 版本号写位置参数：just dist 0.1.1（just dist version="0.1.1" 会被当成字面量 version=0.1.1）
dist version="" skip_server="":
    #!C:/Progra~1/Git/bin/bash.exe
    just release
    ver="{{version}}"
    ver="${ver#version=}"            # 防御：named 风格调用会收到字面 version=0.1.1
    args=""
    [ -n "$ver" ] && args="$args --version $ver"
    [ -n "{{skip_server}}" ] && args="$args --skip-server"
    python tools/make_zip.py $args

# ---------- 安装（把已部署的 build/ 拷贝到安装目录） ----------
install install_dir="":
    #!C:/Progra~1/Git/bin/bash.exe
    target="{{install_dir}}"
    [ -z "$target" ] && target="$LOCALAPPDATA/Programs/aw-qtui"
    mkdir -p "$target"
    rc=0
    robocopy build "$target" /E /XD CMakeFiles *.obj *.ilk *.pdb .ninja CMakeCache.txt cmake_install.cmake build.ninja CTestTestfile.cmake awqtui_autogen server-src >/dev/null || rc=$?
    if [ "$rc" -ge 8 ]; then echo "robocopy install failed rc=$rc"; exit 1; fi
    echo "[install] deployed to $target"

# ---------- AddressSanitizer 诊断构建 ----------
asan:
    #!C:/Progra~1/Git/bin/bash.exe
    . "{{VCENV}}"
    cmake -S . -B build-asan -G Ninja -DCMAKE_BUILD_TYPE=Release -DQt6_DIR={{QT}}/lib/cmake/Qt6 -DCMAKE_RC_COMPILER="$sdkroot/bin/$sdkver/x64/rc.exe" -DCMAKE_MT="$sdkroot/bin/$sdkver/x64/mt.exe" -DCMAKE_CXX_FLAGS="/fsanitize=address /Zi" -DCMAKE_EXE_LINKER_FLAGS="/fsanitize=address"
    cmake --build build-asan --config Release
    "{{QT}}/bin/windeployqt.exe" --release --no-translations --no-system-d3d-compiler --no-opengl-sw build-asan/awqtui.exe

# ---------- TodoStore 自测 ----------
selftest:
    #!C:/Progra~1/Git/bin/bash.exe
    . "{{VCENV}}"
    "{{QT}}/bin/moc.exe" src/todostore.h -o tools/moc_todostore.cpp -I src -I "{{QT}}/include"
    cl /std:c++17 /permissive- /Zc:__cplusplus /EHsc /utf-8 /DQT_CORE_LIB /I"{{QT}}/include" /I"{{QT}}/include/QtCore" /I"{{QT}}/mkspecs/win32-msvc" /I src tools/todostore_selftest.cpp tools/moc_todostore.cpp src/todostore.cpp /Fe:tools/todostore_selftest.exe /link "{{QT}}/lib/Qt6Core.lib"
    echo "selftest built: tools/todostore_selftest.exe (run: tools/todostore_selftest.exe)"

# ---------- 运行 ----------
run port="":
    #!C:/Progra~1/Git/bin/bash.exe
    if [ -n "{{port}}" ]; then
    cmd //c start "" "{{BUILD}}/awqtui.exe" --url http://127.0.0.1:{{port}}
    else
    cmd //c start "" "{{BUILD}}/awqtui.exe"
    fi

# ---------- 通知（Windows Toast；未装 BurntToast 时降级为控制台输出） ----------
notify title="aw-qtui" message="build complete":
    powershell -NoProfile -Command 'if (Get-Module -ListAvailable -Name BurntToast) { Import-Module BurntToast; New-BurntToastNotification -Text "{{title}}", "{{message}}" } else { Write-Host "[notify] BurntToast not installed -> {{title}}: {{message}}" }'

# ---------- 清理 ----------
clean:
    rm -rf build build-dbg build-asan server-src tools/moc_todostore.cpp tools/todostore_selftest.exe

clean-all: clean
    rm -rf dist
