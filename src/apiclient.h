// apiclient.h —— Inbox / 同步 REST 客户端（QNetworkAccessManager，异步）
#pragma once

#include <QByteArray>
#include <QJsonDocument>
#include <QNetworkReply>
#include <QObject>
#include <QString>

class QNetworkAccessManager;

namespace awqtui {

class ApiClient : public QObject
{
    Q_OBJECT
public:
    explicit ApiClient(QObject *parent = nullptr);
    ~ApiClient() override;

    void setBaseUrl(const QString &url) { m_baseUrl = url; }
    QString baseUrl() const { return m_baseUrl; }
    QString deviceId() const { return m_deviceId; }

    // 笔记 CRUD
    QNetworkReply *getNotes(int limit, int offset, const QString &tag, const QString &search,
                            const QString &sortBy);
    QNetworkReply *getNote(qint64 id);
    QNetworkReply *createNote(const QString &content, const QStringList &tags);
    QNetworkReply *updateNote(qint64 id, const QString &content, const QStringList &tags);
    QNetworkReply *deleteNote(qint64 id);
    // 历史版本：GET /inbox/notes/<id>/history（每次更新前的内容快照，按版本倒序）
    QNetworkReply *getNoteHistory(qint64 id);
    // 恢复软删除的笔记：PUT /inbox/notes/<id>/restore
    QNetworkReply *restoreNote(qint64 id);

    // 标签
    QNetworkReply *getTags();
    QNetworkReply *getDetailedTags();

    // 评论
    QNetworkReply *getComments(qint64 noteId);
    QNetworkReply *addComment(qint64 noteId, const QString &content);

    // 关系（GET /inbox/notes/<id>/relations；POST /inbox/notes/<src>/relations/<tgt>）
    QNetworkReply *getRelations(qint64 noteId);
    QNetworkReply *createRelation(qint64 sourceId, qint64 targetId, const QString &relationType);

    // ── 局域网同步 (aw-sync-rust /api/0/sync) ──
    QNetworkReply *getSyncInfo();                                         // GET /info
    QNetworkReply *getSyncConfig();                                       // GET /config
    QNetworkReply *setSyncConfig(const QJsonObject &cfg);                 // PUT /config
    QNetworkReply *createPairCode();                                      // POST /paircode
    QNetworkReply *joinWithCode(const QString &code, const QJsonObject &device); // POST /join
    QNetworkReply *addDevice(const QJsonObject &device);                  // POST /devices
    QNetworkReply *initiatePair(const QString &deviceId);                 // POST /pair/initiate
    QNetworkReply *acceptPair(const QString &deviceId);                   // POST /pair/accept
    QNetworkReply *getSyncDevices();                                      // GET /devices
    QNetworkReply *triggerSync(const QString &deviceId);                  // POST /devices/<id>/sync
    QNetworkReply *removeDevice(const QString &deviceId);                 // DELETE /devices/<id>
    QNetworkReply *clearAllDevices();                                     // DELETE /devices/all（清空所有配对信息）
    QNetworkReply *setDeviceAlias(const QString &deviceId, const QString &alias); // PUT /devices/<id>/alias
    QNetworkReply *getDeviceStats(const QString &deviceId);               // GET /devices/<id>/stats
    QNetworkReply *getDeviceConflicts(const QString &deviceId);           // GET /devices/<id>/conflicts
    QNetworkReply *getSyncLogs(const QString &direction = QString(),      // GET /log?<query>
                               const QString &protocol = QString(),
                               const QString &eventType = QString(),
                               int limit = 200, int offset = 0);
    QNetworkReply *clearSyncLogs();                                       // DELETE /log
    QNetworkReply *getSyncSnapshot();                                     // GET /snapshot
    QNetworkReply *applySnapshot(const QJsonObject &snap);                // POST /apply
    QNetworkReply *pushSnapshot(const QJsonObject &snap);                 // POST /push
    QNetworkReply *getSyncStatus();                                       // GET /status
    QNetworkReply *getTrash(const QString &kind = QString());             // GET /trash
    QNetworkReply *restoreTrash(qint64 id);                               // POST /trash/<id>/restore
    QNetworkReply *deleteTrash(qint64 id);                                // DELETE /trash/<id>
    QNetworkReply *clearAllTrash();                                       // DELETE /trash

    // ActivityWatch /api/0
    QNetworkReply *getBuckets();
    QNetworkReply *getEvents(const QString &bucketId, qint64 startMs, qint64 endMs);
    QNetworkReply *createBucket(const QString &bucketId, const QString &client, const QString &type);
    QNetworkReply *heartbeat(const QString &bucketId, const QJsonObject &data, double durationSec,
                              const QDateTime &timestamp);

    // Inbox Todo (/inbox/todos)
    QNetworkReply *getTodos(bool includeCompleted = false);
    QNetworkReply *getTodo(qint64 id);
    QNetworkReply *createTodo(const QString &title, const QString &content = QString(),
                              const QStringList &tags = {});
    QNetworkReply *updateTodo(qint64 id, const QJsonObject &patch);
    QNetworkReply *deleteTodo(qint64 id);
    // 恢复软删除的任务：PUT /inbox/todos/<id>/restore
    QNetworkReply *restoreTodo(qint64 id);

    // 解析回复：ok=true 且 doc 有效 -> 成功；否则 err 为错误描述
    static bool parseReply(QNetworkReply *reply, QJsonDocument *doc, QString *err);

    QNetworkAccessManager *networkAccessManager() const { return m_nam; }

private:
    QNetworkRequest makeRequest(const QString &path) const;
    QNetworkReply *get(const QString &path);
    QNetworkReply *sendJson(const QByteArray &method, const QString &path, const QJsonObject &body);

    QNetworkAccessManager *m_nam;
    QString m_baseUrl;
    QString m_deviceId;
};

} // namespace awqtui
