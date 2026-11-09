// vigorhubpeer.cpp —— 伴随 HTTP 服务 + 组播宣告（契约对齐 vigorhub src/lan.rs）
#include "vigorhubpeer.h"

#include <QHostAddress>
#include <QHostInfo>
#include <QJsonDocument>
#include <QNetworkInterface>
#include <QPointer>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTimer>
#include <QUdpSocket>

namespace awqtui {

namespace {
constexpr int kClientTimeoutMs = 5000;
constexpr int kMaxRequestHeadBytes = 16 * 1024;  // 只收请求头，GET 不该有大量头部

QByteArray httpResponse(int status, const QByteArray &body)
{
    const char *reason = status == 200 ? "OK" : (status == 404 ? "Not Found" : "Method Not Allowed");
    QByteArray out;
    out.append(QByteArrayLiteral("HTTP/1.1 ") + QByteArray::number(status) + ' ' + reason
               + QByteArrayLiteral("\r\n"));
    out.append(QByteArrayLiteral("Content-Type: application/json; charset=utf-8\r\n"));
    out.append(QByteArrayLiteral("Content-Length: ") + QByteArray::number(body.size())
               + QByteArrayLiteral("\r\n"));
    out.append(QByteArrayLiteral("Connection: close\r\n\r\n"));
    out.append(body);
    return out;
}

// 本机出站 IPv4 对应的网卡。多网卡机器（WSL / VMware / VPN 各挂一张）上，
// 不显式钉住组播出口就会把宣告发到一张根本不通局域网的虚拟网卡上——桌面端
// lan.rs::bind_multicast_socket 同样为此显式设了 IP_MULTICAST_IF。
QNetworkInterface outboundInterface()
{
    QUdpSocket probe;
    probe.connectToHost(QHostAddress(QStringLiteral("8.8.8.8")), 53); // UDP 只选路，不发包
    const QHostAddress local = probe.localAddress();
    for (const QNetworkInterface &iface : QNetworkInterface::allInterfaces()) {
        if (!(iface.flags() & QNetworkInterface::IsUp)
            || !(iface.flags() & QNetworkInterface::CanMulticast))
            continue;
        for (const QNetworkAddressEntry &e : iface.addressEntries())
            if (e.ip() == local)
                return iface;
    }
    return QNetworkInterface(); // 空：交给系统默认出口
}
} // namespace

VigorHubPeer::VigorHubPeer(QObject *parent)
    : QObject(parent)
{
}

VigorHubPeer::~VigorHubPeer()
{
    stop();
}

void VigorHubPeer::setAlias(const QString &alias)
{
    m_alias = alias.trimmed();
}

quint16 VigorHubPeer::port() const
{
    return m_server ? m_server->serverPort() : 0;
}

bool VigorHubPeer::isRunning() const
{
    return m_server != nullptr && m_server->isListening();
}

QHostAddress VigorHubPeer::bindAddress() const
{
    return m_server ? m_server->serverAddress() : QHostAddress();
}

// 跨进程重启稳定：按主机名 + 用户名派生 FNV-1a 64，格式与 hub 的 our_fingerprint
// 一致（32 位小写十六进制）。命名空间前缀**必须**是 aw-qtui| 而不是 health-motion|：
// 同机同用户下两个指纹若相等，hub 会把本机 AW 伴随服务当成自己的环回宣告丢掉。
QString VigorHubPeer::fingerprint()
{
    static const QString fp = [] {
        QString host = qEnvironmentVariable("COMPUTERNAME");
        if (host.isEmpty())
            host = QHostInfo::localHostName();
        const QString seed = QStringLiteral("aw-qtui|%1|%2")
                                 .arg(host, qEnvironmentVariable("USERNAME"));
        const QByteArray bytes = seed.toUtf8();
        quint64 h = 0xcbf29ce484222325ULL;
        for (char b : bytes) {
            h ^= static_cast<quint8>(b);
            h *= 0x100000001b3ULL;
        }
        return QStringLiteral("%1").arg(h, 32, 16, QLatin1Char('0'));
    }();
    return fp;
}

QJsonObject VigorHubPeer::deviceInfo(const QString &alias, quint16 port)
{
    QJsonObject o;
    o.insert(QStringLiteral("alias"), alias);
    o.insert(QStringLiteral("device_type"), QString::fromLatin1(kDeviceType));
    o.insert(QStringLiteral("port"), static_cast<qint64>(port));
    o.insert(QStringLiteral("protocol"), QStringLiteral("http"));
    o.insert(QStringLiteral("fingerprint"), fingerprint());
    return o;
}

// hub 的 Announce 是 #[serde(flatten)] info + announce，所以报文是**平铺**的，
// 不是 {"info":{...}} 嵌套。手机那边两种都认，桌面端只认平铺。
QByteArray VigorHubPeer::announcePayload(const QString &alias, quint16 port)
{
    QJsonObject o = deviceInfo(alias, port);
    o.insert(QStringLiteral("announce"), true);
    return QJsonDocument(o).toJson(QJsonDocument::Compact);
}

QByteArray VigorHubPeer::routeRequest(const QByteArray &requestHead,
                                      const QString &alias,
                                      quint16 port,
                                      const std::function<QByteArray()> &snapshot)
{
    const int eol = requestHead.indexOf('\n');
    const QByteArray line = (eol < 0 ? requestHead : requestHead.left(eol)).trimmed();
    const QList<QByteArray> parts = line.split(' ');
    if (parts.size() < 2)
        return httpResponse(404, QByteArrayLiteral("{\"app\":\"aw-qtui\"}"));
    const QByteArray method = parts.at(0);
    QByteArray path = parts.at(1);
    const int q = path.indexOf('?');
    if (q >= 0)
        path.truncate(q);

    if (method != QByteArrayLiteral("GET"))
        return httpResponse(405, QByteArrayLiteral("{\"ok\":false,\"error\":\"GET only\"}"));

    if (path == QByteArrayLiteral("/api/health-motion/v1/info"))
        return httpResponse(200, QJsonDocument(deviceInfo(alias, port)).toJson(QJsonDocument::Compact));

    if (path == QByteArrayLiteral("/api/health-motion/v1/snapshot")) {
        // provider 没接上时回最小合法快照：Snapshot 的三个字段都可选，空对象即可，
        // 但不能回非 JSON，否则桌面端 pull_once 会判解析失败。
        const QByteArray body = snapshot ? snapshot() : QByteArrayLiteral("{}");
        return httpResponse(200, body.isEmpty() ? QByteArrayLiteral("{}") : body);
    }

    return httpResponse(200, QByteArrayLiteral("{\"app\":\"aw-qtui\"}"));
}

bool VigorHubPeer::start()
{
    if (isRunning())
        return true;

    m_server = new QTcpServer(this);
    // Any + 端口 0：本机回环和局域网其它主机走同一条监听，端口由系统分配后
    // 如实报进 /info 与组播宣告。不写死 127.0.0.1，也不写死 18765（那是 hub 的端口）。
    if (!m_server->listen(QHostAddress::Any, 0)) {
        emit logLine(QStringLiteral("[vigorhub] 伴随服务启动失败：%1").arg(m_server->errorString()));
        m_server->deleteLater();
        m_server = nullptr;
        return false;
    }
    connect(m_server, &QTcpServer::newConnection, this, &VigorHubPeer::onNewConnection);

    m_announce = new QUdpSocket(this);
    const QNetworkInterface iface = outboundInterface();
    if (iface.isValid())
        m_announce->setMulticastInterface(iface);

    m_announceTimer = new QTimer(this);
    m_announceTimer->setTimerType(Qt::VeryCoarseTimer);
    connect(m_announceTimer, &QTimer::timeout, this, &VigorHubPeer::onAnnounceTimer);
    m_announceTimer->start(kAnnounceIntervalMs);
    onAnnounceTimer(); // 立刻宣告一次，别让桌面端等满一个间隔才发现本机

    emit logLine(QStringLiteral("[vigorhub] 伴随服务已启动 :%1（设备类型 %2，指纹 %3）")
                     .arg(port())
                     .arg(QString::fromLatin1(kDeviceType), fingerprint()));
    return true;
}

void VigorHubPeer::stop()
{
    if (m_announceTimer) {
        m_announceTimer->stop();
        m_announceTimer->deleteLater();
        m_announceTimer = nullptr;
    }
    if (m_announce) {
        m_announce->close();
        m_announce->deleteLater();
        m_announce = nullptr;
    }
    for (auto it = m_pending.begin(); it != m_pending.end(); ++it)
        it.key()->deleteLater();
    m_pending.clear();
    if (m_server) {
        m_server->close();
        m_server->deleteLater();
        m_server = nullptr;
    }
}

void VigorHubPeer::onAnnounceTimer()
{
    if (!isRunning() || !m_announce)
        return;
    const QByteArray payload = announcePayload(m_alias, port());
    m_announce->writeDatagram(payload, QHostAddress(QString::fromLatin1(kMulticastAddr)),
                              kMulticastPort);
}

void VigorHubPeer::onNewConnection()
{
    while (m_server && m_server->hasPendingConnections()) {
        QTcpSocket *sock = m_server->nextPendingConnection();
        sock->setSocketOption(QAbstractSocket::LowDelayOption, 1);
        m_pending.insert(sock, QByteArray());
        connect(sock, &QTcpSocket::readyRead, this, [this, sock] { serveClient(sock); });
        connect(sock, &QTcpSocket::disconnected, this, [this, sock] {
            m_pending.remove(sock);
            sock->deleteLater();
        });
        // 半开连接别占着不放：桌面端 pull_once 超时是 5 秒，这边同样 5 秒收摊。
        QPointer<QTcpSocket> guard(sock);
        QTimer::singleShot(kClientTimeoutMs, this, [this, guard] {
            if (guard && m_pending.contains(guard)) {
                m_pending.remove(guard);
                guard->abort();
                guard->deleteLater();
            }
        });
        serveClient(sock); // 请求可能已经随连接一起到了
    }
}

void VigorHubPeer::serveClient(QTcpSocket *sock)
{
    auto it = m_pending.find(sock);
    if (it == m_pending.end())
        return;
    it.value().append(sock->readAll());
    if (it.value().size() > kMaxRequestHeadBytes) {
        replyAndClose(sock, httpResponse(404, QByteArrayLiteral("{\"app\":\"aw-qtui\"}")));
        return;
    }
    const int headEnd = it.value().indexOf(QByteArrayLiteral("\r\n\r\n"));
    if (headEnd < 0)
        return; // 头还没收全，等下一次 readyRead
    const QByteArray head = it.value().left(headEnd);
    const QByteArray response = routeRequest(head, m_alias, port(), m_snapshot);
    if (head.contains(QByteArrayLiteral("/api/health-motion/v1/snapshot")))
        emit snapshotServed();
    replyAndClose(sock, response);
}

void VigorHubPeer::replyAndClose(QTcpSocket *sock, const QByteArray &response)
{
    m_pending.remove(sock);
    sock->write(response);
    sock->flush();
    sock->disconnectFromHost();
}

} // namespace awqtui