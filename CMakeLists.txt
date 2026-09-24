cmake_minimum_required(VERSION 3.16)
project(aw-qtui VERSION 0.1.36 LANGUAGES CXX)

# Ninja 编译 .rc 资源（exe 内嵌图标）需要 CMP0116=NEW（CMake 3.19+ 引入）。
# 显式设定而非抬高 cmake_minimum_required，保留 3.16 的最低版本声明。
if(POLICY CMP0116)
    cmake_policy(SET CMP0116 NEW)
endif()

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
    src/autostart.cpp
    src/autostart.h
    src/globalshortcut.cpp
    src/globalshortcut.h
    src/settingsdialog.cpp
    src/settingsdialog.h
    src/singleinstance.cpp
    src/singleinstance.h
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
    src/inboxpage.ui
    src/syncpage.cpp
    src/syncpage.h
    src/syncpage.ui
    src/syncservice.cpp
    src/syncservice.h
    src/syncdetailspage.cpp
    src/syncdetailspage.h
    src/syncdetailspage.ui
    src/d1syncpage.cpp
    src/d1syncpage.h
    src/d1syncpage.ui
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
    src/activitypage.ui
    src/mainwindow.cpp
    src/mainwindow.h
    src/mainwindow.ui
    src/tagstore.cpp
    src/tagstore.h
    src/todomodels.h
    src/todostore.cpp
    src/todostore.h
    src/todoboard.cpp
    src/todoboard.h
    src/todopage.cpp
    src/todopage.h
    src/todopage.ui
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
    src/daypage.ui
    src/statschart.cpp
    src/statschart.h
    src/statspage.cpp
    src/statspage.h
    src/statspage.ui
    src/timingdialog.cpp
    src/timingdialog.h
    src/inboxsettingspage.cpp
    src/inboxsettingspage.h
    src/inboxsettingspage.ui
    src/trashpage.cpp
    src/trashpage.h
    src/trashpage.ui
    src/querypage.cpp
    src/querypage.h
    src/querypage.ui
    src/cloudbackuppage.cpp
    src/cloudbackuppage.h
    src/cloudbackuppage.ui
)

target_link_libraries(awqtui PRIVATE
    Qt6::Core
    Qt6::Gui
    Qt6::Widgets
    Qt6::Network
)

# 原生 DNS-SD：链接 Dnsapi；GetExtendedTcpTable 需要 iphlpapi；GetDpiForMonitor 需要 shcore
if(WIN32)
    target_link_libraries(awqtui PRIVATE Dnsapi iphlpapi shcore)

    # exe 内嵌图标：任务管理器 / 资源管理器只读 exe 里的图标资源，
    # 运行时 app.setWindowIcon(makeAppIcon()) 影响不到它们。
    # resources/app.ico 由 `just icon` 生成，必须先存在于仓库（已随仓库提交）。
    enable_language(RC)
    target_sources(awqtui PRIVATE resources/app.rc)
endif()

# ---- 版本号注入 C++ ----
# 与 project(VERSION) 保持单一真相，避免 src/config.h 的 kAppVersion 与 CMake 版本不同步。
target_compile_definitions(awqtui PRIVATE AW_VERSION="${PROJECT_VERSION}")

# 服务端 aw-server.exe 的构建与 Qt 运行库部署，改由仓库根 justfile 编排
# （server-aw-server 目标调 cargo workspace + MSVC；deploy 目标调 windeployqt），本 CMakeLists 只负责客户端编译。
