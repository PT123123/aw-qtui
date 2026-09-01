cmake_minimum_required(VERSION 3.16)
project(aw-qtui VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)

# 启用 Windows 10+ 原生 DNS-SD API（DnsServiceBrowse/Register 需要 _WIN32_WINNT >= 0x0A00）
add_compile_definitions(_WIN32_WINNT=0x0A00 WIN32_LEAN_AND_MEAN NOMINMAX)

find_package(Qt6 REQUIRED COMPONENTS Core Gui Widgets Network)
qt_standard_project_setup()

add_executable(awqtui WIN32
    src/main.cpp
    src/config.cpp
    src/config.h
    src/appsettings.cpp
    src/appsettings.h
    src/awserver.cpp
    src/awserver.h
    src/globalshortcut.cpp
    src/globalshortcut.h
    src/settingsdialog.cpp
    src/settingsdialog.h
    src/models.h
    src/apiclient.cpp
    src/apiclient.h
    src/localstore.cpp
    src/localstore.h
    src/mdrender.cpp
    src/mdrender.h
    src/theme.h
    src/widgets.cpp
    src/widgets.h
    src/inboxpage.cpp
    src/inboxpage.h
    src/syncpage.cpp
    src/syncpage.h
    src/mdnsdiscovery.cpp
    src/mdnsdiscovery.h
    src/mockdata.cpp
    src/mockdata.h
    src/charts.cpp
    src/charts.h
    src/timelinewidget.cpp
    src/timelinewidget.h
    src/activitypage.cpp
    src/activitypage.h
    src/timelinepage.cpp
    src/timelinepage.h
    src/mainwindow.cpp
    src/mainwindow.h
    src/tagstore.cpp
    src/tagstore.h
    src/todomodels.h
    src/todostore.cpp
    src/todostore.h
    src/todopage.cpp
    src/todopage.h
    src/filterparser.cpp
    src/filterparser.h
    src/autotagengine.cpp
    src/autotagengine.h
    src/autotagdialog.cpp
    src/autotagdialog.h
    src/addtagdialog.cpp
    src/addtagdialog.h
    src/tageditordialog.cpp
    src/tageditordialog.h
    src/advancedsearchdialog.cpp
    src/advancedsearchdialog.h
    src/untaggedview.cpp
    src/untaggedview.h
    src/daypage.cpp
    src/daypage.h
    src/statschart.cpp
    src/statschart.h
    src/statspage.cpp
    src/statspage.h
    src/timingdialog.cpp
    src/timingdialog.h
)

target_link_libraries(awqtui PRIVATE
    Qt6::Core
    Qt6::Gui
    Qt6::Widgets
    Qt6::Network
)

# 原生 DNS-SD：链接 Dnsapi
if(WIN32)
    target_link_libraries(awqtui PRIVATE Dnsapi)
endif()

# ---- 可选：同时构建并部署 aw-inbox-rust 服务端（sidecar）----
# 开启后，`cmake --build` 会在构建客户端之外，一并调用 build-server.ps1
# （cargo + VS2022 MSVC）构建 aw-inbox-rust.exe 并部署到 <build>/server/。
# release.ps1 的联合构建（-WithServer）默认开启此选项。
option(AW_ENABLE_SERVER "同时构建并部署 aw-inbox-rust.exe 服务端（需要 cargo + VS2022 C++ 工作负载）" OFF)

if(AW_ENABLE_SERVER)
    find_program(POWERSHELL_EXE powershell)
    if(NOT POWERSHELL_EXE)
        message(FATAL_ERROR "AW_ENABLE_SERVER=ON 但未找到 powershell")
    endif()
    find_program(CARGO_EXECUTABLE cargo)
    if(CARGO_EXECUTABLE)
        message(STATUS "aw-server: 启用 sidecar 服务端构建（cargo: ${CARGO_EXECUTABLE}）")
    else()
        message(WARNING "AW_ENABLE_SERVER=ON 但未找到 cargo，将在构建时由 build-server.ps1 报错")
    endif()
    # 服务端构建配置跟随客户端：CMAKE_BUILD_TYPE=Debug 时也编 Debug，其余一律 Release
    set(_aw_server_config "Release")
    if(CMAKE_BUILD_TYPE STREQUAL "Debug")
        set(_aw_server_config "Debug")
    endif()
    add_custom_target(aw-server ALL
        COMMAND "${POWERSHELL_EXE}" -NoProfile -ExecutionPolicy Bypass -File
                "${CMAKE_SOURCE_DIR}/build-server.ps1"
                -Config "${_aw_server_config}"
                -OutDir "${CMAKE_BINARY_DIR}/server"
        COMMENT "构建并部署 aw-inbox-rust.exe (${_aw_server_config}) → ${CMAKE_BINARY_DIR}/server"
        VERBATIM
    )
endif()

# 打包部署：把 Qt DLL 拷到构建目录
install(TARGETS awqtui BUNDLE DESTINATION .)
set(QT_DEPLOYMENT_TEST false)
qt_generate_deploy_app_script(
    TARGET awqtui
    OUTPUT_SCRIPT deploy_script
    NO_UNSUPPORTED_PLATFORM_ERROR
)
install(SCRIPT ${deploy_script})
