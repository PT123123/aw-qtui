// localstore.cpp
#include "localstore.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QSaveFile>
#include <QStandardPaths>

namespace awqtui {

namespace {

QString nowIso()
{
    return QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs);
}

Note noteFromJson(const QJsonObject &o)
{
    Note n = Note::fromJson(o);
    n.pendingOp = o.value(QLatin1String("pending_op")).toString();
    n.commentParentId = o.value(QLatin1String("comment_parent_id")).toVariant().toLongLong();
    return n;
}

QJsonObject noteToJson(const Note &n)
{
    QJsonObject o;
    o.insert(QLatin1String("id"), n.id);
    o.insert(QLatin1String("content"), n.content);
    o.insert(QLatin1String("tags"), QJsonArray::fromStringList(n.tags));
    o.insert(QLatin1String("created_at"), n.createdAt);
    o.insert(QLatin1String("updated_at"), n.updatedAt);
    o.insert(QLatin1String("version"), n.version);
    o.insert(QLatin1String("device_id"), n.deviceId);
    o.insert(QLatin1String("deleted"), n.deleted);
    o.insert(QLatin1String("synced_at"), n.syncedAt);
    o.insert(QLatin1String("conflict"), n.conflict);
    o.insert(QLatin1String("pending_op"), n.pendingOp);
    o.insert(QLatin1String("comment_parent_id"), n.commentParentId);
    return o;
}

QJsonObject commentToJson(const Comment &c)
{
    QJsonObject o;
    o.insert(QLatin1String("id"), c.id);
    o.insert(QLatin1String("content"), c.content);
    o.insert(QLatin1String("created_at"), c.createdAt);
    o.insert(QLatin1String("pending"), c.pending);
    return o;
}

QJsonObject pendingCommentToJson(const PendingComment &c)
{
    QJsonObject o;
    o.insert(QLatin1String("note_id"), c.noteId);
    o.insert(QLatin1String("content"), c.content);
    o.insert(QLatin1String("created_at"), c.createdAt);
    return o;
}

} // namespace

LocalStore::LocalStore() = default;
LocalStore::~LocalStore() = default;

QString LocalStore::filePath()
{
    QString dir = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    if (dir.isEmpty())
        dir = QDir::homePath() + QStringLiteral("/.aw-qtui");
    QDir().mkpath(dir);
    return dir + QStringLiteral("/inbox_local.json");
}

bool LocalStore::load()
{
    QFile f(filePath());
    if (!f.open(QIODevice::ReadOnly))
        return false;
    const QByteArray raw = f.readAll();
    f.close();

    QJsonParseError pe;
    const QJsonDocument doc = QJsonDocument::fromJson(raw, &pe);
    if (pe.error != QJsonParseError::NoError || !doc.isObject())
        return false;

    m_notes.clear();
    const QJsonArray arr = doc.object().value(QLatin1String("notes")).toArray();
    for (const auto &v : arr) {
        if (v.isObject())
            m_notes << noteFromJson(v.toObject());
    }
    m_pinned.clear();
    const QJsonArray pinned = doc.object().value(QLatin1String("pinned")).toArray();
    for (const auto &v : pinned)
        m_pinned.insert(v.toVariant().toLongLong());

    // 评论缓存：{ "<noteId>": [Comment...] }
    m_comments.clear();
    const QJsonObject cm = doc.object().value(QLatin1String("comments")).toObject();
    for (auto it = cm.constBegin(); it != cm.constEnd(); ++it) {
        const qint64 noteId = it.key().toLongLong();
        QList<Comment> list;
        for (const auto &v : it.value().toArray()) {
            if (v.isObject())
                list << Comment::fromJson(v.toObject());
        }
        if (!list.isEmpty())
            m_comments.insert(noteId, list);
    }
    // 待同步评论队列
    m_pendingComments.clear();
    const QJsonArray pc = doc.object().value(QLatin1String("pending_comments")).toArray();
    for (const auto &v : pc) {
        const QJsonObject o = v.toObject();
        PendingComment c;
        c.noteId = o.value(QLatin1String("note_id")).toVariant().toLongLong();
        c.content = o.value(QLatin1String("content")).toString();
        c.createdAt = o.value(QLatin1String("created_at")).toString();
        if (!c.content.isEmpty())
            m_pendingComments << c;
    }
    return true;
}

void LocalStore::save() const
{
    QJsonObject root;
    root.insert(QLatin1String("schema"), 1);
    QJsonArray arr;
    for (const Note &n : m_notes)
        arr.append(noteToJson(n));
    root.insert(QLatin1String("notes"), arr);
    QJsonArray pinned;
    for (const qint64 id : m_pinned)
        pinned.append(id);
    root.insert(QLatin1String("pinned"), pinned);

    // 评论缓存：{ "<noteId>": [Comment...] }
    QJsonObject comments;
    for (auto it = m_comments.constBegin(); it != m_comments.constEnd(); ++it) {
        QJsonArray arr;
        for (const Comment &c : it.value())
            arr.append(commentToJson(c));
        comments.insert(QString::number(it.key()), arr);
    }
    root.insert(QLatin1String("comments"), comments);
    // 待同步评论队列
    QJsonArray pc;
    for (const PendingComment &c : m_pendingComments)
        pc.append(pendingCommentToJson(c));
    root.insert(QLatin1String("pending_comments"), pc);

    QSaveFile f(filePath());
    if (!f.open(QIODevice::WriteOnly))
        return;
    f.write(QJsonDocument(root).toJson(QJsonDocument::Indented));
    f.commit();
}

QList<Note> LocalStore::notes() const
{
    QList<Note> out;
    for (const Note &n : m_notes) {
        if (!n.deleted)
            out << n;
    }
    return out;
}

QList<Note> LocalStore::dirtyNotes() const
{
    QList<Note> out;
    for (const Note &n : m_notes) {
        // 评论笔记不在此推送：它们走评论待同步队列（POST /comments 保持 Comment 关系）
        if (n.commentParentId != 0)
            continue;
        if (!n.pendingOp.isEmpty())
            out << n;
    }
    return out;
}

int LocalStore::pendingCount() const
{
    int c = 0;
    for (const Note &n : m_notes) {
        // 评论笔记计入待同步评论队列（避免与 pendingComment 重复计数）
        if (n.commentParentId != 0)
            continue;
        if (!n.pendingOp.isEmpty())
            ++c;
    }
    c += m_pendingComments.size();
    return c;
}

const Note *LocalStore::find(qint64 id) const
{
    for (const Note &n : m_notes) {
        if (n.id == id)
            return &n;
    }
    return nullptr;
}

Note *LocalStore::mutableFind(qint64 id)
{
    for (Note &n : m_notes) {
        if (n.id == id)
            return &n;
    }
    return nullptr;
}

bool LocalStore::has(qint64 id) const
{
    return find(id) != nullptr;
}

bool LocalStore::isDirty(qint64 id) const
{
    const Note *n = find(id);
    return n && !n->pendingOp.isEmpty();
}

void LocalStore::applyServerNotes(const QList<Note> &server)
{
    for (const Note &s : server) {
        Note *local = mutableFind(s.id);
        if (local && !local->pendingOp.isEmpty())
            continue; // 有本地未同步改动，保留本地，避免覆盖
        if (local)
            *local = s; // 覆盖干净镜像
        else
            m_notes << s;
    }
    // 注意：不做“服务端没有就删本地”的清理——分页只拉到部分数据
}

qint64 LocalStore::nextLocalId() const
{
    qint64 id = -1;
    for (const Note &n : m_notes) {
        if (n.id < 0 && n.id < id)
            id = n.id;
    }
    return id - 1; // 保证唯一且不与已用 id 冲突
}

qint64 LocalStore::insertLocal(const QString &content, const QStringList &tags, const QString &deviceId)
{
    const qint64 id = nextLocalId();

    Note n;
    n.id = id;
    n.content = content;
    n.tags = tags;
    n.createdAt = nowIso();
    n.updatedAt = n.createdAt;
    n.deviceId = deviceId;
    n.pendingOp = QStringLiteral("create");
    m_notes << n;
    return id;
}

void LocalStore::updateLocal(qint64 id, const QString &content, const QStringList &tags)
{
    Note *n = mutableFind(id);
    if (!n)
        return;
    n->content = content;
    n->tags = tags;
    n->updatedAt = nowIso();
    if (n->pendingOp.isEmpty())
        n->pendingOp = QStringLiteral("update");
    // create 未推送时保持 create，推送时自然带上最新内容
}

void LocalStore::markDeleted(qint64 id)
{
    for (int i = 0; i < m_notes.size(); ++i) {
        if (m_notes[i].id != id)
            continue;
        Note &n = m_notes[i];
        if (n.pendingOp == QLatin1String("create")) {
            // 未同步的新建直接撤销，无需 tombstone
            const qint64 parentId = n.commentParentId;
            const QString content = n.content;
            const QString createdAt = n.createdAt;
            m_notes.removeAt(i);
            if (parentId != 0) {
                // 本地未同步的评论笔记被删：连带撤销对应评论缓存与待同步队列，避免补推已删评论
                removePendingComment(parentId, content, createdAt);
            }
            return;
        }
        n.deleted = true;
        n.pendingOp = QStringLiteral("delete");
        n.updatedAt = nowIso();
        return;
    }
}

void LocalStore::removePendingComment(qint64 noteId, const QString &content, const QString &createdAt)
{
    // 从缓存移除本地待同步版本（按内容 + 创建时间精确匹配）
    const auto old = m_comments.constFind(noteId);
    if (old != m_comments.constEnd()) {
        QList<Comment> list = old.value();
        for (int i = list.size() - 1; i >= 0; --i) {
            if (list[i].pending && list[i].content == content && list[i].createdAt == createdAt)
                list.removeAt(i);
        }
        m_comments.insert(noteId, list);
    }
    // 从待同步队列移除对应项
    for (int i = m_pendingComments.size() - 1; i >= 0; --i) {
        if (m_pendingComments[i].noteId == noteId && m_pendingComments[i].content == content
            && m_pendingComments[i].createdAt == createdAt)
            m_pendingComments.removeAt(i);
    }
}

void LocalStore::clearPending(qint64 id)
{
    for (int i = 0; i < m_notes.size(); ++i) {
        if (m_notes[i].id != id)
            continue;
        Note &n = m_notes[i];
        n.pendingOp.clear();
        if (n.deleted) // delete 推送成功，tombstone 可以移除
            m_notes.removeAt(i);
        return;
    }
}

void LocalStore::remapId(qint64 from, qint64 to)
{
    Note *n = mutableFind(from);
    if (n)
        n->id = to;
    // 待同步评论与评论缓存里的 note_id 跟随重映射（不依赖笔记是否找到）：
    // 离线给「尚未同步的新笔记」发的评论，笔记推送成功后必须指向新的服务端 id
    for (PendingComment &c : m_pendingComments) {
        if (c.noteId == from)
            c.noteId = to;
    }
    if (m_comments.contains(from)) {
        QList<Comment> moved = m_comments.take(from);
        if (m_comments.contains(to))
            m_comments[to] += moved;
        else
            m_comments.insert(to, moved);
    }
}

void LocalStore::drop(qint64 id)
{
    for (int i = 0; i < m_notes.size(); ++i) {
        if (m_notes[i].id == id) {
            m_notes.removeAt(i);
            break;
        }
    }
    m_pinned.remove(id);
    // 连带清理该笔记的评论缓存与待同步评论
    m_comments.remove(id);
    for (int i = m_pendingComments.size() - 1; i >= 0; --i) {
        if (m_pendingComments[i].noteId == id)
            m_pendingComments.removeAt(i);
    }
}

// ---- 本地置顶 ----
bool LocalStore::isPinned(qint64 id) const
{
    return m_pinned.contains(id);
}

QList<qint64> LocalStore::pinnedIds() const
{
    return m_pinned.values();
}

void LocalStore::setPinned(qint64 id, bool pinned)
{
    if (pinned)
        m_pinned.insert(id);
    else
        m_pinned.remove(id);
}

void LocalStore::clearPinned(qint64 id)
{
    m_pinned.remove(id);
}

// ---- 评论（离线优先） ----
QList<Comment> LocalStore::commentsFor(qint64 noteId) const
{
    QList<Comment> out;
    const auto it = m_comments.constFind(noteId);
    if (it != m_comments.constEnd())
        out = it.value();
    // 新在前（与服务端一致）
    std::sort(out.begin(), out.end(), [](const Comment &a, const Comment &b) {
        if (a.createdAt != b.createdAt)
            return a.createdAt > b.createdAt;
        return a.id > b.id;
    });
    return out;
}

void LocalStore::setComments(qint64 noteId, const QList<Comment> &server)
{
    // 保留本地待同步评论：它们尚未同步到服务端，服务端返回里必然没有
    QList<Comment> pending;
    const auto old = m_comments.constFind(noteId);
    if (old != m_comments.constEnd()) {
        for (const Comment &c : old.value()) {
            if (c.pending)
                pending << c;
        }
    }
    m_comments.insert(noteId, server);
    if (!pending.isEmpty())
        m_comments[noteId] += pending;
}

QString LocalStore::addLocalComment(qint64 noteId, const QString &content, const QString &deviceId)
{
    // 计算评论时间戳（保证严格递增，避免同毫秒内多条评论排序/转正匹配不稳定）
    QString ts = nowIso();
    const auto old = m_comments.constFind(noteId);
    if (old != m_comments.constEnd() && !old.value().isEmpty()) {
        const QDateTime last = QDateTime::fromString(old.value().last().createdAt, Qt::ISODateWithMs);
        QDateTime t = QDateTime::fromString(ts, Qt::ISODateWithMs);
        if (t <= last)
            t = last.addMSecs(1);
        ts = t.toString(Qt::ISODateWithMs);
    }

    // 1) 本地评论笔记（负 id 占位，收件箱立即可见；同步走评论端点补推并保持 Comment 关系）
    Note n;
    n.id = nextLocalId();
    n.content = content;
    n.tags.clear();
    n.createdAt = ts;
    n.updatedAt = ts;
    n.deviceId = deviceId;
    n.pendingOp = QStringLiteral("create");
    n.commentParentId = noteId;
    m_notes << n;

    // 2) 评论缓存（弹窗展示，标记待同步）
    Comment c;
    c.id = -1; // 本地占位 id
    c.content = content;
    c.createdAt = ts;
    c.pending = true;
    m_comments[noteId].append(c);

    // 3) 待同步评论队列
    PendingComment p;
    p.noteId = noteId;
    p.content = content;
    p.createdAt = ts;
    m_pendingComments << p;
    return ts;
}

void LocalStore::confirmComment(qint64 noteId, const QString &content, const QString &createdAt,
                                qint64 serverNoteId)
{
    // 只有拿到服务端确认（serverNoteId > 0）才算成功：把本地评论笔记转正（重映射 id 并清 pending），
    // 让它作为正式笔记留在收件箱；同时清掉缓存与待同步队列中的本地版本。
    // 未确认（如 4xx/连接失败）时什么都不做，评论继续保持「待同步」状态等待重试。
    if (serverNoteId <= 0)
        return;
    for (int i = 0; i < m_notes.size(); ++i) {
        Note &n = m_notes[i];
        if (n.commentParentId == noteId && n.createdAt == createdAt) {
            n.id = serverNoteId;
            n.commentParentId = 0;
            n.pendingOp.clear();
            break;
        }
    }
    removePendingComment(noteId, content, createdAt);
}

QList<PendingComment> LocalStore::pendingComments() const
{
    return m_pendingComments;
}

int LocalStore::pendingCommentCount() const
{
    return m_pendingComments.size();
}

} // namespace awqtui
