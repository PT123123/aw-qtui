// watcher.h —— 内置 watcher：当前窗口 + AFK 状态，心跳上报到 aw-server
#pragma once

#include <QObject>
#include <QString>

class QTimer;

namespace awqtui {

class ApiClient;

// 当前前台窗口监控：定时查询前台窗口应用名 + 标题，心跳上报
class WindowWatcher : public QObject
{
    Q_OBJECT
public:
    explicit WindowWatcher(ApiClient *api, QObject *parent = nullptr);
    void start();
    void stop();

private slots:
    void onTick();

private:
    ApiClient *m_api;
    QTimer *m_timer;
    QString m_bucketId;
    bool m_bucketCreated = false;

    void ensureBucket();
    static QString currentAppName();
    static QString currentWindowTitle();
};

// AFK 状态监控：定时查询最后输入时间，超过阈值判定 AFK，心跳上报
class AfkWatcher : public QObject
{
    Q_OBJECT
public:
    explicit AfkWatcher(ApiClient *api, QObject *parent = nullptr);
    void start();
    void stop();

private slots:
    void onTick();

private:
    ApiClient *m_api;
    QTimer *m_timer;
    QString m_bucketId;
    QString m_lastStatus;
    bool m_bucketCreated = false;
    static constexpr int AFK_THRESHOLD_SEC = 180;

    void ensureBucket();
    static QString currentAfkStatus();
    static qint64 lastInputAgeSec();
};

} // namespace awqtui
