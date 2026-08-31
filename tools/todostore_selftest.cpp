// todostore_selftest.cpp —— TodoStore 本地 mock 逻辑自测（回归测试，保留）
//
// 编译运行（MSVC + Qt 6.8，独立应用名 aw-qtui-selftest，不碰真实数据）：
//   moc.exe ..\src\todostore.h -o moc_todostore.cpp -I..\src -I"$qt\include"
//   cl /std:c++17 /EHsc /utf-8 /DQT_CORE_LIB /I"$qt\include" /I"$qt\include\QtCore" \
//      /I"$qt\mkspecs\win32-msvc" /I..\src todostore_selftest.cpp moc_todostore.cpp \
//      ..\src\todostore.cpp /Fe:todostore_selftest.exe /link "$qt\lib\Qt6Core.lib"
//   （运行前把 "$qt\bin" 加入 PATH 以定位 Qt6Core.dll）
// 覆盖：重复任务完成生成下一实例、取消完成、任务/子任务 CRUD、删清单迁回收件箱、持久化重载。
#include "todostore.h"

#include <QCoreApplication>
#include <QDate>
#include <cstdio>

using namespace awqtui;

static int g_fail = 0;
#define CHECK(cond, msg) do { if (cond) printf("  [PASS] %s\n", msg); else { printf("  [FAIL] %s\n", msg); ++g_fail; } } while (0)

static const TodoTask *findById(const QList<TodoTask> &ts, qint64 id)
{
    for (const auto &t : ts)
        if (t.id == id)
            return &t;
    return nullptr;
}

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    QCoreApplication::setApplicationName(QStringLiteral("aw-qtui-selftest"));
    QCoreApplication::setOrganizationName(QStringLiteral("aw-qtui"));

    TodoStore store;
    store.load();
    CHECK(store.ready(), "load() sets ready");
    const int seedTasks = int(store.tasks().size());
    printf("  seed: lists=%d tasks=%d\n", int(store.lists().size()), seedTasks);
    CHECK(seedTasks >= 8, "seed has tasks");
    CHECK(!store.lists().isEmpty(), "seed has lists");

    // ── 1) 重复任务完成 → 生成下一实例 ──
    qint64 recId = 0;
    for (const auto &t : store.tasks())
        if (t.recurrence == QLatin1String("weekly")) { recId = t.id; break; }
    CHECK(recId != 0, "found a weekly recurring task in seed");
    int openWeeklyBefore = 0;
    for (const auto &t : store.tasks())
        if (t.recurrence == QLatin1String("weekly") && !t.completed) ++openWeeklyBefore;
    store.setTaskCompleted(recId, true);
    auto after = store.tasks();
    const TodoTask *completed = findById(after, recId);
    CHECK(completed && completed->completed, "original recurring task completed");
    CHECK(after.size() == seedTasks + 1, "completing recurring task creates one more");
    int openWeeklyAfter = 0;
    for (const auto &t : after)
        if (t.recurrence == QLatin1String("weekly") && !t.completed) ++openWeeklyAfter;
    CHECK(openWeeklyAfter == openWeeklyBefore, "new recurring occurrence is open (count preserved)");
    // 下一实例 due 应 >= 今天
    {
        const QDate today = QDate::currentDate();
        bool foundNew = false;
        for (const auto &t : after)
            if (t.recurrence == QLatin1String("weekly") && !t.completed && t.dueDate != completed->dueDate) {
                foundNew = true;
                CHECK(QDate::fromString(t.dueDate, Qt::ISODate) >= today, "next occurrence due >= today");
                break;
            }
        CHECK(foundNew, "next occurrence has advanced due date");
    }

    // ── 2) 取消完成 ──
    store.setTaskCompleted(recId, false);
    CHECK(!findById(store.tasks(), recId)->completed, "un-complete restores task");

    // ── 3) 创建 / 更新 / 子任务 / 删除 ──
    store.createTask(QStringLiteral("自测任务"), 1, QDate::currentDate().toString(Qt::ISODate));
    qint64 newId = 0;
    for (const auto &t : store.tasks())
        if (t.title == QStringLiteral("自测任务")) { newId = t.id; break; }
    CHECK(newId != 0, "createTask works");
    store.addSubtask(newId, QStringLiteral("子步骤A"));
    {
        const TodoTask *t = findById(store.tasks(), newId);
        CHECK(t && t->subtasks.size() == 1, "addSubtask works");
        if (t && !t->subtasks.isEmpty())
            store.toggleSubtask(newId, t->subtasks.first().id);
    }
    CHECK(findById(store.tasks(), newId)->subtasks.first().completed, "toggleSubtask works");
    store.removeSubtask(newId, findById(store.tasks(), newId)->subtasks.first().id);
    CHECK(findById(store.tasks(), newId)->subtasks.isEmpty(), "removeSubtask works");
    store.deleteTask(newId);
    CHECK(findById(store.tasks(), newId) == nullptr, "deleteTask works");

    // ── 4) 删除清单 → 任务迁回收件箱 ──
    {
        const qint64 listId = store.lists().isEmpty() ? 1 : store.lists().first().id;
        int inList = 0;
        for (const auto &t : store.tasks())
            if (t.listId == listId) ++inList;
        store.createTask(QStringLiteral("待迁移"), listId, QString());
        store.deleteList(listId);
        bool moved = false;
        for (const auto &t : store.tasks())
            if (t.title == QStringLiteral("待迁移") && t.listId == 0) moved = true;
        CHECK(moved, "deleteList moves tasks to inbox");
        bool listGone = true;
        for (const auto &l : store.lists())
            if (l.id == listId) listGone = false;
        CHECK(listGone, "deleteList removes the list");
    }

    // ── 5) 持久化：新实例重新 load 读到同一批数据 ──
    {
        TodoStore store2;
        store2.load();
        CHECK(int(store2.tasks().size()) == int(store.tasks().size()), "persistence: reload same task count");
        CHECK(int(store2.lists().size()) == int(store.lists().size()), "persistence: reload same list count");
    }

    printf("\n%s: %d failed\n", g_fail == 0 ? "ALL PASS" : "FAILED", g_fail);
    return g_fail == 0 ? 0 : 1;
}
