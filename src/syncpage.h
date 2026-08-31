// syncpage.h —— 局域网同步页
#pragma once

#include <QWidget>

#include "models.h"

class QLineEdit;
class QPlainTextEdit;
class QPushButton;
class QTableWidget;

namespace awqtui {

class ApiClient;
class MdnsDiscovery;
class StatusBadge;

struct SyncPeer {
    QString name;
    QString host;
    int port = 0;
};

class SyncPage : public QWidget
{
    Q_OBJECT
public:
    explicit SyncPage(ApiClient *api, MdnsDiscovery *mdns, QWidget *parent = nullptr);
    ~SyncPage() override;

    int deviceCount() const { return m_devices.size(); }

    void refreshDevices();
    void doSync();
    void heartbeat(bool quiet = false);
    void setServerUrl(const QString &url);
    QString serverUrl() const;

signals:
    void logMessage(const QString &line);

private slots:
    void onDiscover();
    void onPeerFound(const QString &name, const QString &host, int port);
    void onPeerLost(const QString &name);
    void onAddPeer();

private:
    void buildUi();
    void log(const QString &line);
    void rebuildPeerTable();
    void syncComplete(const SyncSummary &s);

    ApiClient *m_api;
    MdnsDiscovery *m_mdns;
    QList<DeviceInfo> m_devices;
    QList<SyncPeer> m_peers;

    QLineEdit *m_serverEdit;
    StatusBadge *m_serverBadge;
    QTableWidget *m_devTable;
    QPlainTextEdit *m_log;
    QPushButton *m_btnBrowse;
    QLineEdit *m_portEdit;
    QTableWidget *m_peerTable;
    QLineEdit *m_peerAdd;
    bool m_browsing = false;
};

} // namespace awqtui
