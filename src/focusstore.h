// focusstore.h —— 专注数据源抽象 + 本地实现
//
// 专注页（计时/专注记录/专注记录详情/专注时间线/热力图/最佳专注时间/
// 日历/倒数纪念日）只依赖 FocusSource 抽象。当前 FocusStore 是本地实现
// （内存 + focus_local.json 原子持久化），写操作「改内存 → 持久化 →
// 广播 dataChanged」异步风格，匹配未来 Rust 服务端 QNetworkAccessManager
// 的异步回包方式，保证离线优先（类似 Inbox 的本地缓存 + 待同步队列）。
//
// 接入 Rust：新增 class FocusApiStore : public FocusSource，用 HTTP 实现
// 同一套方法并把服务端响应转成 dataChanged 信号，页面零改动。
// （建议契约：GET/POST /focus/sessions、DELETE /focus/sessions/<id>、
//  GET/POST /focus/memorials、DELETE /focus/memorials/<id>，字段见 focusmodels.h）
#pragma once

#include <QList>
#include <QObject>

#include "focusmodels.h"

namespace awqtui {

class ApiClient; // 预留（FocusApiStore 用）

class FocusSource : public QObject
{
    Q_OBJECT
public:
    explicit FocusSource(QObject *parent = nullptr) : QObject(parent) {}
    ~FocusSource() override = default;

    virtual void load() = 0;
    virtual bool ready() const = 0;

    // 快照查询（来源内部已加载；调用方在收到 dataChanged 后再读取）
    virtual QList<FocusSession> sessions() const = 0;
    virtual QList<MemorialDay> memorials() const = 0;

    // 写操作（异步；生效后发 dataChanged）
    virtual void addSession(int kind, qint64 startMs, qint64 endMs, qint64 durationSec,
                            const QString &eventName, qint64 taskId) = 0;
    virtual void deleteSession(qint64 id) = 0;
    virtual void addMemorial(const QString &name, const QString &emoji, const QString &dateIso) = 0;
    virtual void deleteMemorial(qint64 id) = 0;

signals:
    void dataChanged();
};

// ── 本地实现（离线可用，数据存 %APPDATA%\<app>\focus_local.json） ──
class FocusStore : public FocusSource
{
    Q_OBJECT
public:
    explicit FocusStore(QObject *parent = nullptr);
    ~FocusStore() override = default;

    void load() override;
    bool ready() const override { return m_loaded; }

    QList<FocusSession> sessions() const override { return m_sessions; }
    QList<MemorialDay> memorials() const override { return m_memorials; }

    void addSession(int kind, qint64 startMs, qint64 endMs, qint64 durationSec,
                    const QString &eventName, qint64 taskId) override;
    void deleteSession(qint64 id) override;
    void addMemorial(const QString &name, const QString &emoji, const QString &dateIso) override;
    void deleteMemorial(qint64 id) override;

private:
    qint64 nextId();
    void seed();
    void save() const;  // 原子写回磁盘
    void commit();      // 持久化 + 异步广播 dataChanged

    QList<FocusSession> m_sessions;
    QList<MemorialDay> m_memorials;
    qint64 m_nextId = 1;
    bool m_loaded = false;

    static QString filePath();
};

} // namespace awqtui
