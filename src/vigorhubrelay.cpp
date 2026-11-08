// vigorhubrelay.cpp —— VigorHub 推送引擎实现（聚合口径对齐 vigorhub/src/aw.rs）
#include "vigorhubrelay.h"

#include <QDateTime>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QRegularExpression>
#include <QSettings>
#include <QTimer>
#include <QUrl>
#include <algorithm>
#include <limits>

#include "apiclient.h"
#include "awdatastore.h"
#include "config.h"

namespace awqtui {

namespace {
// 与 vigorhub aw.rs 对齐的常量
constexpr int kEventsLimit = 5000;          // 每桶单次拉取的事件上限
constexpr int kFirstPullLookbackDays = 30;  // 首次接入最多回溯多少天
constexpr int kPruneDays = 180;             // 累计表只保留最近 180 天（对齐桌面端裁剪）
constexpr int kInitialDelayMs = 15000;      // 启动后首轮推送的宽限（等 aw-server 就绪）
constexpr int kPushTimeoutMs = 15000;

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

    QJsonArray rows;
    qint64 totalSec = 0;
    QList<qint64> days = m_touchedDays.values();
    std::sort(days.begin(), days.end());
    for (const qint64 day : days) {
        const QHash<QString, qint64> &dayTotals = merged.value(day);
        for (auto appIt = dayTotals.constBegin(); appIt != dayTotals.constEnd(); ++appIt) {
            QJsonObject row;
            row.insert(QStringLiteral("day"), static_cast<qint64>(day));
            row.insert(QStringLiteral("source"), QStringLiteral("desktop"));
            row.insert(QStringLiteral("app"), appIt.key());
            row.insert(QStringLiteral("active_sec"), static_cast<qint64>(appIt.value()));
            rows.append(row);
            totalSec += appIt.value();
        }
    }
    QJsonArray cursors;
    for (auto it = m_newCursors.constBegin(); it != m_newCursors.constEnd(); ++it) {
        QJsonObject c;
        c.insert(QStringLiteral("bucket"), it.key());
        c.insert(QStringLiteral("last_id"), static_cast<qint64>(it.value()));
        cursors.append(c);
    }
    QJsonObject aw;
    aw.insert(QStringLiteral("app_usage"), rows);
    aw.insert(QStringLiteral("cursors"), cursors);
    QJsonObject body;
    body.insert(QStringLiteral("aw"), aw);

    QUrl url(normalizeAddress(m_cfg.address) + QStringLiteral("/api/health-motion/v1/push"));
    QNetworkRequest req(url);
    req.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));
    const QByteArray payload = QJsonDocument(body).toJson(QJsonDocument::Compact);
    QNetworkReply *reply = m_pushNam->post(req, payload);
    connect(reply, &QNetworkReply::finished, this, &VigorHubRelay::onPushReply);
}

void VigorHubRelay::onPushReply()
{
    auto *reply = qobject_cast<QNetworkReply *>(sender());
    reply->deleteLater();
    if (!m_busy)
        return;

    if (reply->error() != QNetworkReply::NoError) {
        // 失败不动游标/累计表：下一轮会用同样的增量重推（全量语义保证不会算重）
        finish(false, QStringLiteral("推送失败：%1").arg(reply->errorString()));
        return;
    }
    const QJsonDocument doc = QJsonDocument::fromJson(reply->readAll());
    if (!doc.object().value(QStringLiteral("ok")).toBool()) {
        finish(false, QStringLiteral("VigorHub 返回了非预期应答"));
        return;
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
