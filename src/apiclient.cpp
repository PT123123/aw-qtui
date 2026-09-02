// apiclient.cpp
#include "apiclient.h"

#include "config.h"

#include <QJsonArray>
#include <QJsonObject>
#include <QJsonParseError>
#include <QNetworkAccessManager>
#include <QNetworkRequest>
#include <QUrl>
#include <QUrlQuery>

namespace awqtui {

ApiClient::ApiClient(QObject *parent)
    : QObject(parent), m_nam(new QNetworkAccessManager(this)), m_baseUrl(kDefaultServerUrl),
      m_deviceId(awqtui::deviceId())
{
}

ApiClient::~ApiClient() = default;

QNetworkRequest ApiClient::makeRequest(const QString &path) const
{
    QNetworkRequest req(QUrl(m_baseUrl + path));
    req.setHeader(QNetworkRequest::UserAgentHeader, QStringLiteral("aw-qtui/0.1"));
    req.setRawHeader("X-Device-ID", m_deviceId.toUtf8());
    req.setRawHeader("Accept", "application/json");
    return req;
}

QNetworkReply *ApiClient::get(const QString &path)
{
    return m_nam->get(makeRequest(path));
}

QNetworkReply *ApiClient::sendJson(const QByteArray &method, const QString &path, const QJsonObject &body)
{
    QNetworkRequest req = makeRequest(path);
    req.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));
    const QByteArray data = QJsonDocument(body).toJson(QJsonDocument::Compact);
    return m_nam->sendCustomRequest(req, method, data);
}

// ------------------------------------------------------------------ //

QNetworkReply *ApiClient::getNotes(int limit, int offset, const QString &tag, const QString &search,
                                   const QString &sortBy)
{
    QUrlQuery q;
    q.addQueryItem(QStringLiteral("limit"), QString::number(limit));
    q.addQueryItem(QStringLiteral("offset"), QString::number(offset));
    if (!tag.isEmpty())
        q.addQueryItem(QStringLiteral("tag"), tag);
    if (!search.isEmpty())
        q.addQueryItem(QStringLiteral("search"), search);
    if (!sortBy.isEmpty())
        q.addQueryItem(QStringLiteral("sort_by"), sortBy);
    const QString query = q.toString(QUrl::FullyEncoded);
    QString path = QStringLiteral("/inbox/notes");
    if (!query.isEmpty())
        path += QLatin1Char('?') + query;
    return get(path);
}

QNetworkReply *ApiClient::getNote(qint64 id)
{
    return get(QStringLiteral("/inbox/notes/%1").arg(id));
}

QNetworkReply *ApiClient::createNote(const QString &content, const QStringList &tags)
{
    QJsonObject body;
    body.insert(QStringLiteral("content"), content);
    body.insert(QStringLiteral("tags"), QJsonArray::fromStringList(tags));
    return sendJson("POST", QStringLiteral("/inbox/notes"), body);
}

QNetworkReply *ApiClient::updateNote(qint64 id, const QString &content, const QStringList &tags)
{
    QJsonObject body;
    body.insert(QStringLiteral("content"), content);
    body.insert(QStringLiteral("tags"), QJsonArray::fromStringList(tags));
    return sendJson("PUT", QStringLiteral("/inbox/notes/%1").arg(id), body);
}

QNetworkReply *ApiClient::deleteNote(qint64 id)
{
    return sendJson("DELETE", QStringLiteral("/inbox/notes/%1").arg(id), QJsonObject());
}

QNetworkReply *ApiClient::getTags()
{
    return get(QStringLiteral("/inbox/tags"));
}

QNetworkReply *ApiClient::getDetailedTags()
{
    return get(QStringLiteral("/inbox/tags/detailed"));
}

QNetworkReply *ApiClient::getComments(qint64 noteId)
{
    return get(QStringLiteral("/inbox/notes/%1/comments").arg(noteId));
}

QNetworkReply *ApiClient::addComment(qint64 noteId, const QString &content)
{
    QJsonObject body;
    body.insert(QStringLiteral("content"), content);
    return sendJson("POST", QStringLiteral("/inbox/notes/%1/comments").arg(noteId), body);
}

QNetworkReply *ApiClient::sync(const QJsonObject &payload)
{
    return sendJson("POST", QStringLiteral("/inbox/sync"), payload);
}

QNetworkReply *ApiClient::getSyncDevices()
{
    return get(QStringLiteral("/inbox/sync/devices"));
}

QNetworkReply *ApiClient::deviceHeartbeat(const QString &name, const QString &platform, qint64 pending,
                                          qint64 localVersion)
{
    QJsonObject body;
    body.insert(QStringLiteral("device_id"), m_deviceId);
    body.insert(QStringLiteral("name"), name);
    body.insert(QStringLiteral("platform"), platform);
    body.insert(QStringLiteral("pending_changes"), pending);
    body.insert(QStringLiteral("local_version"), localVersion);
    return sendJson("POST", QStringLiteral("/inbox/sync/devices/heartbeat"), body);
}

bool ApiClient::parseReply(QNetworkReply *reply, QJsonDocument *doc, QString *err)
{
    const QByteArray raw = reply->readAll();
    const QNetworkReply::NetworkError e = reply->error();
    const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    const QString errString = reply->errorString(); // 先取出来，deleteLater 后不再访问对象
    reply->deleteLater();

    if (e == QNetworkReply::NoError) {
        if (raw.isEmpty()) {
            *doc = QJsonDocument();
            return true; // 例如 204
        }
        QJsonParseError pe;
        *doc = QJsonDocument::fromJson(raw, &pe);
        if (pe.error != QJsonParseError::NoError) {
            if (err)
                *err = QStringLiteral("响应不是合法 JSON: %1").arg(pe.errorString());
            return false;
        }
        return true;
    }
    if (err) {
        QString detail;
        if (!raw.isEmpty())
            detail = QStringLiteral(": %1").arg(QString::fromUtf8(raw).left(300));
        *err = QStringLiteral("%1 (HTTP %2)%3")
                   .arg(errString)
                   .arg(status ? QString::number(status) : QString::number(int(e)))
                   .arg(detail);
    }
    return false;
}

// ------------------------------------------------------------------ //
// ActivityWatch /api/0

QNetworkReply *ApiClient::getBuckets()
{
    return get(QStringLiteral("/api/0/buckets"));
}

QNetworkReply *ApiClient::getEvents(const QString &bucketId, qint64 startMs, qint64 endMs)
{
    QUrlQuery q;
    const QDateTime start = QDateTime::fromMSecsSinceEpoch(startMs, Qt::UTC);
    const QDateTime end = QDateTime::fromMSecsSinceEpoch(endMs, Qt::UTC);
    q.addQueryItem(QStringLiteral("start"), start.toString(Qt::ISODate));
    q.addQueryItem(QStringLiteral("end"), end.toString(Qt::ISODate));
    const QString query = q.toString(QUrl::FullyEncoded);
    const QString encodedId = QString::fromUtf8(QUrl::toPercentEncoding(bucketId));
    QString path = QStringLiteral("/api/0/buckets/%1/events").arg(encodedId);
    if (!query.isEmpty())
        path += QLatin1Char('?') + query;
    return get(path);
}

QNetworkReply *ApiClient::createBucket(const QString &bucketId, const QString &client, const QString &type)
{
    QJsonObject body;
    body.insert(QStringLiteral("client"), client);
    body.insert(QStringLiteral("type"), type);
    body.insert(QStringLiteral("hostname"), QString());
    const QString encodedId = QString::fromUtf8(QUrl::toPercentEncoding(bucketId));
    return sendJson("POST", QStringLiteral("/api/0/buckets/%1").arg(encodedId), body);
}

QNetworkReply *ApiClient::heartbeat(const QString &bucketId, const QJsonObject &data, double durationSec,
                                     const QDateTime &timestamp)
{
    QJsonObject body;
    body.insert(QStringLiteral("data"), data);
    body.insert(QStringLiteral("duration"), durationSec);
    body.insert(QStringLiteral("timestamp"), timestamp.toUTC().toString(Qt::ISODate));
    const QString encodedId = QString::fromUtf8(QUrl::toPercentEncoding(bucketId));
    return sendJson("POST", QStringLiteral("/api/0/buckets/%1/heartbeat").arg(encodedId), body);
}

// ── Inbox Todo ─────────────────────────────────────────────────

QNetworkReply *ApiClient::getTodos(bool includeCompleted)
{
    QString path = QStringLiteral("/inbox/todos");
    if (!includeCompleted)
        path += QStringLiteral("?completed=false");
    return get(path);
}

QNetworkReply *ApiClient::createTodo(const QString &title, const QString &content, const QStringList &tags)
{
    QJsonObject body;
    body.insert(QStringLiteral("title"), title);
    if (!content.isEmpty())
        body.insert(QStringLiteral("content"), content);
    if (!tags.isEmpty()) {
        QJsonArray arr;
        for (const QString &t : tags)
            arr.append(t);
        body.insert(QStringLiteral("tags"), arr);
    }
    return sendJson("POST", QStringLiteral("/inbox/todos"), body);
}

QNetworkReply *ApiClient::updateTodo(qint64 id, const QJsonObject &patch)
{
    return sendJson("PUT", QStringLiteral("/inbox/todos/%1").arg(id), patch);
}

QNetworkReply *ApiClient::deleteTodo(qint64 id)
{
    return sendJson("DELETE", QStringLiteral("/inbox/todos/%1").arg(id), QJsonObject());
}

} // namespace awqtui
