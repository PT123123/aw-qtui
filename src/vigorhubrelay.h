// vigorhubrelay.h —— 无界面常驻的 VigorHub 推送引擎（屏幕使用数据 → 健康运动桌面端）
#pragma once

#include <QByteArray>
#include <QHash>
#include <QJsonArray>
#include <QList>
#include <QObject>
#include <QSet>
#include <QString>
#include <QStringList>

#include "appsettings.h"

class QTimer;

// Qt 网络类是全局类，前向声明必须放在 awqtui 命名空间外，
// 否则会在命名空间内遮蔽全局 ::QNetworkReply 等类型。
class QNetworkAccessManager;
class QNetworkReply;

namespace awqtui {

class ApiClient;
class VigorHubPeer;

// 与 SyncService 同构的常驻后台引擎，两条路同时开着：
//  A) 主动推送（本类）：从本机 aw-server（ApiClient 的 base url）读 currentwindow /
//     afkstatus 桶，每个桶按事件 id 游标增量消费（游标落 QSettings，重启不重算）；
//     用 afkstatus 桶把「人离开」时段从窗口事件时长里裁掉，剩下才是有效使用；
//     按本地日 × 应用聚合成**全量累计**（内部统一用毫秒，保留亚秒精度、兼容旧 ini），
//     打包成 {"aw":{"app_usage":[...],"cursors":[...]}} POST 到
//     http://<桌面>:18765/api/health-motion/v1/push，回 {"ok":true} 视为成功。
//  B) 被发现 / 被拉取（VigorHubPeer）：组播宣告自身 + 暴露 /info、/snapshot，
//     桌面端自动发现本机并周期性来拉。报的是同一份已持久化累计表的全量。
//     这条路与推送目标地址无关：本机回环和局域网其它 AW 主机走完全相同的代码。
//
// 单位：协议字段 active_sec 与桌面端 aw.rs::ms/1000 一样是**秒**，
// 故只在 pushSnapshot 打包时把内部毫秒 /1000 换算；m_totals / m_runTotals
// 以及 QSettings 里的 vigorhub/totals 始终保持毫秒，不要中途改单位。
//
// 端口：桌面端 18765 被占用时会顺延到 18766~18780（lan.rs::port_candidates）。
// 仅当用户仍用默认端口时，推送失败会依次试后续端口，命中后记住实际地址；
// 用户自定义地址/端口一律原样推，不擅自换目标。
//
// 幂等：推送的是「某天某应用的全量累计」而非增量，VigorHub 侧按
// (day, source, app) 全量替换合并（model.rs::merge_usage_full）——重复推送、
// 网络重试、游标重置都只会把对应天校准回正确值，不会把时长算重。
// 只有收到 {"ok":true} 才推进并持久化游标与累计表。
class VigorHubRelay : public QObject
{
    Q_OBJECT
public:
    explicit VigorHubRelay(ApiClient *api, QObject *parent = nullptr);

    VigorHubRelayConfig config() const { return m_cfg; }
    // 保存配置并立即生效（重排自动推送定时器；首次接入的 15 秒内还会补一次首轮推送）
    void setConfig(const VigorHubRelayConfig &cfg);

    bool busy() const { return m_busy; }
    // 最近一次推送结果（"MM-dd hh:mm + 结果"，供页面直接显示）
    QString lastResult() const { return m_lastResult; }
    bool lastOk() const { return m_lastOk; }

    // 立即同步一轮（手动按钮 / 定时器）。已有同步进行中则忽略。
    void syncNow(bool manual = true);

    // 伴随服务当前监听的端口（0 = 没在跑）。页面显示「桌面端可来拉 :端口」用。
    quint16 peerPort() const;
    // 桌面端最近一次真的来拉过快照的时间（"MM-dd hh:mm"，空 = 还没被拉过）。
    QString lastPullAt() const { return m_lastPullAt; }

signals:
    void syncStarted();
    void syncFinished(bool ok, const QString &message);
    void logLine(const QString &line);
    void peerStateChanged();  // 伴随服务起停 / 被拉取，页面据此刷新状态行

private slots:
    void onAutoTimer();
    void onBucketsReply();
    void onEventsReply();
    void onPushReply();

private:
    // ---- 拉取流水线（全异步，回调经成员状态接续）----
    void startRun();
    void fetchNextBucket();   // 取下一个待拉桶；队列空了就打包推送
    void pushSnapshot();
    void postToCurrent();     // 把本轮 payload 发到当前候选地址（失败时在 onPushReply 里换下一个）
    void finish(bool ok, const QString &msg);

    // ---- 聚合口径（与 vigorhub src/aw.rs 完全一致）----
    struct Interval
    {
        qint64 start = 0;
        qint64 end = 0;
    };
    static qint64 parseIsoMs(const QString &iso);  // ISO-8601 → UTC ms（缺时区按 UTC）
    static qint64 localEpochDay(qint64 utcMs);     // UTC ms → 本地时区 epochDay
    static void mergeIntervals(QList<Interval> &iv); // 排序合并重叠/相邻区间
    static qint64 afkOverlap(qint64 start, qint64 end, const QList<Interval> &mergedAfk);

    void loadState();   // QSettings → 游标 + 累计表
    void saveState();   // 累计表裁剪 180 天前旧数据后写回
    void applyTimer();  // 按配置重排自动推送定时器
    void applyPeer();   // 按配置起停伴随服务（被发现 / 被拉取那半边）

    // 把累计表打成协议行。推送发「本轮触碰的天」，被拉取发「全部天」，
    // 两者共用这一份口径，避免两条路各算一套。
    QJsonArray usageRows(const QHash<qint64, QHash<QString, qint64>> &totals,
                         const QList<qint64> &days) const;
    static QJsonArray cursorsArray(const QHash<QString, qint64> &cursors);
    static QByteArray wrapAw(const QJsonArray &rows, const QJsonArray &cursors);
    // 伴随服务 /snapshot 的应答：已持久化的**全量**累计（不是本轮增量）。
    QByteArray fullSnapshot() const;
    QString effectiveAlias() const;
    QString effectiveSource() const;

    ApiClient *m_api = nullptr;
    VigorHubRelayConfig m_cfg;
    QTimer *m_timer = nullptr;                   // 自动推送定时器
    QNetworkAccessManager *m_pushNam = nullptr;  // 推送走 VigorHub 地址，与 ApiClient 分开
    VigorHubPeer *m_peer = nullptr;              // 组播宣告 + /info + /snapshot
    QString m_lastPullAt;
    bool m_busy = false;
    QString m_lastResult;
    bool m_lastOk = false;

    // ---- 持久化状态（QSettings: vigorhub/cursors、vigorhub/totals）----
    QHash<QString, qint64> m_cursors;                // 桶 → 已消费的最大事件 id
    QHash<qint64, QHash<QString, qint64>> m_totals;  // 本地日 → 应用 → 全量累计**毫秒**

    // ---- 单次运行的流水线状态 ----
    struct BucketTask
    {
        QString id;
        bool afk = false;
    };
    QList<BucketTask> m_queue;                        // 待拉取桶（AFK 桶先拉，供裁剪用）
    BucketTask m_currentTask;                         // 正在拉取的桶（串行请求，逐个接续）
    QHash<QString, qint64> m_newCursors;              // 本轮推进后的游标
    QHash<qint64, QHash<QString, qint64>> m_runTotals;  // 本轮增量累计（毫秒，推送成功才并入 m_totals）
    QSet<qint64> m_touchedDays;                       // 本轮有新数据的本地日
    QList<Interval> m_afk;                            // 本轮收集的 AFK 区间
    bool m_afkMerged = false;                         // m_afk 已排序合并（进入窗口桶前做一次）

    // ---- 本轮推送的候选地址与待发载荷（端口顺延回退用）----
    QByteArray m_payload;                             // 重试用同一份全量快照，幂等
    QStringList m_tryBases;                           // 默认端口时含 18765~18780
    int m_tryIndex = 0;                               // 当前试到第几个候选
};

} // namespace awqtui
