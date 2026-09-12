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
    // 两条规则都要就位：TCP 5600（HTTP 同步）+ UDP 46000（局域网发现广播）
    // 除「存在」外还要求覆盖「公用」配置文件，理由见 applyFirewallRule 的注释。
    auto checkOne = [](const QString &ruleName) -> bool {
        QProcess p;
        p.start(QStringLiteral("netsh"),
                {QStringLiteral("advfirewall"), QStringLiteral("firewall"), QStringLiteral("show"),
                 QStringLiteral("rule"), QStringLiteral("name=") + ruleName});
        if (!p.waitForFinished(3000))
            return false;
        if (p.exitCode() != 0)
            return false; // 规则不存在时 netsh 返回 1
        // 只看「配置文件」那一行：其余字段（本地 IP / 远程端口）都会写「任何」，
        // 对整段输出做 contains 会永远命中，检查就失去意义。
        const QString out = QString::fromLocal8Bit(p.readAllStandardOutput());
        const QStringList lines = out.split(QChar('\n'), Qt::SkipEmptyParts);
        for (const QString &raw : lines) {
            const QString line = raw.trimmed();
            if (!line.contains(QStringLiteral("配置文件"))
                && !line.contains(QStringLiteral("Profiles"), Qt::CaseInsensitive))
                continue;
            return line.contains(QStringLiteral("公用"))
                || line.contains(QStringLiteral("任何"))
                || line.contains(QStringLiteral("Public"), Qt::CaseInsensitive)
                || line.contains(QStringLiteral("All"), Qt::CaseInsensitive)
                || line.contains(QStringLiteral("Any"), Qt::CaseInsensitive);
        }
        return false;
    };
    return checkOne(QLatin1String(kServerFirewallRule))
        && checkOne(QLatin1String(kServerFirewallRuleUdp));
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
    // 两条规则：TCP 5600（HTTP 同步端口）+ UDP 46000（局域网设备发现广播端口）
    // aw-sync-rust 的 discovery.rs 在 UDP 46000 收发广播，缺这条规则时 Windows 防火墙
    // 会静默丢弃安卓端发来的发现包，导致两端都看不到对方。
    //
    // profile 必须同时覆盖「专用」和「公用」：Windows 把当前网络判为公用时，只放行
    // 专用 profile 的规则会被完全忽略（入站广播与连接一律丢弃，且没有任何提示）。
    // 家庭路由 / 手机热点很容易被归类为公用，用户也不会专门去改 —— 只绑专用 profile
    // 等于把「局域网发现」这条链路赌在网络分类上，症状是彻底发现不到对端。
    const QStringList delTargets{QLatin1String(kServerFirewallRule),
                                 QLatin1String(kServerFirewallRuleUdp)};
    // 先删同名旧规则再添加，保证幂等：升级场景下旧规则只覆盖专用 profile，
    // 直接 add 会因同名而失败（或残留旧规则），必须先清掉。
    // （netsh 按名字删除会一次删掉所有同名规则，历史上重复添加的副本也一并清掉。）
    for (const QString &name : delTargets) {
        QProcess::execute(QStringLiteral("netsh"),
                          {QStringLiteral("advfirewall"), QStringLiteral("firewall"),
                           QStringLiteral("delete"), QStringLiteral("rule"),
                           QStringLiteral("name=") + name});
    }
    const QStringList tcpArgs{
        QStringLiteral("advfirewall"), QStringLiteral("firewall"), QStringLiteral("add"),
        QStringLiteral("rule"),
        QStringLiteral("name=") + QLatin1String(kServerFirewallRule),
        QStringLiteral("dir=in"), QStringLiteral("action=allow"),
        QStringLiteral("protocol=TCP"),
        QStringLiteral("localport=%1").arg(kServerPort),
        QStringLiteral("profile=private,public")};
    const QStringList udpArgs{
        QStringLiteral("advfirewall"), QStringLiteral("firewall"), QStringLiteral("add"),
        QStringLiteral("rule"),
        QStringLiteral("name=") + QLatin1String(kServerFirewallRuleUdp),
        QStringLiteral("dir=in"), QStringLiteral("action=allow"),
        QStringLiteral("protocol=UDP"),
        QStringLiteral("localport=%1").arg(kServerDiscoveryPort),
        QStringLiteral("profile=private,public")};
    const int tcpRc = QProcess::execute(QStringLiteral("netsh"), tcpArgs);
    const int udpRc = QProcess::execute(QStringLiteral("netsh"), udpArgs);
    // 任一条失败都返回非 0（外层 --firewall-allow 分支据此判定）
    return (tcpRc == 0 && udpRc == 0) ? 0 : (tcpRc != 0 ? tcpRc : udpRc);
#else
    return 0;
#endif
}

} // namespace awqtui
