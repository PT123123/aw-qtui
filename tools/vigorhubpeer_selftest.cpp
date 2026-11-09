// vigorhubpeer_selftest.cpp —— VigorHub 局域网对等半边的自测（真起服务、真收组播）
//
// 编译运行见 justfile 的 peer-selftest 目标。不碰任何用户数据：本测试只构造
// VigorHubPeer（纯网络 + 身份），不构造 VigorHubRelay，因此不读写真实的 awqtui.ini。
//
// 覆盖：
//   身份   —— 指纹跨调用稳定、32 位十六进制、与 hub 的派生命名空间不同（同机才发现得了）
//   报文   —— /info 与组播宣告都是**平铺** DeviceInfo，device_type == "aw"
//   路由   —— /info、/snapshot、未知路径、非 GET、provider 缺失
//   真网络 —— 真 TCP 客户端打 /info 与 /snapshot；真组播接收socket 收一条宣告；
//             监听地址硬门 == Any（进程内非回环探测只作诊断，附 LocalHost-only control）
//   起停   —— start 幂等、stop 后端口归零、再 start 可用
//
// 真网络那几段挂的是**空快照** provider：宣告一发，同机跑着的 vigorhub 就会真的
// 来拉并写进它的 state.json。假数据的逐字节断言只留在纯函数段。
#include "vigorhubpeer.h"

#include <QCoreApplication>
#include <QHostAddress>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkDatagram>
#include <QNetworkInterface>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTimer>
#include <QUdpSocket>
#include <QEventLoop>
#include <cstdio>

using namespace awqtui;

static int g_fail = 0;
static int g_skip = 0;
#define CHECK(cond, msg)                                                       \
    do {                                                                       \
        if (cond)                                                              \
            printf("  [PASS] %s\n", msg);                                      \
        else {                                                                 \
            printf("  [FAIL] %s\n", msg);                                      \
            ++g_fail;                                                          \
        }                                                                      \
    } while (0)

// 与 vigorhub src/lan.rs::our_fingerprint 同口径的另一半：命名空间前缀不同。
// 这里独立算一遍，用来证明「同机同用户下两个指纹不相等」这条跨仓库契约。
static QString hubStyleFingerprint()
{
    const QString seed = QStringLiteral("health-motion|%1|%2")
                             .arg(qEnvironmentVariable("COMPUTERNAME"),
                                  qEnvironmentVariable("USERNAME"));
    const QByteArray bytes = seed.toUtf8();
    quint64 h = 0xcbf29ce484222325ULL;
    for (char b : bytes) {
        h ^= static_cast<quint8>(b);
        h *= 0x100000001b3ULL;
    }
    return QStringLiteral("%1").arg(h, 32, 16, QLatin1Char('0'));
}

static bool isHex32(const QString &s)
{
    if (s.size() != 32)
        return false;
    for (const QChar c : s)
        if (!(c.isDigit() || (c >= QLatin1Char('a') && c <= QLatin1Char('f'))))
            return false;
    return true;
}

// 真发一条 HTTP 请求并把整个应答收回来（对端 Connection: close）。
// 必须跑真事件循环：QTcpServer 的 newConnection 由事件循环派发，用
// waitForReadyRead 干等会让服务端永远不 accept，量到的是假超时。
static QByteArray httpRoundTrip(const QHostAddress &host, quint16 port,
                                const QByteArray &verb, const QByteArray &path,
                                QString *errOut = nullptr)
{
    QTcpSocket s;
    QByteArray all;
    QEventLoop loop;
    QTimer deadline;
    deadline.setSingleShot(true);
    QObject::connect(&deadline, &QTimer::timeout, &loop, [&] {
        if (errOut)
            *errOut = QStringLiteral("timeout after 5s (state=%1)").arg(int(s.state()));
        loop.quit();
    });
    QObject::connect(&s, &QTcpSocket::connected, &loop, [&] {
        s.write(verb + " " + path + " HTTP/1.1\r\nHost: peer\r\nConnection: close\r\n\r\n");
        s.flush();
    });
    QObject::connect(&s, &QTcpSocket::readyRead, &loop, [&] { all.append(s.readAll()); });
    QObject::connect(&s, &QTcpSocket::disconnected, &loop, [&] {
        all.append(s.readAll());
        loop.quit();
    });
    QObject::connect(&s, &QTcpSocket::errorOccurred, &loop, [&](QAbstractSocket::SocketError e) {
        if (errOut)
            *errOut = QStringLiteral("%1 (%2)").arg(s.errorString()).arg(int(e));
        loop.quit();
    });
    deadline.start(5000);
    s.connectToHost(host, port);
    loop.exec();
    return all;
}

static QByteArray httpGet(quint16 port, const QByteArray &path)
{
    return httpRoundTrip(QHostAddress::LocalHost, port, "GET", path);
}

static QByteArray bodyOf(const QByteArray &response)
{
    const int i = response.indexOf("\r\n\r\n");
    return i < 0 ? QByteArray() : response.mid(i + 4);
}

static int statusOf(const QByteArray &response)
{
    const QList<QByteArray> parts = response.left(response.indexOf('\r')).split(' ');
    return parts.size() >= 2 ? parts.at(1).toInt() : 0;
}

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    QCoreApplication::setApplicationName(QStringLiteral("aw-qtui-peer-selftest"));

    printf("== 身份 ==\n");
    const QString fp = VigorHubPeer::fingerprint();
    CHECK(isHex32(fp), "fingerprint is 32 lowercase hex chars");
    CHECK(fp == VigorHubPeer::fingerprint(), "fingerprint is stable across calls");
    CHECK(fp != hubStyleFingerprint(),
          "fingerprint differs from hub namespace (same host+user must not collide)");
    CHECK(QString::fromLatin1(VigorHubPeer::kDeviceType) == QStringLiteral("aw"),
          "device_type is aw, not desktop (hub drops desktop announces)");

    printf("== 报文形状 ==\n");
    const QJsonObject info = VigorHubPeer::deviceInfo(QStringLiteral("自测机"), 45123);
    CHECK(info.value(QStringLiteral("alias")).toString() == QStringLiteral("自测机"), "info carries alias");
    CHECK(info.value(QStringLiteral("device_type")).toString() == QStringLiteral("aw"), "info device_type");
    CHECK(info.value(QStringLiteral("port")).toInt() == 45123, "info carries the given port");
    CHECK(info.value(QStringLiteral("protocol")).toString() == QStringLiteral("http"), "info protocol");
    CHECK(info.value(QStringLiteral("fingerprint")).toString() == fp, "info fingerprint matches");

    const QJsonDocument ann = QJsonDocument::fromJson(VigorHubPeer::announcePayload(QStringLiteral("自测机"), 45123));
    CHECK(ann.isObject(), "announce is a JSON object");
    // hub 的 Announce 是 serde(flatten)，嵌套 {"info":{...}} 它解析不出来。
    CHECK(!ann.object().contains(QStringLiteral("info")), "announce is FLAT, not nested under info");
    CHECK(ann.object().value(QStringLiteral("announce")).toBool() == true, "announce flag is true");
    CHECK(ann.object().value(QStringLiteral("port")).toInt() == 45123, "announce carries port");
    CHECK(ann.object().value(QStringLiteral("device_type")).toString() == QStringLiteral("aw"), "announce device_type");

    printf("== 路由（纯函数）==\n");
    const QByteArray snap = QByteArrayLiteral(
        "{\"aw\":{\"app_usage\":[{\"day\":20000,\"source\":\"desktop\",\"app\":\"自测\",\"active_sec\":7}],"
        "\"cursors\":[{\"bucket\":\"b\",\"last_id\":3}]}}");
    auto provider = [&snap] { return snap; };
    const QByteArray head = QByteArrayLiteral("GET /api/health-motion/v1/info HTTP/1.1\r\nHost: x");
    const QByteArray r = VigorHubPeer::routeRequest(head, QStringLiteral("自测机"), 45123, provider);
    CHECK(statusOf(r) == 200, "GET /info -> 200");
    CHECK(QJsonDocument::fromJson(bodyOf(r)).object().value(QStringLiteral("port")).toInt() == 45123,
          "GET /info body reports the listening port");

    const QByteArray rs = VigorHubPeer::routeRequest(
        QByteArrayLiteral("GET /api/health-motion/v1/snapshot HTTP/1.1\r\nHost: x"),
        QStringLiteral("自测机"), 45123, provider);
    CHECK(statusOf(rs) == 200, "GET /snapshot -> 200");
    CHECK(bodyOf(rs) == snap, "GET /snapshot returns exactly what the provider gave");

    const QByteArray rEmpty = VigorHubPeer::routeRequest(
        QByteArrayLiteral("GET /api/health-motion/v1/snapshot HTTP/1.1\r\nHost: x"),
        QStringLiteral("自测机"), 45123, nullptr);
    CHECK(statusOf(rEmpty) == 200 && bodyOf(rEmpty) == QByteArrayLiteral("{}"),
          "GET /snapshot without provider still returns valid JSON");

    const QByteArray rQuery = VigorHubPeer::routeRequest(
        QByteArrayLiteral("GET /api/health-motion/v1/info?x=1 HTTP/1.1\r\nHost: x"),
        QStringLiteral("自测机"), 45123, provider);
    CHECK(statusOf(rQuery) == 200, "query string does not break path matching");

    const QByteArray rUnknown = VigorHubPeer::routeRequest(
        QByteArrayLiteral("GET /nope HTTP/1.1\r\nHost: x"), QStringLiteral("自测机"), 45123, provider);
    CHECK(bodyOf(rUnknown) == QByteArrayLiteral("{\"app\":\"aw-qtui\"}"), "unknown path -> app marker");

    const QByteArray rPost = VigorHubPeer::routeRequest(
        QByteArrayLiteral("POST /api/health-motion/v1/push HTTP/1.1\r\nHost: x"),
        QStringLiteral("自测机"), 45123, provider);
    CHECK(statusOf(rPost) == 405, "POST -> 405 (peer only serves GET; push goes the other way)");

    printf("== 真网络：TCP ==\n");
    // 真起的这一份**故意不带任何数据**：宣告发出去以后，同机跑着的 vigorhub 会
    // 发现本机并按 60 秒节拍真的来拉 /snapshot，拉到的东西会并进它的 state.json。
    // 第一版夹具在这里挂了上面那份假快照，结果假游标 {"bucket":"b"} 被写进了真库。
    // 假数据的逐字节断言留在上面的纯函数段（那里不联网，谁也拉不到）。
    const QByteArray emptySnap = QByteArrayLiteral("{\"aw\":{\"app_usage\":[],\"cursors\":[]}}");
    auto emptyProvider = [&emptySnap] { return emptySnap; };
    VigorHubPeer peer;
    peer.setAlias(QStringLiteral("自测机"));
    peer.setSnapshotProvider(emptyProvider);
    CHECK(peer.port() == 0 && !peer.isRunning(), "peer starts stopped");
    CHECK(peer.start(), "start() succeeds");
    const quint16 port = peer.port();
    CHECK(port != 0, "listening port is assigned by the OS, not hardcoded");
    CHECK(peer.isRunning(), "isRunning after start");
    CHECK(peer.start() && peer.port() == port, "start() is idempotent");
    // 硬门：读 listen() 之后的真实 socket 状态，证明监听是通配而不是只绑回环。
    // 这条不受防火墙 / 虚拟网卡影响，是「本机传输也走局域网那套代码」的可判定证据。
    CHECK(peer.bindAddress() == QHostAddress(QHostAddress::Any),
          "listener is bound to Any (wildcard), not LocalHost-only");

    const QByteArray liveInfo = httpGet(port, "/api/health-motion/v1/snapshot");
    CHECK(statusOf(liveInfo) == 200, "real TCP GET /snapshot -> 200");
    CHECK(bodyOf(liveInfo) == emptySnap, "real TCP /snapshot body matches provider");
    const QByteArray liveIdent = httpGet(port, "/api/health-motion/v1/info");
    const QJsonObject liveObj = QJsonDocument::fromJson(bodyOf(liveIdent)).object();
    CHECK(statusOf(liveIdent) == 200, "real TCP GET /info -> 200");
    CHECK(liveObj.value(QStringLiteral("port")).toInt() == port,
          "real /info reports the actual listening port (hub pulls this value)");
    CHECK(liveObj.value(QStringLiteral("device_type")).toString() == QStringLiteral("aw"), "real /info device_type");
    CHECK(liveObj.value(QStringLiteral("alias")).toString() == QStringLiteral("自测机"), "real /info alias");

    // 非回环地址的进程内探测只作**诊断信息**，不当门：本机 WLAN 是 Public 配置，
    // 同进程连自己的网卡地址会被防火墙静默丢掉（state=0 超时），而跨进程的真实
    // hub 却拉得到——它的日志里就有 pull:<本机局域网 IP>。判「是不是 localhost-only」
    // 用上面的 bindAddress() 硬门，别用这条会被环境干扰的探测。
    //
    // 但探测本身要有 control 自证：另起一个**只绑 LocalHost** 的服务，用同一个
    // httpRoundTrip 从回环打它必须 200。这一条红了说明是夹具坏了，不是网络坏了。
    {
        QTcpServer loopOnly;
        const bool ok = loopOnly.listen(QHostAddress::LocalHost, 0);
        CHECK(ok, "control: a LocalHost-only server can be created");
        if (ok) {
            QObject::connect(&loopOnly, &QTcpServer::newConnection, [&] {
                QTcpSocket *c = loopOnly.nextPendingConnection();
                QObject::connect(c, &QTcpSocket::readyRead, c, [c] {
                    c->readAll();
                    c->write(QByteArrayLiteral("HTTP/1.1 200 OK\r\nContent-Length: 2\r\n")
                             + QByteArrayLiteral("Connection: close\r\n\r\n{}"));
                    c->flush();
                    c->disconnectFromHost();
                });
            });
            const QByteArray ctl = httpGet(loopOnly.serverPort(), "/control");
            CHECK(statusOf(ctl) == 200,
                  "control: the probe harness itself works over loopback (200 from LocalHost-only server)");
            CHECK(loopOnly.serverAddress() == QHostAddress(QHostAddress::LocalHost),
                  "control: that server really is LocalHost-only, so it differs from the peer under test");
        }
        loopOnly.close();
    }
    QList<QHostAddress> candidates;
    for (const QNetworkInterface &iface : QNetworkInterface::allInterfaces()) {
        if (!(iface.flags() & QNetworkInterface::IsUp) || (iface.flags() & QNetworkInterface::IsLoopBack))
            continue;
        for (const QNetworkAddressEntry &e : iface.addressEntries())
            if (e.ip().protocol() == QAbstractSocket::IPv4Protocol && !e.ip().isLoopback()
                && !candidates.contains(e.ip()))
                candidates.append(e.ip());
    }
    if (candidates.isEmpty()) {
        printf("  [INFO] non-loopback probe: no IPv4 interface is up\n");
    } else {
        for (const QHostAddress &ip : candidates) {
            QString err;
            const QByteArray resp = httpRoundTrip(ip, port, "GET", "/api/health-motion/v1/info", &err);
            const bool ok = statusOf(resp) == 200;
            // 只打印网卡地址的最后一段，完整本机 IP 属于环境信息，不进仓库证据。
            printf("  [INFO] in-process probe to non-loopback .%s -> %s%s\n",
                   qPrintable(ip.toString().section(QLatin1Char('.'), -1)),
                   ok ? "200" : "unreachable",
                   err.isEmpty() ? "" : qPrintable(QStringLiteral(" (%1)").arg(err)));
        }
        printf("  [INFO] 上面这条超时不代表缺陷：跨进程的真实 hub 能拉到本机（见 docs 证据）\n");
    }

    printf("== 真网络：组播宣告 ==\n");
    {
        QUdpSocket rx;
        const bool bound = rx.bind(QHostAddress::AnyIPv4, VigorHubPeer::kMulticastPort,
                                   QUdpSocket::ShareAddress | QUdpSocket::ReuseAddressHint);
        if (!bound) {
            printf("  [SKIP] multicast receive: cannot bind %u (%s) - another process holds the group port\n",
                   unsigned(VigorHubPeer::kMulticastPort), qPrintable(rx.errorString()));
            ++g_skip;
        } else {
            bool joined = false;
            const QHostAddress group(QString::fromLatin1(VigorHubPeer::kMulticastAddr));
            for (const QNetworkInterface &iface : QNetworkInterface::allInterfaces()) {
                if ((iface.flags() & QNetworkInterface::IsUp)
                    && (iface.flags() & QNetworkInterface::CanMulticast)
                    && rx.joinMulticastGroup(group, iface))
                    joined = true;
            }
            if (!joined) {
                printf("  [SKIP] multicast receive: no usable multicast interface to join\n");
                ++g_skip;
            } else {
                // 宣告间隔 2 秒，等最多 8 秒（四个间隔）。必须跑真事件循环，
                // 否则 peer 的宣告定时器根本不会触发。
                //
                // 53317 是共享组播组：vigorhub 自己和 LocalSend 都往这里宣告。
                // 不能拿「收到的第一个数据报」当自己的——必须按指纹认领会来的那条，
                // 否则量到的是别人的身份（第一版夹具就是这么红的）。
                QByteArray got;
                int foreign = 0;
                QEventLoop loop;
                QObject::connect(&rx, &QUdpSocket::readyRead, &loop, [&] {
                    while (rx.hasPendingDatagrams()) {
                        const QByteArray d = rx.receiveDatagram().data();
                        const QJsonObject o = QJsonDocument::fromJson(d).object();
                        if (o.value(QStringLiteral("fingerprint")).toString() == fp) {
                            got = d;
                            loop.quit();
                            return;
                        }
                        ++foreign;
                    }
                });
                QTimer::singleShot(8000, &loop, &QEventLoop::quit);
                loop.exec();
                printf("    multicast: own=%d foreign=%d (shared group with hub / LocalSend)\n",
                       got.isEmpty() ? 0 : 1, foreign);
                CHECK(!got.isEmpty(), "our own multicast announce arrived on 224.0.0.167:53317");
                const QJsonObject o = QJsonDocument::fromJson(got).object();
                CHECK(o.value(QStringLiteral("device_type")).toString() == QStringLiteral("aw"),
                      "received announce has device_type aw");
                CHECK(o.value(QStringLiteral("port")).toInt() == port,
                      "received announce reports the live listening port");
                CHECK(!o.contains(QStringLiteral("info")),
                      "received announce is FLAT on the wire (hub's serde flatten needs this)");
                CHECK(o.value(QStringLiteral("announce")).toBool(), "received announce flag is true");
            }
        }
    }

    printf("== 起停 ==\n");
    peer.stop();
    CHECK(!peer.isRunning() && peer.port() == 0, "stop() releases the port");
    peer.stop();
    CHECK(!peer.isRunning(), "stop() is idempotent");
    CHECK(peer.start(), "start() works again after stop");
    CHECK(peer.port() != 0, "a new port is assigned after restart");
    CHECK(peer.bindAddress() == QHostAddress(QHostAddress::Any),
          "restart still binds Any (not LocalHost-only)");
    CHECK(httpGet(peer.port(), "/api/health-motion/v1/info").startsWith("HTTP/1.1 200"),
          "restarted peer still serves /info");
    peer.stop();

    printf("\npeer-selftest: fail=%d skip=%d\n", g_fail, g_skip);
    return g_fail == 0 ? 0 : 1;
}