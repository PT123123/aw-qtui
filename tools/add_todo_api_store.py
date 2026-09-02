path = 'src/todostore.cpp'

with open(path, 'r', encoding='utf-8') as f:
    content = f.read()

# 1. 加 include
old_inc = '#include <QTimer>'
new_inc = '''#include <QTimer>
#include <QNetworkReply>

#include "apiclient.h"
#include "theme.h"'''
if old_inc in content:
    content = content.replace(old_inc, new_inc, 1)
    print('Include added')
else:
    print('Include pattern not found')

# 2. 在 namespace 结束前加 TodoApiStore 实现
impl = '''

// ── TodoApiStore（Rust /inbox/todos） ────────────────────────

TodoApiStore::TodoApiStore(ApiClient *api, QObject *parent)
    : TodoSource(parent), m_api(api)
{
}

qint64 TodoApiStore::tagToListId(const QString &tag)
{
    if (tag.isEmpty())
        return 0;
    // 用 tag 字符串的 hash 作为 listId（正整数）
    return qHash(tag) % 1000000 + 1;
}

QString TodoApiStore::listIdToTag(qint64 listId)
{
    if (listId <= 0)
        return QString();
    // 反向查找：从 m_lists 里找对应 id 的 name
    for (const auto &l : m_lists)
        if (l.id == listId)
            return l.name;
    return QString();
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
        // due_date 是 ISO 8601，只取日期部分
        t.dueDate = due.left(10);
    }

    const auto tags = o.value(QLatin1String("tags")).toArray();
    for (const auto &v : tags)
        t.tags << v.toString();

    // listId 用第一个 tag 模拟
    if (!t.tags.isEmpty())
        t.listId = tagToListId(t.tags.first());
    else
        t.listId = 0;

    // subtasks / recurrence 暂不支持
    return t;
}

void TodoApiStore::rebuildLists()
{
    m_lists.clear();
    // 收集箱（id=0）
    TodoList inbox;
    inbox.id = 0;
    inbox.name = QStringLiteral("收集箱");
    m_lists.append(inbox);

    QSet<QString> seen;
    for (const auto &t : m_tasks) {
        for (const auto &tag : t.tags) {
            if (seen.contains(tag))
                continue;
            seen.insert(tag);
            TodoList l;
            l.id = tagToListId(tag);
            l.name = tag;
            l.color = colorForString(tag);
            m_lists.append(l);
        }
    }
}

void TodoApiStore::load()
{
    if (!m_api)
        return;
    QNetworkReply *reply = m_api->getTodos(true); // include completed
    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        QJsonDocument doc;
        QString err;
        if (ApiClient::parseReply(reply, &doc, &err)) {
            m_tasks.clear();
            const auto arr = doc.array();
            for (const auto &v : arr)
                m_tasks.append(todoToTask(v.toObject()));
            rebuildLists();
            m_loaded = true;
            emit dataChanged();
        } else {
            qWarning() << "[TodoApiStore] load failed:" << err;
        }
        reply->deleteLater();
    });
}

void TodoApiStore::createList(const QString &name, const QString &color)
{
    Q_UNUSED(color);
    // lists 用 tags 模拟，创建 list 就是确保 tag 存在
    // 不需要实际操作，下次 load 时会自动出现
    Q_UNUSED(name);
}

void TodoApiStore::renameList(qint64 listId, const QString &name)
{
    // 重命名 list = 把所有该 tag 的 todo 改成新 tag
    const QString oldTag = listIdToTag(listId);
    if (oldTag.isEmpty())
        return;
    for (auto &t : m_tasks) {
        if (t.tags.contains(oldTag)) {
            t.tags.removeAll(oldTag);
            t.tags.prepend(name);
            QJsonObject patch;
            patch.insert(QStringLiteral("tags"), QJsonArray::fromStringList(t.tags));
            QNetworkReply *reply = m_api->updateTodo(t.id, patch);
            connect(reply, &QNetworkReply::finished, reply, &QNetworkReply::deleteLater);
        }
    }
    rebuildLists();
    emit dataChanged();
}

void TodoApiStore::deleteList(qint64 listId)
{
    const QString tag = listIdToTag(listId);
    if (tag.isEmpty())
        return;
    for (auto &t : m_tasks) {
        if (t.tags.contains(tag)) {
            t.tags.removeAll(tag);
            QJsonObject patch;
            patch.insert(QStringLiteral("tags"), QJsonArray::fromStringList(t.tags));
            QNetworkReply *reply = m_api->updateTodo(t.id, patch);
            connect(reply, &QNetworkReply::finished, reply, &QNetworkReply::deleteLater);
        }
    }
    rebuildLists();
    emit dataChanged();
}

void TodoApiStore::createTask(const QString &title, qint64 listId, const QString &dueDate)
{
    QStringList tags;
    const QString tag = listIdToTag(listId);
    if (!tag.isEmpty())
        tags << tag;

    QJsonObject patch;
    if (!dueDate.isEmpty())
        patch.insert(QStringLiteral("due_date"), dueDate);

    QNetworkReply *reply = m_api->createTodo(title, QString(), tags);
    connect(reply, &QNetworkReply::finished, this, [this, reply, dueDate]() {
        reply->deleteLater();
        // 如果有 dueDate，需要额外 update（因为 createTodo 不支持 dueDate）
        if (!dueDate.isEmpty()) {
            QJsonDocument doc;
            QString err;
            if (ApiClient::parseReply(reply, &doc, &err)) {
                qint64 id = doc.object().value(QLatin1String("id")).toVariant().toLongLong();
                QJsonObject p;
                p.insert(QStringLiteral("due_date"), dueDate);
                QNetworkReply *r2 = m_api->updateTodo(id, p);
                connect(r2, &QNetworkReply::finished, r2, &QNetworkReply::deleteLater);
            }
        }
        load(); // 重新加载
    });
}

void TodoApiStore::updateTask(const TodoTask &task)
{
    QJsonObject patch;
    patch.insert(QStringLiteral("title"), task.title);
    patch.insert(QStringLiteral("content"), task.notes);
    patch.insert(QStringLiteral("priority"), task.priority);
    if (!task.dueDate.isEmpty())
        patch.insert(QStringLiteral("due_date"), task.dueDate);
    patch.insert(QStringLiteral("tags"), QJsonArray::fromStringList(task.tags));
    QNetworkReply *reply = m_api->updateTodo(task.id, patch);
    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        reply->deleteLater();
        load();
    });
}

void TodoApiStore::setTaskCompleted(qint64 taskId, bool completed)
{
    QJsonObject patch;
    patch.insert(QStringLiteral("completed"), completed);
    QNetworkReply *reply = m_api->updateTodo(taskId, patch);
    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        reply->deleteLater();
        load();
    });
}

void TodoApiStore::deleteTask(qint64 taskId)
{
    QNetworkReply *reply = m_api->deleteTodo(taskId);
    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        reply->deleteLater();
        load();
    });
}

void TodoApiStore::addSubtask(qint64 taskId, const QString &title)
{
    Q_UNUSED(taskId);
    Q_UNUSED(title);
    // 暂不支持 subtasks
}

void TodoApiStore::toggleSubtask(qint64 taskId, qint64 subtaskId)
{
    Q_UNUSED(taskId);
    Q_UNUSED(subtaskId);
    // 暂不支持 subtasks
}

void TodoApiStore::removeSubtask(qint64 taskId, qint64 subtaskId)
{
    Q_UNUSED(taskId);
    Q_UNUSED(subtaskId);
    // 暂不支持 subtasks
}

'''

# 在 namespace 结束前插入
old_ns = '} // namespace awqtui'
if old_ns in content:
    content = content.replace(old_ns, impl + old_ns, 1)
    print('TodoApiStore impl added')
else:
    print('Namespace end not found')

with open(path, 'w', encoding='utf-8') as f:
    f.write(content)
print('Done')
