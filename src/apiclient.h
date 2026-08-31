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

    // 标签
    QNetworkReply *getTags();
    QNetworkReply *getDetailedTags();

    // 评论
    QNetworkReply *getComments(qint64 noteId);
    QNetworkReply *addComment(qint64 noteId, const QString &content);

    // 局域网同步
    QNetworkReply *sync(const QJsonObject &payload);
    QNetworkReply *getSyncDevices();
    QNetworkReply *deviceHeartbeat(const QString &name, const QString &platform, qint64 pending,
                                   qint64 localVersion);

    // 解析回复：ok=true 且 doc 有效 -> 成功；否则 err 为错误描述
    static bool parseReply(QNetworkReply *reply, QJsonDocument *doc, QString *err);

private:
    QNetworkRequest makeRequest(const QString &path) const;
    QNetworkReply *get(const QString &path);
    QNetworkReply *sendJson(const QByteArray &method, const QString &path, const QJsonObject &body);

    QNetworkAccessManager *m_nam;
    QString m_baseUrl;
    QString m_deviceId;
};

} // namespace awqtui
