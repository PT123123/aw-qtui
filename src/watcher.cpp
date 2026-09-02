// watcher.cpp —— 内置 watcher 实现（Windows）
#include "watcher.h"

#include "apiclient.h"

#include <QDateTime>
#include <QFileInfo>
#include <QHostInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkReply>
#include <QTimer>

#include <windows.h>

namespace awqtui {

static QString hostname()
{
    QString h = QHostInfo::localHostName();
    if (h.isEmpty())
        h = QStringLiteral("unknown");
    return h;
}

// ── WindowWatcher ──────────────────────────────────────────────

WindowWatcher::WindowWatcher(ApiClient *api, QObject *parent)
    : QObject(parent), m_api(api), m_timer(new QTimer(this)),
      m_bucketId(QStringLiteral("aw-watcher-window_%1").arg(hostname()))
{
    m_timer->setInterval(1000);
    connect(m_timer, &QTimer::timeout, this, &WindowWatcher::onTick);
}

void WindowWatcher::start()
{
    ensureBucket();
    m_timer->start();
}

void WindowWatcher::stop()
{
    m_timer->stop();
}

void WindowWatcher::ensureBucket()
{
    if (m_bucketCreated || !m_api)
        return;
    QNetworkReply *reply = m_api->createBucket(m_bucketId, QStringLiteral("aw-qtui-watcher"),
                                                QStringLiteral("currentwindow"));
    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        // 200 = already exists, 201 = created; either way bucket is ready
        m_bucketCreated = true;
        reply->deleteLater();
    });
}

QString WindowWatcher::currentAppName()
{
    HWND hwnd = GetForegroundWindow();
    if (!hwnd)
        return QString();

    DWORD pid = 0;
    GetWindowThreadProcessId(hwnd, &pid);
    if (!pid)
        return QString();

    HANDLE hProc = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
    if (!hProc)
        return QString();

    wchar_t path[MAX_PATH];
    DWORD size = MAX_PATH;
    QString result;
    if (QueryFullProcessImageNameW(hProc, 0, path, &size)) {
        result = QString::fromWCharArray(path, static_cast<int>(size));
    }
    CloseHandle(hProc);

    if (result.isEmpty())
        return QString();
    return QFileInfo(result).baseName();
}

QString WindowWatcher::currentWindowTitle()
{
    HWND hwnd = GetForegroundWindow();
    if (!hwnd)
        return QString();

    wchar_t title[512];
    int len = GetWindowTextW(hwnd, title, 512);
    if (len <= 0)
        return QString();
    return QString::fromWCharArray(title, len);
}

void WindowWatcher::onTick()
{
    if (!m_api || !m_bucketCreated)
        return;

    const QString app = currentAppName();
    const QString title = currentWindowTitle();
    if (app.isEmpty())
        return;

    QJsonObject data;
    data.insert(QStringLiteral("app"), app);
    data.insert(QStringLiteral("title"), title);

    const QDateTime now = QDateTime::currentDateTime();
    const double duration = 1.0; // 1s heartbeat interval
    QNetworkReply *reply = m_api->heartbeat(m_bucketId, data, duration, now);
    connect(reply, &QNetworkReply::finished, reply, &QNetworkReply::deleteLater);
}

// ── AfkWatcher ─────────────────────────────────────────────────

AfkWatcher::AfkWatcher(ApiClient *api, QObject *parent)
    : QObject(parent), m_api(api), m_timer(new QTimer(this)),
      m_bucketId(QStringLiteral("aw-watcher-afk_%1").arg(hostname()))
{
    m_timer->setInterval(10000); // 10s tick
    connect(m_timer, &QTimer::timeout, this, &AfkWatcher::onTick);
}

void AfkWatcher::start()
{
    ensureBucket();
    m_timer->start();
    // 立即发一次初始心跳
    QTimer::singleShot(0, this, &AfkWatcher::onTick);
}

void AfkWatcher::stop()
{
    m_timer->stop();
}

void AfkWatcher::ensureBucket()
{
    if (m_bucketCreated || !m_api)
        return;
    QNetworkReply *reply = m_api->createBucket(m_bucketId, QStringLiteral("aw-qtui-watcher"),
                                                QStringLiteral("afkstatus"));
    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        m_bucketCreated = true;
        reply->deleteLater();
    });
}

qint64 AfkWatcher::lastInputAgeSec()
{
    LASTINPUTINFO lii;
    lii.cbSize = sizeof(LASTINPUTINFO);
    if (!GetLastInputInfo(&lii))
        return 0;
    const DWORD now = GetTickCount();
    if (now < lii.dwTime)
        return 0; // tick counter wrap
    return static_cast<qint64>(now - lii.dwTime) / 1000;
}

QString AfkWatcher::currentAfkStatus()
{
    return (lastInputAgeSec() >= AFK_THRESHOLD_SEC) ? QStringLiteral("afk")
                                                      : QStringLiteral("not-afk");
}

void AfkWatcher::onTick()
{
    if (!m_api || !m_bucketCreated)
        return;

    const QString status = currentAfkStatus();

    QJsonObject data;
    data.insert(QStringLiteral("status"), status);

    const QDateTime now = QDateTime::currentDateTime();
    const double duration = 10.0; // 10s heartbeat interval
    QNetworkReply *reply = m_api->heartbeat(m_bucketId, data, duration, now);
    connect(reply, &QNetworkReply::finished, reply, &QNetworkReply::deleteLater);

    m_lastStatus = status;
}

} // namespace awqtui
