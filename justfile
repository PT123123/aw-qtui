# justfile —— aw-qtui 任务编排器（最顶层入口）
#
# 运行（Windows Terminal / PowerShell 7 直接敲 just；无需 Developer Command Prompt，
#       也不依赖 Git Bash / cmd.exe —— 所有 recipe 都是 PowerShell 7 脚本）：
#   just                显示帮助
#   just release        构建 Release 客户端 + 服务端 + 部署 + 发通知
#   just debug          构建 Debug 客户端 + 服务端
#   just build          仅构建 Release 客户端（不带服务端）
#   just build-dbg      仅构建 Debug 客户端
#   just server         构建并部署 aw-server.exe（完整 /api/0 + /inbox + /todo）到 build/server/
#   just dist           打包 build/release/aw-qtui-<ver>-win64.zip（版本 +0.01，写回 CMakeLists）
#   just deploy-workshop 构建 release 并部署到 c:/workshop/aw-qtui-<ver>（patch +1，写回 CMakeLists）
#   just install        把已部署的 build/ 拷贝到安装目录（默认 %LOCALAPPDATA%/Programs/aw-qtui）
#   just asan           AddressSanitizer 诊断构建
#   just selftest       编译并运行 TodoStore 自测
#   just mem-baseline   逐级量 base 内存（详见 README「内存与性能基线」）
#   just run            运行 build/awqtui.exe
#   just notify         发送 Windows Toast 通知
#   just clean          清理 build / build-dbg / build-asan / server-src
#   just clean-all      同 clean，并额外清理 dist/
#
# 设计：本 justfile 只做「任务编排」，真正的编译引擎是 cmake -G Ninja（ninja 调 cl），
#       服务端是 cargo。VC / Windows SDK 环境由 tools/vcenv.ps1 注入（不依赖 Developer Prompt）。
#       每个 recipe 首行是 `#!pwsh -NoProfile` shebang：just 会把整段 recipe 写成临时脚本
#       交给 PowerShell 7 执行，因此多行之间变量/函数可共享（注意 just 的 shebang 只接受
#       一个参数，故只写 -NoProfile）。需严格失败即停的 recipe 首行显式声明
#       $ErrorActionPreference / $PSNativeCommandUseErrorActionPreference。
#       覆盖「变量」用 just VAR=... recipe（如 just QT="C:/Qt/6.8.3/msvc2022_64" release）。
#       覆盖「recipe 参数」用位置参数（本版本 just 不解析 name=value 命名参数）：
#         just build Debug build-dbg / just dist 0.1.1 / just run 8080 / just install C:/path
#   QT=  VS_DIR=  SDKROOT=  VSWHERE=  VCVER=  SDKVERSION=  SERVER=  BUILD=  DBG=

set shell := ["pwsh", "-NoProfile", "-Command"]

# ---------- 变量（export 的会进入 recipe 环境，供 tools/vcenv.ps1 读取） ----------
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

VCENV := "tools/vcenv.ps1"

# ---------- 帮助（默认目标） ----------
#[default]
help:
    #!pwsh -NoProfile
    Write-Host 'aw-qtui build & task targets (just):'
    Write-Host '  just release       Release client + server + deploy + notify (default: just)'
    Write-Host '  just debug         Debug client + server'
    Write-Host '  just build         Release client only'
    Write-Host '  just build-dbg     Debug client only'
    Write-Host '  just server        build & deploy aw-server.exe (full /api/0 + /inbox + /todo) to build/server/'
    Write-Host '  just server-aw-server  alias of server (backward compat)'
    Write-Host '  just deploy        deploy Qt runtimes (windeployqt)'
    Write-Host '  just icon          生成 exe 内嵌图标 resources/app.ico（需先 just build）'
    Write-Host '  just stage-dist     暂存正式版到 build/dist/'
    Write-Host '  just dist          package dist/aw-qtui-<ver>-win64.zip (bump +0.01)'
    Write-Host '  just deploy-workshop  build release & copy to c:/workshop/aw-qtui-<ver> (patch +1)'
    Write-Host '  just install       copy deployed build/ into install dir'
    Write-Host '  just asan          AddressSanitizer build'
    Write-Host '  just selftest      compile & run TodoStore self-test'
    Write-Host '  just mem-baseline  逐级量 base 内存（私有/工作集），核对 README 的内存基线'
    Write-Host '  just run           run build/awqtui.exe'
    Write-Host '  just notify        send Windows Toast notification'
    Write-Host '  just clean         clean build / build-dbg / build-asan / server-src'
    Write-Host '  just clean-all     also clean dist/'
    Write-Host 'overrides: QT= VS_DIR= SDKROOT= VSWHERE= VCVER= SDKVERSION= SERVER= BUILD= DBG='

# ---------- 客户端：cmake -G Ninja + cmake --build ----------
build cfg="Release" builddir="build":
    #!pwsh -NoProfile
    $ErrorActionPreference = 'Stop'; $PSNativeCommandUseErrorActionPreference = $true
    . '{{VCENV}}'
    $qtDir = '{{QT}}'
    $cmakeArgs = @(
        '-S', '.', '-B', '{{builddir}}', '-G', 'Ninja'
        "-DCMAKE_BUILD_TYPE={{cfg}}"
        "-DQt6_DIR=$qtDir/lib/cmake/Qt6"
        "-DCMAKE_RC_COMPILER=$sdkroot/bin/$sdkver/x64/rc.exe"
        "-DCMAKE_MT=$sdkroot/bin/$sdkver/x64/mt.exe"
    )
    cmake @cmakeArgs
    cmake --build '{{builddir}}' --config '{{cfg}}'

build-dbg: (build "Debug" "build-dbg")

# ---------- 部署 Qt 运行库 ----------
deploy:
    #!pwsh -NoProfile
    $ErrorActionPreference = 'Stop'; $PSNativeCommandUseErrorActionPreference = $true
    & '{{QT}}/bin/windeployqt.exe' --release --no-translations --no-system-d3d-compiler --no-opengl-sw '{{BUILD}}/awqtui.exe'

deploy-dbg:
    #!pwsh -NoProfile
    $ErrorActionPreference = 'Stop'; $PSNativeCommandUseErrorActionPreference = $true
    & '{{QT}}/bin/windeployqt.exe' --debug --no-translations --no-system-d3d-compiler --no-opengl-sw '{{DBG}}/awqtui.exe'

# ---------- 生成 exe 内嵌图标 resources/app.ico ----------
# 图标绘制真相只有一份（theme.h renderAppIconPixmap）：先让编译好的 exe 离屏导出各尺寸 PNG，
# 再合成 .ico 供 resources/app.rc 内嵌进 awqtui.exe；aw-server.exe 复用同一文件。
icon:
    #!pwsh -NoProfile
    $ErrorActionPreference = 'Stop'; $PSNativeCommandUseErrorActionPreference = $true
    # 导出源必须是「当前源码编出来的 exe」——否则改了 renderAppIconPixmap 之后
    # 仍会按旧图案生成 app.ico。ninja 增量无改动时几乎瞬完，所以无条件走一次。
    just build
    . '{{VCENV}}'
    $exe = '{{BUILD}}/awqtui.exe'
    if (-not (Test-Path -LiteralPath $exe)) { throw "找不到 $exe - 先 just build" }
    $exeAbs = (Get-Item -LiteralPath $exe).FullName
    $tmpAbs = Join-Path (Get-Item -LiteralPath '{{BUILD}}').FullName 'icon-src'
    if (Test-Path -LiteralPath $tmpAbs) { Remove-Item -Recurse -Force -LiteralPath $tmpAbs }
    # GUI 子系统 exe：PowerShell 的 & 不会等待，必须 Start-Process -Wait
    $p = Start-Process -FilePath $exeAbs -ArgumentList @('--emit-icon', $tmpAbs) -Wait -PassThru -NoNewWindow
    if ($p.ExitCode -ne 0) { throw "--emit-icon 失败 (exit $($p.ExitCode))" }
    & 'tools/make-ico.ps1' -PngDir $tmpAbs -Out 'resources/app.ico'
    Write-Host '[icon] resources/app.ico 已更新 - 重新 just build 才会内嵌进 exe'

# ---------- 服务端：cargo 编 aw-server workspace（aw-server.exe：/api/0 + /inbox + /todo）并部署 ----------
server:
    #!pwsh -NoProfile
    $ErrorActionPreference = 'Stop'; $PSNativeCommandUseErrorActionPreference = $true
    . '{{VCENV}}'
    # vendor/aw-server-rust 是指向仓库外共享源码目录 ../aw-server-plus 的 junction
    $ws = 'vendor/aw-server-rust'
    if (-not (Test-Path -LiteralPath "$ws/Cargo.toml")) {
        throw "aw-server-rust workspace not found: clone PT123123/aw-server-plus to <workspace>/aw-server-plus, then create junction 'vendor/aw-server-rust' -> ../../aw-server-plus (see README)"
    }
    # 服务端源码与 aw-android 共用一份，但 cargo 缓存必须各走各的：共享源码里的 target/
    # 归 Android 交叉编译（rust-android-gradle 硬编码 <module>/target），且 aw-android 构建
    # 注入的 RUSTFLAGS 会让本机增量缓存整体失效——所以这里显式指到本仓库私有目录。
    $env:CARGO_TARGET_DIR = Join-Path (Get-Location).Path '.cargo-target-server'
    # 图标内嵌：aw-server 的 build.rs 用 rc.exe 编 windows/app.rc，ico 由这里从主仓库同步过去
    $icoDst = "$ws/aw-server/windows/app.ico"
    if (Test-Path -LiteralPath 'resources/app.ico') {
        New-Item -ItemType Directory -Force -Path (Split-Path -Parent $icoDst) | Out-Null
        Copy-Item -Force -LiteralPath 'resources/app.ico' -Destination $icoDst
    } else {
        Write-Host '[server] resources/app.ico 缺失，aw-server.exe 将没有内嵌图标 - 先 just icon' -ForegroundColor Yellow
    }
    Write-Host "[server] workspace: $ws (target: $env:CARGO_TARGET_DIR)"
    cargo build --release -p aw-server --manifest-path "$ws/Cargo.toml"
    $src = "$env:CARGO_TARGET_DIR/release/aw-server.exe"
    if (-not (Test-Path -LiteralPath $src)) { throw "build artifact missing: $src" }
    $dst = '{{BUILD}}/server/aw-server.exe'
    New-Item -ItemType Directory -Force -Path (Split-Path -Parent $dst) | Out-Null
    try {
        Copy-Item -Force -LiteralPath $src -Destination $dst -ErrorAction Stop
    } catch {
        throw "cannot overwrite $dst - target may be running; stop awqtui / aw-server.exe and retry. original error: $($_.Exception.Message)"
    }
    Write-Host "[server] deployed $dst"

# ---------- 服务端（完整）：与 server 等价（backward compat 别名） ----------
server-aw-server: server

# ---------- 暂存正式版到 build/dist/ ----------
stage-dist:
    #!pwsh -NoProfile
    $ErrorActionPreference = 'Stop'
    Write-Host '[stage-dist] staging release artifacts into build/dist/'
    $dist = 'build/dist'
    if (Test-Path -LiteralPath $dist) { Remove-Item -Recurse -Force -LiteralPath $dist }
    New-Item -ItemType Directory -Force -Path $dist | Out-Null
    # 可执行文件 + DLL
    if (Test-Path -LiteralPath 'build/awqtui.exe') { Copy-Item -Force -LiteralPath 'build/awqtui.exe' -Destination $dist }
    Get-ChildItem -Path 'build' -Filter '*.dll' -File -ErrorAction SilentlyContinue |
        ForEach-Object { Copy-Item -Force -LiteralPath $_.FullName -Destination $dist }
    # Qt 插件目录
    foreach ($d in 'platforms', 'styles', 'imageformats', 'iconengines', 'networkinformation', 'tls') {
        $src = "build/$d"
        if (Test-Path -LiteralPath $src) { Copy-Item -Recurse -Force -LiteralPath $src -Destination "$dist/$d" }
    }
    # 服务端
    if (Test-Path -LiteralPath 'build/server/aw-server.exe') {
        New-Item -ItemType Directory -Force -Path "$dist/server" | Out-Null
        Copy-Item -Force -LiteralPath 'build/server/aw-server.exe' -Destination "$dist/server/"
    }
    # README
    if (Test-Path -LiteralPath 'README.md') { Copy-Item -Force -LiteralPath 'README.md' -Destination $dist }
    Write-Host '[stage-dist] done: build/dist/'

# ---------- 聚合：release / debug ----------
release:
    #!pwsh -NoProfile
    $ErrorActionPreference = 'Stop'; $PSNativeCommandUseErrorActionPreference = $true
    just build
    just deploy
    if ('{{SERVER}}') { just server }
    just stage-dist
    just notify 'aw-qtui' 'release build complete'

debug:
    #!pwsh -NoProfile
    $ErrorActionPreference = 'Stop'; $PSNativeCommandUseErrorActionPreference = $true
    just build-dbg
    just deploy-dbg
    if ('{{SERVER}}') { just server }
    just notify 'aw-qtui' 'debug build complete'

# ---------- 打包发布 ----------
# 版本号写位置参数：just dist 0.1.1（just dist version="0.1.1" 会被当成字面量 version=0.1.1）
dist version="" skip_server="":
    #!pwsh -NoProfile
    $ErrorActionPreference = 'Stop'; $PSNativeCommandUseErrorActionPreference = $true
    just release
    $ver = '{{version}}'
    $ver = $ver -replace '^version=', ''            # 防御：named 风格调用会收到字面 version=0.1.1
    $extra = @()
    if ($ver) { $extra += '--version'; $extra += $ver }
    if ('{{skip_server}}') { $extra += '--skip-server' }
    python tools/make_zip.py --root build/dist @extra

# ---------- 版本号推进（deploy-workshop 会在构建前调用） ----------
# 单独拆成一步，是为了守住「目录名 == exe 内 AW_VERSION」这个不变量：
# 版本必须**先**写回 CMakeLists.txt，构建出来的 exe 才带新号；
# 否则新版与运行中的旧版内部版本相同，单实例仲裁走同版本分支，交接无声失败。
bump-version:
    #!pwsh -NoProfile
    $ErrorActionPreference = 'Stop'; $PSNativeCommandUseErrorActionPreference = $true
    python tools/deploy_workshop.py --bump-only

# ---------- 部署到 workshop（c:/workshop/aw-qtui-<ver>） ----------
# 顺序不可换：先 bump 写回 -> 刷图标 -> 再构建 -> 再部署（原因见 tools/deploy_workshop.py 顶部）。
deploy-workshop:
    #!pwsh -NoProfile
    $ErrorActionPreference = 'Stop'; $PSNativeCommandUseErrorActionPreference = $true
    just bump-version
    # 内嵌图标必须与设置里选中的变体同色，且要在构建之前就位：
    # release 里的 just build / just server 会把它编进 awqtui.exe 和 aw-server.exe。
    # 任务管理器 / 资源管理器只认这份静态资源，运行时 setWindowIcon 影响不到它们。
    just icon
    just release
    python tools/deploy_workshop.py --no-bump

# ---------- 安装（把已部署的 build/ 拷贝到安装目录） ----------
install install_dir="":
    #!pwsh -NoProfile
    $ErrorActionPreference = 'Stop'
    $target = '{{install_dir}}'
    if (-not $target) { $target = Join-Path $env:LOCALAPPDATA 'Programs/aw-qtui' }
    New-Item -ItemType Directory -Force -Path $target | Out-Null
    # robocopy 退出码 0-7 均为成功、>=8 才是失败，故此处不启用「native 失败即抛」
    robocopy build $target /E /XD CMakeFiles *.obj *.ilk *.pdb .ninja CMakeCache.txt cmake_install.cmake build.ninja CTestTestfile.cmake awqtui_autogen server-src | Out-Null
    if ($LASTEXITCODE -ge 8) { throw "robocopy install failed rc=$LASTEXITCODE" }
    Write-Host "[install] deployed to $target"

# ---------- AddressSanitizer 诊断构建 ----------
asan:
    #!pwsh -NoProfile
    $ErrorActionPreference = 'Stop'; $PSNativeCommandUseErrorActionPreference = $true
    . '{{VCENV}}'
    $qtDir = '{{QT}}'
    $cmakeArgs = @(
        '-S', '.', '-B', 'build-asan', '-G', 'Ninja'
        '-DCMAKE_BUILD_TYPE=Release'
        "-DQt6_DIR=$qtDir/lib/cmake/Qt6"
        "-DCMAKE_RC_COMPILER=$sdkroot/bin/$sdkver/x64/rc.exe"
        "-DCMAKE_MT=$sdkroot/bin/$sdkver/x64/mt.exe"
        '-DCMAKE_CXX_FLAGS=/fsanitize=address /Zi'
        '-DCMAKE_EXE_LINKER_FLAGS=/fsanitize=address'
    )
    cmake @cmakeArgs
    cmake --build build-asan --config Release
    & '{{QT}}/bin/windeployqt.exe' --release --no-translations --no-system-d3d-compiler --no-opengl-sw build-asan/awqtui.exe

# ---------- TodoStore 自测 ----------
selftest:
    #!pwsh -NoProfile
    $ErrorActionPreference = 'Stop'; $PSNativeCommandUseErrorActionPreference = $true
    . '{{VCENV}}'
    $qtDir = '{{QT}}'
    & "$qtDir/bin/moc.exe" src/todostore.h -o tools/moc_todostore.cpp -I src -I "$qtDir/include"
    $clArgs = @(
        '/std:c++17', '/permissive-', '/Zc:__cplusplus', '/EHsc', '/utf-8', '/DQT_CORE_LIB'
        "/I$qtDir/include", "/I$qtDir/include/QtCore", "/I$qtDir/mkspecs/win32-msvc", '/I', 'src'
        'tools/todostore_selftest.cpp', 'tools/moc_todostore.cpp', 'src/todostore.cpp'
        '/Fe:tools/todostore_selftest.exe'
        '/link', "$qtDir/lib/Qt6Core.lib"
    )
    cl @clArgs
    Write-Host 'selftest built: tools/todostore_selftest.exe (run: tools/todostore_selftest.exe)'

# ---------- 内存基线自测（tools/mem_baseline.cpp） ----------
# 逐级构造真实控件，每级打印进程私有内存 / 工作集 —— README「内存与性能基线」里的
# 数字就是它量的，改动后重跑即可核对地板有没有被抬高。
# 链接的是 build-verify 的那份目标文件（与 build/ 同源同配置）：**改过 src/ 必须先**
# `just build Release build-verify`，否则量到的是旧代码的地板（和探针复用旧 obj 一个坑）。
# 参数：real（默认；真实平台但不弹窗，最接近实机）/ offscreen（离屏，快，绝对值偏低）
mem-baseline mode="real":
    #!pwsh -NoProfile
    $ErrorActionPreference = 'Stop'; $PSNativeCommandUseErrorActionPreference = $true
    $qtDir = '{{QT}}'
    if (-not (Test-Path 'build-verify/CMakeFiles/awqtui.dir')) {
        throw 'build-verify 不存在：先跑 `just build Release build-verify`'
    }
    . '{{VCENV}}'
    $objs = @(Get-ChildItem -Recurse 'build-verify/CMakeFiles/awqtui.dir' -Filter '*.obj' |
              Where-Object { $_.Name -ne 'main.cpp.obj' } | ForEach-Object { $_.FullName })
    $ver = (Select-String -Path 'CMakeLists.txt' -Pattern 'project\(aw-qtui VERSION ([0-9]+\.[0-9]+\.[0-9]+)' |
            Select-Object -First 1).Matches[0].Groups[1].Value
    cl /nologo /std:c++17 /EHsc /MD /O2 /Zc:__cplusplus /permissive- /utf-8 `
       /DWIN32 /D_WINDOWS /DUNICODE /D_UNICODE /DNOMINMAX /D_WIN32_WINNT=0x0A00 `
       /DQT_CORE_LIB /DQT_GUI_LIB /DQT_WIDGETS_LIB /DQT_NETWORK_LIB /DQT_NO_DEBUG `
       "/DAW_VERSION=`"$ver`"" `
       /I'src' /I'build-verify/awqtui_autogen/include' `
       "/I$qtDir/include" "/I$qtDir/include/QtCore" "/I$qtDir/include/QtGui" `
       "/I$qtDir/include/QtWidgets" "/I$qtDir/include/QtNetwork" `
       'tools/mem_baseline.cpp' '/Fo:build-verify/mem_baseline.obj' '/Fe:build-verify/mem_baseline.exe' `
       $objs `
       /link /LIBPATH:"$qtDir/lib" Qt6Widgets.lib Qt6Network.lib Qt6Gui.lib Qt6Core.lib `
       Dnsapi.lib iphlpapi.lib shcore.lib d3d11.lib dxgi.lib dxguid.lib d3d12.lib mpr.lib userenv.lib `
       shell32.lib ws2_32.lib kernel32.lib user32.lib gdi32.lib winspool.lib ole32.lib oleaut32.lib uuid.lib comdlg32.lib advapi32.lib psapi.lib
    $env:PATH = "$qtDir/bin;$env:PATH"
    $env:QT_PLUGIN_PATH = "$qtDir/plugins"
    if ('{{mode}}' -eq 'offscreen') { $env:QT_QPA_PLATFORM = 'offscreen' }
    & 'build-verify/mem_baseline.exe' '{{mode}}'

# ---------- 运行 ----------
run port="":
    #!pwsh -NoProfile
    $ErrorActionPreference = 'Stop'
    $exe = Join-Path (Get-Location).Path 'build/awqtui.exe'
    if ('{{port}}') {
        Start-Process -FilePath $exe -ArgumentList '--url', "http://127.0.0.1:{{port}}"
    } else {
        Start-Process -FilePath $exe
    }

# ---------- 通知（Windows Toast；未装 BurntToast 时降级为控制台输出） ----------
notify title="aw-qtui" message="build complete":
    #!pwsh -NoProfile
    if (Get-Module -ListAvailable -Name BurntToast) {
        Import-Module BurntToast
        New-BurntToastNotification -Text '{{title}}', '{{message}}'
    } else {
        Write-Host "[notify] BurntToast not installed -> {{title}}: {{message}}"
    }

# ---------- 清理 ----------
clean:
    #!pwsh -NoProfile
    foreach ($t in 'build', 'build-dbg', 'build-asan', 'server-src', 'tools/moc_todostore.cpp', 'tools/todostore_selftest.exe') {
        if (Test-Path -LiteralPath $t) { Remove-Item -Recurse -Force -LiteralPath $t }
    }

clean-all:
    #!pwsh -NoProfile
    just clean
    if (Test-Path -LiteralPath 'dist') { Remove-Item -Recurse -Force -LiteralPath 'dist' }
