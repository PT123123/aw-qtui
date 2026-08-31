// activitypage.h —— ActivityWatch 风格 Activity 统计面板
#pragma once

#include <QDate>
#include <QWidget>
#include "mockdata.h"

class QLabel;
class QPushButton;
class QTabWidget;

namespace awqtui {

class HourlyActivityBars;
class HorizontalBarChart;
class CategoryBars;
class DonutChart;

class ActivityPage : public QWidget
{
    Q_OBJECT
public:
    explicit ActivityPage(QWidget *parent = nullptr);

    void setDate(const QDate &date);
    QDate date() const { return m_date; }
    void refresh();

private slots:
    void onPrevDay();
    void onNextDay();
    void onToday();

private:
    void buildUi();
    void reloadData();
    QStringList computeHourlyCategories() const;
    QList<BarItem> computeTopDomains(int limit) const;
    QList<BarItem> computeTopUrls(int limit) const;
    QList<BarItem> mockEditorFiles(int limit) const;

    QDate m_date;
    QList<TimelineLane> m_lanes;

    QLabel *m_dateLabel;
    QLabel *m_hostLabel;
    QLabel *m_activeLabel;
    QPushButton *m_prevBtn;
    QPushButton *m_nextBtn;
    QPushButton *m_todayBtn;
    HourlyActivityBars *m_hourlyBars;
    QTabWidget *m_tabs;

    HorizontalBarChart *m_topApps;
    HorizontalBarChart *m_topTitles;
    HorizontalBarChart *m_topCats;
    CategoryBars *m_catBars;
    HorizontalBarChart *m_catTree;
    DonutChart *m_donut;

    HorizontalBarChart *m_winApps;
    HorizontalBarChart *m_winTitles;

    HorizontalBarChart *m_topDomains;
    HorizontalBarChart *m_topUrls;

    HorizontalBarChart *m_editorFiles;
};

} // namespace awqtui
