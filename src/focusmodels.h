// focusmodels.h —— 专注数据模型（番茄/正计时会话 + 倒数纪念日）
//
// 字段设计对齐后续 Rust 服务端契约（届时 FocusApiStore 读写同一批字段）：
//   - kind: 服务端用字符串 "pomodoro" / "stopwatch"，客户端以 int FocusKind 存储
//   - start_ms / end_ms：epoch 毫秒（UTC），UI 侧按本地时区显示
//   - duration_sec：实际专注秒数（番茄=目标时长；正计时=累计时长，不含暂停）
//   - event：事件名 / 关联任务标题；task_id：关联任务（0 = 未关联）
//   - memorials.date_iso：yyyy-MM-dd，剩余天数 = date - today
#pragma once

#include <QDate>
#include <QDateTime>
#include <QJsonArray>
#include <QJsonObject>
#include <QList>
#include <QString>
#include <QTime>

namespace awqtui {

enum FocusKind {
    FocusPomodoro = 0,   // 番茄倒计时
    FocusStopwatch = 1   // 正计时
};

// 专注会话（一条 = 一次完成的番茄 / 一段正计时）
struct FocusSession {
    qint64 id = 0;
    int kind = FocusPomodoro;
    qint64 startMs = 0;       // 开始 epoch ms
    qint64 endMs = 0;         // 结束 epoch ms
    qint64 durationSec = 0;   // 专注秒数
    QString eventName;        // 事件名 / 关联任务标题
    qint64 taskId = 0;        // 关联任务 id（0 = 未关联）
    QString createdAt;        // ISO 创建时间

    QJsonObject toJson() const
    {
        QJsonObject o;
        o.insert(QLatin1String("id"), id);
        o.insert(QLatin1String("kind"), kind == FocusStopwatch ? QLatin1String("stopwatch") : QLatin1String("pomodoro"));
        o.insert(QLatin1String("start_ms"), startMs);
        o.insert(QLatin1String("end_ms"), endMs);
        o.insert(QLatin1String("duration_sec"), durationSec);
        o.insert(QLatin1String("event"), eventName);
        o.insert(QLatin1String("task_id"), taskId);
        o.insert(QLatin1String("created_at"), createdAt);
        return o;
    }

    static FocusSession fromJson(const QJsonObject &o)
    {
        FocusSession s;
        s.id = o.value(QLatin1String("id")).toVariant().toLongLong();
        s.kind = (o.value(QLatin1String("kind")).toString() == QLatin1String("stopwatch"))
                     ? FocusStopwatch : FocusPomodoro;
        s.startMs = o.value(QLatin1String("start_ms")).toVariant().toLongLong();
        s.endMs = o.value(QLatin1String("end_ms")).toVariant().toLongLong();
        s.durationSec = o.value(QLatin1String("duration_sec")).toVariant().toLongLong();
        s.eventName = o.value(QLatin1String("event")).toString();
        s.taskId = o.value(QLatin1String("task_id")).toVariant().toLongLong();
        s.createdAt = o.value(QLatin1String("created_at")).toString();
        return s;
    }
};

// 倒数纪念日
struct MemorialDay {
    qint64 id = 0;
    QString name;
    QString emoji;     // 可选展示图标；空 = 默认 🎉
    QString dateIso;   // yyyy-MM-dd
    QString createdAt;

    QJsonObject toJson() const
    {
        QJsonObject o;
        o.insert(QLatin1String("id"), id);
        o.insert(QLatin1String("name"), name);
        o.insert(QLatin1String("emoji"), emoji);
        o.insert(QLatin1String("date_iso"), dateIso);
        o.insert(QLatin1String("created_at"), createdAt);
        return o;
    }

    static MemorialDay fromJson(const QJsonObject &o)
    {
        MemorialDay m;
        m.id = o.value(QLatin1String("id")).toVariant().toLongLong();
        m.name = o.value(QLatin1String("name")).toString();
        m.emoji = o.value(QLatin1String("emoji")).toString();
        m.dateIso = o.value(QLatin1String("date_iso")).toString();
        m.createdAt = o.value(QLatin1String("created_at")).toString();
        return m;
    }
};

// ── 统计辅助（UI / 图表共用；纯函数，不依赖数据源） ──────────────

// 会话所在日期（按本地时区）
inline QDate sessionDate(const FocusSession &s)
{
    return QDateTime::fromMSecsSinceEpoch(s.startMs).date();
}

// 某天 00:00 的 epoch ms
inline qint64 dayStartMs(const QDate &d)
{
    return d.startOfDay().toMSecsSinceEpoch();
}

// 会话与 [fromMs, toMs) 区间的重叠秒数
inline qint64 overlapSec(const FocusSession &s, qint64 fromMs, qint64 toMs)
{
    const qint64 s0 = qMax(s.startMs, fromMs);
    const qint64 s1 = qMin(s.endMs, toMs);
    return s1 > s0 ? (s1 - s0) / 1000 : 0;
}

// 某天的专注总秒数（跨天会话按重叠计）
inline qint64 dayFocusSec(const QList<FocusSession> &sessions, const QDate &day)
{
    const qint64 a = dayStartMs(day);
    const qint64 b = dayStartMs(day.addDays(1));
    qint64 total = 0;
    for (const auto &s : sessions)
        total += overlapSec(s, a, b);
    return total;
}

// 时长格式化：75 秒 -> "1m15s"，3600 -> "1h0m"，1500 -> "25m"
inline QString fmtDuration(qint64 sec)
{
    if (sec < 60)
        return QStringLiteral("%1s").arg(sec);
    const qint64 h = sec / 3600, m = (sec % 3600) / 60;
    if (h > 0)
        return QStringLiteral("%1h%2m").arg(h).arg(m);
    return QStringLiteral("%1m").arg(m);
}

// 时长格式化（分钟输入）：90 分钟 -> "1h30m"
inline QString fmtMinutes(qint64 min)
{
    return fmtDuration(min * 60);
}

// epoch ms -> 本地 "HH:mm"
inline QString fmtClock(qint64 ms)
{
    return QDateTime::fromMSecsSinceEpoch(ms).toString(QStringLiteral("HH:mm"));
}

} // namespace awqtui
