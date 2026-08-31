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

# 打包部署：把 Qt DLL 拷到构建目录
install(TARGETS awqtui BUNDLE DESTINATION .)
set(QT_DEPLOYMENT_TEST false)
qt_generate_deploy_app_script(
    TARGET awqtui
    OUTPUT_SCRIPT deploy_script
    NO_UNSUPPORTED_PLATFORM_ERROR
)
install(SCRIPT ${deploy_script})
