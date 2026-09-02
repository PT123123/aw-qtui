// focusstore.cpp —— FocusStore 本地实现
#include "focusstore.h"

#include <QDate>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>
#include <QStandardPaths>
#include <QTime>
#include <QTimer>

namespace awqtui {

namespace {
QString nowIso()
{
    return QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs);
}
} // namespace

FocusStore::FocusStore(QObject *parent) : FocusSource(parent) {}

QString FocusStore::filePath()
{
    QString dir = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    if (dir.isEmpty())
        dir = QDir::homePath() + QStringLiteral("/.aw-qtui");
    QDir().mkpath(dir);
    return dir + QStringLiteral("/focus_local.json");
}

qint64 FocusStore::nextId()
{
    return m_nextId++;
}

// 首次运行种子数据：过去 7 天 + 今天的若干专注会话，方便各统计视图联调
void FocusStore::seed()
{
    const QDate today = QDate::currentDate();

    auto add = [this](int kind, const QDate &d, const QTime &start, const QTime &end,
                      const QString &ev, qint64 taskId = 0) {
        FocusSession s;
        s.id = nextId();
        s.kind = kind;
        const QDateTime st(d, start), en(d, end);
        s.startMs = st.toMSecsSinceEpoch();
        s.endMs = en.toMSecsSinceEpoch();
        s.durationSec = qMax<qint64>(0, st.secsTo(en));
        s.eventName = ev;
        s.taskId = taskId;
        s.createdAt = nowIso();
        m_sessions.append(s);
    };

    // 今天
    add(FocusPomodoro, today, QTime(9, 30), QTime(9, 55), QStringLiteral("需求整理"));
    add(FocusPomodoro, today, QTime(10, 0), QTime(10, 25), QStringLiteral("需求整理"));
    add(FocusStopwatch, today, QTime(14, 0), QTime(15, 30), QStringLiteral("读反爬策略笔记"));
    // 昨天
    add(FocusPomodoro, today.addDays(-1), QTime(16, 53), QTime(17, 18), QStringLiteral("aw-qtui 联调"));
    add(FocusPomodoro, today.addDays(-1), QTime(17, 25), QTime(17, 50), QStringLiteral("aw-qtui 联调"));
    // 前三天
    add(FocusPomodoro, today.addDays(-2), QTime(9, 0), QTime(9, 25), QStringLiteral("爬虫采集脚本"));
    add(FocusStopwatch, today.addDays(-2), QTime(20, 0), QTime(21, 0), QStringLiteral("健身"));
    add(FocusPomodoro, today.addDays(-3), QTime(15, 0), QTime(15, 25), QStringLiteral("需求整理"));
    add(FocusPomodoro, today.addDays(-4), QTime(11, 0), QTime(11, 30), QStringLiteral("写周报"));
    add(FocusStopwatch, today.addDays(-5), QTime(22, 0), QTime(23, 0), QStringLiteral("学习粒子物理"));
    add(FocusPomodoro, today.addDays(-6), QTime(8, 30), QTime(9, 0), QStringLiteral("健身"));

    // 纪念日种子
    auto addMem = [this, today](const QString &name, const QString &emoji, int daysFromToday) {
        MemorialDay m;
        m.id = nextId();
        m.name = name;
        m.emoji = emoji;
        m.dateIso = today.addDays(daysFromToday).toString(Qt::ISODate);
        m.createdAt = nowIso();
        m_memorials.append(m);
    };
    addMem(QStringLiteral("春节"), QStringLiteral("🧧"), 47);
    addMem(QStringLiteral("老妈生日"), QStringLiteral("🎂"), 90);
    addMem(QStringLiteral("发工资"), QStringLiteral("💰"), 13);
}

void FocusStore::load()
{
    bool fileExisted = false;
    QFile f(filePath());
    if (f.open(QIODevice::ReadOnly)) {
        fileExisted = true;
        const QJsonDocument doc = QJsonDocument::fromJson(f.readAll());
        const QJsonObject root = doc.object();
        m_sessions.clear();
        m_memorials.clear();
        const auto sa = root.value(QLatin1String("sessions")).toArray();
        for (const auto &v : sa)
            m_sessions.append(FocusSession::fromJson(v.toObject()));
        const auto ma = root.value(QLatin1String("memorials")).toArray();
        for (const auto &v : ma)
            m_memorials.append(MemorialDay::fromJson(v.toObject()));
        m_nextId = root.value(QLatin1String("next_id")).toVariant().toLongLong();
        if (m_nextId < 1)
            m_nextId = 1;
    }
    // 仅首次运行（文件不存在 / 空文件）才种种子；用户清空数据后重启不得重新播种
    if (!fileExisted && m_sessions.isEmpty() && m_memorials.isEmpty())
        seed();
    save();
    m_loaded = true;
    emit dataChanged();
}

void FocusStore::commit()
{
    save();
    // 后投递 dataChanged：模拟异步回包，避免在控件信号处理栈内重入销毁 sender
    QTimer::singleShot(0, this, [this] { emit dataChanged(); });
}

void FocusStore::save() const
{
    QJsonObject root;
    QJsonArray sa;
    for (const auto &s : m_sessions)
        sa.append(s.toJson());
    root.insert(QLatin1String("sessions"), sa);
    QJsonArray ma;
    for (const auto &m : m_memorials)
        ma.append(m.toJson());
    root.insert(QLatin1String("memorials"), ma);
    root.insert(QLatin1String("next_id"), m_nextId);

    QSaveFile sf(filePath());
    if (sf.open(QIODevice::WriteOnly)) {
        sf.write(QJsonDocument(root).toJson(QJsonDocument::Indented));
        sf.commit();
    }
}

void FocusStore::addSession(int kind, qint64 startMs, qint64 endMs, qint64 durationSec,
                            const QString &eventName, qint64 taskId)
{
    FocusSession s;
    s.id = nextId();
    s.kind = kind;
    s.startMs = startMs;
    s.endMs = endMs;
    s.durationSec = durationSec;
    s.eventName = eventName;
    s.taskId = taskId;
    s.createdAt = nowIso();
    m_sessions.append(s);
    commit();
}

void FocusStore::deleteSession(qint64 id)
{
    m_sessions.removeIf([id](const FocusSession &s) { return s.id == id; });
    commit();
}

void FocusStore::addMemorial(const QString &name, const QString &emoji, const QString &dateIso)
{
    MemorialDay m;
    m.id = nextId();
    m.name = name;
    m.emoji = emoji;
    m.dateIso = dateIso;
    m.createdAt = nowIso();
    m_memorials.append(m);
    commit();
}

void FocusStore::deleteMemorial(qint64 id)
{
    m_memorials.removeIf([id](const MemorialDay &m) { return m.id == id; });
    commit();
}

} // namespace awqtui
