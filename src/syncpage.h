// syncpage.h —— 局域网同步页 (aw-sync-rust /api/0/sync)
#pragma once

#include <QWidget>

#include "models.h"

#include <QCheckBox>
#include <QComboBox>
class QLabel;
class QLineEdit;
class QPlainTextEdit;
class QPushButton;
class QTableWidget;
class QTabWidget;

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
    void onRefreshConfig();
    void onSaveConfig();
    void onCreatePairCode();
    void onJoinDevice();
    void onInitiatePair();
    void onAcceptPair();
    void onSyncNow();
    void onRemoveDevice();
    void onSetAlias();
    void onClearLogs();
    void onClearAllTrash();
    void onCloudKindChanged(int idx);
    void onCloudTest();
    void onCloudSave();
    void onCloudSyncNow();

private:
    void buildUi();
    void log(const QString &line);
    void rebuildPeerTable();
    void syncComplete(const ApplyResult &r);
    void refreshSyncConfig();
    void refreshDeviceStats(const QString &deviceId);
    void refreshTrash();

    ApiClient *m_api;
    MdnsDiscovery *m_mdns;
    QList<SyncDevice> m_devices;
    QList<SyncPeer> m_peers;
    QString m_currentPairCode;

    // 服务端地址
    QLineEdit *m_serverEdit;
    StatusBadge *m_serverBadge;

    // 设备列表
    QTableWidget *m_devTable;

    // 同步配置
    QCheckBox *m_chkEnabled;
    QCheckBox *m_chkHttp;
    QCheckBox *m_chkSyncInbox;
    QCheckBox *m_chkSyncActivity;
    QCheckBox *m_chkSyncTodo;
    QLineEdit *m_editAlias;
    QLineEdit *m_editListenPort;
    QLineEdit *m_editUdpPort;
    QPushButton *m_btnSaveConfig;

    // 配对
    QLabel *m_lblPairCode;
    QLineEdit *m_editPairCode;
    QPushButton *m_btnCreatePairCode;
    QPushButton *m_btnJoinDevice;
    QPushButton *m_btnInitiatePair;
    QPushButton *m_btnAcceptPair;

    // 操作
    QPushButton *m_btnSyncNow;
    QPushButton *m_btnRemoveDevice;
    QPushButton *m_btnSetAlias;
    QPushButton *m_btnClearLogs;

    // mDNS 自动发现
    QPushButton *m_btnBrowse;
    QLineEdit *m_portEdit;
    QTableWidget *m_peerTable;
    QLineEdit *m_peerAdd;
    bool m_browsing = false;

    // 日志
    QPlainTextEdit *m_log;

    // 统计 & 回收站
    QLabel *m_lblStats;
    QTableWidget *m_trashTable;
    QPushButton *m_btnClearTrash;

    // 云存储（实验性）
    QComboBox *m_cloudKind;
    QLineEdit *m_webdavUrl;
    QLineEdit *m_webdavUser;
    QLineEdit *m_webdavPass;
    QLineEdit *m_webdavPath;
    QLineEdit *m_s3Endpoint;
    QLineEdit *m_s3AccessKey;
    QLineEdit *m_s3SecretKey;
    QLineEdit *m_s3Bucket;
    QLineEdit *m_s3Region;
    QLineEdit *m_s3Path;
    QCheckBox *m_s3PathStyle;
    QCheckBox *m_s3Tls;
    QPushButton *m_btnCloudTest;
    QPushButton *m_btnCloudSave;
    QPushButton *m_btnCloudSync;
    QLabel *m_lblCloudStatus;
    CloudStorageConfig m_cloudCfg;
};

} // namespace awqtui
