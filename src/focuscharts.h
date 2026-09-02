// focuscharts.h —— 专注统计图表 / 日历（全部 QPainter 自绘，跟随主题色）
//
// 包含：专注时间线（周热力格）、热力图（月度 + 年度）、最佳专注时间（24h 柱状）、
// 日历（月历 + 当日任务 + 当日专注）。
#pragma once

#include <QDate>
#include <QList>
#include <QWidget>

#include "focusmodels.h"

class QLabel;
class QListWidget;

namespace awqtui {

class FocusSource;
class TodoSource;

// ── 周热力格：横轴 周一~周日，纵轴 0~24 点，色块深浅 = 该时段专注分钟 ──
class WeekHeatWidget : public QWidget
{
    Q_OBJECT
public:
    explicit WeekHeatWidget(QWidget *parent = nullptr);
    void setSessions(const QList<FocusSession> &sessions, const QDate &weekStart);
    QSize sizeHint() const override;

protected:
    void paintEvent(QPaintEvent *) override;

private:
    QList<FocusSession> m_sessions;
    QDate m_weekStart;
    QVector<QVector<qint64>> m_minutes; // [7][24] 分钟
    qint64 m_maxMinutes = 0;
};

// ── 专注时间线页 ──
class FocusWeekPage : public QWidget
{
    Q_OBJECT
public:
    explicit FocusWeekPage(FocusSource *focus, QWidget *parent = nullptr);
    void applyUiScale();
    void refresh();

private:
    void shiftWeek(int n);
    void updateTitle();
    FocusSource *m_focus = nullptr;
    QDate m_weekStart;      // 周一
    QLabel *m_title = nullptr;
    WeekHeatWidget *m_heat = nullptr;
};

// ── 月度热力图：月历色块，深浅 = 当天专注时长 ──
class MonthlyHeatWidget : public QWidget
{
    Q_OBJECT
public:
    explicit MonthlyHeatWidget(QWidget *parent = nullptr);
    void setSessions(const QList<FocusSession> &sessions, const QDate &month);
    QSize sizeHint() const override;

protected:
    void paintEvent(QPaintEvent *) override;

private:
    QList<FocusSession> m_sessions;
    QDate m_month;
    qint64 m_maxMinutes = 0;
};

// ── 年度热力图：365 天贡献图（7 行 x 53 周，五档 0m/≤1h/≤3h/≤5h/>5h） ──
class AnnualHeatWidget : public QWidget
{
    Q_OBJECT
public:
    explicit AnnualHeatWidget(QWidget *parent = nullptr);
    void setSessions(const QList<FocusSession> &sessions, int year);
    QSize sizeHint() const override;

protected:
    void paintEvent(QPaintEvent *) override;

private:
    QList<FocusSession> m_sessions;
    int m_year = QDate::currentDate().year();
};

// ── 热力图页：月度 + 年度 ──
class FocusHeatmapPage : public QWidget
{
    Q_OBJECT
public:
    explicit FocusHeatmapPage(FocusSource *focus, QWidget *parent = nullptr);
    void applyUiScale();
    void refresh();

private:
    void shiftMonth(int n);
    void shiftYear(int n);
    void updateTitles();
    FocusSource *m_focus = nullptr;
    QDate m_month;          // 当月 1 号
    int m_year;
    QLabel *m_monthTitle = nullptr;
    QLabel *m_yearTitle = nullptr;
    MonthlyHeatWidget *m_monthHeat = nullptr;
    AnnualHeatWidget *m_yearHeat = nullptr;
};

// ── 最佳专注时间：24h 按 3h 分桶柱状图，纵轴 = 总专注时长 ──
class BestChartWidget : public QWidget
{
    Q_OBJECT
public:
    explicit BestChartWidget(QWidget *parent = nullptr);
    void setSessions(const QList<FocusSession> &sessions, const QDate &month);
    QSize sizeHint() const override;

protected:
    void paintEvent(QPaintEvent *) override;

private:
    QList<FocusSession> m_sessions;
    QDate m_month;
    QVector<qint64> m_bucketSec; // [8] 秒
    qint64 m_maxSec = 0;
};

// ── 最佳专注时间页 ──
class FocusBestPage : public QWidget
{
    Q_OBJECT
public:
    explicit FocusBestPage(FocusSource *focus, QWidget *parent = nullptr);
    void applyUiScale();
    void refresh();

private:
    void shiftMonth(int n);
    void updateTitle();
    FocusSource *m_focus = nullptr;
    QDate m_month;          // 当月 1 号
    QLabel *m_title = nullptr;
    BestChartWidget *m_chart = nullptr;
};

// ── 月历控件：点选日期 -> dateSelected ──
class MonthCalendarWidget : public QWidget
{
    Q_OBJECT
public:
    explicit MonthCalendarWidget(QWidget *parent = nullptr);
    void setSessions(const QList<FocusSession> &sessions, const QDate &month);
    void selectDate(const QDate &date);
    QDate selectedDate() const { return m_selected; }
    QSize sizeHint() const override;

signals:
    void dateSelected(const QDate &date);

protected:
    void paintEvent(QPaintEvent *) override;
    void mousePressEvent(QMouseEvent *) override;

private:
    QRect dayRect(int index) const;
    QDate m_month;
    QList<FocusSession> m_sessions;
    QDate m_selected;
    bool m_hasSel = false;
};

// ── 日历页：月历 + 当日任务（按截止日期）+ 当日专注 ──
class FocusCalendarPage : public QWidget
{
    Q_OBJECT
public:
    explicit FocusCalendarPage(FocusSource *focus, TodoSource *todo, QWidget *parent = nullptr);
    void applyUiScale();
    void refresh();

private:
    void shiftMonth(int n);
    void onDateSelected(const QDate &date);
    void updateTitle();
    void updateDayDetail(const QDate &date);

    FocusSource *m_focus = nullptr;
    TodoSource *m_todo = nullptr;
    QDate m_month;
    QLabel *m_title = nullptr;
    MonthCalendarWidget *m_cal = nullptr;
    QLabel *m_dayTitle = nullptr;
    QListWidget *m_taskList = nullptr;
    QListWidget *m_focusList = nullptr;
};

} // namespace awqtui
