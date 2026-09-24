// syncpage.h —— 局域网同步页视图 (aw-sync-rust /api/0/sync)
//
// 后台同步逻辑（心跳 / 设备轮询 / 去抖自动推送 / 配对检测）已抽入无界面常驻的
// SyncService（syncservice.h）。本页仅负责展示与用户操作：订阅服务信号渲染设备、
// 状态、日志，并把配置/配对/回收站/快照等操作转发给 ApiClient。SynPage 属可淘汰页，
// 切走即销毁，服务常驻不受影响。
#pragma once

#include <QWidget>

#include "models.h"

#include <QCheckBox>
#include <QList>
class QComboBox;
class QLabel;
class QLineEdit;
class QPlainTextEdit;
class QPushButton;
class QTableWidget;
class QTimer;

// Qt Designer 布局（syncpage.ui），全局命名空间
namespace Ui { class SyncPage; }

namespace awqtui {

class ApiClient;
class SyncService;
class StatusBadge;

class SyncPage : public QWidget
{
    Q_OBJECT
public:
    explicit SyncPage(ApiClient *api, SyncService *service, QWidget *parent = nullptr);
    ~SyncPage() override;

    void refreshDevices();          // 请求服务拉取设备并回显
    void heartbeat(bool quiet = false); // 请求服务心跳并刷新徽标
    void setServerUrl(const QString &url);
    QString serverUrl() const;

    // 进入同步页：启动服务端发现广播 + 服务「局域网自动开启同步」+ 立即刷新
    void onEnteredSyncPage();

signals:
    // 日志追加（SyncDetailsPage 转发引擎/本页日志到此）
    void logMessage(const QString &line);

private slots:
    void onRefreshConfig();
    void onSaveConfig();
    void onInitiatePair();
    void onAcceptPair();
    void onSyncNow();
    void onRemoveDevice();
    // 设备行「归并」按钮：把候选旧记录归并进这一行（QMessageBox 确认，默认「否」）
    void onMergeDevice();
    void onSetAlias();
    void onClearAllDevices();
    // 一键清理：淘汰静默未配对行 + 删除长期同步不上的旧配对
    void onPurgeStaleDevices();
    void onClearLogs();
    void onClearAllTrash();
    void onRestoreTrashRow();
    void onDeleteTrashRow();
    void onExportSnapshot();
    void onImportSnapshot();
    void onUsePairCode();

private:
    // Qt Designer 生成的布局对象（syncpage.ui -> ui_syncpage.h）
    Ui::SyncPage *ui = nullptr;
    void buildUi();
    void log(const QString &line);
    void refreshSyncConfig();
    void refreshDeviceStats(const QString &deviceId);
    void refreshTrash();
    void renderDevices(const QList<SyncDevice> &devices);
    void updatePairBanner();
    void applyBadgeState(int state, const QString &text);

    ApiClient *m_api;
    SyncService *m_service;
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

    // 配对请求横幅（有 incoming_pair_request 的设备时显示在设备表上方）
    QWidget *m_pairBanner = nullptr;
    QLabel *m_pairBannerLbl = nullptr;
    QString m_pairBannerId;        // 当前横幅对应的请求方
    QString m_lastStatusText;      // 最近一次心跳成功的状态文本（同步结束回显徽标）
};

} // namespace awqtui