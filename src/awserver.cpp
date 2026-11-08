// awserver.cpp —— 本地服务端管理实现
#include "awserver.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QDebug>
#include <QDir>
#include <QFileInfo>
#include <QProcess>
#include <QSettings>
#include <QStandardPaths>
#include <QTcpSocket>
#include <QThread>
#include <QTimer>

#include <string>
#include <array>
#ifdef Q_OS_WIN
#include <winsock2.h>
#include <ws2tcpip.h>
#include <iphlpapi.h>
#include <windows.h>
#include <shellapi.h>
#endif

namespace awqtui {

namespace {
constexpr const char *kServerExeName = "aw-server.exe";
// 同族版本目录前缀：c:/workshop/aw-qtui-<ver>/…（用于识别运行中的 server 来自哪个发行版）
constexpr const char *kAppFamilyDirPrefix = "aw-qtui-";
} // namespace

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
    if (probePort(host, port, 600)) {
        // 端口已被监听：识别占用者是否为本族旧版本打包的 aw-server，是则淘汰并拉起当前版。
        // 这样部署新版本后，运行中的旧服务端会被新发行版覆盖、由当前版接替（对齐客户端交接）。
        if (replaceStaleServer(port))
            ; // 旧版已让位、端口空出 → 继续走拉起分支
        else
            return true; // 占用者就是当前版 / 不可判定 / 替换失败 → 复用现状，不折腾
    }

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

QString ServerLauncher::processExePath(quint32 pid)
{
    if (pid == 0)
        return QString();
    const HANDLE h = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
    if (!h)
        return QString();
    DWORD size = 1024;
    std::array<wchar_t, 1024> buf{};
    if (!QueryFullProcessImageNameW(h, 0, buf.data(), &size)) {
        CloseHandle(h);
        return QString();
    }
    CloseHandle(h);
    return QString::fromWCharArray(buf.data(), size);
}

// 端口已被同族旧版本 aw-server 占用时的替换：判定 → 结束旧进程 → 等端口释放。
bool ServerLauncher::replaceStaleServer(quint16 port)
{
#ifdef Q_OS_WIN
    // 1) 枚举监听端口的进程 pid
    quint32 pid = 0;
    ULONG size = 0;
    if (GetExtendedTcpTable(nullptr, &size, FALSE, AF_INET, TCP_TABLE_OWNER_PID_LISTENER, 0)
            == ERROR_INSUFFICIENT_BUFFER
        && size > 0) {
        QByteArray buf(int(size), Qt::Uninitialized);
        if (GetExtendedTcpTable(buf.data(), &size, FALSE, AF_INET,
                                TCP_TABLE_OWNER_PID_LISTENER, 0)
                == NO_ERROR) {
            const auto *tab = reinterpret_cast<MIB_TCPTABLE_OWNER_PID *>(buf.data());
            for (ULONG i = 0; i < tab->dwNumEntries; ++i) {
                const auto &row = tab->table[i];
                if (row.dwState != MIB_TCP_STATE_LISTEN)
                    continue;
                if (row.dwLocalPort == htons(port)) {
                    pid = row.dwOwningPid;
                    break;
                }
            }
        }
    }
    if (!pid)
        return false; // 拿不到 pid（如非管理员查看不了他人监听表）→ 保守不动

    // 2) 判定：必须是本族版本目录下的 aw-server.exe，且不是当前发行版打包的那个
    const QString exe = processExePath(pid);
    if (exe.isEmpty())
        return false;
    const QFileInfo fi(exe);
    if (fi.fileName().compare(QLatin1String(kServerExeName), Qt::CaseInsensitive) != 0)
        return false; // 占用者不是 aw-server（可能是用户自己的服务）→ 不碰
    const QString parentDirName = QFileInfo(QFileInfo(fi.absolutePath()).absolutePath()).fileName();
    if (!parentDirName.startsWith(QLatin1String(kAppFamilyDirPrefix)))
        return false; // 不在 aw-qtui-<ver> 家族目录 → 无法判定版本，保守不动

    const QString mine = locateServerExe();
    const QString mineNorm = QDir::cleanPath(mine).toLower();
    const QString theirsNorm = QDir::cleanPath(exe).toLower();
    if (!mine.isEmpty() && !mineNorm.isEmpty()
        && mineNorm == theirsNorm)
        return false; // 就是当前发行版打包的 server → 已在最新，无需替换

    // 3) 结束旧版本进程。服务端是无状态 sidecar，无优雅退出 IPC（纯 HTTP 监听者），
    //    数据全部落在 SQLite（aw-server.db / inbox / todo / sync.db），崩溃安全；
    //    且本类看护（setWatch）会在探测失败时自动重拉，故终止进程是安全的替换手段。
    qWarning() << "[awserver] 检测到旧版本服务端 (pid=" << pid << ")，结束并由当前版接替"
               << exe;
    const HANDLE h = OpenProcess(PROCESS_TERMINATE, FALSE, pid);
    if (h) {
        TerminateProcess(h, 0);
        CloseHandle(h);
    }

    // 4) 等待端口释放（最多约 6 秒）；未释放则回退保留现状
    const qint64 deadline = QDateTime::currentMSecsSinceEpoch() + 6000;
    while (QDateTime::currentMSecsSinceEpoch() < deadline) {
        if (!probePort(kServerProbeHost, port, 200))
            return true; // 端口空出，可拉起当前版
        QThread::msleep(200);
    }
    qWarning() << "[awserver] 旧版服务端未在超时内退出，保留现状";
    return false;
#else
    Q_UNUSED(port);
    return false;
#endif
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
    // 与 LocalSend 同策略的被动检测：程序不代写规则、不提权，入站放行交给
    // Windows 首次监听时的「允许访问」弹窗（普通权限）或用户手动添加。
    // 这里只按惯用名查规则是否存在（纯读，无需 admin），供启动日志提示与排错。
    // 注意：用户在系统弹窗里点「允许」时，系统自建的规则按 exe 路径命名、
    // 不叫这个名字 —— 检测不到不代表没放行，日志措辞要留这个余地。
    auto checkOne = [](const QString &ruleName) -> bool {
        QProcess p;
        p.start(QStringLiteral("netsh"),
                {QStringLiteral("advfirewall"), QStringLiteral("firewall"), QStringLiteral("show"),
                 QStringLiteral("rule"), QStringLiteral("name=") + ruleName});
        if (!p.waitForFinished(3000))
            return false;
        return p.exitCode() == 0; // 规则不存在时 netsh 返回 1
    };
    return checkOne(QLatin1String(kServerFirewallRule))
        && checkOne(QLatin1String(kServerFirewallRuleUdp));
#else
    return true; // 非 Windows 不涉及系统防火墙
#endif
}

} // namespace awqtui
