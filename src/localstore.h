// localstore.h —— 收件箱本地存储（离线优先）
//
// 目标：即使 aw-inbox 服务端不在，收件箱的新建/编辑/删除也能即时生效并持久化，
// 待服务端恢复后自动补推（eventually sync）。
//
// 数据约定：
//   - 持久化文件：AppDataLocation/inbox_local.json（与 device_id 同目录）
//   - 服务端 id 恒为正数；本地新建尚未同步的笔记用【负数】占位 id
//   - pendingOp 标记待同步操作："create" / "update" / "delete"，空串表示已与服务端一致
//   - 删除不直接删掉记录：标记 deleted=true + op="delete" 作为 tombstone，推送成功后清除
#pragma once

#include <QList>
#include <QMap>
#include <QSet>
#include <QString>
#include <QStringList>

#include "models.h"

namespace awqtui {

class LocalStore
{
public:
    LocalStore();
    ~LocalStore();

    // 从磁盘加载，失败返回 false（首次运行 / 文件损坏都视为空库）
    bool load();
    // 写回磁盘（QSaveFile 原子写入）
    void save() const;

    // 未删除的全部笔记（含本地待同步），按本地加入顺序
    QList<Note> notes() const;
    // 已软删除的笔记（tombstone，待同步 delete 或推送后清理）
    QList<Note> deletedNotes() const;
    // 需要推送的改动（含已删除的 tombstone）
    QList<Note> dirtyNotes() const;
    int pendingCount() const;

    const Note *find(qint64 id) const;
    bool has(qint64 id) const;
    bool isDirty(qint64 id) const;

    // 服务端成功拉取后调用：用服务端状态覆盖干净副本，保留本地脏改动
    void applyServerNotes(const QList<Note> &server);

    // 本地新建：分配负 id 占位，op=create，返回分配的 id
    qint64 insertLocal(const QString &content, const QStringList &tags, const QString &deviceId);
    // 本地编辑：若原本是未同步新建则保持 create，否则置 op=update
    void updateLocal(qint64 id, const QString &content, const QStringList &tags);
    // 本地删除：未同步的新建直接移除；服务端笔记标记 tombstone + op=delete
    void markDeleted(qint64 id);
    // 恢复已软删除的笔记（清掉 deleted + op，标记 update 以便重新同步为未删除）
    void undelete(qint64 id);

    // 推送成功后的收尾
    void clearPending(qint64 id);         // op 清空；若是 delete tombstone 则直接移除
    void remapId(qint64 from, qint64 to); // create 推送成功后 负id -> 服务端id
    void drop(qint64 id);                 // 彻底移除（如在线删除成功）

    // 本地置顶（纯客户端字段，不同步服务端；独立持久化避免被 applyServerNotes 覆盖）
    bool isPinned(qint64 id) const;
    QList<qint64> pinnedIds() const;
    void setPinned(qint64 id, bool pinned);
    void clearPinned(qint64 id);

    // ---- 评论（离线优先，与笔记同一套本地优先策略） ----
    // 某笔记的全部评论缓存（含本地待同步评论，新在前）
    QList<Comment> commentsFor(qint64 noteId) const;
    // 用服务端评论覆盖缓存，但保留本地待同步评论（服务端必然还没有它们）
    void setComments(qint64 noteId, const QList<Comment> &server);
    // 本地新建评论：同时创建一条本地评论笔记（收件箱可见）+ 写入评论缓存 + 入待同步队列，
    // 返回创建时间戳（严格递增，作为后续转正匹配的唯一键）
    QString addLocalComment(qint64 noteId, const QString &content, const QString &deviceId);
    // 评论推送成功后：从缓存与待同步队列移除对应项；
    // serverNoteId > 0 时把本地评论笔记重映射到服务端 id 并清除 pending（转正为正式笔记）
    void confirmComment(qint64 noteId, const QString &content, const QString &createdAt,
                        qint64 serverNoteId = 0);
    // 待同步评论队列
    QList<PendingComment> pendingComments() const;
    int pendingCommentCount() const;

private:
    static QString filePath();
    Note *mutableFind(qint64 id);
    // 分配本地占位 id（严格递减的负数，保证唯一且不与服务端正数 id 冲突）
    qint64 nextLocalId() const;
    // 移除评论缓存与待同步队列中的本地待同步版本（按内容 + 创建时间精确匹配）
    void removePendingComment(qint64 noteId, const QString &content, const QString &createdAt);

    QList<Note> m_notes;
    QSet<qint64> m_pinned;
    QMap<qint64, QList<Comment>> m_comments; // noteId -> 评论缓存
    QList<PendingComment> m_pendingComments; // 待同步评论队列
};

} // namespace awqtui
