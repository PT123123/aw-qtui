// timelinepage.h —— ActivityWatch Timeline / Tockler 风格可交互时间线页
#pragma once

#include <QDate>
#include <QHash>
#include <QJsonArray>
#include <QList>
#include <QWidget>
#include "awdatastore.h"
#include "mockdata.h"

class QLabel;
class QPushButton;
class QComboBox;
class QNetworkReply;

namespace awqtui {

class ApiClient;
class TimelineWidget;

class TimelinePage : public QWidget
{
    Q_OBJECT
public:
    explicit TimelinePage(ApiClient *api, QWidget *parent = nullptr);

    void setDate(const QDate &date);
    QDate date() const { return m_date; }
    void refresh();
    // 按当前主题重建页面内联样式（主题切换时调用）
    void applyTheme();

private slots:
    void onPrevDay();
    void onNextDay();
    void onToday();
    void onResetView();
    void onRangeChanged(qint64 startMs, qint64 endMs);
    void onBucketsLoaded();
    void onEventLoaded();

private:
    void buildUi();
    void reloadData();
    void fetchAllEvents();
    void updateStats();
    void showEmptyState(const QString &msg);

    ApiClient *m_api = nullptr;
    QDate m_date;
    QList<TimelineLane> m_lanes;
    QList<BucketInfo> m_buckets;
    QHash<QString, QJsonArray> m_eventsMap;
    int m_pendingEvents = 0;
    bool m_loading = false;

    QLabel *m_dateLabel = nullptr;
    QPushButton *m_prevBtn = nullptr;
    QPushButton *m_nextBtn = nullptr;
    QPushButton *m_todayBtn = nullptr;
    QComboBox *m_intervalCombo = nullptr;
    QComboBox *m_showLastCombo = nullptr;
    QLabel *m_eventsLabel = nullptr;
    QPushButton *m_resetBtn = nullptr;

    TimelineWidget *m_timeline = nullptr;

    QLabel *m_totalTracked = nullptr;
    QLabel *m_afkTime = nullptr;
    QLabel *m_firstActivity = nullptr;
    QLabel *m_lastActivity = nullptr;
};

} // namespace awqtui
