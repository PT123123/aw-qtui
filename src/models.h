// models.h —— 数据模型，字段对齐 aw-inbox/src/models.rs 与 aw-inbox-datastore
#pragma once

#include <QJsonArray>
#include <QJsonObject>
#include <QString>
#include <QStringList>

namespace awqtui {

// NoteResponse: id, content, tags, created_at, updated_at, version, device_id, deleted, synced_at, conflict
struct Note {
    qint64 id = 0;
    QString content;
    QStringList tags;
    QString createdAt;
    QString updatedAt;
    qint64 version = 0;
    QString deviceId;
    bool deleted = false;
    QString syncedAt;
    bool conflict = false;
    // 本地离线存储专用：待同步操作（"" 干净 / create / update / delete），服务端无此字段
    QString pendingOp;
    // 本地置顶标记（纯客户端，不同步服务端；由 InboxPage 从 LocalStore 注入）
    bool pinned = false;
    // 本地离线存储专用：>0 表示这条本地笔记是一条「评论笔记」，指向被评论的父笔记 id。
    // 服务端把评论也建模为笔记（notes 表 + Comment 关系），会出现在 GET /inbox/notes 中；
    // 本地新建评论同步走评论端点（POST /inbox/notes/<父id>/comments），不走普通笔记 create。
    qint64 commentParentId = 0;

    static Note fromJson(const QJsonObject &o)
    {
        Note n;
        n.id = o.value(QLatin1String("id")).toVariant().toLongLong();
        n.content = o.value(QLatin1String("content")).toString();
        const auto t = o.value(QLatin1String("tags")).toArray();
        for (const auto &v : t)
            n.tags << v.toString();
        n.createdAt = o.value(QLatin1String("created_at")).toString();
        n.updatedAt = o.value(QLatin1String("updated_at")).toString();
        n.version = o.value(QLatin1String("version")).toVariant().toLongLong();
        n.deviceId = o.value(QLatin1String("device_id")).toString();
        n.deleted = o.value(QLatin1String("deleted")).toBool();
        n.syncedAt = o.value(QLatin1String("synced_at")).toString();
        n.conflict = o.value(QLatin1String("conflict")).toBool();
        n.pendingOp = o.value(QLatin1String("pending_op")).toString();
        return n;
    }
};

// DetailedTag: name, count, last_modified
struct DetailedTag {
    QString name;
    qint64 count = 1;
    QString lastModified;

    static DetailedTag fromJson(const QJsonObject &o)
    {
        DetailedTag t;
        t.name = o.value(QLatin1String("name")).toString();
        if (t.name.isEmpty())
            t.name = o.value(QLatin1String("tag")).toString();
        t.count = o.value(QLatin1String("count")).toVariant().toLongLong();
        t.count = t.count ? t.count : 1;
        t.lastModified = o.value(QLatin1String("last_modified")).toString();
        return t;
    }
};

// 评论（服务端把评论也按 Note 返回）
struct Comment {
    qint64 id = 0;
    QString content;
    QString createdAt;
    // 本地离线存储专用：true 表示本条是本地新建、尚未同步到服务端
    bool pending = false;

    static Comment fromJson(const QJsonObject &o)
    {
        Comment c;
        c.id = o.value(QLatin1String("id")).toVariant().toLongLong();
        c.content = o.value(QLatin1String("content")).toString();
        c.createdAt = o.value(QLatin1String("created_at")).toString();
        c.pending = o.value(QLatin1String("pending")).toBool();
        return c;
    }
};

// 待同步评论（离线时新建的评论，重连后逐条补推 POST）
struct PendingComment {
    qint64 noteId = 0;
    QString content;
    QString createdAt;
};

// DeviceInfo: device_id, name, platform, last_seen_at, last_synced_at, pending_changes, version, is_current, status
struct DeviceInfo {
    QString deviceId;
    QString name;
    QString platform;
    QString lastSeenAt;
    QString lastSyncedAt;
    qint64 pendingChanges = 0;
    qint64 version = 0;
    bool isCurrent = false;
    QString status;

    static DeviceInfo fromJson(const QJsonObject &o)
    {
        DeviceInfo d;
        d.deviceId = o.value(QLatin1String("device_id")).toString();
        d.name = o.value(QLatin1String("name")).toString();
        d.platform = o.value(QLatin1String("platform")).toString();
        d.lastSeenAt = o.value(QLatin1String("last_seen_at")).toString();
        d.lastSyncedAt = o.value(QLatin1String("last_synced_at")).toString();
        d.pendingChanges = o.value(QLatin1String("pending_changes")).toVariant().toLongLong();
        d.version = o.value(QLatin1String("version")).toVariant().toLongLong();
        d.isCurrent = o.value(QLatin1String("is_current")).toBool();
        d.status = o.value(QLatin1String("status")).toString();
        return d;
    }
};

// 同步结果摘要（SyncResponse 的精简视图）
struct SyncSummary {
    qint64 currentVersion = 0;
    int pulledCount = 0;
    bool hasMore = false;
    int conflictCount = 0;
    QJsonArray conflicts;
    QJsonArray pushResults;

    static SyncSummary fromJson(const QJsonObject &o)
    {
        SyncSummary s;
        s.currentVersion = o.value(QLatin1String("current_version")).toVariant().toLongLong();
        s.pulledCount = o.value(QLatin1String("pulled_notes")).toArray().size();
        s.hasMore = o.value(QLatin1String("has_more")).toBool();
        s.conflicts = o.value(QLatin1String("conflicts")).toArray();
        s.conflictCount = s.conflicts.size();
        s.pushResults = o.value(QLatin1String("push_results")).toArray();
        return s;
    }
};

} // namespace awqtui
