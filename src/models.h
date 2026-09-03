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
        // 若服务端/本地 JSON 携带父笔记 id（评论笔记引用被评论的笔记），一并读入
        n.commentParentId = o.value(QLatin1String("comment_parent_id")).toVariant().toLongLong();
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

// ── aw-sync-rust 局域网同步模型 ──

// SyncConfig: enabled, http_enabled, discovery_method, listen_port, udp_port, sync_inbox, sync_activity, sync_todo, self_alias, probe_interval
struct SyncConfig {
    bool enabled = false;
    bool httpEnabled = true;
    QString discoveryMethod;
    quint16 listenPort = 5600;
    quint16 udpPort = 46000;
    bool syncInbox = true;
    bool syncActivity = true;
    bool syncTodo = true;
    QString selfAlias;
    quint16 probeInterval = 10;

    static SyncConfig fromJson(const QJsonObject &o)
    {
        SyncConfig c;
        c.enabled = o.value(QLatin1String("enabled")).toBool();
        c.httpEnabled = o.value(QLatin1String("http_enabled")).toBool(true);
        c.discoveryMethod = o.value(QLatin1String("discovery_method")).toString();
        c.listenPort = o.value(QLatin1String("listen_port")).toInt(5600);
        c.udpPort = o.value(QLatin1String("udp_port")).toInt(46000);
        c.syncInbox = o.value(QLatin1String("sync_inbox")).toBool(true);
        c.syncActivity = o.value(QLatin1String("sync_activity")).toBool(true);
        c.syncTodo = o.value(QLatin1String("sync_todo")).toBool(true);
        c.selfAlias = o.value(QLatin1String("self_alias")).toString();
        c.probeInterval = o.value(QLatin1String("probe_interval")).toInt(10);
        return c;
    }

    QJsonObject toJson() const
    {
        QJsonObject o;
        o.insert(QStringLiteral("enabled"), enabled);
        o.insert(QStringLiteral("http_enabled"), httpEnabled);
        o.insert(QStringLiteral("discovery_method"), discoveryMethod);
        o.insert(QStringLiteral("listen_port"), listenPort);
        o.insert(QStringLiteral("udp_port"), udpPort);
        o.insert(QStringLiteral("sync_inbox"), syncInbox);
        o.insert(QStringLiteral("sync_activity"), syncActivity);
        o.insert(QStringLiteral("sync_todo"), syncTodo);
        o.insert(QStringLiteral("self_alias"), selfAlias);
        o.insert(QStringLiteral("probe_interval"), probeInterval);
        return o;
    }
};

// SyncDevice: id, name, device_kind, ip, port, paired_at, last_sync_at, last_seen_at, is_online, is_self, paired, alias
struct SyncDevice {
    QString id;
    QString name;
    QString deviceKind;
    QString ip;
    quint16 port = 5600;
    QString pairedAt;
    QString lastSyncAt;
    QString lastSeenAt;
    bool isOnline = false;
    bool isSelf = false;
    bool paired = false;
    QString alias;

    static SyncDevice fromJson(const QJsonObject &o)
    {
        SyncDevice d;
        d.id = o.value(QLatin1String("id")).toString();
        d.name = o.value(QLatin1String("name")).toString();
        d.deviceKind = o.value(QLatin1String("device_kind")).toString();
        d.ip = o.value(QLatin1String("ip")).toString();
        d.port = o.value(QLatin1String("port")).toInt(5600);
        d.pairedAt = o.value(QLatin1String("paired_at")).toString();
        d.lastSyncAt = o.value(QLatin1String("last_sync_at")).toString();
        d.lastSeenAt = o.value(QLatin1String("last_seen_at")).toString();
        d.isOnline = o.value(QLatin1String("is_online")).toBool();
        d.isSelf = o.value(QLatin1String("is_self")).toBool();
        d.paired = o.value(QLatin1String("paired")).toBool();
        d.alias = o.value(QLatin1String("alias")).toString();
        return d;
    }

    QString displayName() const
    {
        if (!alias.isEmpty())
            return alias;
        if (isSelf)
            return name + QStringLiteral("（本机）");
        return name;
    }
};

// DeviceSyncStats: device_id, pending_push_count, pending_conflict_count, total_synced_count, total_synced_size, local_note_count, remote_note_count, last_sync_at, last_full_sync_at, sync_frequency_minutes, last_error, last_error_at
struct SyncStats {
    QString deviceId;
    qint32 pendingPushCount = 0;
    qint32 pendingConflictCount = 0;
    qint64 totalSyncedCount = 0;
    qint64 totalSyncedSize = 0;
    qint32 localNoteCount = 0;
    qint32 remoteNoteCount = 0;
    QString lastSyncAt;
    QString lastFullSyncAt;
    qint32 syncFrequencyMinutes = 0;
    QString lastError;
    QString lastErrorAt;

    static SyncStats fromJson(const QJsonObject &o)
    {
        SyncStats s;
        s.deviceId = o.value(QLatin1String("device_id")).toString();
        s.pendingPushCount = o.value(QLatin1String("pending_push_count")).toInt();
        s.pendingConflictCount = o.value(QLatin1String("pending_conflict_count")).toInt();
        s.totalSyncedCount = o.value(QLatin1String("total_synced_count")).toVariant().toLongLong();
        s.totalSyncedSize = o.value(QLatin1String("total_synced_size")).toVariant().toLongLong();
        s.localNoteCount = o.value(QLatin1String("local_note_count")).toInt();
        s.remoteNoteCount = o.value(QLatin1String("remote_note_count")).toInt();
        s.lastSyncAt = o.value(QLatin1String("last_sync_at")).toString();
        s.lastFullSyncAt = o.value(QLatin1String("last_full_sync_at")).toString();
        s.syncFrequencyMinutes = o.value(QLatin1String("sync_frequency_minutes")).toInt();
        s.lastError = o.value(QLatin1String("last_error")).toString();
        s.lastErrorAt = o.value(QLatin1String("last_error_at")).toString();
        return s;
    }
};

// ApplyResult: applied, created, updated, ignored, archived, deleted, conflicts, errors
struct ApplyResult {
    qint32 applied = 0;
    qint32 created = 0;
    qint32 updated = 0;
    qint32 ignored = 0;
    qint32 archived = 0;
    qint32 deleted = 0;
    qint32 conflicts = 0;
    QStringList errors;

    static ApplyResult fromJson(const QJsonObject &o)
    {
        ApplyResult r;
        r.applied = o.value(QLatin1String("applied")).toInt();
        r.created = o.value(QLatin1String("created")).toInt();
        r.updated = o.value(QLatin1String("updated")).toInt();
        r.ignored = o.value(QLatin1String("ignored")).toInt();
        r.archived = o.value(QLatin1String("archived")).toInt();
        r.deleted = o.value(QLatin1String("deleted")).toInt();
        r.conflicts = o.value(QLatin1String("conflicts")).toInt();
        const auto errArr = o.value(QLatin1String("errors")).toArray();
        for (const auto &v : errArr)
            r.errors << v.toString();
        return r;
    }

    QString summary() const
    {
        return QStringLiteral("应用 %1 条（新增 %2 / 更新 %3 / 删除 %4），忽略 %5，归档 %6，错误 %7")
            .arg(applied)
            .arg(created)
            .arg(updated)
            .arg(deleted)
            .arg(ignored)
            .arg(archived)
            .arg(errors.size());
    }
};

// ConflictSummary: note_id, note_title, detected_at, resolved, resolution
struct ConflictInfo {
    qint64 noteId = 0;
    QString noteTitle;
    QString detectedAt;
    bool resolved = false;
    QString resolution;

    static ConflictInfo fromJson(const QJsonObject &o)
    {
        ConflictInfo c;
        c.noteId = o.value(QLatin1String("note_id")).toVariant().toLongLong();
        c.noteTitle = o.value(QLatin1String("note_title")).toString();
        c.detectedAt = o.value(QLatin1String("detected_at")).toString();
        c.resolved = o.value(QLatin1String("resolved")).toBool();
        c.resolution = o.value(QLatin1String("resolution")).toString();
        return c;
    }
};

// TrashEntry: id, kind, logical_key, archived, winner_rev, reason, source_device, archived_at, restored
struct TrashEntry {
    qint64 id = 0;
    QString kind;
    QString logicalKey;
    QString archived;
    QString winnerRev;
    QString reason;
    QString sourceDevice;
    QString archivedAt;
    bool restored = false;

    static TrashEntry fromJson(const QJsonObject &o)
    {
        TrashEntry t;
        t.id = o.value(QLatin1String("id")).toVariant().toLongLong();
        t.kind = o.value(QLatin1String("kind")).toString();
        t.logicalKey = o.value(QLatin1String("logical_key")).toString();
        t.archived = o.value(QLatin1String("archived")).toString();
        t.winnerRev = o.value(QLatin1String("winner_rev")).toString();
        t.reason = o.value(QLatin1String("reason")).toString();
        t.sourceDevice = o.value(QLatin1String("source_device")).toString();
        t.archivedAt = o.value(QLatin1String("archived_at")).toString();
        t.restored = o.value(QLatin1String("restored")).toBool();
        return t;
    }
};

// SyncLogEntry: id, timestamp, direction, protocol, peer_id, event_type, status, message, data_size
struct SyncLogEntry {
    qint64 id = 0;
    QString timestamp;
    QString direction;
    QString protocol;
    QString peerId;
    QString eventType;
    QString status;
    QString message;
    qint64 dataSize = 0;

    static SyncLogEntry fromJson(const QJsonObject &o)
    {
        SyncLogEntry e;
        e.id = o.value(QLatin1String("id")).toVariant().toLongLong();
        e.timestamp = o.value(QLatin1String("timestamp")).toString();
        e.direction = o.value(QLatin1String("direction")).toString();
        e.protocol = o.value(QLatin1String("protocol")).toString();
        e.peerId = o.value(QLatin1String("peer_id")).toString();
        e.eventType = o.value(QLatin1String("event_type")).toString();
        e.status = o.value(QLatin1String("status")).toString();
        e.message = o.value(QLatin1String("message")).toString();
        e.dataSize = o.value(QLatin1String("data_size")).toVariant().toLongLong();
        return e;
    }
};

} // namespace awqtui
