// todomodels.h —— Todo 数据模型（参照 TickTick / Super Productivity 任务语义）
//
// 字段设计对齐后续 Rust 服务端契约（届时 TodoApiStore 读写同一批字段）：
//   - listId == 0 表示「收集箱」（无清单归属）
//   - dueDate 用 ISO yyyy-MM-dd；空串 = 无期限
//   - recurrence 重复规则："" / daily / weekdays / weekly / monthly
//   - 所有 id 为正整数，由数据源统一分配
#pragma once

#include <QJsonArray>
#include <QJsonObject>
#include <QList>
#include <QString>
#include <QStringList>

namespace awqtui {

// 优先级（对齐 TickTick：无/低/中/高）
enum TodoPriority {
    TodoPriorityNone = 0,
    TodoPriorityLow = 1,
    TodoPriorityMedium = 2,
    TodoPriorityHigh = 3
};

// 子任务
struct TodoSubtask {
    qint64 id = 0;
    QString title;
    bool completed = false;

    QJsonObject toJson() const
    {
        QJsonObject o;
        o.insert(QLatin1String("id"), id);
        o.insert(QLatin1String("title"), title);
        o.insert(QLatin1String("completed"), completed);
        return o;
    }

    static TodoSubtask fromJson(const QJsonObject &o)
    {
        TodoSubtask s;
        s.id = o.value(QLatin1String("id")).toVariant().toLongLong();
        s.title = o.value(QLatin1String("title")).toString();
        s.completed = o.value(QLatin1String("completed")).toBool();
        return s;
    }
};

// 任务
struct TodoTask {
    qint64 id = 0;
    QString title;
    QString notes;             // 备注/描述
    qint64 listId = 0;         // 0 = 收集箱
    QStringList tags;
    int priority = TodoPriorityNone;
    QString dueDate;           // ISO yyyy-MM-dd；空 = 无期限
    bool completed = false;
    QString completedAt;       // ISO 时间
    QString recurrence;        // "" / daily / weekdays / weekly / monthly
    QString createdAt;
    QString updatedAt;
    int sortOrder = 0;
    QList<TodoSubtask> subtasks;

    QJsonObject toJson() const
    {
        QJsonObject o;
        o.insert(QLatin1String("id"), id);
        o.insert(QLatin1String("title"), title);
        o.insert(QLatin1String("notes"), notes);
        o.insert(QLatin1String("list_id"), listId);
        o.insert(QLatin1String("tags"), QJsonArray::fromStringList(tags));
        o.insert(QLatin1String("priority"), priority);
        o.insert(QLatin1String("due_date"), dueDate);
        o.insert(QLatin1String("completed"), completed);
        o.insert(QLatin1String("completed_at"), completedAt);
        o.insert(QLatin1String("recurrence"), recurrence);
        o.insert(QLatin1String("created_at"), createdAt);
        o.insert(QLatin1String("updated_at"), updatedAt);
        o.insert(QLatin1String("sort_order"), sortOrder);
        QJsonArray subs;
        for (const auto &s : subtasks)
            subs.append(s.toJson());
        o.insert(QLatin1String("subtasks"), subs);
        return o;
    }

    static TodoTask fromJson(const QJsonObject &o)
    {
        TodoTask t;
        t.id = o.value(QLatin1String("id")).toVariant().toLongLong();
        t.title = o.value(QLatin1String("title")).toString();
        t.notes = o.value(QLatin1String("notes")).toString();
        t.listId = o.value(QLatin1String("list_id")).toVariant().toLongLong();
        const auto tg = o.value(QLatin1String("tags")).toArray();
        for (const auto &v : tg)
            t.tags << v.toString();
        t.priority = o.value(QLatin1String("priority")).toInt();
        t.dueDate = o.value(QLatin1String("due_date")).toString();
        t.completed = o.value(QLatin1String("completed")).toBool();
        t.completedAt = o.value(QLatin1String("completed_at")).toString();
        t.recurrence = o.value(QLatin1String("recurrence")).toString();
        t.createdAt = o.value(QLatin1String("created_at")).toString();
        t.updatedAt = o.value(QLatin1String("updated_at")).toString();
        t.sortOrder = o.value(QLatin1String("sort_order")).toInt();
        const auto ss = o.value(QLatin1String("subtasks")).toArray();
        for (const auto &v : ss)
            t.subtasks.append(TodoSubtask::fromJson(v.toObject()));
        return t;
    }

    bool hasDue() const { return !dueDate.isEmpty(); }
    int openSubtaskCount() const
    {
        int n = 0;
        for (const auto &s : subtasks)
            if (!s.completed)
                ++n;
        return n;
    }
};

// 清单
struct TodoList {
    qint64 id = 0;
    QString name;
    QString color;             // hex；空 = 按名称派生
    int sortOrder = 0;

    QJsonObject toJson() const
    {
        QJsonObject o;
        o.insert(QLatin1String("id"), id);
        o.insert(QLatin1String("name"), name);
        o.insert(QLatin1String("color"), color);
        o.insert(QLatin1String("sort_order"), sortOrder);
        return o;
    }

    static TodoList fromJson(const QJsonObject &o)
    {
        TodoList l;
        l.id = o.value(QLatin1String("id")).toVariant().toLongLong();
        l.name = o.value(QLatin1String("name")).toString();
        l.color = o.value(QLatin1String("color")).toString();
        l.sortOrder = o.value(QLatin1String("sort_order")).toInt();
        return l;
    }
};

// 重复规则 → 可读文案（UI 用）
inline QString recurrenceLabel(const QString &rule)
{
    if (rule == QLatin1String("daily")) return QStringLiteral("每天");
    if (rule == QLatin1String("weekdays")) return QStringLiteral("每个工作日");
    if (rule == QLatin1String("weekly")) return QStringLiteral("每周");
    if (rule == QLatin1String("monthly")) return QStringLiteral("每月");
    return QStringLiteral("不重复");
}

// 重复规则 ← 可读文案（UI 用）
inline QString recurrenceRule(const QString &label)
{
    if (label == QStringLiteral("每天")) return QStringLiteral("daily");
    if (label == QStringLiteral("每个工作日")) return QStringLiteral("weekdays");
    if (label == QStringLiteral("每周")) return QStringLiteral("weekly");
    if (label == QStringLiteral("每月")) return QStringLiteral("monthly");
    return QString();
}

} // namespace awqtui
