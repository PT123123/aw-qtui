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
#include <QSet>
#include <QSharedPointer>
#include <QStandardPaths>
#include <QTimer>
#include <QNetworkReply>

#include "apiclient.h"

namespace awqtui {

namespace {
QString nowIso()
{
    return QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs);
}
} // namespace

// ── TodoSource 批量操作默认实现：逐条转发到单项接口 ──────────
// 子类（如 TodoStore）可覆盖为「改完内存只提交一次」。
void TodoSource::setTasksCompleted(const QList<qint64> &taskIds, bool completed)
{
    for (qint64 id : taskIds)
        setTaskCompleted(id, completed);
}

void TodoSource::deleteTasks(const QList<qint64> &taskIds)
{
    for (qint64 id : taskIds)
        deleteTask(id);
}

void TodoSource::moveTasks(const QList<qint64> &taskIds, qint64 listId)
{
    const QList<TodoTask> snap = tasks();
    for (qint64 id : taskIds) {
        for (const auto &t : snap) {
            if (t.id != id)
                continue;
            TodoTask u = t;
            u.listId = listId;
            updateTask(u);
            break;
        }
    }
}

void TodoSource::setTasksPriority(const QList<qint64> &taskIds, int priority)
{
    const QList<TodoTask> snap = tasks();
    for (qint64 id : taskIds) {
        for (const auto &t : snap) {
            if (t.id != id)
                continue;
            TodoTask u = t;
            u.priority = priority;
            updateTask(u);
            break;
        }
    }
}

void TodoSource::setTasksDueDate(const QList<qint64> &taskIds, const QString &dueDate)
{
    const QList<TodoTask> snap = tasks();
    for (qint64 id : taskIds) {
        for (const auto &t : snap) {
            if (t.id != id)
                continue;
            TodoTask u = t;
            u.dueDate = dueDate;
            updateTask(u);
            break;
        }
    }
}

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

void TodoStore::applyComplete(qint64 taskId, bool completed)
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
            // 注意：append 可能使 m_tasks 重新分配，t 指针随即失效，必须重新取
            m_tasks.append(next);
            t = mutableTask(taskId);
            if (!t)
                return;
        }
        t->completed = true;
        t->completedAt = nowIso();
        t->updatedAt = nowIso();
    } else if (!completed && t->completed) {
        t->completed = false;
        t->completedAt.clear();
        t->updatedAt = nowIso();
    }
}

void TodoStore::setTaskCompleted(qint64 taskId, bool completed)
{
    applyComplete(taskId, completed);
    commit();
}

void TodoStore::deleteTask(qint64 taskId)
{
    m_tasks.removeIf([taskId](const TodoTask &t) { return t.id == taskId; });
    commit();
}

// ── 批量操作（多选模式）：一次性改完内存，只 commit 一次 ────
void TodoStore::setTasksCompleted(const QList<qint64> &taskIds, bool completed)
{
    if (taskIds.isEmpty())
        return;
    for (qint64 id : taskIds)
        applyComplete(id, completed);
    commit();
}

void TodoStore::deleteTasks(const QList<qint64> &taskIds)
{
    if (taskIds.isEmpty())
        return;
    QSet<qint64> doomed;
    for (qint64 id : taskIds)
        doomed.insert(id);
    m_tasks.removeIf([&doomed](const TodoTask &t) { return doomed.contains(t.id); });
    commit();
}

void TodoStore::moveTasks(const QList<qint64> &taskIds, qint64 listId)
{
    if (taskIds.isEmpty())
        return;
    for (qint64 id : taskIds) {
        if (TodoTask *t = mutableTask(id)) {
            t->listId = listId;
            t->updatedAt = nowIso();
        }
    }
    commit();
}

void TodoStore::setTasksPriority(const QList<qint64> &taskIds, int priority)
{
    if (taskIds.isEmpty())
        return;
    for (qint64 id : taskIds) {
        if (TodoTask *t = mutableTask(id)) {
            t->priority = priority;
            t->updatedAt = nowIso();
        }
    }
    commit();
}

void TodoStore::setTasksDueDate(const QList<qint64> &taskIds, const QString &dueDate)
{
    if (taskIds.isEmpty())
        return;
    for (qint64 id : taskIds) {
        if (TodoTask *t = mutableTask(id)) {
            t->dueDate = dueDate;   // 空串 = 清除截止日期（本地实现支持）
            t->updatedAt = nowIso();
        }
    }
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



// ── TodoApiStore（Rust /inbox/todos + /inbox/todo-lists，契约对齐 Android RestTodoSource） ──

TodoApiStore::TodoApiStore(ApiClient *api, QObject *parent)
    : TodoSource(parent), m_api(api)
{
}

TodoTask TodoApiStore::todoToTask(const QJsonObject &o)
{
    TodoTask t;
    t.id = o.value(QLatin1String("id")).toVariant().toLongLong();
    t.title = o.value(QLatin1String("title")).toString();
    t.notes = o.value(QLatin1String("content")).toString();
    t.priority = o.value(QLatin1String("priority")).toInt(TodoPriorityNone);
    t.completed = o.value(QLatin1String("completed")).toBool();
    t.completedAt = o.value(QLatin1String("completed_at")).toString();
    t.createdAt = o.value(QLatin1String("created_at")).toString();
    t.updatedAt = o.value(QLatin1String("updated_at")).toString();

    const QString due = o.value(QLatin1String("due_date")).toString();
    if (!due.isEmpty()) {
        // due_date 是 RFC3339，只取日期部分
        t.dueDate = due.left(10);
    }

    const auto tags = o.value(QLatin1String("tags")).toArray();
    for (const auto &v : tags)
        t.tags << v.toString();

    // 清单 = 独立实体，list_id 关联（0 = 收集箱），与 tag 无关
    t.listId = o.value(QLatin1String("list_id")).toVariant().toLongLong();

    // 子任务：todos.subtasks JSON 列（id 由客户端分配，服务端原样存储）
    const auto subs = o.value(QLatin1String("subtasks")).toArray();
    for (const auto &v : subs)
        t.subtasks.append(TodoSubtask::fromJson(v.toObject()));
    return t;
}

qint64 TodoApiStore::nextSubtaskId() const
{
    qint64 maxId = 0;
    for (const auto &t : m_tasks)
        for (const auto &s : t.subtasks)
            maxId = qMax(maxId, s.id);
    return maxId + 1;
}

void TodoApiStore::fetchTodos()
{
    if (!m_api)
        return;
    QNetworkReply *reply = m_api->getTodos(true); // include completed（服务端始终过滤 deleted）
    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        QJsonDocument doc;
        QString err;
        if (ApiClient::parseReply(reply, &doc, &err)) {
            m_tasks.clear();
            const auto arr = doc.array();
            for (const auto &v : arr)
                m_tasks.append(todoToTask(v.toObject()));
        } else {
            qWarning() << "[TodoApiStore] load todos failed:" << err;
        }
        reply->deleteLater();
        m_loaded = true;
        emit dataChanged();
    });
}

void TodoApiStore::fetchLists()
{
    if (!m_api)
        return;
    QNetworkReply *reply = m_api->getTodoLists();
    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        QJsonDocument doc;
        QString err;
        if (ApiClient::parseReply(reply, &doc, &err)) {
            m_lists.clear();
            // 收集箱（虚拟清单，id=0）
            TodoList inbox;
            inbox.id = 0;
            inbox.name = QStringLiteral("收集箱");
            m_lists.append(inbox);
            const auto arr = doc.array();
            for (const auto &v : arr)
                m_lists.append(TodoList::fromJson(v.toObject()));
            // 清单可能已在别处删除：悬空 list_id 的任务按收集箱展示（不丢任务）
            QSet<qint64> known;
            for (const auto &l : m_lists)
                known.insert(l.id);
            for (auto &t : m_tasks)
                if (t.listId != 0 && !known.contains(t.listId))
                    t.listId = 0;
        } else {
            qWarning() << "[TodoApiStore] load lists failed:" << err;
        }
        reply->deleteLater();
        emit dataChanged();
    });
}

void TodoApiStore::load()
{
    fetchTodos();
    fetchLists();
}

void TodoApiStore::reload()
{
    load();
}

// ── 清单 CRUD（独立实体 /inbox/todo-lists） ────────────────
void TodoApiStore::createList(const QString &name, const QString &color)
{
    if (name.trimmed().isEmpty())
        return;
    QNetworkReply *reply = m_api->createTodoList(name.trimmed(), color, int(m_lists.size()));
    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        reply->deleteLater();
        reload();
    });
}

void TodoApiStore::renameList(qint64 listId, const QString &name)
{
    if (listId <= 0 || name.trimmed().isEmpty())
        return;
    QJsonObject patch;
    patch.insert(QStringLiteral("name"), name.trimmed());
    QNetworkReply *reply = m_api->updateTodoList(listId, patch);
    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        reply->deleteLater();
        reload();
    });
}

void TodoApiStore::deleteList(qint64 listId)
{
    if (listId <= 0)
        return;
    // 服务端把其下任务 list_id 归零（回收集箱），任务本身不删除
    QNetworkReply *reply = m_api->deleteTodoList(listId);
    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        reply->deleteLater();
        reload();
    });
}

// ── 任务 CRUD ──────────────────────────────────────────────
void TodoApiStore::createTask(const QString &title, qint64 listId, const QString &dueDate)
{
    if (title.trimmed().isEmpty())
        return;
    QNetworkReply *reply = m_api->createTodo(title.trimmed(), QString(), {}, listId, dueDate);
    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        reply->deleteLater();
        reload();
    });
}

void TodoApiStore::updateTask(const TodoTask &task)
{
    QJsonObject patch;
    patch.insert(QStringLiteral("title"), task.title);
    patch.insert(QStringLiteral("content"), task.notes);
    patch.insert(QStringLiteral("priority"), task.priority);
    patch.insert(QStringLiteral("list_id"), task.listId);
    // 已知缺口：无法清空 due_date（Option 字段省略 = 保留原值，与 Android 端一致）
    if (!task.dueDate.isEmpty())
        patch.insert(QStringLiteral("due_date"), task.dueDate + QStringLiteral("T00:00:00Z"));
    patch.insert(QStringLiteral("tags"), QJsonArray::fromStringList(task.tags));
    QJsonArray subs;
    for (const auto &s : task.subtasks)
        subs.append(s.toJson());
    patch.insert(QStringLiteral("subtasks"), subs);
    QNetworkReply *reply = m_api->updateTodo(task.id, patch);
    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        reply->deleteLater();
        reload();
    });
}

void TodoApiStore::setTaskCompleted(qint64 taskId, bool completed)
{
    QJsonObject patch;
    patch.insert(QStringLiteral("completed"), completed);
    QNetworkReply *reply = m_api->updateTodo(taskId, patch);
    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        reply->deleteLater();
        reload();
    });
}

void TodoApiStore::deleteTask(qint64 taskId)
{
    QNetworkReply *reply = m_api->deleteTodo(taskId);
    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        reply->deleteLater();
        reload();
    });
}

// ── 子任务：todos.subtasks 整组读改写 ─────────────────────
void TodoApiStore::mutateSubtasks(qint64 taskId,
                                  const std::function<void(QList<TodoSubtask> &)> &fn)
{
    const TodoTask *cached = nullptr;
    for (const auto &t : m_tasks)
        if (t.id == taskId)
            cached = &t;
    if (!cached)
        return;
    TodoTask task = *cached;
    fn(task.subtasks);
    QJsonArray subs;
    for (const auto &s : task.subtasks)
        subs.append(s.toJson());
    QJsonObject patch;
    patch.insert(QStringLiteral("subtasks"), subs);
    QNetworkReply *reply = m_api->updateTodo(taskId, patch);
    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        reply->deleteLater();
        reload();
    });
}

void TodoApiStore::addSubtask(qint64 taskId, const QString &title)
{
    const QString trimmed = title.trimmed();
    if (trimmed.isEmpty())
        return;
    mutateSubtasks(taskId, [this, trimmed](QList<TodoSubtask> &subs) {
        TodoSubtask s;
        s.id = nextSubtaskId();
        s.title = trimmed;
        subs.append(s);
    });
}

void TodoApiStore::toggleSubtask(qint64 taskId, qint64 subtaskId)
{
    mutateSubtasks(taskId, [subtaskId](QList<TodoSubtask> &subs) {
        for (auto &s : subs)
            if (s.id == subtaskId)
                s.completed = !s.completed;
    });
}

void TodoApiStore::removeSubtask(qint64 taskId, qint64 subtaskId)
{
    mutateSubtasks(taskId, [subtaskId](QList<TodoSubtask> &subs) {
        subs.removeIf([subtaskId](const TodoSubtask &s) { return s.id == subtaskId; });
    });
}

// ── 批量操作（多选模式）：并发发请求，全部回包后只 reload 一次 ──
void TodoApiStore::batchRequests(const QList<qint64> &taskIds,
                                 const std::function<QNetworkReply *(qint64)> &makeRequest)
{
    if (!m_api || taskIds.isEmpty())
        return;
    // 计数放在堆上由各 lambda 共享；最后一个回包的人负责触发一次 reload
    auto remaining = QSharedPointer<int>::create(taskIds.size());
    for (qint64 id : taskIds) {
        QNetworkReply *reply = makeRequest(id);
        if (!reply) {
            if (--(*remaining) == 0)
                reload();
            continue;
        }
        connect(reply, &QNetworkReply::finished, this, [this, reply, remaining]() {
            reply->deleteLater();
            if (--(*remaining) == 0)
                reload();
        });
    }
}

void TodoApiStore::setTasksCompleted(const QList<qint64> &taskIds, bool completed)
{
    batchRequests(taskIds, [this, completed](qint64 id) -> QNetworkReply * {
        QJsonObject patch;
        patch.insert(QStringLiteral("completed"), completed);
        return m_api->updateTodo(id, patch);
    });
}

void TodoApiStore::deleteTasks(const QList<qint64> &taskIds)
{
    batchRequests(taskIds, [this](qint64 id) -> QNetworkReply * {
        return m_api->deleteTodo(id);
    });
}

void TodoApiStore::moveTasks(const QList<qint64> &taskIds, qint64 listId)
{
    batchRequests(taskIds, [this, listId](qint64 id) -> QNetworkReply * {
        QJsonObject patch;
        patch.insert(QStringLiteral("list_id"), listId);
        return m_api->updateTodo(id, patch);
    });
}

void TodoApiStore::setTasksPriority(const QList<qint64> &taskIds, int priority)
{
    batchRequests(taskIds, [this, priority](qint64 id) -> QNetworkReply * {
        QJsonObject patch;
        patch.insert(QStringLiteral("priority"), priority);
        return m_api->updateTodo(id, patch);
    });
}

void TodoApiStore::setTasksDueDate(const QList<qint64> &taskIds, const QString &dueDate)
{
    // 已知缺口：服务端 due_date 是 Option（省略 = 保留原值），无法通过 patch 清空，
    // 与单项 updateTask 同款限制，故「清除截止日期」在 API 源下不发请求。
    if (dueDate.isEmpty())
        return;
    const QString iso = dueDate + QStringLiteral("T00:00:00Z");
    batchRequests(taskIds, [this, iso](qint64 id) -> QNetworkReply * {
        QJsonObject patch;
        patch.insert(QStringLiteral("due_date"), iso);
        return m_api->updateTodo(id, patch);
    });
}

} // namespace awqtui
