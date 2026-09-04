cmake_minimum_required(VERSION 3.16)
project(aw-qtui VERSION 0.1.1 LANGUAGES CXX)

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
    src/syncdetailspage.cpp
    src/syncdetailspage.h
    src/d1syncpage.cpp
    src/d1syncpage.h
    src/mdnsdiscovery.cpp
    src/mdnsdiscovery.h
    src/mockdata.cpp
    src/mockdata.h
    src/awdatastore.cpp
    src/awdatastore.h
    src/watcher.cpp
    src/watcher.h
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
    src/focusmodels.h
    src/focusstore.cpp
    src/focusstore.h
    src/focuswidgets.cpp
    src/focuswidgets.h
    src/focuscharts.cpp
    src/focuscharts.h
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
    src/inboxsettingspage.cpp
    src/inboxsettingspage.h
    src/trashpage.cpp
    src/trashpage.h
    src/stopwatchpage.cpp
    src/stopwatchpage.h
    src/querypage.cpp
    src/querypage.h
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

# ---- 版本号注入 C++ ----
# 与 project(VERSION) 保持单一真相，避免 src/config.h 的 kAppVersion 与 CMake 版本不同步。
target_compile_definitions(awqtui PRIVATE AW_VERSION="${PROJECT_VERSION}")

# 服务端 aw-server.exe 的构建与 Qt 运行库部署，改由仓库根 justfile 编排
# （server-aw-server 目标调 cargo workspace + MSVC；deploy 目标调 windeployqt），本 CMakeLists 只负责客户端编译。
