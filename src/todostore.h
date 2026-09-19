// todostore.h —— Todo 数据源抽象 + 本地 mock 实现
//
// 设计目标：Todo 页只依赖 TodoSource 抽象；当前 TodoStore 是本地 mock
// （内存 + todo_local.json 持久化 + 首次运行种子数据）。
// 写操作全部「改内存 → 持久化 → 广播 dataChanged」异步风格（QTimer 后投递），
// 匹配未来 Rust 服务端 QNetworkAccessManager 的异步回包方式。
//
// 接入 Rust 时：新增 class TodoApiStore : public TodoSource，用 HTTP 实现同一套
// 方法并把服务端响应转成 dataChanged 信号，Todo 页代码零改动。
#pragma once

#include <functional>
#include <QList>
#include <QObject>

#include "todomodels.h"

class QNetworkReply;

namespace awqtui {

class ApiClient;

class TodoSource : public QObject
{
    Q_OBJECT
public:
    explicit TodoSource(QObject *parent = nullptr) : QObject(parent) {}
    ~TodoSource() override = default;

    // 初始加载；加载完成或已有数据后发 dataChanged
    virtual void load() = 0;
    virtual bool ready() const = 0;

    // 快照查询（来源内部已加载；调用方在收到 dataChanged 后再读取）
    virtual QList<TodoList> lists() const = 0;
    virtual QList<TodoTask> tasks() const = 0;

    // 能力开关：服务端无 recurrence 字段（Android 端 supportsRecurrence=false 同款语义）
    virtual bool supportsRecurrence() const { return true; }
    // 能力开关：能否清除截止日期。服务端 due_date 为 Option（省略 = 保留原值），清空不生效
    virtual bool supportsDueClear() const { return true; }

    // 写操作（异步；生效后发 dataChanged）
    virtual void createList(const QString &name, const QString &color) = 0;
    virtual void renameList(qint64 listId, const QString &name) = 0;
    virtual void deleteList(qint64 listId) = 0;
    virtual void createTask(const QString &title, qint64 listId, const QString &dueDate) = 0;
    virtual void updateTask(const TodoTask &task) = 0;
    virtual void setTaskCompleted(qint64 taskId, bool completed) = 0;
    virtual void deleteTask(qint64 taskId) = 0;
    virtual void addSubtask(qint64 taskId, const QString &title) = 0;
    virtual void toggleSubtask(qint64 taskId, qint64 subtaskId) = 0;
    virtual void removeSubtask(qint64 taskId, qint64 subtaskId) = 0;

    // 批量操作（多选模式）：基类默认逐条转发到上面的单项接口；
    // 本地实现覆盖为「改完内存只提交一次」，避免 N 次广播 dataChanged。
    // dueDate 传空串表示清除截止日期（API 源受服务端 Option 语义限制，清除不生效）。
    virtual void setTasksCompleted(const QList<qint64> &taskIds, bool completed);
    virtual void deleteTasks(const QList<qint64> &taskIds);
    virtual void moveTasks(const QList<qint64> &taskIds, qint64 listId);
    virtual void setTasksPriority(const QList<qint64> &taskIds, int priority);
    virtual void setTasksDueDate(const QList<qint64> &taskIds, const QString &dueDate);

signals:
    void dataChanged();
};

// ── 本地 mock 实现（离线可用，数据存 %APPDATA%\<app>\todo_local.json） ──
class TodoStore : public TodoSource
{
    Q_OBJECT
public:
    explicit TodoStore(QObject *parent = nullptr);
    ~TodoStore() override = default;

    void load() override;
    bool ready() const override { return m_loaded; }

    QList<TodoList> lists() const override { return m_lists; }
    QList<TodoTask> tasks() const override { return m_tasks; }

    void createList(const QString &name, const QString &color) override;
    void renameList(qint64 listId, const QString &name) override;
    void deleteList(qint64 listId) override;
    void createTask(const QString &title, qint64 listId, const QString &dueDate) override;
    void updateTask(const TodoTask &task) override;
    void setTaskCompleted(qint64 taskId, bool completed) override;
    void deleteTask(qint64 taskId) override;
    void addSubtask(qint64 taskId, const QString &title) override;
    void toggleSubtask(qint64 taskId, qint64 subtaskId) override;
    void removeSubtask(qint64 taskId, qint64 subtaskId) override;

    // 批量：一次改完内存只 commit 一次
    void setTasksCompleted(const QList<qint64> &taskIds, bool completed) override;
    void deleteTasks(const QList<qint64> &taskIds) override;
    void moveTasks(const QList<qint64> &taskIds, qint64 listId) override;
    void setTasksPriority(const QList<qint64> &taskIds, int priority) override;
    void setTasksDueDate(const QList<qint64> &taskIds, const QString &dueDate) override;

private:
    qint64 nextId();
    TodoTask *mutableTask(qint64 id);
    const TodoTask *findTask(qint64 id) const;
    TodoList *mutableList(qint64 id);
    // 单任务完成状态切换（含重复任务生成下一实例）；不落盘、不广播，由调用方 commit
    void applyComplete(qint64 taskId, bool completed);
    // 计算重复任务的下一发生日期（basedOn 为空则取今天）；无法推进返回空串
    static QString nextRecurrenceDate(const QString &rule, const QString &basedOn);
    void seed();
    void save() const;         // 原子写回磁盘
    void commit();             // 持久化 + 异步广播 dataChanged

    QList<TodoList> m_lists;
    QList<TodoTask> m_tasks;
    qint64 m_nextId = 1;
    bool m_loaded = false;

    static QString filePath();
};

// ── Rust 服务端实现（/inbox/todos + /inbox/todo-lists；服务端 7116825 起
//    清单为独立实体、任务以 list_id 关联、子任务存 todos.subtasks JSON 列，
//    与 Android RestTodoSource 同一契约。重复规则服务端不支持，UI 不展示） ──
class TodoApiStore : public TodoSource
{
    Q_OBJECT
public:
    explicit TodoApiStore(ApiClient *api, QObject *parent = nullptr);
    ~TodoApiStore() override = default;

    void load() override;
    bool ready() const override { return m_loaded; }
    bool supportsRecurrence() const override { return false; }
    bool supportsDueClear() const override { return false; }

    QList<TodoList> lists() const override { return m_lists; }
    QList<TodoTask> tasks() const override { return m_tasks; }

    void createList(const QString &name, const QString &color) override;
    void renameList(qint64 listId, const QString &name) override;
    void deleteList(qint64 listId) override;
    void createTask(const QString &title, qint64 listId, const QString &dueDate) override;
    void updateTask(const TodoTask &task) override;
    void setTaskCompleted(qint64 taskId, bool completed) override;
    void deleteTask(qint64 taskId) override;
    void addSubtask(qint64 taskId, const QString &title) override;
    void toggleSubtask(qint64 taskId, qint64 subtaskId) override;
    void removeSubtask(qint64 taskId, qint64 subtaskId) override;

    // 批量：并发发 N 个请求，全部回包后只 reload 一次（否则 N 次整体刷新会闪）
    void setTasksCompleted(const QList<qint64> &taskIds, bool completed) override;
    void deleteTasks(const QList<qint64> &taskIds) override;
    void moveTasks(const QList<qint64> &taskIds, qint64 listId) override;
    void setTasksPriority(const QList<qint64> &taskIds, int priority) override;
    void setTasksDueDate(const QList<qint64> &taskIds, const QString &dueDate) override;

private:
    static TodoTask todoToTask(const QJsonObject &o);
    // gen = 本轮拉取的代际号：回包落地前先比对，过期轮次的结果整体丢弃（看门狗放行后迟到的回包）
    void fetchTodos(int gen);
    void fetchLists(int gen);
    // 一轮的两个回包都到齐后收尾：悬空清单归位 + 只广播一次 dataChanged
    void finishFetch();
    // 子任务整组读改写（对齐 Android mutateSubtasks）
    void mutateSubtasks(qint64 taskId, const std::function<void(QList<TodoSubtask> &)> &fn);
    // 全局唯一子任务 id：所有任务已有子任务 id 的最大值 + 1（对齐 Android nextSubtaskId）
    qint64 nextSubtaskId() const;
    // 对一批任务各发一个请求，全部 finished 后只 reload 一次
    void batchRequests(const QList<qint64> &taskIds,
                       const std::function<QNetworkReply *(qint64 taskId)> &makeRequest);
    void reload();

    ApiClient *m_api = nullptr;
    QList<TodoList> m_lists;
    QList<TodoTask> m_tasks;
    bool m_loaded = false;
    // 拉取轮次状态：在途不叠加请求（15s 远端轮询 × 写后重拉会叠加），收尾后按需补做一轮
    int m_fetchGen = 0;
    int m_fetchInflight = 0;
    bool m_fetchQueued = false;
    bool m_listsFresh = false;   // 本轮清单是否成功回来，决定要不要做悬空 list_id 归位
};

} // namespace awqtui
