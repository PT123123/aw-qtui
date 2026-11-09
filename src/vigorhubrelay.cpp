// vigorhubrelay.cpp —— VigorHub 推送引擎实现（聚合口径对齐 vigorhub/src/aw.rs）
#include "vigorhubrelay.h"

#include <QDateTime>
#include <QHostInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QRegularExpression>
#include <QSettings>
#include <QStringList>
#include <QTimer>
#include <QUrl>
#include <algorithm>
#include <limits>

#include "apiclient.h"
#include "awdatastore.h"
#include "config.h"
#include "vigorhubpeer.h"

namespace awqtui {

namespace {
// 与 vigorhub aw.rs 对齐的常量
constexpr int kEventsLimit = 5000;          // 每桶单次拉取的事件上限
constexpr int kFirstPullLookbackDays = 30;  // 首次接入最多回溯多少天
constexpr int kPruneDays = 180;             // 累计表只保留最近 180 天（对齐桌面端裁剪）
constexpr int kInitialDelayMs = 15000;      // 启动后首轮推送的宽限（等 aw-server 就绪）
constexpr int kPushTimeoutMs = 15000;
constexpr int kVigorHubDefaultPort = 18765;     // 对齐 vigorhub lan.rs DEFAULT_HTTP_PORT
constexpr int kVigorHubPortFallbackCount = 16;  // 对齐 lan.rs PORT_FALLBACK_COUNT（18765~18780）
constexpr int kFallbackTimeoutMs = 3000;       // 回退探测别卡满 kPushTimeoutMs

// 地址归一：补协议、去尾斜杠 → "http://127.0.0.1:18765"
QString normalizeAddress(QString addr)
{
    addr = addr.trimmed();
    if (!addr.isEmpty() && !addr.startsWith(QStringLiteral("http://"))
        && !addr.startsWith(QStringLiteral("https://")))
        addr.prepend(QStringLiteral("http://"));
    while (addr.endsWith(QLatin1Char('/')))
        addr.chop(1);
    return addr;
}

// 桌面端 18765 被占用时会顺延监听端口（lan.rs::port_candidates）。只在用户仍用
// 默认端口时生成候选；自定义地址 / 自定义端口一律原样推，不擅自换目标。
QStringList pushBaseCandidates(const QString &base)
{
    QStringList out{base};
    const QUrl u(base);
    if (u.port() != kVigorHubDefaultPort)
        return out;
    for (int i = 1; i < kVigorHubPortFallbackCount; ++i) {
        QUrl c(base);
        c.setPort(kVigorHubDefaultPort + i);
        out.append(c.toString(QUrl::RemovePath | QUrl::RemoveQuery | QUrl::RemoveFragment));
    }
    return out;
}
} // namespace

VigorHubRelay::VigorHubRelay(ApiClient *api, QObject *parent)
    : QObject(parent)
    , m_api(api)
{
    m_cfg = loadVigorHubRelay();
    m_timer = new QTimer(this);
    m_timer->setTimerType(Qt::VeryCoarseTimer);
    connect(m_timer, &QTimer::timeout, this, &VigorHubRelay::onAutoTimer);
    m_pushNam = new QNetworkAccessManager(this);
    m_pushNam->setTransferTimeout(kPushTimeoutMs);

    loadState();
    applyTimer();

    // 伴随服务：让桌面端能发现本机并主动来拉。报的是已持久化的全量累计表，
    // 与推送共用同一份数据，所以「被拉取」不需要等一轮推送跑完才有内容。
    m_peer = new VigorHubPeer(this);
    m_peer->setSnapshotProvider([this] { return fullSnapshot(); });
    connect(m_peer, &VigorHubPeer::logLine, this, &VigorHubRelay::logLine);
    connect(m_peer, &VigorHubPeer::snapshotServed, this, [this] {
        m_lastPullAt = QDateTime::currentDateTime().toString(QStringLiteral("MM-dd hh:mm"));
        emit logLine(QStringLiteral("[vigorhub] 桌面端来拉了一次快照（全量 %1 天）")
                         .arg(m_totals.size()));
        emit peerStateChanged();
    });
    applyPeer();

    // 启动后宽限片刻再推首轮：等内嵌 aw-server 拉起、攒一点窗口事件
    if (m_cfg.enabled)
        QTimer::singleShot(kInitialDelayMs, this, [this] {
            if (m_cfg.enabled)
                syncNow(false);
        });
}

void VigorHubRelay::setConfig(const VigorHubRelayConfig &cfg)
{
    m_cfg = cfg;
    saveVigorHubRelay(cfg);
    applyTimer();
    applyPeer();
    emit logLine(QStringLiteral("[vigorhub] 配置已保存（%1 · 间隔 %2 秒 · 地址 %3）")
                     .arg(cfg.enabled ? QStringLiteral("开") : QStringLiteral("关"))
                     .arg(cfg.intervalSec)
                     .arg(normalizeAddress(cfg.address)));
}

void VigorHubRelay::applyTimer()
{
    if (m_cfg.enabled && m_cfg.intervalSec > 0)
        m_timer->start(m_cfg.intervalSec * 1000);
    else
        m_timer->stop();
}

// 伴随服务只跟总开关走，不跟推送间隔走：间隔设成「仅手动」时桌面端照样能来拉，
// 否则用户一关自动推送就把「被发现」这条路也一起断了。
void VigorHubRelay::applyPeer()
{
    if (!m_peer)
        return;
    m_peer->setAlias(effectiveAlias());
    if (m_cfg.enabled)
        m_peer->start();
    else
        m_peer->stop();
    emit peerStateChanged();
}

quint16 VigorHubRelay::peerPort() const
{
    return m_peer ? m_peer->port() : 0;
}

QString VigorHubRelay::effectiveAlias() const
{
    if (!m_cfg.alias.trimmed().isEmpty())
        return m_cfg.alias.trimmed();
    return QStringLiteral("ActivityWatch（%1）").arg(QHostInfo::localHostName());
}

// 存量数据的来源标识一直是 "desktop"，默认值必须保持它，否则旧行和新行会按
// 不同的 (day, source, app) 键并存，同一天的时长被算两遍。
QString VigorHubRelay::effectiveSource() const
{
    const QString s = m_cfg.sourceLabel.trimmed();
    return s.isEmpty() ? QStringLiteral("desktop") : s;
}

void VigorHubRelay::onAutoTimer()
{
    syncNow(false);
}

void VigorHubRelay::syncNow(bool manual)
{
    if (m_busy)
        return;
    Q_UNUSED(manual);
    startRun();
}

// ---- 拉取流水线（全异步，状态挂成员上逐段接续）----

void VigorHubRelay::startRun()
{
    m_busy = true;
    m_queue.clear();
    m_runTotals.clear();
    m_touchedDays.clear();
    m_afk.clear();
    m_afkMerged = false;
    m_newCursors = m_cursors; // 从已持久化游标出发增量消费
    emit syncStarted();

    QNetworkReply *reply = m_api->getBuckets();
    connect(reply, &QNetworkReply::finished, this, &VigorHubRelay::onBucketsReply);
}

void VigorHubRelay::onBucketsReply()
{
    auto *reply = qobject_cast<QNetworkReply *>(sender());
    reply->deleteLater();
    if (!m_busy)
        return;

    if (reply->error() != QNetworkReply::NoError) {
        finish(false, QStringLiteral("本机 aw-server 未响应：%1").arg(reply->errorString()));
        return;
    }
    const QJsonDocument doc = QJsonDocument::fromJson(reply->readAll());
    if (!doc.isObject()) {
        finish(false, QStringLiteral("本机 aw-server 返回了非预期内容"));
        return;
    }
    const QList<BucketInfo> buckets = parseBuckets(doc.object());
    // AFK 桶先拉（窗口事件裁剪要用），窗口桶后拉
    for (const BucketInfo &b : buckets)
        if (b.type == QStringLiteral("afkstatus"))
            m_queue.append({b.id, true});
    for (const BucketInfo &b : buckets)
        if (b.type == QStringLiteral("currentwindow"))
            m_queue.append({b.id, false});

    if (m_queue.isEmpty()) {
        finish(true, QStringLiteral("没有可读的 ActivityWatch 桶"));
        return;
    }
    fetchNextBucket();
}

void VigorHubRelay::fetchNextBucket()
{
    if (m_queue.isEmpty()) {
        pushSnapshot();
        return;
    }
    // 进入窗口桶前把 AFK 区间排序合并好（后续逐事件线性扫描）
    const BucketTask task = m_queue.takeFirst();
    if (!task.afk && !m_afkMerged) {
        mergeIntervals(m_afk);
        m_afkMerged = true;
    }
    m_currentTask = task;
    QNetworkReply *reply = m_api->getRecentEvents(task.id, kEventsLimit);
    connect(reply, &QNetworkReply::finished, this, &VigorHubRelay::onEventsReply);
}

void VigorHubRelay::onEventsReply()
{
    auto *reply = qobject_cast<QNetworkReply *>(sender());
    reply->deleteLater();
    if (!m_busy)
        return;

    const BucketTask task = m_currentTask;
    if (reply->error() != QNetworkReply::NoError) {
        // 单桶失败只跳过该桶，不影响其他桶（与桌面端 pull_endpoint 同策略）
        emit logLine(QStringLiteral("[vigorhub] 桶 %1 读取失败，跳过：%2")
                         .arg(task.id, reply->errorString()));
        fetchNextBucket();
        return;
    }
    const QJsonDocument doc = QJsonDocument::fromJson(reply->readAll());
    const QJsonArray events = doc.array();

    const qint64 nowMs = QDateTime::currentMSecsSinceEpoch();
    const qint64 lookbackMs = qint64(kFirstPullLookbackDays) * 86400000;
    const qint64 cursor = m_newCursors.value(task.id, 0);
    qint64 maxId = cursor;

    if (task.afk) {
        for (const QJsonValue &v : events) {
            const QJsonObject ev = v.toObject();
            const QJsonObject data = ev.value(QStringLiteral("data")).toObject();
            if (data.value(QStringLiteral("status")).toString() != QStringLiteral("afk"))
                continue;
            const qint64 durMs = qint64(ev.value(QStringLiteral("duration")).toDouble() * 1000.0);
            if (durMs <= 0)
                continue;
            const qint64 start = parseIsoMs(ev.value(QStringLiteral("timestamp")).toString());
            if (start > 0)
                m_afk.append({start, start + durMs});
        }
    } else {
        for (const QJsonValue &v : events) {
            const QJsonObject ev = v.toObject();
            const QJsonObject data = ev.value(QStringLiteral("data")).toObject();
            const qint64 id = static_cast<qint64>(ev.value(QStringLiteral("id")).toDouble());
            if (id <= cursor) // 游标增量：老事件直接跳过
                continue;
            maxId = qMax(maxId, id);
            const qint64 start = parseIsoMs(ev.value(QStringLiteral("timestamp")).toString());
            if (start <= 0)
                continue;
            // 首次接入只回溯最近 30 天，历史太老忽略（游标仍推进到最后）
            if (cursor == 0 && nowMs - start > lookbackMs)
                continue;
            const qint64 durMs = qint64(ev.value(QStringLiteral("duration")).toDouble() * 1000.0);
            const qint64 activeMs =
                durMs > 0 ? qMax<qint64>(0, durMs - afkOverlap(start, start + durMs, m_afk)) : 0;
            if (activeMs <= 0)
                continue;
            // 跨天事件按开始时间所在本地日记
            const qint64 day = localEpochDay(start);
            const QString app = data.value(QStringLiteral("app")).toString();
            m_runTotals[day][app.isEmpty() ? QStringLiteral("未知应用") : app] += activeMs;
            m_touchedDays.insert(day);
        }
    }
    if (maxId > m_newCursors.value(task.id, 0))
        m_newCursors.insert(task.id, maxId);
    fetchNextBucket();
}

// ---- 打包推送 ----

void VigorHubRelay::pushSnapshot()
{
    if (m_touchedDays.isEmpty()) {
        // 没有新数据就不推也不动游标：下一轮重新扫一遍同样得出空增量，无副作用
        finish(true, QStringLiteral("暂无新数据"));
        return;
    }

    // 全量口径：本轮增量并入已持久化的累计表，被触碰的天发**整天**的累计值
    QHash<qint64, QHash<QString, qint64>> merged = m_totals;
    for (auto dayIt = m_runTotals.constBegin(); dayIt != m_runTotals.constEnd(); ++dayIt) {
        QHash<QString, qint64> &dayTotals = merged[dayIt.key()];
        for (auto appIt = dayIt.value().constBegin(); appIt != dayIt.value().constEnd(); ++appIt)
            dayTotals[appIt.key()] += appIt.value();
    }

    QList<qint64> days = m_touchedDays.values();
    std::sort(days.begin(), days.end());
    const QJsonArray rows = usageRows(merged, days);
    const QJsonArray cursors = cursorsArray(m_newCursors);
    qint64 totalActiveSec = 0;  // 日志用：协议字段 active_sec 的合计（秒）
    for (const QJsonValue &v : rows)
        totalActiveSec += v.toObject().value(QStringLiteral("active_sec")).toVariant().toLongLong();

    m_payload = wrapAw(rows, cursors);
    m_tryBases = pushBaseCandidates(normalizeAddress(m_cfg.address));
    m_tryIndex = 0;
    emit logLine(QStringLiteral("[vigorhub] 本轮 %1 天 / %2 行 / 共 %3 秒")
                     .arg(m_touchedDays.size()).arg(rows.size()).arg(totalActiveSec));
    postToCurrent();
}

// 累计表 → 协议行。内部累计一律毫秒，协议 active_sec 与桌面端 aw.rs::ms/1000
// 对齐是**秒**，只在这里换算；m_totals / QSettings 里的值不要跟着改单位。
QJsonArray VigorHubRelay::usageRows(const QHash<qint64, QHash<QString, qint64>> &totals,
                                    const QList<qint64> &days) const
{
    const QString source = effectiveSource();
    QJsonArray rows;
    for (const qint64 day : days) {
        const QHash<QString, qint64> &dayTotals = totals.value(day);
        for (auto appIt = dayTotals.constBegin(); appIt != dayTotals.constEnd(); ++appIt) {
            QJsonObject row;
            row.insert(QStringLiteral("day"), static_cast<qint64>(day));
            row.insert(QStringLiteral("source"), source);
            row.insert(QStringLiteral("app"), appIt.key());
            row.insert(QStringLiteral("active_sec"), static_cast<qint64>(appIt.value() / 1000));
            rows.append(row);
        }
    }
    return rows;
}

QJsonArray VigorHubRelay::cursorsArray(const QHash<QString, qint64> &cursors)
{
    QJsonArray out;
    for (auto it = cursors.constBegin(); it != cursors.constEnd(); ++it) {
        QJsonObject c;
        c.insert(QStringLiteral("bucket"), it.key());
        c.insert(QStringLiteral("last_id"), static_cast<qint64>(it.value()));
        out.append(c);
    }
    return out;
}

QByteArray VigorHubRelay::wrapAw(const QJsonArray &rows, const QJsonArray &cursors)
{
    QJsonObject aw;
    aw.insert(QStringLiteral("app_usage"), rows);
    aw.insert(QStringLiteral("cursors"), cursors);
    QJsonObject body;
    body.insert(QStringLiteral("aw"), aw);
    return QJsonDocument(body).toJson(QJsonDocument::Compact);
}

// 被拉取时报**全部**已持久化的天，而不是本轮触碰的天：桌面端可能在任何时刻来拉，
// 只报增量会让它那边缺掉之前推过的天。全量替换合并（model.rs::merge_usage_full）
// 保证重复拉取幂等。游标同样报已提交的那份，不报本轮还没推送成功的 m_newCursors。
QByteArray VigorHubRelay::fullSnapshot() const
{
    QList<qint64> days = m_totals.keys();
    std::sort(days.begin(), days.end());
    return wrapAw(usageRows(m_totals, days), cursorsArray(m_cursors));
}

void VigorHubRelay::postToCurrent()
{
    const QString base = m_tryBases.value(m_tryIndex);
    const QUrl url(base + QStringLiteral("/api/health-motion/v1/push"));
    QNetworkRequest req(url);
    req.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));
    QNetworkReply *reply = m_pushNam->post(req, m_payload);
    if (m_tryIndex > 0) {
        // 回退请求快速失败：端口被别的程序占着时不要卡满 15s 才试下一个
        QTimer::singleShot(kFallbackTimeoutMs, reply, [reply] {
            if (!reply->isFinished())
                reply->abort();
        });
    }
    connect(reply, &QNetworkReply::finished, this, &VigorHubRelay::onPushReply);
}

void VigorHubRelay::onPushReply()
{
    auto *reply = qobject_cast<QNetworkReply *>(sender());
    const bool netOk = reply->error() == QNetworkReply::NoError;
    const QString reason = netOk ? QStringLiteral("应答非 ok") : reply->errorString();
    const QByteArray body = netOk ? reply->readAll() : QByteArray();
    reply->deleteLater();
    if (!m_busy)
        return;

    // 失败不动游标/累计表：下一轮会用同样的增量重推（全量语义保证不会算重）
    const bool ok = netOk
                    && QJsonDocument::fromJson(body).object()
                           .value(QStringLiteral("ok")).toBool();
    if (!ok) {
        if (m_tryIndex + 1 < m_tryBases.size()) {
            const QString from = m_tryBases.at(m_tryIndex);
            ++m_tryIndex;
            emit logLine(QStringLiteral("[vigorhub] %1 不通（%2），改试 %3")
                             .arg(from, reason, m_tryBases.at(m_tryIndex)));
            postToCurrent();
            return;
        }
        finish(false, QStringLiteral("推送失败（%1）：%2")
                          .arg(m_tryBases.value(m_tryIndex), reason));
        return;
    }

    // 顺延端口命中：记住实际地址，后续轮次不必再从 18765 逐个试
    if (m_tryIndex > 0) {
        m_cfg.address = m_tryBases.at(m_tryIndex);
        saveVigorHubRelay(m_cfg);
        emit logLine(QStringLiteral("[vigorhub] 桌面端端口已顺延，自动记住 %1")
                         .arg(m_cfg.address));
    }

    // 成功才提交本轮结果：游标推进 + 累计表并入并落盘（重启不重算）
    for (auto dayIt = m_runTotals.constBegin(); dayIt != m_runTotals.constEnd(); ++dayIt) {
        QHash<QString, qint64> &dayTotals = m_totals[dayIt.key()];
        for (auto appIt = dayIt.value().constBegin(); appIt != dayIt.value().constEnd(); ++appIt)
            dayTotals[appIt.key()] += appIt.value();
    }
    m_cursors = m_newCursors;
    saveState();
    finish(true, QStringLiteral("已推送 %1 天的聚合数据").arg(m_touchedDays.size()));
}

void VigorHubRelay::finish(bool ok, const QString &msg)
{
    m_busy = false;
    m_lastOk = ok;
    m_lastResult = QStringLiteral("%1 · %2")
                       .arg(QDateTime::currentDateTime().toString(QStringLiteral("MM-dd hh:mm")), msg);
    emit syncFinished(ok, msg);
    emit logLine(QStringLiteral("[vigorhub] %1%2").arg(ok ? QString() : QStringLiteral("✗ "), msg));
}

// ---- 聚合辅助 ----

qint64 VigorHubRelay::parseIsoMs(const QString &iso)
{
    QString t = iso.trimmed();
    if (t.size() < 19)
        return 0;
    // 缺时区标记按 UTC（与 vigorhub aw.rs 口径一致）
    static const QRegularExpression tzTail(QStringLiteral("(?:[Zz]|[+-]\\d{2}:?\\d{2})$"));
    if (!tzTail.match(t).hasMatch())
        t += QLatin1Char('Z');
    const QDateTime dt = QDateTime::fromString(t, Qt::ISODateWithMs);
    if (!dt.isValid())
        return 0;
    return dt.toMSecsSinceEpoch();
}

qint64 VigorHubRelay::localEpochDay(qint64 utcMs)
{
    const QDate d = QDateTime::fromMSecsSinceEpoch(utcMs).date(); // 本地时区
    return qint64(QDate(1970, 1, 1).daysTo(d));
}

void VigorHubRelay::mergeIntervals(QList<Interval> &iv)
{
    std::sort(iv.begin(), iv.end(), [](const Interval &a, const Interval &b) {
        return a.start < b.start;
    });
    QList<Interval> out;
    out.reserve(iv.size());
    for (const Interval &cur : iv) {
        if (!out.isEmpty() && cur.start <= out.last().end) {
            out.last().end = qMax(out.last().end, cur.end);
        } else {
            out.append(cur);
        }
    }
    iv = out;
}

qint64 VigorHubRelay::afkOverlap(qint64 start, qint64 end, const QList<Interval> &mergedAfk)
{
    qint64 overlap = 0;
    for (const Interval &iv : mergedAfk) {
        if (iv.end <= start)
            continue;
        if (iv.start >= end)
            break; // 已排序，后面的更靠右
        overlap += qMin(end, iv.end) - qMax(start, iv.start);
    }
    return overlap;
}

// ---- 状态持久化 ----

void VigorHubRelay::loadState()
{
    QSettings s(settingsFilePath(), QSettings::IniFormat);
    const QJsonDocument cur =
        QJsonDocument::fromJson(s.value(QStringLiteral("vigorhub/cursors")).toByteArray());
    if (cur.isObject()) {
        const QJsonObject o = cur.object();
        for (auto it = o.constBegin(); it != o.constEnd(); ++it)
            m_cursors.insert(it.key(), it.value().toVariant().toLongLong());
    }
    const QJsonDocument tot =
        QJsonDocument::fromJson(s.value(QStringLiteral("vigorhub/totals")).toByteArray());
    if (tot.isObject()) {
        const QJsonObject o = tot.object();
        for (auto dayIt = o.constBegin(); dayIt != o.constEnd(); ++dayIt) {
            const qint64 day = dayIt.key().toLongLong();
            const QJsonObject apps = dayIt.value().toObject();
            for (auto appIt = apps.constBegin(); appIt != apps.constEnd(); ++appIt)
                m_totals[day][appIt.key()] = appIt.value().toVariant().toLongLong();
        }
    }
}

void VigorHubRelay::saveState()
{
    // 只保留最近 180 天（与桌面端 prune_usage 同口径），避免 ini 无限膨胀
    const qint64 cutoff = localEpochDay(QDateTime::currentMSecsSinceEpoch()) - kPruneDays;
    for (auto it = m_totals.begin(); it != m_totals.end();) {
        if (it.key() < cutoff)
            it = m_totals.erase(it);
        else
            ++it;
    }

    QJsonObject curObj;
    for (auto it = m_cursors.constBegin(); it != m_cursors.constEnd(); ++it)
        curObj.insert(it.key(), static_cast<qint64>(it.value()));
    QJsonObject totObj;
    for (auto dayIt = m_totals.constBegin(); dayIt != m_totals.constEnd(); ++dayIt) {
        QJsonObject apps;
        for (auto appIt = dayIt.value().constBegin(); appIt != dayIt.value().constEnd(); ++appIt)
            apps.insert(appIt.key(), static_cast<qint64>(appIt.value()));
        totObj.insert(QString::number(dayIt.key()), apps);
    }

    QSettings s(settingsFilePath(), QSettings::IniFormat);
    s.setValue(QStringLiteral("vigorhub/cursors"),
               QJsonDocument(curObj).toJson(QJsonDocument::Compact));
    s.setValue(QStringLiteral("vigorhub/totals"),
               QJsonDocument(totObj).toJson(QJsonDocument::Compact));
    s.sync();
}

} // namespace awqtui
