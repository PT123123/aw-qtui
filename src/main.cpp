// main.cpp —— 入口：python 之外完全独立运行的 C++ Qt 客户端
#include "apiclient.h"
#include "config.h"
#include "mainwindow.h"
#include "theme.h"

#include <QApplication>
#include <QCommandLineParser>
#include <QDir>
#include <QFile>
#include <QMessageBox>
#include <QTextStream>
#include <QTimer>
#include <QStringList>
#include <exception>
#include <new>

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
