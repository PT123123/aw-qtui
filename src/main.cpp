// main.cpp —— 入口：python 之外完全独立运行的 C++ Qt 客户端
#include "apiclient.h"
#include "config.h"
#include "mainwindow.h"
#include "theme.h"

#include <QApplication>
#include <QCommandLineParser>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QMessageBox>
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
    parser.process(app);

    app.setStyleSheet(kGlobalQss);
    qDebug() << "stylesheet set";

    qDebug() << "creating MainWindow...";
    MainWindow win(parser.value(urlOpt));
    qDebug() << "MainWindow created";
    win.show();
    qDebug() << "win.show() done, entering exec";

    const QString shotDir = parser.value(shotOpt);
    if (!shotDir.isEmpty()) {
        QDir().mkpath(shotDir);
        const int shotDelay = qMax(0, parser.value(shotDelayOpt).toInt());
        QTimer::singleShot(shotDelay, &win, [&win, &app, shotDir] {
            win.grab().save(shotDir + QStringLiteral("/inbox.png"));
            win.switchPage(1);
            QTimer::singleShot(600, &win, [&win, &app, shotDir] {
                win.grab().save(shotDir + QStringLiteral("/sync.png"));
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
