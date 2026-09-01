// main.cpp —— 入口：python 之外完全独立运行的 C++ Qt 客户端
#include "apiclient.h"
#include "appsettings.h"
#include "awserver.h"
#include "config.h"
#include "mainwindow.h"
#include "settingsdialog.h"
#include "theme.h"

#include <QApplication>
#include <QColor>
#include <QCommandLineParser>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QLockFile>
#include <QMessageBox>
#include <QStandardPaths>
#include <QTextStream>
#include <QTimer>
#include <QStringList>
#include <exception>
#include <new>

// 崩溃诊断：SEH 未处理异常（如访问违例 0xC0000005）走这里落盘 minidump + 异常上下文，
// 便于定位 Qt 层 use-after-free / 内存破坏。产物在 %TEMP%\awqtui_crash\ 下。
#include <windows.h>
#include <dbghelp.h>
#pragma comment(lib, "dbghelp.lib")

static LONG WINAPI CrashHandler(EXCEPTION_POINTERS *ep)
{
    SYSTEMTIME st;
    GetLocalTime(&st);
    const QString stamp = QStringLiteral("%1%2%3_%4%5%6")
                              .arg(st.wYear, 4, 10, QLatin1Char('0'))
                              .arg(st.wMonth, 2, 10, QLatin1Char('0'))
                              .arg(st.wDay, 2, 10, QLatin1Char('0'))
                              .arg(st.wHour, 2, 10, QLatin1Char('0'))
                              .arg(st.wMinute, 2, 10, QLatin1Char('0'))
                              .arg(st.wSecond, 2, 10, QLatin1Char('0'));
    const QString dir = QDir::tempPath() + QStringLiteral("/awqtui_crash");
    QDir().mkpath(dir);

    // 1) 异常上下文日志
    {
        const QString logPath = dir + QStringLiteral("/crash_%1.log").arg(stamp);
        QFile f(logPath);
        if (f.open(QIODevice::WriteOnly | QIODevice::Text)) {
            QTextStream out(&f);
            out << "exception=0x" << QString::number(ep->ExceptionRecord->ExceptionCode, 16)
                << " address=0x" << QString::number(reinterpret_cast<quintptr>(ep->ExceptionRecord->ExceptionAddress), 16)
                << "\n";
            if (ep->ContextRecord) {
                const auto *c = ep->ContextRecord;
                out << "rip=0x" << QString::number(c->Rip, 16)
                    << " rsp=0x" << QString::number(c->Rsp, 16)
                    << " rbp=0x" << QString::number(c->Rbp, 16)
                    << " rax=0x" << QString::number(c->Rax, 16)
                    << " rbx=0x" << QString::number(c->Rbx, 16)
                    << " rcx=0x" << QString::number(c->Rcx, 16)
                    << " rdx=0x" << QString::number(c->Rdx, 16)
                    << " rsi=0x" << QString::number(c->Rsi, 16)
                    << " rdi=0x" << QString::number(c->Rdi, 16)
                    << "\n";
                // 返回地址栈（调用栈回溯，供无符号快速定位）
                const quintptr *p = reinterpret_cast<const quintptr *>(c->Rsp);
                for (int i = 0; i < 32; ++i) {
                    quintptr val;
                    if (!IsBadReadPtr(p, sizeof(quintptr))) {
                        val = *p;
                        out << "rsp[" << i << "]=0x" << QString::number(val, 16) << "\n";
                    }
                    ++p;
                }
            }
            out.flush();
        }
    }
    // 2) 完整 minidump
    {
        const QString dmpPath = dir + QStringLiteral("/crash_%1.dmp").arg(stamp);
        const HANDLE h = CreateFileW(reinterpret_cast<LPCWSTR>(dmpPath.utf16()), GENERIC_WRITE, 0,
                                     nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (h != INVALID_HANDLE_VALUE) {
            MINIDUMP_EXCEPTION_INFORMATION mei{};
            mei.ThreadId = GetCurrentThreadId();
            mei.ExceptionPointers = ep;
            mei.ClientPointers = FALSE;
            MiniDumpWriteDump(GetCurrentProcess(), GetCurrentProcessId(), h,
                              MiniDumpWithFullMemory, &mei, nullptr, nullptr);
            CloseHandle(h);
        }
    }
    return EXCEPTION_EXECUTE_HANDLER;
}

using namespace awqtui;

// 调试日志：把 qDebug 输出重定向到 %TEMP%\awqtui_debug.log
static void debugMessageHandler(QtMsgType type, const QMessageLogContext &ctx, const QString &msg)
{
    static QFile logFile(QDir::tempPath() + QStringLiteral("/awqtui_debug.log"));
    static bool initialized = false;
    if (!initialized) {
        logFile.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text);
        initialized = true;
    }
    QTextStream out(&logFile);
    const char *typeStr = "DEBUG";
    switch (type) {
    case QtWarningMsg: typeStr = "WARN"; break;
    case QtCriticalMsg: typeStr = "CRIT"; break;
    case QtFatalMsg: typeStr = "FATAL"; break;
    default: break;
    }
    out << QStringLiteral("[%1] %2: %3\n").arg(typeStr).arg(ctx.function ? ctx.function : "?").arg(msg);
    out.flush();
}

int main(int argc, char *argv[])
{
    qInstallMessageHandler(debugMessageHandler);
    SetUnhandledExceptionFilter(&CrashHandler);
    qDebug() << "=== awqtui starting ===";

    QApplication app(argc, argv);
    qDebug() << "QApplication created";
    QApplication::setApplicationName(QStringLiteral("aw-qtui"));
    QApplication::setApplicationVersion(kAppVersion);
    QApplication::setOrganizationName(QStringLiteral("aw-qtui"));

    // 单实例互斥：防止双开（QLockFile，崩溃残留锁可被自动接管）
    const QString lockPath = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation)
                             + QStringLiteral("/awqtui.lock");
    QDir().mkpath(QFileInfo(lockPath).absolutePath());
    QLockFile lock(lockPath);
    if (!lock.tryLock(0)) {
        QMessageBox::information(nullptr, QStringLiteral("aw-qtui"),
                                 QStringLiteral("aw-qtui 已在运行，本实例将退出。"));
        return 0;
    }

    QCommandLineParser parser;
    parser.setApplicationDescription(
        QStringLiteral("aw-qtui — ActivityWatch 收件箱 / 局域网同步桌面客户端（对齐 aw-inbox / aw-server-rust 协议）"));
    parser.addHelpOption();
    parser.addVersionOption();
    QCommandLineOption urlOpt(QStringLiteral("url"), QStringLiteral("服务端地址（默认 %1）").arg(kDefaultServerUrl),
                              QStringLiteral("url"));
    parser.addOption(urlOpt);
    QCommandLineOption shotOpt(QStringLiteral("screenshot"),
                               QStringLiteral("启动后把两页截图存到目录并退出（测试用）"),
                               QStringLiteral("dir"));
    parser.addOption(shotOpt);
    QCommandLineOption shotDelayOpt(QStringLiteral("shot-delay"),
                                    QStringLiteral("截图前等待毫秒数（默认 1200，配合 --screenshot）"),
                                    QStringLiteral("ms"), QStringLiteral("1200"));
    parser.addOption(shotDelayOpt);
    QCommandLineOption shotSettingsOpt(QStringLiteral("shot-settings"),
                                       QStringLiteral("打开设置对话框并截图后退出（测试用）"),
                                       QStringLiteral("dir"));
    parser.addOption(shotSettingsOpt);
    parser.process(app);

    // 应用主题：加载上次保存的主题，更新语义色并生成全局 QSS
    const QString themeId = loadThemeId();
    gTheme = findTheme(themeId);
    applyThemeColors(*gTheme);
    app.setStyleSheet(gGlobalQss);
    // emoji 程序图标（纯代码渲染，无需额外资源文件）
    app.setWindowIcon(makeEmojiIcon(QString::fromUtf8(gTheme->emoji), QColor(gTheme->accent)));
    qDebug() << "stylesheet set, theme =" << gTheme->id;

    qDebug() << "creating MainWindow...";
    // 本地服务端自动管理：相对路径定位 exe → 端口探测 → 拉起 → 看护 + 自启
    ServerLauncher server;
    const QString url = parser.value(urlOpt);
    const bool isLocalDefault = url.isEmpty() || url == kDefaultServerUrl;
    if (isLocalDefault && loadServerAutoManage()) {
        const QString dataDir = ServerLauncher::defaultServerDataDir();
        server.ensureServerRunning(kServerProbeHost, kServerPort, dataDir);
        if (loadServerAutostart()) {
            const QString exe = ServerLauncher::locateServerExe();
            // 幂等覆盖：每次启动重写为当前 exe 绝对路径，发布目录变更后自启仍指向正确位置
            if (!exe.isEmpty())
                ServerLauncher::installAutostart(exe, dataDir, kServerPort);
        }
        server.setWatch(true, kServerProbeHost, kServerPort, dataDir);
        // 局域网互通：server 监听 0.0.0.0:5600，主窗口显示后若入站规则缺失则主动弹 UAC 请求放行
        QTimer::singleShot(400, [] {
            if (!ServerLauncher::firewallRuleExists())
                ServerLauncher::requestFirewallAllow();
        });
        qInfo() << "[main] 本地服务端管理已启用（监听" << kServerListenHost << ":" << kServerPort << "）";
    }
    MainWindow win(url);
    qDebug() << "MainWindow created";
    win.show();
    qDebug() << "win.show() done, entering exec";

    const QString shotDir = parser.value(shotOpt);
    if (!shotDir.isEmpty()) {
        QDir().mkpath(shotDir);
        const int shotDelay = qMax(0, parser.value(shotDelayOpt).toInt());
        QTimer::singleShot(shotDelay, &win, [&win, &app, shotDir] {
            win.grab().save(shotDir + QStringLiteral("/inbox.png"));
            win.switchPage(3); // Todo
            QTimer::singleShot(600, &win, [&win, &app, shotDir] {
                win.grab().save(shotDir + QStringLiteral("/todo.png"));
                win.switchPage(1); // Timeline（沿用原 sync.png 命名，保持测试脚本兼容）
                QTimer::singleShot(600, &win, [&win, &app, shotDir] {
                    win.grab().save(shotDir + QStringLiteral("/sync.png"));
                    app.quit();
                });
            });
        });
    }

    // 设置对话框截图（测试用）：直接构造对话框，抓图后退出
    const QString shotSettingsDir = parser.value(shotSettingsOpt);
    if (!shotSettingsDir.isEmpty()) {
        QDir().mkpath(shotSettingsDir);
        const int shotDelay = qMax(0, parser.value(shotDelayOpt).toInt());
        QTimer::singleShot(shotDelay, &win, [&app, shotSettingsDir] {
            SettingsDialog dlg(loadShortcuts(), loadThemeId(), loadUiEffects());
            dlg.show();
            QTimer::singleShot(600, &dlg, [&dlg, &app, shotSettingsDir] {
                dlg.grab().save(shotSettingsDir + QStringLiteral("/settings.png"));
                app.quit();
            });
        });
    }

    try {
        return QApplication::exec();
    } catch (const std::bad_array_new_length &e) {
        qCritical() << "FATAL std::bad_array_new_length:" << e.what();
        QMessageBox::critical(nullptr, QStringLiteral("崩溃"),
            QStringLiteral("捕获到 std::bad_array_new_length 异常：%1\n\n这通常是因为某处 new[]/resize 传入了负数或溢出的大小。").arg(QString::fromUtf8(e.what())));
        return 1;
    } catch (const std::bad_alloc &e) {
        qCritical() << "FATAL std::bad_alloc:" << e.what();
        QMessageBox::critical(nullptr, QStringLiteral("崩溃"),
            QStringLiteral("内存分配失败：%1").arg(QString::fromUtf8(e.what())));
        return 1;
    } catch (const std::exception &e) {
        qCritical() << "FATAL std::exception:" << e.what();
        QMessageBox::critical(nullptr, QStringLiteral("崩溃"),
            QStringLiteral("未捕获异常：%1").arg(QString::fromUtf8(e.what())));
        return 1;
    } catch (...) {
        qCritical() << "FATAL unknown exception";
        QMessageBox::critical(nullptr, QStringLiteral("崩溃"), QStringLiteral("未知类型异常"));
        return 1;
    }
}
