// todostore.cpp —— TodoStore 本地 mock 实现
#include "todostore.h"

#include <QDate>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QSaveFile>
#include <QStandardPaths>
#include <QTimer>

namespace awqtui {

namespace {
QString nowIso()
{
    return QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs);
}
} // namespace

TodoStore::TodoStore(QObject *parent) : TodoSource(parent) {}

QString TodoStore::filePath()
{
    QString dir = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    if (dir.isEmpty())
        dir = QDir::homePath() + QStringLiteral("/.aw-qtui");
    QDir().mkpath(dir);
    return dir + QStringLiteral("/todo_local.json");
}

qint64 TodoStore::nextId()
{
    return m_nextId++;
}

TodoTask *TodoStore::mutableTask(qint64 id)
{
    for (auto &t : m_tasks)
        if (t.id == id)
            return &t;
    return nullptr;
}

const TodoTask *TodoStore::findTask(qint64 id) const
{
    for (const auto &t : m_tasks)
        if (t.id == id)
            return &t;
    return nullptr;
}

TodoList *TodoStore::mutableList(qint64 id)
{
    for (auto &l : m_lists)
        if (l.id == id)
            return &l;
    return nullptr;
}

QString TodoStore::nextRecurrenceDate(const QString &rule, const QString &basedOn)
{
    if (rule.isEmpty())
        return QString();
    QDate base = basedOn.isEmpty() ? QDate::currentDate() : QDate::fromString(basedOn, Qt::ISODate);
    if (!base.isValid())
        base = QDate::currentDate();
    QDate next;
    if (rule == QLatin1String("daily"))
        next = base.addDays(1);
    else if (rule == QLatin1String("weekdays")) {
        next = base.addDays(1);
        while (next.dayOfWeek() >= 6) // 周六(6)/周日(7) 跳到下个工作日
            next = next.addDays(1);
    } else if (rule == QLatin1String("weekly"))
        next = base.addDays(7);
    else if (rule == QLatin1String("monthly"))
        next = base.addMonths(1);
    else
        return QString();
    const QDate today = QDate::currentDate();
    if (next < today)
        next = today;
    return next.toString(Qt::ISODate);
}

// ── 首次运行种子数据（贴合真实使用场景，便于联调各视图） ──
void TodoStore::seed()
{
    auto addList = [this](const QString &name, const QString &color) {
        TodoList l;
        l.id = nextId();
        l.name = name;
        l.color = color;
        l.sortOrder = int(m_lists.size());
        m_lists.append(l);
        return l.id;
    };
    const qint64 listWork = addList(QStringLiteral("工作"), QStringLiteral("#4c8bf5"));
    const qint64 listLife = addList(QStringLiteral("生活"), QStringLiteral("#3fb950"));
    const qint64 listShop = addList(QStringLiteral("购物"), QStringLiteral("#d29922"));

    const QDate today = QDate::currentDate();
    const QString d0 = today.toString(Qt::ISODate);
    const QString d1 = today.addDays(1).toString(Qt::ISODate);
    const QString d2 = today.addDays(2).toString(Qt::ISODate);
    const QString d3 = today.addDays(3).toString(Qt::ISODate);

    auto addTask = [this](const QString &title, qint64 listId, int priority, const QString &due,
                          const QString &recur, const QStringList &tags, const QString &notes,
                          bool done = false) {
        TodoTask t;
        t.id = nextId();
        t.title = title;
        t.listId = listId;
        t.priority = priority;
        t.dueDate = due;
        t.recurrence = recur;
        t.tags = tags;
        t.notes = notes;
        t.createdAt = nowIso();
        t.updatedAt = nowIso();
        t.sortOrder = int(m_tasks.size());
        if (done) {
            t.completed = true;
            t.completedAt = nowIso();
        }
        m_tasks.append(t);
    };

    // 收集箱
    addTask(QStringLiteral("给 aw-qtui 的 Todo 页接入 Rust 服务端"),
            0, TodoPriorityHigh, d0, QString(),
            {QStringLiteral("aw-qtui"), QStringLiteral("rust")},
            QStringLiteral("在 TodoSource 抽象上新增 TodoApiStore，走 HTTP 实现同一套 CRUD，页面零改动。"));
    addTask(QStringLiteral("整理 aw-inbox-rust 契约缺口清单"),
            0, TodoPriorityMedium, d1, QString(),
            {QStringLiteral("aw-qtui")},
            QStringLiteral("offset/sort_by 解析但忽略、SyncRequest 部分字段未用等，列成待办。"));
    // 工作
    addTask(QStringLiteral("跑通 aw-qtui 构建 + 联调 mock"),
            listWork, TodoPriorityHigh, d0, QString(),
            {QStringLiteral("aw-qtui")},
            QStringLiteral("make 无服务端构建；mock_inbox_server.py 5620 联调。"));
    addTask(QStringLiteral("复查 ActivityWatch 时间线 mock 随机性"),
            listWork, TodoPriorityLow, d3, QString(),
            {QStringLiteral("aw-qtui"), QStringLiteral("mock")}, QString());
    addTask(QStringLiteral("周报：爬虫项目进度同步"),
            listWork, TodoPriorityLow, d0, QStringLiteral("weekly"),
            {QStringLiteral("工作")}, QStringLiteral("每周一同步上周采集与反爬进展。"));
    addTask(QStringLiteral("读完生成式反爬策略笔记"),
            listWork, TodoPriorityNone, QString(), QString(),
            {QStringLiteral("爬虫")}, QString(), /*done*/ true);
    // 生活
    addTask(QStringLiteral("健身：卧推 5×5（当前 1RM 80kg）"),
            listLife, TodoPriorityMedium, d0, QStringLiteral("weekly"),
            {QStringLiteral("健身")},
            QStringLiteral("周日练胸；注意夹胸时右肩肌肉拉扯感，先小重量找动作。"));
    addTask(QStringLiteral("研究运动损伤：夹胸时肌肉拉扯感"),
            listLife, TodoPriorityLow, QString(), QString(),
            {QStringLiteral("健身"), QStringLiteral("医学")},
            QStringLiteral("查胸大肌/肩袖相关，排除痛风可能。"));
    addTask(QStringLiteral("安眠药机理备忘：曲唑酮 vs 右佐匹克隆"),
            listLife, TodoPriorityMedium, QString(), QString(),
            {QStringLiteral("医学")},
            QStringLiteral("对比受体机制、依赖性与副作用。"));
    // 购物
    addTask(QStringLiteral("看拜耳耳螨药价格"),
            listShop, TodoPriorityLow, d2, QString(),
            {QStringLiteral("宠物")}, QString());
}

void TodoStore::load()
{
    QFile f(filePath());
    if (f.open(QIODevice::ReadOnly)) {
        const QJsonDocument doc = QJsonDocument::fromJson(f.readAll());
        const QJsonObject root = doc.object();
        m_lists.clear();
        m_tasks.clear();
        const auto la = root.value(QLatin1String("lists")).toArray();
        for (const auto &v : la)
            m_lists.append(TodoList::fromJson(v.toObject()));
        const auto ta = root.value(QLatin1String("tasks")).toArray();
        for (const auto &v : ta)
            m_tasks.append(TodoTask::fromJson(v.toObject()));
        m_nextId = root.value(QLatin1String("next_id")).toVariant().toLongLong();
        if (m_nextId < 1)
            m_nextId = 1;
    }
    // 首次运行 / 文件损坏：生成种子数据
    if (m_lists.isEmpty() && m_tasks.isEmpty())
        seed();
    save();
    m_loaded = true;
    emit dataChanged();
}

void TodoStore::commit()
{
    save();
    // 后投递 dataChanged：模拟异步回包，避免在控件信号处理栈内重入销毁 sender
    QTimer::singleShot(0, this, [this] { emit dataChanged(); });
}

void TodoStore::save() const
{
    QJsonObject root;
    QJsonArray la;
    for (const auto &l : m_lists)
        la.append(l.toJson());
    root.insert(QLatin1String("lists"), la);
    QJsonArray ta;
    for (const auto &t : m_tasks)
        ta.append(t.toJson());
    root.insert(QLatin1String("tasks"), ta);
    root.insert(QLatin1String("next_id"), m_nextId);

    QSaveFile sf(filePath());
    if (sf.open(QIODevice::WriteOnly)) {
        sf.write(QJsonDocument(root).toJson(QJsonDocument::Indented));
        sf.commit();
    }
}

// ── 清单 CRUD ──────────────────────────────────────────────
void TodoStore::createList(const QString &name, const QString &color)
{
    TodoList l;
    l.id = nextId();
    l.name = name;
    l.color = color;
    l.sortOrder = int(m_lists.size());
    m_lists.append(l);
    commit();
}

void TodoStore::renameList(qint64 listId, const QString &name)
{
    TodoList *l = mutableList(listId);
    if (!l)
        return;
    l->name = name;
    commit();
}

void TodoStore::deleteList(qint64 listId)
{
    if (listId <= 0)
        return;
    // 任务迁回收件箱，避免误删数据
    for (auto &t : m_tasks) {
        if (t.listId == listId) {
            t.listId = 0;
            t.updatedAt = nowIso();
        }
    }
    m_lists.removeIf([listId](const TodoList &l) { return l.id == listId; });
    commit();
}

// ── 任务 CRUD ──────────────────────────────────────────────
void TodoStore::createTask(const QString &title, qint64 listId, const QString &dueDate)
{
    TodoTask t;
    t.id = nextId();
    t.title = title;
    t.listId = listId;
    t.dueDate = dueDate;
    t.createdAt = nowIso();
    t.updatedAt = nowIso();
    int maxOrder = 0;
    for (const auto &x : m_tasks)
        if (x.sortOrder > maxOrder)
            maxOrder = x.sortOrder;
    t.sortOrder = maxOrder + 1;
    m_tasks.append(t);
    commit();
}

void TodoStore::updateTask(const TodoTask &task)
{
    TodoTask *t = mutableTask(task.id);
    if (!t)
        return;
    const QString created = t->createdAt;
    *t = task;
    t->createdAt = created; // 保护创建时间不被调用方覆盖
    t->updatedAt = nowIso();
    commit();
}

void TodoStore::setTaskCompleted(qint64 taskId, bool completed)
{
    TodoTask *t = mutableTask(taskId);
    if (!t)
        return;
    if (completed && !t->completed) {
        // 重复任务：完成当前实例，并生成下一实例
        if (!t->recurrence.isEmpty()) {
            const QString nextDue = nextRecurrenceDate(t->recurrence, t->dueDate);
            TodoTask next = *t;
            next.id = nextId();
            next.completed = false;
            next.completedAt.clear();
            next.dueDate = nextDue;
            next.createdAt = nowIso();
            next.updatedAt = nowIso();
            for (auto &s : next.subtasks)
                s.completed = false;
            m_tasks.append(next);
        }
        t->completed = true;
        t->completedAt = nowIso();
        t->updatedAt = nowIso();
    } else if (!completed && t->completed) {
        t->completed = false;
        t->completedAt.clear();
        t->updatedAt = nowIso();
    }
    commit();
}

void TodoStore::deleteTask(qint64 taskId)
{
    m_tasks.removeIf([taskId](const TodoTask &t) { return t.id == taskId; });
    commit();
}

// ── 子任务 ─────────────────────────────────────────────────
void TodoStore::addSubtask(qint64 taskId, const QString &title)
{
    TodoTask *t = mutableTask(taskId);
    if (!t)
        return;
    TodoSubtask s;
    s.id = nextId();
    s.title = title;
    t->subtasks.append(s);
    t->updatedAt = nowIso();
    commit();
}

void TodoStore::toggleSubtask(qint64 taskId, qint64 subtaskId)
{
    TodoTask *t = mutableTask(taskId);
    if (!t)
        return;
    for (auto &s : t->subtasks) {
        if (s.id == subtaskId) {
            s.completed = !s.completed;
            break;
        }
    }
    t->updatedAt = nowIso();
    commit();
}

void TodoStore::removeSubtask(qint64 taskId, qint64 subtaskId)
{
    TodoTask *t = mutableTask(taskId);
    if (!t)
        return;
    t->subtasks.removeIf([subtaskId](const TodoSubtask &s) { return s.id == subtaskId; });
    t->updatedAt = nowIso();
    commit();
}

} // namespace awqtui
