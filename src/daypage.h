// daypage.h —— 【时间线/日审阅】页（编辑模式）：
// 时间线选择 → 打标签 → 明细/汇总 → 过滤 → 未标记；支持 1天 / 7天 / 自定义起始日期的时间范围。
#pragma once

#include <QDate>
#include <QPair>
#include <QWidget>

#include "filterparser.h"
#include "tagstore.h"
#include "timelinewidget.h"

class QComboBox;
class QDateEdit;
class QLabel;
class QLineEdit;
class QNetworkReply;
class QPushButton;
class QScrollArea;
class QStackedWidget;
class QTabWidget;
class QTableWidget;
class QTableWidgetItem;

// Qt Designer 布局（daypage.ui），全局命名空间
namespace Ui { class DayPage; }

namespace awqtui {

class ApiClient;
struct BucketInfo;

class UntaggedView;

class DayPage : public QWidget
{
    Q_OBJECT
public:
    explicit DayPage(ApiClient *api, TagStore *store, QWidget *parent = nullptr);
    ~DayPage() override;

    void setDate(const QDate &date);
    QDate currentDate() const { return m_end; }
    void goToDay(qint64 dayStartMs);
    void refresh() { reload(); }
    // B 方案：切走隐藏页时释放驻留数据（事件缓存/时间线/表格），切回时由 refresh() 重拉
    void releaseWeight();
    // 按当前主题重建页面内联样式并重载表格前景色（主题切换时调用）
    void applyTheme();

signals:
    void tagsChanged();

protected:
    void keyPressEvent(QKeyEvent *event) override;

private slots:
    void onPrevDay();
    void onNextDay();
    void onToday();
    void onRange1Day();
    void onRange7Days();
    void onStartDateChanged(const QDate &date);
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
    void onTopAppsItemChanged(QTableWidgetItem *item);
    void onDetailsDoubleClicked(int row, int col);
    void onBucketsLoaded();
    void onEventLoaded();

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

    // 时间范围：[m_start, m_end]（含端点，按天对齐）
    enum RangeMode { Range1Day = 1, Range7Days = 7, RangeCustom = 0 };

    // Qt Designer 生成的布局对象（daypage.ui -> ui_daypage.h）
    Ui::DayPage *ui = nullptr;
    void buildUi();
    void applyRangeMode();               // 按 m_rangeMode 重算 m_start/m_end
    void shiftRange(int days);           // 整体平移窗口（◀ ▶）
    qint64 rangeStartMs() const;
    qint64 rangeEndMs() const;
    void updateRangeWidgets();           // 同步按钮选中态 / 日期编辑器 / 日期标签
    void reload();
    void rebuildTagsLane();
    void rebuildDetails();
    void rebuildSummary();
    void rebuildTopApps();
    void refreshStatus();
    void syncCheckboxes();
    void fetchAllEvents();
    void showEmptyState(const QString &msg);
    QList<QPair<qint64, qint64>> selectedRanges() const;
    QList<QPair<qint64, qint64>> activeRanges() const;
    QList<QPair<qint64, qint64>> untaggedRanges() const;
    void selectRanges(const QList<QPair<qint64, qint64>> &ranges, bool append = false);
    void applyShortcutKey(int key);
    void setStatus(const QString &msg);
    bool rowInSelection(const ActivityInfo &info) const;

    ApiClient *m_api = nullptr;
    TagStore *m_store = nullptr;
    QDate m_start;                       // 范围起始日（含）
    QDate m_end;                         // 范围结束日（含）
    int m_rangeMode = Range1Day;
    QList<BucketInfo> m_buckets;
    QHash<QString, QJsonArray> m_eventsMap;
    int m_pendingEvents = 0;
    // 每次 fetchAllEvents/releaseWeight 递增：在途 reply 落地时比对代次，切页期间作废的请求不污染新加载
    int m_fetchGen = 0;
    bool m_loading = false;
    QList<TimelineLane> m_lanes;

    QLabel *m_dateLabel = nullptr;
    QLabel *m_statusLabel = nullptr;
    QPushButton *m_prevBtn = nullptr;
    QPushButton *m_nextBtn = nullptr;
    QPushButton *m_todayBtn = nullptr;
    // 时间范围选择
    QPushButton *m_rangeBtn1d = nullptr;
    QPushButton *m_rangeBtn7d = nullptr;
    QDateEdit *m_startDateEdit = nullptr;
    QLabel *m_eventsLabel = nullptr;
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
    QTableWidget *m_topAppsTable = nullptr;
    QLabel *m_bottomSummary = nullptr;

    QList<QPair<qint64, qint64>> m_selection;
    QList<ActivityInfo> m_details;
    bool m_updating = false;
    bool m_showOnlyUntagged = false;
    FilterQuery m_filter;
};

} // namespace awqtui
