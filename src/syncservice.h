// syncservice.h —— 无界面常驻的局域网同步引擎 (aw-sync-rust /api/0/sync)
#pragma once

#include <QList>
#include <QObject>
#include <QString>
#include <QStringList>

#include "models.h"

class QTimer;

namespace awqtui {

class ApiClient;

// 常驻的同步核心：自持设备表、同步队列与去抖自动推送，即使「同步」页控件被
// 销毁/重建也照常工作。SyncPage 只是它的一个视图：订阅这里的信号渲染设备、
// 状态与日志，并把用户操作转发回来。本类不持有任何 QWidget —— 纯后台逻辑。
class SyncService : public QObject
{
    Q_OBJECT
public:
    explicit SyncService(ApiClient *api, QObject *parent = nullptr);

    const QList<SyncDevice> &devices() const { return m_devices; }
    // 是否已有任何「已配对且在线」的对端（自动推送据此判断是否立即推）
    bool anyPeerOnline() const;
    // 同步总开关（视图配置页加载/保存后回写，自动推送据此判断是否立即推）
    bool isEnabled() const { return m_enabled; }
    void setEnabled(bool on) { m_enabled = on; }

    // 进入/离开同步页。引擎的轮询与自动推送独立于页面生命周期，常驻运行；
    // 进入时额外做「局域网环境自动开启同步」并立即拉取设备；离开只交给页面停广播。
    void onSyncPageOpened();
    void onSyncPageClosed();

    // 对外动作
    void refreshDevices();
    void heartbeat(bool quiet = false);
    void doSync();                       // 同步全部已配对在线设备（依次）
    void syncDevice(const QString &id);  // 同步单台
    // 配对请求本轮已忽略：加入已提醒列表，避免重复打扰
    void markPairIgnored(const QString &id);

signals:
    void devicesChanged(const QList<SyncDevice> &devices); // 设备表刷新完成
    void statusUpdated(const QString &text);   // 心跳成功（徽标置「已连接」）
    void statusError(const QString &err);      // 拉取/心跳失败（徽标置「错误」）
    void syncBusy(const QString &deviceName);  // 正在与某台同步（徽标置「同步中」）
    void syncIdle();                           // 同步/拉取结束（徽标回「已连接」）
    void logLine(const QString &line);         // 引擎产生的日志行（视图日志面板）
    void pairRequestReceived(const QString &deviceName); // 发现新配对请求（系统托盘）

private slots:
    void onRefreshTimer();
    void onLocalDataChanged();
    void processSyncQueue();

private:
    void log(const QString &line);
    void fetchStatus(bool quiet);
    void updatePairStatus();
    void syncComplete(const ApplyResult &r);
    void setRefreshInterval(int ms);
    static bool onLocalNetwork();

    ApiClient *m_api = nullptr;
    QTimer *m_refreshTimer = nullptr;
    QTimer *m_syncDebounce = nullptr;

    QList<SyncDevice> m_devices;
    QStringList m_syncQueue;
    QStringList m_notifiedPairReq;

    bool m_enabled = false;
    bool m_refreshing = false;   // 设备拉取在途，避免快速连点叠加请求
    QString m_serverText;        // 最近一次心跳成功的状态文本（syncIdle 回显用）
};

} // namespace awqtui