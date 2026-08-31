// mdnsdiscovery.h —— 局域网自动发现（Win32 原生 DNS-SD，服务类型 _activitywatch._tcp.local.）
#pragma once

#include <QHash>
#include <QObject>
#include <QString>
#include <QThread>

namespace awqtui {

// 在专用线程里跑 Win32 DNS-SD 的 worker（Qt 事件循环会派发其窗口消息回调）
class MdnsWorker : public QObject
{
    Q_OBJECT
public:
    explicit MdnsWorker(QObject *parent = nullptr);
    ~MdnsWorker() override;

signals:
    void peerFound(const QString &name, const QString &host, int port);
    void peerLost(const QString &name);
    void statusChanged(const QString &message);
    void errorOccurred(const QString &message);

public slots:
    void startBrowse();
    void stopBrowse();
    void registerService(int port);
    void unregisterService();

public:
    // 供 C 回调（worker 线程内）调用
    void handleBrowseResult(unsigned long status, void *records);
    void handleResolveResult(unsigned long status, void *instance);
    void handleRegisterResult(unsigned long status);
    QString instanceName() const;

private:
    QString m_baseName;                 // aw-sync-<hostname>
    void *m_browseCancel = nullptr;     // DNS_SERVICE_CANCEL*
    bool m_browseActive = false;
    QHash<QString, void *> m_resolveCancels; // 实例名 -> DNS_SERVICE_CANCEL*
    void *m_registerCancel = nullptr;
    bool m_registerActive = false;
};

class MdnsDiscovery : public QObject
{
    Q_OBJECT
public:
    explicit MdnsDiscovery(QObject *parent = nullptr);
    ~MdnsDiscovery() override;

    bool isBrowsing() const { return m_browsing; }
    bool isRegistered() const { return m_registered; }

signals:
    void peerFound(const QString &name, const QString &host, int port);
    void peerLost(const QString &name);
    void statusChanged(const QString &message);
    void errorOccurred(const QString &message);
    // 内部投递信号（供公共入口槽跨线程转发给 worker，与入口槽区分命名，
    // 避免与同名槽冲突导致“signal not found”且连接不生效）
    void browseRequested();
    void stopBrowseRequested();
    void registerRequested(int port);
    void unregisterRequested();

public slots:
    void startBrowse();
    void stopBrowse();
    bool registerService(int port); // true = 请求已提交，结果经 statusChanged/errorOccurred 通知
    void unregisterService();

private:
    QThread m_thread;
    MdnsWorker *m_worker;
    bool m_browsing = false;
    bool m_registered = false;
};

} // namespace awqtui
