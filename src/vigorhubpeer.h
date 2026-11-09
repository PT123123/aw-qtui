// vigorhubpeer.h —— 局域网对等的「被发现 / 被拉取」半边
#pragma once

#include <QByteArray>
#include <QHash>
#include <QHostAddress>
#include <QJsonObject>
#include <QObject>
#include <QString>
#include <functional>

class QTcpServer;
class QTcpSocket;
class QUdpSocket;
class QTimer;

namespace awqtui {

// VigorHubRelay 负责「推过去」，本类负责「让它来拉」——两边合起来才和手环保姆的
// 局域网能力对称：
//   1) 嵌一个极简 HTTP 服务，暴露
//        GET /api/health-motion/v1/info      报自身 DeviceInfo
//        GET /api/health-motion/v1/snapshot  报当前**全量**使用聚合，供桌面端拉取
//      其余路径回 {"app":"aw-qtui"}。
//   2) 每 2 秒往 UDP 组播 224.0.0.167:53317 宣告自身，桌面端据此把本机列进
//      「已发现设备」并周期性拉取。
//
// 契约以 vigorhub src/lan.rs / src/model.rs 为准。API 路径里的 health-motion 是
// 协议名（桌面程序后来改名 vigorhub，路径没跟着改），不要顺手改名。
//
// **本机传输也走同一条路**：监听 QHostAddress::Any、端口由系统分配、宣告里报实际
// 端口，代码里没有任何一处把目标写死成 127.0.0.1。桌面端从回环拉本机、从局域网
// 拉另一台 AW 主机，走的是完全相同的代码；后续局域网里加别的 AW 主机不用改这里。
//
// device_type 必须是 "aw" 而不是 "desktop"：桌面端 lan.rs::peer_from_announce 会
// 把 "desktop" 宣告整条丢掉（另一台 hub 的 /snapshot 明确回 ok:false，互拉无意义）。
// 指纹派生命名空间也必须和 hub 不同（hub 用 "health-motion|"，这里用 "aw-qtui|"）：
// 两者常常跑在同一台机器同一个用户下，指纹若相等，hub 会把本机 AW 当成自己丢掉。
class VigorHubPeer : public QObject
{
    Q_OBJECT
public:
    // 与 vigorhub src/lan.rs 的 MULTICAST_ADDR / MULTICAST_PORT 对齐。
    static constexpr const char *kMulticastAddr = "224.0.0.167";
    static constexpr quint16 kMulticastPort = 53317;
    static constexpr const char *kDeviceType = "aw";
    static constexpr int kAnnounceIntervalMs = 2000;

    explicit VigorHubPeer(QObject *parent = nullptr);
    ~VigorHubPeer() override;

    void setAlias(const QString &alias);
    QString alias() const { return m_alias; }

    // /snapshot 的内容来源。由 VigorHubRelay 提供它已持久化的全量累计表，
    // 这样「被拉取」和「主动推送」报的是同一份数据，不会各算一套。
    void setSnapshotProvider(std::function<QByteArray()> fn) { m_snapshot = std::move(fn); }

    bool start();  // 幂等：已在运行直接返回 true
    void stop();   // 幂等
    bool isRunning() const;
    quint16 port() const;
    // 实际绑定的地址。自测据此断言监听是通配（Any）而不是只绑回环——
    // 这条读的是 listen() 之后的真实 socket 状态，不受防火墙影响。
    QHostAddress bindAddress() const;

    // ---- 纯函数：身份与报文形状，自测直接打这里，不需要网络 ----
    static QString fingerprint();
    static QJsonObject deviceInfo(const QString &alias, quint16 port);
    static QByteArray announcePayload(const QString &alias, quint16 port);

    // 把一条 HTTP 请求头路由成完整应答字节。抽成纯函数是为了让自测既能逐字节
    // 断言报文形状，也能通过真 socket 打同一条路径。
    static QByteArray routeRequest(const QByteArray &requestHead,
                                   const QString &alias,
                                   quint16 port,
                                   const std::function<QByteArray()> &snapshot);

signals:
    void logLine(const QString &line);
    void snapshotServed();  // 桌面端真的来拉过一次（页面据此显示「已被拉取」）

private slots:
    void onNewConnection();
    void onAnnounceTimer();

private:
    void serveClient(QTcpSocket *sock);
    void replyAndClose(QTcpSocket *sock, const QByteArray &response);

    QTcpServer *m_server = nullptr;
    QUdpSocket *m_announce = nullptr;
    QTimer *m_announceTimer = nullptr;
    QString m_alias;
    std::function<QByteArray()> m_snapshot;
    // 每个连接攒到请求头收全为止；键是 socket，断开即删。
    QHash<QTcpSocket *, QByteArray> m_pending;
};

} // namespace awqtui