// awserver.cpp —— 本地服务端管理实现
#include "awserver.h"

#include <QCoreApplication>
#include <QDebug>
#include <QDir>
#include <QFileInfo>
#include <QProcess>
#include <QSettings>
#include <QStandardPaths>
#include <QTcpSocket>
#include <QTimer>

#include <string>
#ifdef Q_OS_WIN
#include <windows.h>
#include <shellapi.h>
#endif

namespace awqtui {

namespace {
constexpr const char *kServerExeName = "aw-server.exe";
}

ServerLauncher::ServerLauncher(QObject *parent)
    : QObject(parent), m_timer(new QTimer(this))
{
    m_timer->setSingleShot(false);
    connect(m_timer, &QTimer::timeout, this, &ServerLauncher::onWatchTick);
}

ServerLauncher::~ServerLauncher() = default;

QString ServerLauncher::locateServerExe()
{
    const QString appDir = QCoreApplication::applicationDirPath();
    const QStringList candidates = {
        appDir + QStringLiteral("/server/") + QLatin1String(kServerExeName),
        appDir + QStringLiteral("/") + QLatin1String(kServerExeName),
    };
    for (const QString &c : candidates) {
        if (QFileInfo::exists(c))
            return c;
    }
    return QString();
}

bool ServerLauncher::probePort(const QString &host, quint16 port, int timeoutMs)
{
    QTcpSocket sock;
    sock.connectToHost(host, port);
    return sock.waitForConnected(timeoutMs);
}

QString ServerLauncher::defaultServerDataDir()
{
    QString base = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    if (base.isEmpty())
        base = QDir::homePath() + QStringLiteral("/.aw-qtui");
    return base + QStringLiteral("/server");
}

bool ServerLauncher::ensureServerRunning(const QString &host, quint16 port, const QString &dataDir)
{
    if (probePort(host, port, 600))
        return true; // 已在运行

    const QString exe = locateServerExe();
    if (exe.isEmpty()) {
        qWarning() << "[awserver] 未找到服务端 exe，跳过拉起";
        return false;
    }

    QStringList args;
    args << QStringLiteral("--host") << QLatin1String(kServerListenHost)
         << QStringLiteral("--port") << QString::number(port)
         << QStringLiteral("--dbpath") << dataDir + QStringLiteral("/aw-server.db")
         << QStringLiteral("--no-legacy-import");

    QDir().mkpath(dataDir);

    // startDetached：进程独立于本客户端存活；工作目录设为数据目录
    const bool ok = QProcess::startDetached(exe, args, dataDir);
    qInfo() << "[awserver] 拉起服务端" << (ok ? "成功" : "失败") << exe << args;
    return ok;
}

void ServerLauncher::setWatch(bool on, const QString &host, quint16 port, const QString &dataDir,
                              int intervalMs)
{
    m_host = host;
    m_port = port;
    m_dataDir = dataDir;
    m_watch = on;
    if (on) {
        m_timer->start(intervalMs);
    } else {
        m_timer->stop();
    }
}

void ServerLauncher::onWatchTick()
{
    if (!m_watch)
        return;
    const bool running = probePort(m_host, m_port, 600);
    if (running != m_lastRunning) {
        m_lastRunning = running;
        emit serverStateChanged(running);
    }
    if (!running) {
        qWarning() << "[awserver] 检测到服务端未运行，重新拉起";
        ensureServerRunning(m_host, m_port, m_dataDir);
    }
}

// ---- 自启（HKCU Run 注册表，当前用户登录自启，无需 admin，任意会话上下文可用）----
// 说明：计划任务（Task Scheduler）在某些受限/非提升上下文创建会失败，注册表 Run 键最稳。

namespace {

QString autostartRunKey()
{
    return QStringLiteral(
        "HKEY_CURRENT_USER\\Software\\Microsoft\\Windows\\CurrentVersion\\Run");
}

} // namespace

bool ServerLauncher::installAutostart(const QString &serverExe, const QString &dataDir, quint16 port)
{
    if (serverExe.isEmpty() || !QFileInfo::exists(serverExe))
        return false;

    const QString cmd = QStringLiteral("\"%1\" --host %2 --port %3 --dbpath \"%4/aw-server.db\" --no-legacy-import")
                            .arg(serverExe)
                            .arg(QLatin1String(kServerListenHost))
                            .arg(port)
                            .arg(dataDir);
    QSettings s(autostartRunKey(), QSettings::NativeFormat);
    s.setValue(QStringLiteral("aw-qtui-server"), cmd);
    s.sync();
    const bool ok = s.status() == QSettings::NoError;
    qInfo() << "[awserver] 自启注册(HKCU Run):" << (ok ? "成功" : "失败") << cmd;
    return ok;
}

bool ServerLauncher::uninstallAutostart()
{
    QSettings s(autostartRunKey(), QSettings::NativeFormat);
    s.remove(QStringLiteral("aw-qtui-server"));
    s.sync();
    return true;
}

bool ServerLauncher::autostartInstalled()
{
    QSettings s(autostartRunKey(), QSettings::NativeFormat);
    return s.contains(QStringLiteral("aw-qtui-server"));
}

bool ServerLauncher::firewallRuleExists()
{
#ifdef Q_OS_WIN
    QProcess p;
    p.start(QStringLiteral("netsh"),
            {QStringLiteral("advfirewall"), QStringLiteral("firewall"), QStringLiteral("show"),
             QStringLiteral("rule"), QStringLiteral("name=") + QLatin1String(kServerFirewallRule)});
    if (!p.waitForFinished(3000))
        return false;
    // netsh show rule 对不存在的规则返回非 0 退出码
    return p.exitCode() == 0;
#else
    return true; // 非 Windows 不涉及系统防火墙
#endif
}

bool ServerLauncher::requestFirewallAllow()
{
#ifdef Q_OS_WIN
    const QString appExe = QCoreApplication::applicationFilePath();
    if (appExe.isEmpty() || !QFileInfo::exists(appExe)) {
        qWarning() << "[awserver] 无法定位自身程序以请求提权";
        return false;
    }
    // 提权运行自身（--firewall-allow）：UAC 显示的是 aw-qtui，而不是系统工具 net/netsh。
    // 用户看到的授权对象是自己的程序，授权后由提权实例落地添加防火墙规则。
    SHELLEXECUTEINFOW sei = {};
    sei.cbSize = sizeof(sei);
    sei.fMask = SEE_MASK_NOCLOSEPROCESS;
    sei.lpVerb = L"runas"; // 提权运行 → 弹 UAC，由用户确认授权
    sei.lpFile = appExe.toStdWString().c_str();
    sei.lpParameters = L"--firewall-allow";
    sei.nShow = SW_HIDE;
    if (!ShellExecuteExW(&sei)) {
        const DWORD err = GetLastError();
        qWarning() << "[awserver] 防火墙放行请求未完成（用户取消或提权失败），err=" << err;
        return false;
    }
    WaitForSingleObject(sei.hProcess, 15000);
    DWORD code = 0;
    GetExitCodeProcess(sei.hProcess, &code);
    CloseHandle(sei.hProcess);
    qInfo() << "[awserver] 防火墙放行完成（提权实例），exit=" << code;
    return code == 0;
#else
    return true;
#endif
}

int ServerLauncher::applyFirewallRule()
{
#ifdef Q_OS_WIN
    const QStringList args{
        QStringLiteral("advfirewall"), QStringLiteral("firewall"), QStringLiteral("add"),
        QStringLiteral("rule"),
        QStringLiteral("name=") + QLatin1String(kServerFirewallRule),
        QStringLiteral("dir=in"), QStringLiteral("action=allow"),
        QStringLiteral("protocol=TCP"), QStringLiteral("localport=%1").arg(kServerPort),
        QStringLiteral("profile=private")};
    return QProcess::execute(QStringLiteral("netsh"), args);
#else
    return 0;
#endif
}

} // namespace awqtui
