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
class QTimer;

namespace awqtui {

class ApiClient;
class MdnsDiscovery;
class StatusBadge;

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

    // 探测是否处于可局域网同步的网络环境（存在非 loopback 的 IPv4）
    static bool onLocalNetwork();

    // 进入同步页时调用（启动 UDP 广播发现 + 网络环境自动开启同步 + 定时刷新）
    void onEnteredSyncPage();
    // 离开同步页时停止定时刷新（由 MainWindow 调用）
    void stopRefresh();

signals:
    void logMessage(const QString &line);

private slots:
    void onRefreshConfig();
    void onSaveConfig();
    void onInitiatePair();
    void onAcceptPair();
    void onSyncNow();
    void onRemoveDevice();
    void onSetAlias();
    void onClearAllDevices();
    void onClearLogs();
    void onClearAllTrash();
    void onRestoreTrashRow();
    void onDeleteTrashRow();
    void onExportSnapshot();
    void onImportSnapshot();
    void onRefreshTimer();

private:
    void buildUi();
    void log(const QString &line);
    void syncComplete(const ApplyResult &r);
    void refreshSyncConfig();
    void refreshDeviceStats(const QString &deviceId);
    void refreshTrash();

    ApiClient *m_api;
    MdnsDiscovery *m_mdns; // 保留指针但不再作为发现源（服务端用 UDP 广播发现）
    QList<SyncDevice> m_devices;

    // 服务端地址
    QLineEdit *m_serverEdit;
    StatusBadge *m_serverBadge;

    // 设备列表
    QTableWidget *m_devTable;

    // 同步配置（总开关和基础配置，同步范围已在SettingsDialog同步Tab设置）
    QCheckBox *m_chkEnabled;
    QCheckBox *m_chkHttp;
    QLineEdit *m_editAlias;
    QLineEdit *m_editListenPort;
    QLineEdit *m_editUdpPort;
    QComboBox *m_cmbSyncInterval = nullptr;    // 三档自动同步频率（狂暴/平和/静默）
    QPushButton *m_btnSaveConfig;

    // 配对（对齐 Android：addDevice + pair/initiate + pair/accept，无配对码）
    QPushButton *m_btnInitiatePair;
    QPushButton *m_btnAcceptPair;

    // 操作
    QPushButton *m_btnSyncNow;
    QPushButton *m_btnRemoveDevice;
    QPushButton *m_btnSetAlias;
    QPushButton *m_btnClearAllDevices;
    QPushButton *m_btnClearLogs;

    // 快照传输（WiFi 热点点对点，bb3f187）
    QPushButton *m_btnExportSnapshot;
    QPushButton *m_btnImportSnapshot;
    QLabel *m_lblSnapshot;

    // 日志
    QPlainTextEdit *m_log;

    // 统计 & 回收站
    QLabel *m_lblStats;
    QTableWidget *m_trashTable;
    QPushButton *m_btnRestoreTrash;
    QPushButton *m_btnDeleteTrash;
    QPushButton *m_btnClearTrash;

    // 定时刷新（进入页面后周期性拉取设备/状态，及时呈现 UDP 广播发现的设备）
    QTimer *m_refreshTimer = nullptr;
};

} // namespace awqtui
