// mockdata.cpp
#include "mockdata.h"

#include <QDateTime>
#include <QHash>
#include <QTime>
#include <functional>

namespace awqtui {

// ── 颜色 ────────────────────────────────────────────────────
QColor colorForString(const QString &s)
{
    const uint h = qHash(s);
    const int hue = h % 360;
    return QColor::fromHsl(hue, 135, 168);
}

QColor colorForCategory(const QString &category)
{
    const QString c = category.toLower();
    if (c.startsWith(QStringLiteral("work"))) {
        if (c.contains(QStringLiteral("cod")) || c.contains(QStringLiteral("dev")))
            return QColor(QStringLiteral("#4c8bf5"));
        if (c.contains(QStringLiteral("email")))
            return QColor(QStringLiteral("#6aa6f7"));
        if (c.contains(QStringLiteral("research")))
            return QColor(QStringLiteral("#3a7bd5"));
        return QColor(QStringLiteral("#5b9bf6"));
    }
    if (c.startsWith(QStringLiteral("play"))) {
        if (c.contains(QStringLiteral("game")))
            return QColor(QStringLiteral("#3fb950"));
        if (c.contains(QStringLiteral("video")))
            return QColor(QStringLiteral("#e5534b"));
        if (c.contains(QStringLiteral("social")))
            return QColor(QStringLiteral("#d29922"));
        return QColor(QStringLiteral("#4ac76a"));
    }
    if (c.startsWith(QStringLiteral("neutral")))
        return QColor(QStringLiteral("#8b95a3"));
    return QColor(QStringLiteral("#5a6270")); // Uncategorized
}

// ── 确定性伪随机（xorshift32） ─────────────────────────────
static quint32 g_rng;
static quint32 rng()
{
    g_rng ^= g_rng << 13;
    g_rng ^= g_rng >> 17;
    g_rng ^= g_rng << 5;
    return g_rng;
}
static double rng01() { return rng() / double(0xFFFFFFFFu); }
static int rngInt(int lo, int hi) { return lo + static_cast<int>(rng01() * (hi - lo + 1)); }
static QString pick(const QStringList &pool) { return pool.at(rngInt(0, pool.size() - 1)); }

static qint64 dayStart(const QDate &d)
{
    return QDateTime(d, QTime(0, 0), Qt::LocalTime).toMSecsSinceEpoch();
}
static qint64 at(qint64 base, int h, int m = 0, int s = 0)
{
    return base + (h * 3600LL + m * 60LL + s) * 1000;
}

// ── 一天的时段定义 ──────────────────────────────────────────
struct Segment {
    int startH, startM, endH, endM;
    bool afk;
    QString category;
    QStringList apps;    // window 行应用池
    QStringList urls;    // web 行标签池（空表示该时段不用浏览器）
    int switchMin;       // 平均切换间隔（分钟）
};

static QList<Segment> daySegments()
{
    return {
        {0, 0, 7, 30, true,  QStringLiteral("Uncategorized"), {}, {}, 0},
        {7, 30, 8, 30, false, QStringLiteral("Neutral > Misc"),
            {QStringLiteral("explorer.exe"), QStringLiteral("WeChat.exe"), QStringLiteral("chrome.exe")},
            {QStringLiteral("zhihu.com"), QStringLiteral("weibo.com"), QStringLiteral("bilibili.com")}, 8},
        {8, 30, 12, 0, false, QStringLiteral("Work > Coding"),
            {QStringLiteral("Code.exe"), QStringLiteral("WindowsTerminal.exe"), QStringLiteral("chrome.exe"), QStringLiteral("idea64.exe")},
            {QStringLiteral("github.com"), QStringLiteral("stackoverflow.com"), QStringLiteral("docs.rs"), QStringLiteral("localhost:5600")}, 12},
        {12, 0, 13, 30, false, QStringLiteral("Play > Video"),
            {QStringLiteral("chrome.exe"), QStringLiteral("WeChat.exe")},
            {QStringLiteral("bilibili.com"), QStringLiteral("youtube.com"), QStringLiteral("zhihu.com")}, 15},
        {13, 30, 18, 0, false, QStringLiteral("Work > Coding"),
            {QStringLiteral("Code.exe"), QStringLiteral("WindowsTerminal.exe"), QStringLiteral("chrome.exe"), QStringLiteral("idea64.exe"), QStringLiteral("notepad++.exe")},
            {QStringLiteral("github.com"), QStringLiteral("stackoverflow.com"), QStringLiteral("developer.mozilla.org"), QStringLiteral("activitywatch.net")}, 10},
        {18, 0, 19, 30, true,  QStringLiteral("Uncategorized"), {}, {}, 0},
        {19, 30, 22, 0, false, QStringLiteral("Play > Game"),
            {QStringLiteral("ApexLegends.exe"), QStringLiteral("steam.exe"), QStringLiteral("chrome.exe"), QStringLiteral("WeChat.exe")},
            {QStringLiteral("bilibili.com"), QStringLiteral("youtube.com"), QStringLiteral("twitter.com")}, 18},
        {22, 0, 23, 30, false, QStringLiteral("Play > Social"),
            {QStringLiteral("chrome.exe"), QStringLiteral("WeChat.exe"), QStringLiteral("spotify.exe")},
            {QStringLiteral("zhihu.com"), QStringLiteral("weibo.com"), QStringLiteral("twitter.com"), QStringLiteral("news.ycombinator.com")}, 10},
        {23, 30, 24, 0, false, QStringLiteral("Neutral > Misc"),
            {QStringLiteral("explorer.exe"), QStringLiteral("notepad++.exe")},
            {}, 12},
    };
}

// ── 时间线生成 ──────────────────────────────────────────────
QList<TimelineLane> generateTimelineLanes(const QDate &date)
{
    g_rng = static_cast<quint32>(date.year() * 10000 + date.month() * 100 + date.day());
    const qint64 base = dayStart(date);

    TimelineLane afkLane;
    afkLane.name = QStringLiteral("afk-status");
    TimelineLane winLane;
    winLane.name = QStringLiteral("aw-watcher-window_DESKTOP-QTUI");
    TimelineLane webLane;
    webLane.name = QStringLiteral("aw-watcher-web-chrome");

    const QColor afkColor(QStringLiteral("#a34a4a"));
    const QColor notAfkColor(QStringLiteral("#3fb950"));
    const QColor idleColor(QStringLiteral("#3a3f47"));

    for (const Segment &seg : daySegments()) {
        const qint64 s = at(base, seg.startH, seg.startM);
        const qint64 e = at(base, seg.endH, seg.endM);

        // afk-status 行
        TimelineEvent ae;
        ae.startMs = s;
        ae.endMs = e;
        ae.color = seg.afk ? afkColor : notAfkColor;
        ae.label = seg.afk ? QStringLiteral("afk") : QStringLiteral("not-afk");
        ae.category = QStringLiteral("afk-status");
        afkLane.events.append(ae);

        if (seg.afk) {
            // afk 时段 window 行显示一段 idle
            TimelineEvent ie;
            ie.startMs = s; ie.endMs = e;
            ie.color = idleColor; ie.label = QStringLiteral("idle");
            ie.category = QStringLiteral("Uncategorized");
            winLane.events.append(ie);
            continue;
        }

        // window 行：按 switchMin 切换应用
        qint64 cur = s;
        while (cur < e) {
            const int spanSec = rngInt(seg.switchMin * 40, seg.switchMin * 90);
            qint64 nxt = cur + spanSec * 1000LL;
            if (nxt > e) nxt = e;
            const QString app = pick(seg.apps);
            TimelineEvent ev;
            ev.startMs = cur;
            ev.endMs = nxt;
            ev.label = app;
            ev.color = colorForString(app);
            ev.category = seg.category;
            ev.detail = app;
            winLane.events.append(ev);
            cur = nxt;
        }

        // web 行：仅当有 url 池且时段包含浏览器时生成
        if (!seg.urls.isEmpty() && seg.apps.contains(QStringLiteral("chrome.exe"))) {
            qint64 wcur = s;
            while (wcur < e) {
                const int spanSec = rngInt(seg.switchMin * 50, seg.switchMin * 120);
                qint64 nxt = wcur + spanSec * 1000LL;
                if (nxt > e) nxt = e;
                const QString url = pick(seg.urls);
                TimelineEvent ev;
                ev.startMs = wcur;
                ev.endMs = nxt;
                ev.label = url;
                ev.color = colorForString(url);
                ev.category = seg.category;
                ev.detail = QStringLiteral("https://%1").arg(url);
                webLane.events.append(ev);
                wcur = nxt;
            }
        }
    }

    return {afkLane, winLane, webLane};
}

// ── 统计辅助：把事件按 label 聚合时长 ───────────────────────
static QList<BarItem> aggregate(const QList<TimelineEvent> &events,
                                  const std::function<QString(const TimelineEvent &)> &keyFn,
                                  const std::function<QColor(const QString &, const TimelineEvent &)> &colorFn,
                                  int limit)
{
    QHash<QString, qint64> dur;
    QHash<QString, QColor> col;
    for (const auto &ev : events) {
        const QString k = keyFn(ev);
        if (k.isEmpty()) continue;
        dur[k] += (ev.endMs - ev.startMs) / 1000;
        if (!col.contains(k))
            col[k] = colorFn(k, ev);
    }
    QList<BarItem> items;
    for (auto it = dur.constBegin(); it != dur.constEnd(); ++it) {
        BarItem b;
        b.label = it.key();
        b.valueSeconds = it.value();
        b.color = col.value(it.key());
        items.append(b);
    }
    std::sort(items.begin(), items.end(), [](const BarItem &a, const BarItem &b) {
        return a.valueSeconds > b.valueSeconds;
    });
    if (items.size() > limit) items = items.mid(0, limit);
    return items;
}

QList<BarItem> generateTopApps(const QDate &date, int limit)
{
    const auto lanes = generateTimelineLanes(date);
    for (const auto &lane : lanes) {
        if (lane.name.startsWith(QStringLiteral("aw-watcher-window"))) {
            return aggregate(lane.events,
                [](const TimelineEvent &e) { return e.label; },
                [](const QString &k, const TimelineEvent &) { return colorForString(k); },
                limit);
        }
    }
    return {};
}

QList<BarItem> generateTopTitles(const QDate &date, int limit)
{
    // 用 window 事件的 label + 随机窗口标题后缀模拟
    const auto lanes = generateTimelineLanes(date);
    for (const auto &lane : lanes) {
        if (lane.name.startsWith(QStringLiteral("aw-watcher-window"))) {
            static const QStringList titleSuffix = {
                QStringLiteral(" — main.cpp"), QStringLiteral(" — CMakeLists.txt"),
                QStringLiteral(" — timelinewidget.cpp"), QStringLiteral(" — README.md"),
                QStringLiteral(" — Settings"), QStringLiteral(" — mainwindow.h"),
                QStringLiteral(" — mockdata.h"), QStringLiteral(" — activitypage.cpp"),
            };
            QHash<QString, qint64> dur;
            for (const auto &ev : lane.events) {
                if (ev.label == QStringLiteral("idle")) continue;
                const QString title = ev.label + pick(titleSuffix);
                dur[title] += (ev.endMs - ev.startMs) / 1000;
            }
            QList<BarItem> items;
            for (auto it = dur.constBegin(); it != dur.constEnd(); ++it) {
                BarItem b;
                b.label = it.key();
                b.valueSeconds = it.value();
                b.color = colorForString(it.key());
                items.append(b);
            }
            std::sort(items.begin(), items.end(), [](const BarItem &a, const BarItem &b) {
                return a.valueSeconds > b.valueSeconds;
            });
            if (items.size() > limit) items = items.mid(0, limit);
            return items;
        }
    }
    return {};
}

QList<BarItem> generateTopCategories(const QDate &date, int limit)
{
    const auto lanes = generateTimelineLanes(date);
    QList<TimelineEvent> all;
    for (const auto &lane : lanes) {
        if (lane.name.startsWith(QStringLiteral("aw-watcher-window")))
            all.append(lane.events);
    }
    return aggregate(all,
        [](const TimelineEvent &e) { return e.category; },
        [](const QString &k, const TimelineEvent &) { return colorForCategory(k); },
        limit);
}

QList<BarItem> generateCategoryTree(const QDate &date)
{
    // 扁平展示：分类 + 子项
    QList<BarItem> cats = generateTopCategories(date, 10);
    QList<BarItem> tree;
    for (const auto &c : cats) {
        tree.append(c);
        // 每个分类下挂 1-2 个应用
        const auto apps = generateTopApps(date, 12);
        int added = 0;
        for (const auto &a : apps) {
            if (added >= 2) break;
            BarItem sub = a;
            sub.subLabel = QStringLiteral("  └ ") + a.label;
            sub.valueSeconds = qMin(a.valueSeconds, c.valueSeconds / 2);
            tree.append(sub);
            ++added;
        }
    }
    return tree;
}

QList<qint64> generateHourlyActivity(const QDate &date)
{
    g_rng = static_cast<quint32>(date.year() * 10000 + date.month() * 100 + date.day() + 777);
    QList<qint64> hours(24, 0);
    const auto lanes = generateTimelineLanes(date);
    for (const auto &lane : lanes) {
        if (lane.name != QStringLiteral("afk-status")) continue;
        for (const auto &ev : lane.events) {
            if (ev.label != QStringLiteral("not-afk")) continue;
            // 把事件时长分配到各小时
            qint64 cur = ev.startMs;
            while (cur < ev.endMs) {
                const QDateTime dt = QDateTime::fromMSecsSinceEpoch(cur, Qt::LocalTime);
                const int h = dt.time().hour();
                const qint64 hourEnd = QDateTime(dt.date(), QTime(h, 59, 59, 999), Qt::LocalTime).toMSecsSinceEpoch();
                const qint64 segEnd = qMin(ev.endMs, hourEnd + 1);
                hours[h] += (segEnd - cur) / 1000;
                cur = segEnd;
            }
        }
    }
    // 加一点随机抖动让柱状图更自然
    for (int i = 0; i < 24; ++i) {
        if (hours[i] > 0)
            hours[i] = qMax<qint64>(0, hours[i] + rngInt(-120, 120));
    }
    return hours;
}

} // namespace awqtui
