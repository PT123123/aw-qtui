# Makefile —— aw-qtui 构建入口（GNU Make 编排 cmake + ninja）
#
# 运行：在 Windows Terminal / PowerShell 直接敲 make（无需先开 Developer Command Prompt）。
# 说明：本 Makefile 只做「编排层」，真正的编译引擎是 cmake -G Ninja（ninja 调 cl）。
#       VC / Windows SDK 环境由本文件自动注入（不依赖 Developer Prompt，也不调 cmd）。
#       SHELL 固定指向 Git bash，命令解释与调用方 shell 无关。
#
# 常用目标：
#   make                构建 Release 客户端 + 服务端（等价于 make release）
#   make debug          构建 Debug 客户端 + 服务端
#   make build          仅构建 Release 客户端（不带服务端）
#   make build-dbg      仅构建 Debug 客户端
#   make dist           版本号 +0.01 → 打包 dist/aw-qtui-<ver>-win64.zip
#   make asan           AddressSanitizer 诊断构建
#   make selftest       编译并报告 TodoStore 自测
#   make run            运行 build/awqtui.exe
#   make clean          清理 build / build-dbg / build-asan / server-src
#   make help           查看全部目标
#
# 可覆盖变量（命令行传入，如 make QT=C:/Qt/6.8.3/msvc2022_64 release）：
#   QT=           Qt 安装根（默认 C:/Qt/6.8.3/msvc2022_64）
#   VS_DIR=       指定 VS 安装根（自动探测失败时用）
#   SDKROOT=      Windows SDK 根（默认 C:/Program Files (x86)/Windows Kits/10）
#   VCVER=        MSVC 工具版本（默认自动探测，如 14.44.35207）
#   SDKVERSION=   Windows SDK 版本（默认自动探测，如 10.0.26100.0）
#   SERVER_SRC=   服务端 crate 根（默认 vendor/aw-inbox）
#   SERVER=       置空可跳过服务端（make release SERVER=）
#   VERSION=      打包时指定版本号（不自动 +0.01、不写回 CMakeLists）
#   SKIP_SERVER=  置空以外的任意值 → 打包不含服务端
#   PORT=         运行端口（make run PORT=5620 → --url http://127.0.0.1:5620）

# ---------- 跨平台 Shell 探测（遵循 makefile-windows-crossplatform 技能） ----------
# 用 8.3 短路径 C:/Progra~1/... 规避空格被 $(firstword) 截断。
GIT_BASH := $(firstword $(wildcard C:/Progra~1/Git/bin/bash.exe) $(wildcard C:/Progra~1/Git/usr/bin/bash.exe))
ifneq ($(GIT_BASH),)
SHELL := $(GIT_BASH)
else
SHELL := /bin/bash
endif
IS_WIN := $(if $(GIT_BASH),1,$(if $(findstring Windows,$(OS)),1,))
.SHELLFLAGS := -c

# ---------- 通用变量（不区分平台） ----------
CFG         ?= Release
VERSION     ?=
SKIP_SERVER ?=
SERVER      ?= 1
PORT        ?=

# ---------- Windows 专属默认值 ----------
ifeq ($(IS_WIN),1)
QT          ?= C:/Qt/6.8.3/msvc2022_64
VS_DIR      ?= C:/Program Files/Microsoft Visual Studio/2022/Community
SDKROOT     ?= C:/Program Files (x86)/Windows Kits/10
VSWHERE     ?= C:/Program Files (x86)/Microsoft Visual Studio/Installer/vswhere.exe
SERVER_SRC  ?= vendor/aw-inbox
BUILD       ?= build
DBG         ?= build-dbg
# 关闭 MSYS 自动路径转换，避免 C:/... 被改成 /c/... 传给原生 exe
export MSYS_NO_PATHCONV := 1
endif

# ---------- VC / Windows SDK 环境注入（recipe 内执行，自动处理含空格路径） ----------
# 用法：$(VCENV) <后续命令>。会在同一 shell 内 export INCLUDE/LIB/PATH，使 cl/ninja/cargo 可用。
# 探测失败时用 VS_DIR=/SDKROOT=/VCVER=/SDKVERSION= 显式覆盖。
VCENV = vsdir="$(VS_DIR)"; \
	sdkroot="$(SDKROOT)"; \
	vcver="$(VCVER)"; \
	sdkver="$(SDKVERSION)"; \
	[ -x "$(VSWHERE)" ] && vsdir=`"$(VSWHERE)" -latest -property installationPath 2>/dev/null`; \
	[ -z "$$vcver" ] && vcver=$$(ls "$$vsdir/VC/Tools/MSVC" 2>/dev/null | sort | tail -1); \
	[ -z "$$sdkver" ] && sdkver=$$(ls "$$sdkroot/Include" 2>/dev/null | grep -E '^10\.0\.[0-9]+\.[0-9]+$$' | sort | tail -1); \
	[ -z "$$sdkver" ] && sdkver=10.0.26100.0; \
	export INCLUDE="$$vsdir/VC/Tools/MSVC/$$vcver/include;$$sdkroot/Include/$$sdkver/ucrt;$$sdkroot/Include/$$sdkver/um;$$sdkroot/Include/$$sdkver/shared"; \
	export LIB="$$vsdir/VC/Tools/MSVC/$$vcver/lib/x64;$$sdkroot/Lib/$$sdkver/um/x64;$$sdkroot/Lib/$$sdkver/ucrt/x64"; \
	export PATH="$$vsdir/VC/Tools/MSVC/$$vcver/bin/Hostx64/x64:$$sdkroot/bin/$$sdkver/x64:$$vsdir/Common7/IDE/CommonExtensions/Microsoft/CMake/CMake/bin:$$vsdir/Common7/IDE/CommonExtensions/Microsoft/CMake/Ninja:$(QT)/bin:$$PATH"; \
	echo "[vc-env] VS=$$vsdir  VCVER=$$vcver  SDK=$$sdkver";

.PHONY: all help release debug build build-dbg deploy deploy-dbg \
        server with-server dist asan selftest run clean clean-all

all: release

help:
	@echo "aw-qtui build targets:"
	@echo "  make             Release client + server (default)"
	@echo "  make debug       Debug client + server"
	@echo "  make build       Release client only"
	@echo "  make build-dbg   Debug client only"
	@echo "  make server      build & deploy server to \$$(BUILD)/server/"
	@echo "  make dist        package dist/aw-qtui-<ver>-win64.zip (bump version +0.01)"
	@echo "  make asan        AddressSanitizer build"
	@echo "  make selftest    compile & report TodoStore self-test"
	@echo "  make run         run \$$(BUILD)/awqtui.exe"
	@echo "  make clean       clean build / build-dbg / build-asan / server-src"
	@echo "  make clean-all   also clean dist/"
	@echo "overrides: QT= VS_DIR= SDKROOT= VCVER= SDKVERSION= SERVER_SRC= SERVER= VERSION= SKIP_SERVER= PORT="

# ---------- 客户端：cmake -G Ninja + cmake --build ----------
release: CFG = Release
release: build deploy $(if $(SERVER),with-server)

debug: CFG = Debug
debug: BUILD = build-dbg
debug: build-dbg deploy-dbg $(if $(SERVER),with-server)

build:
	$(VCENV) \
	cmake -S . -B $(BUILD) -G Ninja -DCMAKE_BUILD_TYPE=$(CFG) -DQt6_DIR=$(QT)/lib/cmake/Qt6 -DCMAKE_RC_COMPILER="$$sdkroot/bin/$$sdkver/x64/rc.exe" -DCMAKE_MT="$$sdkroot/bin/$$sdkver/x64/mt.exe" && \
	cmake --build $(BUILD) --config $(CFG)

build-dbg: CFG = Debug
build-dbg: BUILD = build-dbg
build-dbg:
	$(VCENV) \
	cmake -S . -B $(BUILD) -G Ninja -DCMAKE_BUILD_TYPE=$(CFG) -DQt6_DIR=$(QT)/lib/cmake/Qt6 -DCMAKE_RC_COMPILER="$$sdkroot/bin/$$sdkver/x64/rc.exe" -DCMAKE_MT="$$sdkroot/bin/$$sdkver/x64/mt.exe" && \
	cmake --build $(BUILD) --config $(CFG)

# ---------- 部署 Qt 运行库 ----------
deploy:
	"$(QT)/bin/windeployqt.exe" --release --no-translations --no-system-d3d-compiler --no-opengl-sw $(BUILD)/awqtui.exe

deploy-dbg:
	"$(QT)/bin/windeployqt.exe" --debug --no-translations --no-system-d3d-compiler --no-opengl-sw $(DBG)/awqtui.exe

# ---------- 服务端：cargo 编 aw-inbox-rust.exe 并部署 ----------
# 源码定位顺序：SERVER_SRC → $$AW_SERVER_SRC → vendor/aw-inbox → vendor/aw-server-rust/aw-inbox-rust
server with-server:
	$(VCENV) \
	export PATH="$$HOME/.cargo/bin:$$PATH"; \
	CRATE="$(SERVER_SRC)"; \
	[ -z "$$CRATE" ] && [ -n "$$AW_SERVER_SRC" ] && CRATE="$$AW_SERVER_SRC"; \
	[ -z "$$CRATE" ] && [ -f vendor/aw-inbox/Cargo.toml ] && CRATE=vendor/aw-inbox; \
	[ -z "$$CRATE" ] && [ -f vendor/aw-server-rust/aw-inbox-rust/Cargo.toml ] && CRATE=vendor/aw-server-rust/aw-inbox-rust; \
	[ -z "$$CRATE" ] && { echo "aw-inbox-rust source not found: run 'git submodule update --init --recursive' or set SERVER_SRC="; exit 1; }; \
	echo "[server] crate root: $$CRATE"; \
	mkdir -p server-src && robocopy "$$CRATE" server-src/aw-inbox-rust /E /XD target .git node_modules >/dev/null; rc=$$?; [ $$rc -ge 8 ] && { echo "robocopy sync failed rc=$$rc"; exit 1; }; \
	export CARGO_TARGET_DIR="$$PWD/server-src/target"; \
	cargo build $(if $(findstring Debug,$(CFG)),,--release) --manifest-path server-src/aw-inbox-rust/Cargo.toml; \
	PROF=$(if $(findstring Debug,$(CFG)),debug,release); \
	SRC="server-src/target/$$PROF/aw-inbox-rust.exe"; \
	[ -f "$$SRC" ] || { echo "build artifact missing: $$SRC"; exit 1; }; \
	DST="$(BUILD)/server/aw-inbox-rust.exe"; \
	mkdir -p "$(BUILD)/server"; \
	mv -f "$$DST" "$$DST.bak" 2>/dev/null || true; \
	cp -f "$$SRC" "$$DST"; \
	rm -f "$$DST.bak" 2>/dev/null || true; \
	echo "[server] deployed $$DST"

# ---------- 打包发布 ----------
dist: release
	python tools/make_zip.py $(if $(VERSION),--version $(VERSION),) $(if $(SKIP_SERVER),--skip-server,)

# ---------- AddressSanitizer 诊断构建 ----------
asan:
	$(VCENV) \
	cmake -S . -B build-asan -G Ninja -DCMAKE_BUILD_TYPE=Release -DQt6_DIR=$(QT)/lib/cmake/Qt6 -DCMAKE_RC_COMPILER="$$sdkroot/bin/$$sdkver/x64/rc.exe" -DCMAKE_MT="$$sdkroot/bin/$$sdkver/x64/mt.exe" -DCMAKE_CXX_FLAGS="/fsanitize=address /Zi" -DCMAKE_EXE_LINKER_FLAGS="/fsanitize=address" && \
	cmake --build build-asan --config Release
	"$(QT)/bin/windeployqt.exe" --release --no-translations --no-system-d3d-compiler --no-opengl-sw build-asan/awqtui.exe

# ---------- TodoStore 自测 ----------
selftest:
	$(VCENV) \
	"$(QT)/bin/moc.exe" src/todostore.h -o tools/moc_todostore.cpp -I src -I "$(QT)/include" && \
	cl /std:c++17 /permissive- /Zc:__cplusplus /EHsc /utf-8 /DQT_CORE_LIB /I"$(QT)/include" /I"$(QT)/include/QtCore" /I"$(QT)/mkspecs/win32-msvc" /I src tools/todostore_selftest.cpp tools/moc_todostore.cpp src/todostore.cpp /Fe:tools/todostore_selftest.exe /link "$(QT)/lib/Qt6Core.lib" && \
	echo "selftest built: tools/todostore_selftest.exe (run: tools/todostore_selftest.exe)"

# ---------- 运行 ----------
run:
	cmd /c start "" "$(BUILD)/awqtui.exe" $(if $(PORT),--url http://127.0.0.1:$(PORT),)

# ---------- 清理 ----------
clean:
	-rm -rf build build-dbg build-asan server-src tools/moc_todostore.cpp tools/todostore_selftest.exe
clean-all: clean
	-rm -rf dist
