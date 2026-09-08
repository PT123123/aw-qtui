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

// Qt Designer 布局（syncpage.ui），全局命名空间
namespace Ui { class SyncPage; }

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
    // 同步一台设备（空 = 全部已配对在线设备依次同步；选中行时「立即同步」优先同步选中设备）
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

    // 本机数据变更（apiclient 写操作成功）：去抖后立即推送，不必等轮询周期
    void onLocalDataChanged();

signals:
    void logMessage(const QString &line);
    // 发现新的配对请求（MainWindow 弹系统托盘通知）
    void pairRequestReceived(const QString &deviceName);

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
    void onUsePairCode();

private:
    // Qt Designer 生成的布局对象（syncpage.ui -> ui_syncpage.h）
    Ui::SyncPage *ui = nullptr;
    void buildUi();
    void log(const QString &line);
    void syncComplete(const ApplyResult &r);
    void refreshSyncConfig();
    void refreshDeviceStats(const QString &deviceId);
    void refreshTrash();
    void syncDevice(const QString &deviceId);
    void processSyncQueue();
    void updatePairBanner();
    void setRefreshInterval(int ms);

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
    QComboBox *m_cmbSyncInterval = nullptr;    // 自动同步频率（实时10s/标准60s/省电5min/仅手动）
    QPushButton *m_btnSaveConfig;

    // 操作
    QPushButton *m_btnSyncNow;
    QPushButton *m_btnRemoveDevice;
    QPushButton *m_btnClearLogs;

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

    // 事件驱动同步：本机数据变更后去抖推送
    QTimer *m_syncDebounce = nullptr;

    // 配对请求横幅（有 incoming_pair_request 的设备时显示在设备表上方）
    QWidget *m_pairBanner = nullptr;
    QLabel *m_pairBannerLbl = nullptr;
    QString m_pairBannerId;        // 当前横幅对应的请求方
    QStringList m_notifiedPairReq; // 已提醒/已忽略的请求方，避免重复打扰

    // 「立即同步」的顺序同步队列（多台在线设备逐台执行）
    QStringList m_syncQueue;
};

} // namespace awqtui
