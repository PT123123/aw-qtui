// activitypage.h —— ActivityWatch 风格 Activity 统计面板
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
class QTabWidget;
class QNetworkReply;

namespace awqtui {

class ApiClient;
class HourlyActivityBars;
class HorizontalBarChart;
class CategoryBars;
class DonutChart;

class ActivityPage : public QWidget
{
    Q_OBJECT
public:
    explicit ActivityPage(ApiClient *api, QWidget *parent = nullptr);

    void setDate(const QDate &date);
    QDate date() const { return m_date; }
    void refresh();
    // 按当前主题重建页面内联样式（主题切换时调用）
    void applyTheme();

private slots:
    void onPrevDay();
    void onNextDay();
    void onToday();
    void onBucketsLoaded();
    void onEventLoaded();

private:
    void buildUi();
    void reloadData();
    void fetchAllEvents();
    void updateUiFromLanes();
    void showEmptyState(const QString &msg);
    QStringList computeHourlyCategories() const;
    QList<BarItem> mockEditorFiles(int limit) const;

    ApiClient *m_api = nullptr;
    QDate m_date;
    QList<TimelineLane> m_lanes;
    QList<BucketInfo> m_buckets;
    QHash<QString, QJsonArray> m_eventsMap;
    int m_pendingEvents = 0;
    bool m_loading = false;

    QLabel *m_dateLabel = nullptr;
    QLabel *m_hostLabel = nullptr;
    QLabel *m_activeLabel = nullptr;
    QPushButton *m_prevBtn = nullptr;
    QPushButton *m_nextBtn = nullptr;
    QPushButton *m_todayBtn = nullptr;
    HourlyActivityBars *m_hourlyBars = nullptr;
    QTabWidget *m_tabs = nullptr;

    HorizontalBarChart *m_topApps = nullptr;
    HorizontalBarChart *m_topTitles = nullptr;
    HorizontalBarChart *m_topCats = nullptr;
    CategoryBars *m_catBars = nullptr;
    HorizontalBarChart *m_catTree = nullptr;
    DonutChart *m_donut = nullptr;

    HorizontalBarChart *m_winApps = nullptr;
    HorizontalBarChart *m_winTitles = nullptr;

    HorizontalBarChart *m_topDomains = nullptr;
    HorizontalBarChart *m_topUrls = nullptr;

    HorizontalBarChart *m_editorFiles;
};

} // namespace awqtui
