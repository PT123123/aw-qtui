// awdatastore.cpp —— ActivityWatch /api/0 数据转换层实现
#include "awdatastore.h"

#include <QDateTime>
#include <QHash>
#include <QJsonArray>
#include <QJsonObject>
#include <QJsonValue>
#include <QUrl>

namespace awqtui {

// ── 解析 buckets ──────────────────────────────────────────────
QList<BucketInfo> parseBuckets(const QJsonObject &bucketsObj)
{
    QList<BucketInfo> result;
    for (auto it = bucketsObj.begin(); it != bucketsObj.end(); ++it) {
        const QJsonObject b = it.value().toObject();
        BucketInfo info;
        info.id = it.key();
        info.type = b.value(QStringLiteral("type")).toString();
        info.client = b.value(QStringLiteral("client")).toString();
        info.hostname = b.value(QStringLiteral("hostname")).toString();
        result.append(info);
    }
    return result;
}

// ── 解析 events ────────────────────────────────────────────────
static qint64 parseTimestamp(const QString &ts)
{
    // aw-server uses ISO 8601 with possible fractional seconds and Z suffix
    QDateTime dt = QDateTime::fromString(ts, Qt::ISODate);
    if (!dt.isValid()) {
        // fallback: try with milliseconds
        dt = QDateTime::fromString(ts, QStringLiteral("yyyy-MM-ddTHH:mm:ss.zzzZ"));
    }
    if (!dt.isValid())
        return 0;
    return dt.toMSecsSinceEpoch();
}

QList<TimelineEvent> parseEvents(const QJsonArray &events, const BucketInfo &bucket)
{
    QList<TimelineEvent> result;
    for (const QJsonValue &v : events) {
        const QJsonObject e = v.toObject();
        const QString ts = e.value(QStringLiteral("timestamp")).toString();
        const double durationSec = e.value(QStringLiteral("duration")).toDouble();
        const QJsonObject data = e.value(QStringLiteral("data")).toObject();

        TimelineEvent ev;
        ev.startMs = parseTimestamp(ts);
        ev.endMs = ev.startMs + qint64(durationSec * 1000.0);

        if (bucket.type == QStringLiteral("afkstatus")) {
            const QString status = data.value(QStringLiteral("status")).toString();
            ev.label = status;
            ev.detail = status;
            ev.color = (status == QStringLiteral("afk")) ? QColor(140, 140, 140) : QColor(80, 180, 120);
            ev.category = (status == QStringLiteral("afk")) ? QStringLiteral("AFK") : QStringLiteral("Active");
        } else if (bucket.type == QStringLiteral("currentwindow")) {
            ev.label = data.value(QStringLiteral("app")).toString();
            ev.detail = data.value(QStringLiteral("title")).toString();
            ev.color = colorForString(ev.label);
            ev.category = QString(); // TODO: category rules
        } else if (bucket.type == QStringLiteral("web.tab")) {
            const QString url = data.value(QStringLiteral("url")).toString();
            const QUrl parsed(url);
            ev.label = parsed.host().isEmpty() ? url : parsed.host();
            ev.detail = data.value(QStringLiteral("title")).toString();
            if (ev.detail.isEmpty())
                ev.detail = url;
            ev.color = colorForString(ev.label);
            ev.category = QStringLiteral("Web");
        } else {
            // fallback: use first data field as label
            if (!data.isEmpty()) {
                ev.label = data.begin().value().toString();
            } else {
                ev.label = bucket.type;
            }
            ev.detail = QString();
            ev.color = colorForString(ev.label);
        }

        if (ev.label.isEmpty())
            ev.label = QStringLiteral("(unknown)");
        result.append(ev);
    }
    return result;
}

// ── 组装 lanes ─────────────────────────────────────────────────
QList<TimelineLane> buildLanes(const QList<BucketInfo> &buckets,
                                const QHash<QString, QJsonArray> &eventsMap)
{
    QList<TimelineLane> lanes;
    for (const BucketInfo &b : buckets) {
        TimelineLane lane;
        lane.name = b.id;
        if (eventsMap.contains(b.id)) {
            lane.events = parseEvents(eventsMap.value(b.id), b);
        }
        lanes.append(lane);
    }
    return lanes;
}

// ── 统计辅助 ───────────────────────────────────────────────────
static QList<BarItem> topFromLanes(const QList<TimelineLane> &lanes,
                                     const std::function<QString(const TimelineEvent &)> &keyFn,
                                     const std::function<QString(const TimelineEvent &)> &subFn,
                                     int limit)
{
    QHash<QString, qint64> dur;
    QHash<QString, QString> subMap;
    for (const TimelineLane &lane : lanes) {
        // skip afk lane for app/title stats
        if (lane.name.contains(QStringLiteral("afk")))
            continue;
        for (const TimelineEvent &ev : lane.events) {
            const QString key = keyFn(ev);
            if (key.isEmpty())
                continue;
            dur[key] += (ev.endMs - ev.startMs) / 1000;
            if (!subFn(ev).isEmpty())
                subMap[key] = subFn(ev);
        }
    }
    QList<BarItem> items;
    for (auto it = dur.begin(); it != dur.end(); ++it) {
        BarItem item;
        item.label = it.key();
        item.valueSeconds = it.value();
        item.color = colorForString(it.key());
        item.subLabel = subMap.value(it.key());
        items.append(item);
    }
    std::sort(items.begin(), items.end(),
              [](const BarItem &a, const BarItem &b) { return a.valueSeconds > b.valueSeconds; });
    if (items.size() > limit)
        items = items.mid(0, limit);
    return items;
}

QList<BarItem> topAppsFromLanes(const QList<TimelineLane> &lanes, int limit)
{
    return topFromLanes(lanes,
                        [](const TimelineEvent &ev) { return ev.label; },
                        [](const TimelineEvent &) { return QString(); },
                        limit);
}

QList<BarItem> topTitlesFromLanes(const QList<TimelineLane> &lanes, int limit)
{
    return topFromLanes(lanes,
                        [](const TimelineEvent &ev) { return ev.detail.isEmpty() ? ev.label : ev.detail; },
                        [](const TimelineEvent &ev) { return ev.label; },
                        limit);
}

QList<BarItem> topCategoriesFromLanes(const QList<TimelineLane> &lanes, int limit)
{
    QHash<QString, qint64> dur;
    for (const TimelineLane &lane : lanes) {
        for (const TimelineEvent &ev : lane.events) {
            const QString cat = ev.category.isEmpty() ? QStringLiteral("Uncategorized") : ev.category;
            dur[cat] += (ev.endMs - ev.startMs) / 1000;
        }
    }
    QList<BarItem> items;
    for (auto it = dur.begin(); it != dur.end(); ++it) {
        BarItem item;
        item.label = it.key();
        item.valueSeconds = it.value();
        item.color = colorForCategory(it.key());
        items.append(item);
    }
    std::sort(items.begin(), items.end(),
              [](const BarItem &a, const BarItem &b) { return a.valueSeconds > b.valueSeconds; });
    if (items.size() > limit)
        items = items.mid(0, limit);
    return items;
}

QList<qint64> hourlyFromLanes(const QList<TimelineLane> &lanes)
{
    QList<qint64> result(24, 0);
    for (const TimelineLane &lane : lanes) {
        if (lane.name.contains(QStringLiteral("afk")))
            continue;
        for (const TimelineEvent &ev : lane.events) {
            qint64 cur = ev.startMs;
            while (cur < ev.endMs) {
                const QDateTime dt = QDateTime::fromMSecsSinceEpoch(cur, Qt::LocalTime);
                const int h = dt.time().hour();
                const qint64 hourEnd = QDateTime(dt.date(), QTime(h, 59, 59, 999), Qt::LocalTime).toMSecsSinceEpoch();
                const qint64 segEnd = qMin(ev.endMs, hourEnd + 1);
                result[h] += (segEnd - cur) / 1000;
                cur = segEnd;
            }
        }
    }
    return result;
}

QList<BarItem> categoryTreeFromLanes(const QList<TimelineLane> &lanes)
{
    // flat category -> duration (same as topCategories but no limit)
    return topCategoriesFromLanes(lanes, 999);
}

QList<BarItem> topDomainsFromLanes(const QList<TimelineLane> &lanes, int limit)
{
    QHash<QString, qint64> dur;
    for (const TimelineLane &lane : lanes) {
        if (!lane.name.contains(QStringLiteral("web")))
            continue;
        for (const TimelineEvent &ev : lane.events) {
            dur[ev.label] += (ev.endMs - ev.startMs) / 1000;
        }
    }
    QList<BarItem> items;
    for (auto it = dur.begin(); it != dur.end(); ++it) {
        BarItem item;
        item.label = it.key();
        item.valueSeconds = it.value();
        item.color = colorForString(it.key());
        items.append(item);
    }
    std::sort(items.begin(), items.end(),
              [](const BarItem &a, const BarItem &b) { return a.valueSeconds > b.valueSeconds; });
    if (items.size() > limit)
        items = items.mid(0, limit);
    return items;
}

QList<BarItem> topUrlsFromLanes(const QList<TimelineLane> &lanes, int limit)
{
    QHash<QString, qint64> dur;
    for (const TimelineLane &lane : lanes) {
        if (!lane.name.contains(QStringLiteral("web")))
            continue;
        for (const TimelineEvent &ev : lane.events) {
            const QString url = ev.detail.isEmpty() ? ev.label : ev.detail;
            dur[url] += (ev.endMs - ev.startMs) / 1000;
        }
    }
    QList<BarItem> items;
    for (auto it = dur.begin(); it != dur.end(); ++it) {
        BarItem item;
        item.label = it.key();
        item.valueSeconds = it.value();
        item.color = colorForString(it.key());
        item.subLabel = QString();
        items.append(item);
    }
    std::sort(items.begin(), items.end(),
              [](const BarItem &a, const BarItem &b) { return a.valueSeconds > b.valueSeconds; });
    if (items.size() > limit)
        items = items.mid(0, limit);
    return items;
}

} // namespace awqtui
