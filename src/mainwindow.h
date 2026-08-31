// mainwindow.h —— 主窗口：左侧导航 + 页面堆栈
#pragma once

#include <QMainWindow>
#include <QStackedWidget>

class QKeyEvent;
class QLabel;
class QPushButton;

namespace awqtui {

class ApiClient;
class MdnsDiscovery;
class InboxPage;
class SyncPage;
class ActivityPage;
class TimelinePage;
class DayPage;
class StatsPage;
class TagStore;

class MainWindow : public QMainWindow
{
    Q_OBJECT
public:
    explicit MainWindow(const QString &serverUrl = QString(), QWidget *parent = nullptr);
    ~MainWindow() override;

    InboxPage *inboxPage() const { return m_inbox; }
    SyncPage *syncPage() const { return m_sync; }
    ActivityPage *activityPage() const { return m_activity; }
    TimelinePage *timelinePage() const { return m_timeline; }
    DayPage *dayPage() const { return m_day; }
    StatsPage *statsPage() const { return m_stats; }

    void switchPage(int index);

protected:
    void keyPressEvent(QKeyEvent *event) override;

private:
    void buildUi();
    void updateStatus();

    ApiClient *m_api;
    MdnsDiscovery *m_mdns;
    TagStore *m_tagStore;
    ActivityPage *m_activity;
    TimelinePage *m_timeline;
    InboxPage *m_inbox;
    SyncPage *m_sync;
    DayPage *m_day;
    StatsPage *m_stats;
    QStackedWidget *m_stack;
    QPushButton *m_navActivity;
    QPushButton *m_navTimeline;
    QPushButton *m_navInbox;
    QPushButton *m_navSync;
    QPushButton *m_navDay;
    QPushButton *m_navStats;
    QLabel *m_deviceLabel;
};

} // namespace awqtui
