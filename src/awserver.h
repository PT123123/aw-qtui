// awserver.h —— 本地 aw-inbox-rust 服务端管理：定位/探测/拉起/看护/自启
#pragma once

#include <QObject>
#include <QString>

class QTimer;

namespace awqtui {

// 本地服务端默认端口 / 局域网监听地址（多机互通，Syncthing 式）
constexpr quint16 kServerPort = 5600;
constexpr const char *kServerListenHost = "0.0.0.0";   // 监听所有网卡（局域网多机互通）
constexpr const char *kServerProbeHost = "127.0.0.1";  // 探测始终用回环（0.0.0.0 监听时回环可连）
constexpr const char *kServerFirewallRule = "aw-qtui-server"; // 防火墙规则名

// 本地服务端管理（sidecar）。
// 客户端通过相对路径定位打包的 aw-inbox-rust.exe，端口探测确认未运行则拉起，
// 并周期性看护：若探测失败则重新拉起，保证采集/读写连续性。
class ServerLauncher : public QObject
{
    Q_OBJECT
public:
    explicit ServerLauncher(QObject *parent = nullptr);
    ~ServerLauncher() override;

    // 定位打包的服务端 exe（优先 <appDir>/server/aw-inbox-rust.exe，其次 <appDir>/aw-inbox-rust.exe）。
    // 未找到返回空串。
    static QString locateServerExe();

    // 端口探测：host:port 是否已在监听（同步，阻塞至多 timeoutMs 毫秒）。
    static bool probePort(const QString &host, quint16 port, int timeoutMs = 600);

    // 默认服务端数据目录：%APPDATA%\aw-qtui\aw-qtui\server（与客户端 ini 同一根）
    static QString defaultServerDataDir();

    // 拉起本地 server（startDetached，工作目录=数据目录）。已监听则不重复拉起。
    // 返回 true 表示已确保 server 在运行（要么已在、要么启动下发成功）。
    bool ensureServerRunning(const QString &host, quint16 port, const QString &dataDir);

    // 开启/关闭看护轮询：每 intervalMs 探测一次，异常退出则重新拉起。
    void setWatch(bool on, const QString &host, quint16 port, const QString &dataDir,
                  int intervalMs = 15000);
    bool watchEnabled() const { return m_watch; }

    // 自启：注册/移除 Task Scheduler ONLOGON 任务（当前用户，无需 admin），
    // 任务带失败重启策略（RestartCount）。server 独立于客户端常驻，客户端没开也采集。
    static bool installAutostart(const QString &serverExe, const QString &dataDir, quint16 port);
    static bool uninstallAutostart();
    static bool autostartInstalled();

    // 防火墙：5600/TCP 入站规则是否已存在；不存在则提权请求放行（ShellExecute runas → UAC）。
    // 局域网多机互通依赖 server 监听 0.0.0.0，放行由程序主动向用户请求，而非留给手动。
    static bool firewallRuleExists();
    static bool requestFirewallAllow();

    // 提权实例执行的落地动作：用 netsh 添加 5600/TCP 入站放行规则（当前进程须已提权）。
    // 返回 netsh 退出码（0=成功）。由 main 的 --firewall-allow 分支调用。
    static int applyFirewallRule();

signals:
    // server 运行状态变化（看护轮询探测到后发出）
    void serverStateChanged(bool running);

private slots:
    void onWatchTick();

private:
    QString m_host;
    quint16 m_port = 0;
    QString m_dataDir;
    bool m_watch = false;
    bool m_lastRunning = false;
    QTimer *m_timer;
};

} // namespace awqtui
