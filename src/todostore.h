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

#include <QList>
#include <QObject>

#include "todomodels.h"

namespace awqtui {

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

private:
    qint64 nextId();
    TodoTask *mutableTask(qint64 id);
    const TodoTask *findTask(qint64 id) const;
    TodoList *mutableList(qint64 id);
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

} // namespace awqtui
