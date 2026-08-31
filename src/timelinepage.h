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

    QLabel *m_dateLabel;
    QPushButton *m_prevBtn;
    QPushButton *m_nextBtn;
    QPushButton *m_todayBtn;
    QComboBox *m_intervalCombo;
    QComboBox *m_showLastCombo;
    QLabel *m_eventsLabel;
    QPushButton *m_resetBtn;

    TimelineWidget *m_timeline;

    QLabel *m_totalTracked;
    QLabel *m_afkTime;
    QLabel *m_firstActivity;
    QLabel *m_lastActivity;
};

} // namespace awqtui
