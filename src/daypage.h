// daypage.h —— Day 视图：时间线选择 → 打标签 → 明细/汇总勾选 → 过滤 → 未标记/高级搜索
#pragma once

#include <QDate>
#include <QPair>
#include <QWidget>

#include "filterparser.h"
#include "tagstore.h"

class QComboBox;
class QLabel;
class QLineEdit;
class QPushButton;
class QScrollArea;
class QStackedWidget;
class QTabWidget;
class QTableWidget;
class QTableWidgetItem;

namespace awqtui {

class TimelineWidget;
class UntaggedView;

class DayPage : public QWidget
{
    Q_OBJECT
public:
    explicit DayPage(TagStore *store, QWidget *parent = nullptr);

    void setDate(const QDate &date);
    QDate currentDate() const { return m_date; }
    void goToDay(qint64 dayStartMs);
    void refresh() { reload(); }

signals:
    void tagsChanged();

protected:
    void keyPressEvent(QKeyEvent *event) override;

private slots:
    void onPrevDay();
    void onNextDay();
    void onToday();
    void toggleSelectMode(bool on);
    void onSelModeChanged(int);
    void onAddTag();
    void onOpenTagEditor();
    void onOpenAdvancedSearch();
    void onOpenAutoTag();
    void onCopyAutotags();
    void onOpenTiming();
    void onTagAway();
    void toggleUntagged();
    void onFilterEdited(const QString &);
    void onTimelineSelection(const QList<QPair<qint64, qint64>> &ranges);
    void onDetailsItemChanged(QTableWidgetItem *item);
    void onSummaryItemChanged(QTableWidgetItem *item);
    void onDetailsDoubleClicked(int row, int col);

private:
    struct ActivityInfo {
        QString title;
        QString group;
        qint64 startMs = 0;
        qint64 endMs = 0;
        QString notes;
        bool billable = false;
        bool isTagSegment = false;
        qint64 tagId = 0;
    };

    void buildUi();
    void reload();
    void rebuildTagsLane();
    void rebuildDetails();
    void rebuildSummary();
    void refreshStatus();
    void syncCheckboxes();
    QList<QPair<qint64, qint64>> selectedRanges() const;
    QList<QPair<qint64, qint64>> activeRanges() const;
    QList<QPair<qint64, qint64>> untaggedRanges() const;
    void selectRanges(const QList<QPair<qint64, qint64>> &ranges, bool append = false);
    void applyShortcutKey(int key);
    void setStatus(const QString &msg);
    bool rowInSelection(const ActivityInfo &info) const;

    TagStore *m_store = nullptr;
    QDate m_date;

    QLabel *m_dateLabel = nullptr;
    QLabel *m_statusLabel = nullptr;
    QPushButton *m_prevBtn = nullptr;
    QPushButton *m_nextBtn = nullptr;
    QPushButton *m_todayBtn = nullptr;
    QPushButton *m_selectToggle = nullptr;
    QComboBox *m_selModeCombo = nullptr;
    QPushButton *m_addTagBtn = nullptr;
    QPushButton *m_tagEditorBtn = nullptr;
    QPushButton *m_autoTagBtn = nullptr;
    QPushButton *m_copyAutotagBtn = nullptr;
    QPushButton *m_timingBtn = nullptr;
    QPushButton *m_awayBtn = nullptr;
    QPushButton *m_untaggedBtn = nullptr;
    QPushButton *m_advSearchBtn = nullptr;
    QLineEdit *m_filterEdit = nullptr;
    QStackedWidget *m_stack = nullptr;
    TimelineWidget *m_timeline = nullptr;
    QScrollArea *m_untaggedScroll = nullptr;
    UntaggedView *m_untaggedView = nullptr;
    QTabWidget *m_bottomTabs = nullptr;
    QTableWidget *m_detailsTable = nullptr;
    QTableWidget *m_summaryTable = nullptr;
    QLabel *m_bottomSummary = nullptr;

    QList<QPair<qint64, qint64>> m_selection;
    QList<ActivityInfo> m_details;
    bool m_updating = false;
    bool m_showOnlyUntagged = false;
    FilterQuery m_filter;
};

} // namespace awqtui
