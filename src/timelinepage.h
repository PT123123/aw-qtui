// timelinepage.h —— ActivityWatch Timeline / Tockler 风格可交互时间线页
#pragma once

#include <QDate>
#include <QWidget>
#include "mockdata.h"

class QLabel;
class QPushButton;
class QComboBox;

namespace awqtui {

class TimelineWidget;

class TimelinePage : public QWidget
{
    Q_OBJECT
public:
    explicit TimelinePage(QWidget *parent = nullptr);

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

private:
    void buildUi();
    void reloadData();
    void updateStats();

    QDate m_date;
    QList<TimelineLane> m_lanes;

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
