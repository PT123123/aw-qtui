// mdnsdiscovery.cpp —— Win32 DNS-SD (dnsapi.dll) 封装
//
// 说明：Windows 10+ 原生 DNS-SD API（DnsServiceBrowse/Resolve/Register）的
// 回调经由调用线程的窗口消息队列派发，Qt 在 Windows 上的事件分发器会处理
// 线程消息，因此把操作放在 QThread 的 Qt 事件循环里即可收到回调。
#include "mdnsdiscovery.h"

#include "config.h"

#include <QMetaObject>
#include <QTimer>

#include <ws2tcpip.h>
#include <windns.h>
#include <winsock2.h>

#include <cstring>

#pragma comment(lib, "Dnsapi.lib")
#pragma comment(lib, "ws2_32.lib")

namespace awqtui {

namespace {
constexpr const wchar_t *kServiceType = L"_activitywatch._tcp.local.";

// 把 SOCKADDR/网络序地址转成 IPv4 点分字符串
QString ipToString(const unsigned long *ip4)
{
    if (!ip4)
        return QString();
    char buf[INET_ADDRSTRLEN] = {0};
    struct in_addr a;
    a.s_addr = *ip4;
    if (inet_ntop(AF_INET, &a, buf, sizeof(buf)))
        return QString::fromLatin1(buf);
    return QString();
}

// 取实例名的短名：aw-sync-HOST._activitywatch._tcp.local. -> aw-sync-HOST
QString shortInstanceName(const wchar_t *fqdn)
{
    if (!fqdn)
        return QString();
    const QString s = QString::fromWCharArray(fqdn);
    const int dot = s.indexOf(QLatin1Char('.'));
    return dot > 0 ? s.left(dot) : s;
}

// ---------------- C 回调（运行在 worker 线程） ----------------
void CALLBACK BrowseCallback(DWORD status, PVOID ctx, PDNS_RECORD records)
{
    auto *w = static_cast<MdnsWorker *>(ctx);
    if (w)
        w->handleBrowseResult(status, records);
}
void CALLBACK ResolveCallback(DWORD status, PVOID ctx, PDNS_SERVICE_INSTANCE instance)
{
    auto *w = static_cast<MdnsWorker *>(ctx);
    if (w)
        w->handleResolveResult(status, instance);
}
void CALLBACK RegisterCallback(DWORD status, PVOID ctx, PDNS_SERVICE_INSTANCE instance)
{
    auto *w = static_cast<MdnsWorker *>(ctx);
    if (w)
        w->handleRegisterResult(status);
}
} // namespace

// ------------------------------------------------------------------ //
// MdnsWorker

MdnsWorker::MdnsWorker(QObject *parent)
    : QObject(parent), m_baseName(QStringLiteral("aw-sync-%1").arg(hostname()))
{
}

MdnsWorker::~MdnsWorker()
{
    stopBrowse();
    unregisterService();
}

QString MdnsWorker::instanceName() const
{
    return m_baseName + QStringLiteral(".") + QString::fromWCharArray(kServiceType);
}

void MdnsWorker::handleBrowseResult(unsigned long status, void *records)
{
    // DNS_REQUEST_PENDING = 0 表示有记录
    if (status != 0 || !records)
        return;
    auto *rec = static_cast<DNS_RECORDW *>(records);
    for (; rec; rec = rec->pNext) {
        if (rec->wType != DNS_TYPE_PTR)
            continue;
        const QString shortName = shortInstanceName(rec->pName);
        if (shortName.isEmpty())
            continue;
        if (m_resolveCancels.contains(shortName))
            continue; // 已在解析
        // 发起解析
        DNS_SERVICE_RESOLVE_REQUEST req{};
        req.Version = DNS_QUERY_REQUEST_VERSION1;
        req.InterfaceIndex = 0;
        req.QueryName = rec->pName;
        req.pResolveCompletionCallback = ResolveCallback;
        req.pQueryContext = this;
        auto *cancel = new DNS_SERVICE_CANCEL{};
        const DWORD ret = DnsServiceResolve(&req, cancel);
        if (ret == DNS_REQUEST_PENDING) {
            m_resolveCancels.insert(shortName, cancel);
        } else {
            delete cancel;
            if (ret != ERROR_CANCELLED)
                emit errorOccurred(QStringLiteral("解析 %1 失败（%2）").arg(shortName).arg(ret));
        }
    }
}

void MdnsWorker::handleResolveResult(unsigned long status, void *instance)
{
    auto *inst = static_cast<DNS_SERVICE_INSTANCE *>(instance);
    if (status != 0 || !inst)
        return;
    const QString shortName = shortInstanceName(inst->pszInstanceName);
    const QString host = QString::fromWCharArray(inst->pszHostName ? inst->pszHostName : L"");
    QString ip;
    if (inst->ip4Address)
        ip = ipToString(inst->ip4Address);
    else if (inst->ip6Address)
        ip = QStringLiteral("[ipv6]");
    const int port = inst->wPort;
    DnsServiceFreeInstance(inst);
    if (m_resolveCancels.contains(shortName)) {
        auto *cancel = m_resolveCancels.take(shortName);
        delete cancel;
    }
    // 主机名优先，其次解析出的 IP
    QString addr = host;
    if (addr.isEmpty() || addr.startsWith(QLatin1String("localhost")))
        addr = ip;
    emit peerFound(shortName, addr, port);
}

void MdnsWorker::handleRegisterResult(unsigned long status)
{
    if (status == 0) {
        emit statusChanged(QStringLiteral("已注册 mDNS 服务 %1").arg(instanceName()));
    } else {
        emit errorOccurred(QStringLiteral("mDNS 注册失败（%1）").arg(status));
    }
}

void MdnsWorker::startBrowse()
{
    if (m_browseActive)
        return;
    DNS_SERVICE_BROWSE_REQUEST req{};
    req.Version = DNS_QUERY_REQUEST_VERSION1;
    req.InterfaceIndex = 0;
    req.QueryName = const_cast<wchar_t *>(kServiceType);
    req.pBrowseCallback = BrowseCallback;
    req.pQueryContext = this;
    m_browseCancel = new DNS_SERVICE_CANCEL{};
    const DWORD ret = DnsServiceBrowse(&req, static_cast<DNS_SERVICE_CANCEL *>(m_browseCancel));
    if (ret == DNS_REQUEST_PENDING) {
        m_browseActive = true;
        emit statusChanged(QStringLiteral("已开始浏览 %1").arg(QString::fromWCharArray(kServiceType)));
    } else {
        delete static_cast<DNS_SERVICE_CANCEL *>(m_browseCancel);
        m_browseCancel = nullptr;
        emit errorOccurred(QStringLiteral("启动 mDNS 浏览失败（%1）").arg(ret));
    }
}

void MdnsWorker::stopBrowse()
{
    if (m_browseActive && m_browseCancel) {
        DnsServiceBrowseCancel(static_cast<DNS_SERVICE_CANCEL *>(m_browseCancel));
        m_browseActive = false;
    }
    if (m_browseCancel) {
        delete static_cast<DNS_SERVICE_CANCEL *>(m_browseCancel);
        m_browseCancel = nullptr;
    }
    for (auto it = m_resolveCancels.begin(); it != m_resolveCancels.end(); ++it) {
        DnsServiceResolveCancel(static_cast<DNS_SERVICE_CANCEL *>(it.value()));
        delete static_cast<DNS_SERVICE_CANCEL *>(it.value());
    }
    m_resolveCancels.clear();
}

void MdnsWorker::registerService(int port)
{
    if (m_registerActive)
        return;
    const QString name = instanceName();
    DNS_SERVICE_INSTANCE *instance =
        DnsServiceConstructInstance(reinterpret_cast<const wchar_t *>(name.utf16()),
                                    nullptr, // 使用本机默认主机名
                                    nullptr, nullptr, WORD(port), 0, 0, 0, nullptr, nullptr);
    if (!instance) {
        emit errorOccurred(QStringLiteral("构造 mDNS 服务实例失败"));
        return;
    }
    DNS_SERVICE_REGISTER_REQUEST req{};
    req.Version = DNS_QUERY_REQUEST_VERSION1;
    req.InterfaceIndex = 0;
    req.pServiceInstance = instance;
    req.pRegisterCompletionCallback = RegisterCallback;
    req.pQueryContext = this;
    req.unicastEnabled = FALSE;
    m_registerCancel = new DNS_SERVICE_CANCEL{};
    const DWORD ret = DnsServiceRegister(&req, static_cast<DNS_SERVICE_CANCEL *>(m_registerCancel));
    DnsServiceFreeInstance(instance);
    if (ret == DNS_REQUEST_PENDING) {
        m_registerActive = true;
    } else {
        delete static_cast<DNS_SERVICE_CANCEL *>(m_registerCancel);
        m_registerCancel = nullptr;
        if (ret != ERROR_ALREADY_EXISTS) {
            emit errorOccurred(QStringLiteral("mDNS 注册请求失败（%1）").arg(ret));
        } else {
            emit statusChanged(QStringLiteral("mDNS 实例已存在（%1）").arg(name));
        }
    }
}

void MdnsWorker::unregisterService()
{
    if (m_registerActive && m_registerCancel) {
        DnsServiceRegisterCancel(static_cast<DNS_SERVICE_CANCEL *>(m_registerCancel));
        m_registerActive = false;
    }
    if (m_registerCancel) {
        delete static_cast<DNS_SERVICE_CANCEL *>(m_registerCancel);
        m_registerCancel = nullptr;
    }
}

// ------------------------------------------------------------------ //
// MdnsDiscovery

MdnsDiscovery::MdnsDiscovery(QObject *parent) : QObject(parent)
{
    m_worker = new MdnsWorker;
    m_worker->moveToThread(&m_thread);

    // worker 信号 -> 本对象信号（跨线程自动队列连接）
    connect(m_worker, &MdnsWorker::peerFound, this, &MdnsDiscovery::peerFound);
    connect(m_worker, &MdnsWorker::peerLost, this, &MdnsDiscovery::peerLost);
    connect(m_worker, &MdnsWorker::statusChanged, this, &MdnsDiscovery::statusChanged);
    connect(m_worker, &MdnsWorker::errorOccurred, this, &MdnsDiscovery::errorOccurred);

    // 本对象内部投递信号 -> worker 槽（队列调用，安全跨线程）
    connect(this, &MdnsDiscovery::browseRequested, m_worker, &MdnsWorker::startBrowse);
    connect(this, &MdnsDiscovery::stopBrowseRequested, m_worker, &MdnsWorker::stopBrowse);
    connect(this, &MdnsDiscovery::registerRequested, m_worker, &MdnsWorker::registerService);
    connect(this, &MdnsDiscovery::unregisterRequested, m_worker, &MdnsWorker::unregisterService);

    m_thread.start();
}

MdnsDiscovery::~MdnsDiscovery()
{
    QMetaObject::invokeMethod(m_worker, "stopBrowse", Qt::BlockingQueuedConnection);
    QMetaObject::invokeMethod(m_worker, "unregisterService", Qt::BlockingQueuedConnection);
    m_thread.quit();
    m_thread.wait(2000);
    delete m_worker;
}

void MdnsDiscovery::startBrowse()
{
    if (m_browsing)
        return;
    m_browsing = true;
    emit browseRequested();
}

void MdnsDiscovery::stopBrowse()
{
    if (!m_browsing)
        return;
    m_browsing = false;
    emit stopBrowseRequested();
}

bool MdnsDiscovery::registerService(int port)
{
    if (m_registered)
        return true;
    m_registered = true;
    emit registerRequested(port);
    return true;
}

void MdnsDiscovery::unregisterService()
{
    if (!m_registered)
        return;
    m_registered = false;
    emit unregisterRequested();
}

} // namespace awqtui
