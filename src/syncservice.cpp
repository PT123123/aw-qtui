// syncservice.cpp —— 无界面常驻的局域网同步引擎（后台心跳/自动推送/配对检测）
#include "syncservice.h"

#include "apiclient.h"
#include "config.h"
#include "widgets.h"

#include <QDateTime>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkInterface>
#include <QTimer>

namespace awqtui {

SyncService::SyncService(ApiClient *api, QObject *parent)
    : QObject(parent), m_api(api)
{
    // 常驻轮询：周期性拉取设备表 + 心跳。即使「同步」页从未打开、或已销毁，
    // 也在后台维持设备列表（配对检测 / 自动推送的对端在线判断都依赖它）。
    m_refreshTimer = new QTimer(this);
    m_refreshTimer->setInterval(5000);
    connect(m_refreshTimer, &QTimer::timeout, this, &SyncService::onRefreshTimer);
    m_refreshTimer->start();

    // 事件驱动同步：本机数据变更后去抖 4 秒推送一次（合并连续编辑）
    m_syncDebounce = new QTimer(this);
    m_syncDebounce->setSingleShot(true);
    m_syncDebounce->setInterval(4000);
    connect(m_syncDebounce, &QTimer::timeout, this, &SyncService::onLocalDataChanged);
    connect(m_api, &ApiClient::localDataChanged, this, [this] {
        if (!m_enabled)
            return;                   // 同步总开关关闭：等服务端自动循环
        if (!anyPeerOnline())
            return;                   // 对端都不在线：等自动循环
        m_syncDebounce->start();
    });
}

bool SyncService::anyPeerOnline() const
{
    for (const SyncDevice &d : m_devices)
        if (d.paired && !d.isSelf && d.isOnline)
            return true;
    return false;
}

void SyncService::onSyncPageOpened()
{
    if (!m_api)
        return;
    // 若在网络环境且同步尚未开启，自动开启（对齐 Android LanSyncNetworkMonitor 行为）
    if (onLocalNetwork() && !m_enabled) {
        m_enabled = true;
        log(QStringLiteral("已探测到局域网环境，自动开启局域网同步"));
        refreshDevices();
        QNetworkReply *r = m_api->getSyncConfig();
        connect(r, &QNetworkReply::finished, this, [this, r] {
            QJsonDocument doc;
            QString err;
            SyncConfig cfg;
            if (ApiClient::parseReply(r, &doc, &err)) {
                cfg = SyncConfig::fromJson(doc.object());
                cfg.enabled = true;
                QNetworkReply *rp = m_api->setSyncConfig(cfg.toJson());
                connect(rp, &QNetworkReply::finished, this, [rp] { rp->deleteLater(); });
            }
            r->deleteLater();
        });
    }
    refreshDevices();
    heartbeat(true);
}

void SyncService::onSyncPageClosed()
{
    // 引擎的轮询/自动推送常驻，离开页面不停止；广播停在 MainWindow（页面级）。
}

// 去抖定时器到期：本机数据在 4 秒内持续变更，合并为一次推送
void SyncService::onLocalDataChanged()
{
    if (!m_enabled)
        return;
    if (!anyPeerOnline())
        return;               // 对端都不在线：交给服务端自动循环
    log(QStringLiteral("本机数据有改动，立即推送…"));
    doSync();
}

// 探测是否处于可局域网同步的网络环境：存在至少一个非 loopback 的 IPv4 地址
bool SyncService::onLocalNetwork()
{
    for (const QNetworkInterface &iface : QNetworkInterface::allInterfaces()) {
        if (!(iface.flags() & QNetworkInterface::IsUp))
            continue;
        if (iface.flags() & QNetworkInterface::IsLoopBack)
            continue;
        for (const QHostAddress &addr : iface.allAddresses()) {
            if (addr.protocol() == QAbstractSocket::IPv4Protocol && !addr.isLoopback())
                return true;
        }
    }
    return false;
}

void SyncService::onRefreshTimer()
{
    refreshDevices();
    heartbeat(true);
}

void SyncService::setRefreshInterval(int ms)
{
    if (m_refreshTimer)
        m_refreshTimer->setInterval(ms);
}

void SyncService::log(const QString &line)
{
    emit logLine(line);
}

void SyncService::refreshDevices()
{
    if (!m_api) {
        emit devicesChanged(m_devices);
        return;
    }
    if (m_refreshing)
        return; // 上一次拉取在途，跳过本轮，避免叠加
    m_refreshing = true;
    emit syncBusy(QString());
    QNetworkReply *r = m_api->getSyncDevices();
    connect(r, &QNetworkReply::finished, this, [this, r] {
        m_refreshing = false;
        QJsonDocument doc;
        QString err;
        if (!ApiClient::parseReply(r, &doc, &err)) {
            if (!m_serverText.isEmpty())
                emit statusError(err);
            r->deleteLater();
            return;
        }
        m_devices.clear();
        const auto arr = doc.array();
        for (const auto &v : arr) {
            if (v.isObject())
                m_devices << SyncDevice::fromJson(v.toObject());
        }
        emit devicesChanged(m_devices);
        updatePairStatus();
        emit syncIdle();
        r->deleteLater();
    });
}

// 检测待处理配对请求：新请求触发一次日志 + 系统托盘通知（幂等，避免反复打扰）
void SyncService::updatePairStatus()
{
    QString pendingName;
    for (const SyncDevice &d : m_devices) {
        if (!d.isSelf && !d.paired && d.pairRequestPending) {
            pendingName = d.displayName();
            if (!m_notifiedPairReq.contains(d.id)) {
                m_notifiedPairReq.append(d.id);
                log(QStringLiteral("收到来自「%1」的配对请求").arg(pendingName));
                emit pairRequestReceived(pendingName);
            }
            break;
        }
    }
    setRefreshInterval(pendingName.isEmpty() ? 5000 : 3000); // 有待处理请求时加快刷新
}

void SyncService::markPairIgnored(const QString &id)
{
    if (!id.isEmpty() && !m_notifiedPairReq.contains(id))
        m_notifiedPairReq.append(id);
}

void SyncService::heartbeat(bool quiet)
{
    if (!m_api)
        return;
    fetchStatus(quiet);
}

void SyncService::fetchStatus(bool quiet)
{
    if (!m_api)
        return;
    if (!quiet)
        log(QStringLiteral("发送心跳…"));
    // 心跳通过 GET /status 实现（同时拿到同步开关/发现状态/监听端口）
    QNetworkReply *r = m_api->getSyncStatus();
    connect(r, &QNetworkReply::finished, this, [this, r, quiet] {
        QJsonDocument doc;
        QString err;
        if (!ApiClient::parseReply(r, &doc, &err)) {
            if (!quiet)
                log(QStringLiteral("心跳失败：%1").arg(err));
            if (!m_serverText.isEmpty())
                emit statusError(err);
            r->deleteLater();
            return;
        }
        if (!quiet)
            log(QStringLiteral("心跳已发送"));
        const auto o = doc.object();
        m_enabled = o.value(QStringLiteral("enabled")).toBool();
        const bool discovery = o.value(QStringLiteral("discovery_running")).toBool();
        const int listenPort = o.value(QStringLiteral("listen_port")).toInt(5600);
        QString text = m_enabled ? QStringLiteral("同步已开启") : QStringLiteral("同步未开启");
        if (m_enabled)
            text += QStringLiteral(" · 监听 %1").arg(listenPort);
        text += discovery ? QStringLiteral(" · 发现运行中") : QStringLiteral(" · 发现未开启");
        m_serverText = text;
        emit statusUpdated(text);
        r->deleteLater();
    });
}

void SyncService::doSync()
{
    for (const SyncDevice &d : m_devices) {
        if (d.paired && !d.isSelf && d.isOnline)
            m_syncQueue.append(d.id);
    }
    if (m_syncQueue.isEmpty()) {
        log(QStringLiteral("没有已配对且在线的设备可同步"));
        return;
    }
    log(QStringLiteral("开始与 %1 台在线设备同步…").arg(m_syncQueue.size()));
    processSyncQueue();
}

void SyncService::syncDevice(const QString &deviceId)
{
    if (!m_api)
        return;
    QString name = deviceId.left(8);
    for (const SyncDevice &d : m_devices)
        if (d.id == deviceId)
            name = d.displayName();
    emit syncBusy(name);
    log(QStringLiteral("开始与 %1 同步…").arg(name));

    QNetworkReply *r = m_api->triggerSync(deviceId);
    connect(r, &QNetworkReply::finished, this, [this, r] {
        QJsonDocument doc;
        QString err;
        if (!ApiClient::parseReply(r, &doc, &err)) {
            emit statusError(QStringLiteral("同步失败"));
            log(QStringLiteral("同步失败：%1").arg(err));
            processSyncQueue();
            r->deleteLater();
            return;
        }
        syncComplete(ApplyResult::fromJson(doc.object().value(QStringLiteral("result")).toObject()));
        refreshDevices();
        processSyncQueue();
        r->deleteLater();
    });
}

void SyncService::processSyncQueue()
{
    // 队列非空时由上一台完成回调驱动继续；空即收尾
    if (m_syncQueue.isEmpty()) {
        emit syncIdle();
        return;
    }
    const QString next = m_syncQueue.takeFirst();
    syncDevice(next);
}

void SyncService::syncComplete(const ApplyResult &r)
{
    log(QStringLiteral("同步完成：%1").arg(r.summary()));
    for (const TransferRecord &rec : r.records) {
        const QString label = rec.title.isEmpty() ? rec.logicalKey : rec.title;
        QString line = QStringLiteral("  · [%1] %2 %3")
                           .arg(rec.kind, TransferRecord::actionLabel(rec.action), label);
        if (!rec.reason.isEmpty())
            line += QStringLiteral("（%1）").arg(rec.reason);
        log(line);
    }
    for (const QString &e : r.errors)
        log(QStringLiteral("  ✗ %1").arg(e));
    emit syncIdle();
}

} // namespace awqtui