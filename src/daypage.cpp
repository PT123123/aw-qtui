// daypage.cpp —— Day 视图实现
#include "daypage.h"

#include <QComboBox>
#include <QDateTime>
#include <QHeaderView>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QMap>
#include <QMessageBox>
#include <QPushButton>
#include <QScrollArea>
#include <QSignalBlocker>
#include <QSplitter>
#include <QStackedWidget>
#include <QTabWidget>
#include <QTableWidget>
#include <QVBoxLayout>

#include "addtagdialog.h"
#include "advancedsearchdialog.h"
#include "autotagdialog.h"
#include "autotagengine.h"
#include "charts.h"
#include "mockdata.h"
#include "tageditordialog.h"
#include "theme.h"
#include "timelinewidget.h"
#include "timingdialog.h"
#include "untaggedview.h"

namespace awqtui {

// ── 区间工具 ────────────────────────────────────────────────
static QList<QPair<qint64, qint64>> mergeRanges(QList<QPair<qint64, qint64>> ranges)
{
    std::sort(ranges.begin(), ranges.end(),
              [](const QPair<qint64, qint64> &a, const QPair<qint64, qint64> &b) {
                  return a.first < b.first;
              });
    QList<QPair<qint64, qint64>> out;
    for (const auto &r : ranges) {
        if (r.second <= r.first)
            continue;
        if (out.isEmpty() || r.first > out.last().second)
            out.append(r);
        else
            out.last().second = qMax(out.last().second, r.second);
    }
    return out;
}

static QList<QPair<qint64, qint64>> subtractRanges(
    const QList<QPair<qint64, qint64>> &active, const QList<QPair<qint64, qint64>> &tagged)
{
    QList<QPair<qint64, qint64>> out;
    for (const auto &a : active) {
        QList<QPair<qint64, qint64>> cuts;
        for (const auto &t : tagged) {
            if (t.second <= a.first || t.first >= a.second)
                continue;
            cuts.append(t);
        }
        std::sort(cuts.begin(), cuts.end(),
                  [](const QPair<qint64, qint64> &x, const QPair<qint64, qint64> &y) {
                      return x.first < y.first;
                  });
        qint64 cur = a.first;
        for (const auto &c : cuts) {
            if (c.first > cur)
                out.append({cur, qMin(c.first, a.second)});
            cur = qMax(cur, c.second);
            if (cur >= a.second)
                break;
        }
        if (cur < a.second)
            out.append({cur, a.second});
    }
    return out;
}

DayPage::DayPage(TagStore *store, QWidget *parent)
    : QWidget(parent), m_store(store)
{
    m_date = QDate::currentDate();
    buildUi();
    reload();
}

void DayPage::buildUi()
{
    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(12, 10, 12, 10);
    root->setSpacing(8);

    // ── 第一行：日期导航 + 状态 ──
    auto *row1 = new QHBoxLayout;
    m_prevBtn = new QPushButton(QStringLiteral("◀"));
    m_prevBtn->setObjectName(QStringLiteral("ToolBtn"));
    m_nextBtn = new QPushButton(QStringLiteral("▶"));
    m_nextBtn->setObjectName(QStringLiteral("ToolBtn"));
    m_todayBtn = new QPushButton(QStringLiteral("Today"));
    m_todayBtn->setObjectName(QStringLiteral("ToolBtn"));
    m_dateLabel = new QLabel;
    m_dateLabel->setStyleSheet(QStringLiteral("color:#e6e6e6;font-size:15px;font-weight:600;"));
    row1->addWidget(m_prevBtn);
    row1->addWidget(m_nextBtn);
    row1->addWidget(m_todayBtn);
    row1->addSpacing(12);
    row1->addWidget(m_dateLabel);
    row1->addStretch(1);
    m_statusLabel = new QLabel;
    m_statusLabel->setStyleSheet(QStringLiteral("color:#6b7280;font-size:11px;"));
    row1->addWidget(m_statusLabel);
    root->addLayout(row1);

    // ── 第二行：选择模式 + 操作 + 过滤 ──
    auto *row2 = new QHBoxLayout;
    m_selectToggle = new QPushButton(QStringLiteral("选择模式"));
    m_selectToggle->setObjectName(QStringLiteral("ToolBtn"));
    m_selectToggle->setCheckable(true);
    m_selModeCombo = new QComboBox;
    m_selModeCombo->addItems({QStringLiteral("Select all"), QStringLiteral("Select only untagged"),
                              QStringLiteral("Show only untagged")});
    m_addTagBtn = new QPushButton(QStringLiteral("＋ Add tag"));
    m_addTagBtn->setObjectName(QStringLiteral("PrimaryBtn"));
    m_tagEditorBtn = new QPushButton(QStringLiteral("Tag editor"));
    m_tagEditorBtn->setObjectName(QStringLiteral("ToolBtn"));
    m_autoTagBtn = new QPushButton(QStringLiteral("自动标签"));
    m_autoTagBtn->setObjectName(QStringLiteral("ToolBtn"));
    m_copyAutotagBtn = new QPushButton(QStringLiteral("复制 autotags"));
    m_copyAutotagBtn->setObjectName(QStringLiteral("ToolBtn"));
    m_untaggedBtn = new QPushButton(QStringLiteral("未标记"));
    m_untaggedBtn->setObjectName(QStringLiteral("ToolBtn"));
    m_awayBtn = new QPushButton(QStringLiteral("Tag away"));
    m_awayBtn->setObjectName(QStringLiteral("ToolBtn"));
    m_timingBtn = new QPushButton(QStringLiteral("⏱ 计时"));
    m_timingBtn->setObjectName(QStringLiteral("ToolBtn"));
    m_advSearchBtn = new QPushButton(QStringLiteral("高级搜索"));
    m_advSearchBtn->setObjectName(QStringLiteral("ToolBtn"));
    row2->addWidget(m_selectToggle);
    row2->addWidget(m_selModeCombo);
    row2->addSpacing(8);
    row2->addWidget(m_addTagBtn);
    row2->addWidget(m_tagEditorBtn);
    row2->addWidget(m_autoTagBtn);
    row2->addWidget(m_copyAutotagBtn);
    row2->addWidget(m_untaggedBtn);
    row2->addWidget(m_awayBtn);
    row2->addWidget(m_timingBtn);
    row2->addWidget(m_advSearchBtn);
    row2->addStretch(1);
    m_filterEdit = new QLineEdit;
    m_filterEdit->setPlaceholderText(
        QStringLiteral("Filter… group: / duration>1m / start>22:00 / -xxx / or / ? * / #\"regex\""));
    m_filterEdit->setMaximumWidth(420);
    row2->addWidget(m_filterEdit);
    root->addLayout(row2);

    // ── 中部：时间线 / 未标记 切换 ──
    m_stack = new QStackedWidget;

    auto *dayPane = new QWidget;
    auto *dl = new QVBoxLayout(dayPane);
    dl->setContentsMargins(0, 0, 0, 0);
    m_timeline = new TimelineWidget;
    m_timeline->setTimeRange(m_date.startOfDay().toMSecsSinceEpoch(),
                             m_date.addDays(1).startOfDay().toMSecsSinceEpoch());
    dl->addWidget(m_timeline, 1);

    // 底部：Details / Summary
    m_bottomTabs = new QTabWidget;
    m_detailsTable = new QTableWidget(0, 7);
    m_detailsTable->setHorizontalHeaderLabels(
        {QString(), QStringLiteral("Title"), QStringLiteral("Group"), QStringLiteral("Start"),
         QStringLiteral("End"), QStringLiteral("Duration"), QStringLiteral("Notes")});
    m_detailsTable->horizontalHeader()->setSectionResizeMode(QHeaderView::Interactive);
    m_detailsTable->horizontalHeader()->setMinimumSectionSize(48);
    m_detailsTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_detailsTable->setSelectionMode(QAbstractItemView::ExtendedSelection);
    m_detailsTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_detailsTable->setColumnWidth(0, 28);
    m_bottomTabs->addTab(m_detailsTable, QStringLiteral("Details"));

    auto *summaryPane = new QWidget;
    auto *sml = new QVBoxLayout(summaryPane);
    sml->setContentsMargins(0, 0, 0, 0);
    m_summaryTable = new QTableWidget(0, 4);
    m_summaryTable->setHorizontalHeaderLabels(
        {QString(), QStringLiteral("Group"), QStringLiteral("Duration"), QStringLiteral("Count")});
    m_summaryTable->horizontalHeader()->setSectionResizeMode(QHeaderView::Interactive);
    m_summaryTable->horizontalHeader()->setMinimumSectionSize(48);
    m_summaryTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_summaryTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_summaryTable->setColumnWidth(0, 28);
    sml->addWidget(m_summaryTable, 1);
    m_bottomSummary = new QLabel;
    m_bottomSummary->setStyleSheet(QStringLiteral("color:#9aa4b0;font-size:11px;"));
    sml->addWidget(m_bottomSummary);
    m_bottomTabs->addTab(summaryPane, QStringLiteral("Summary"));

    auto *split = new QSplitter(Qt::Vertical);
    split->addWidget(m_timeline);
    split->addWidget(m_bottomTabs);
    split->setStretchFactor(0, 3);
    split->setStretchFactor(1, 2);
    dl->addWidget(split, 1);
    m_stack->addWidget(dayPane);

    m_untaggedView = new UntaggedView(m_store);
    m_untaggedScroll = new QScrollArea;
    m_untaggedScroll->setWidget(m_untaggedView);
    m_untaggedScroll->setWidgetResizable(true);
    m_untaggedScroll->setStyleSheet(QStringLiteral("QScrollArea{background:#1b1d21;border:none;}"));
    m_stack->addWidget(m_untaggedScroll);
    root->addWidget(m_stack, 1);

    // ── 连接 ──
    connect(m_prevBtn, &QPushButton::clicked, this, &DayPage::onPrevDay);
    connect(m_nextBtn, &QPushButton::clicked, this, &DayPage::onNextDay);
    connect(m_todayBtn, &QPushButton::clicked, this, &DayPage::onToday);
    connect(m_selectToggle, &QPushButton::toggled, this, &DayPage::toggleSelectMode);
    connect(m_selModeCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
            &DayPage::onSelModeChanged);
    connect(m_addTagBtn, &QPushButton::clicked, this, &DayPage::onAddTag);
    connect(m_tagEditorBtn, &QPushButton::clicked, this, &DayPage::onOpenTagEditor);
    connect(m_autoTagBtn, &QPushButton::clicked, this, &DayPage::onOpenAutoTag);
    connect(m_copyAutotagBtn, &QPushButton::clicked, this, &DayPage::onCopyAutotags);
    connect(m_untaggedBtn, &QPushButton::clicked, this, &DayPage::toggleUntagged);
    connect(m_awayBtn, &QPushButton::clicked, this, &DayPage::onTagAway);
    connect(m_timingBtn, &QPushButton::clicked, this, &DayPage::onOpenTiming);
    connect(m_advSearchBtn, &QPushButton::clicked, this, &DayPage::onOpenAdvancedSearch);
    connect(m_filterEdit, &QLineEdit::textChanged, this, &DayPage::onFilterEdited);
    connect(m_timeline, &TimelineWidget::selectionChanged, this, &DayPage::onTimelineSelection);
    connect(m_detailsTable, &QTableWidget::itemChanged, this, &DayPage::onDetailsItemChanged);
    connect(m_summaryTable, &QTableWidget::itemChanged, this, &DayPage::onSummaryItemChanged);
    connect(m_detailsTable, &QTableWidget::cellDoubleClicked, this,
            &DayPage::onDetailsDoubleClicked);
    connect(m_untaggedView, &UntaggedView::dayClicked, this, [this](const QDate &d) {
        m_stack->setCurrentIndex(0);
        setDate(d);
    });
}

void DayPage::setDate(const QDate &date)
{
    m_date = date;
    m_timeline->setTimeRange(m_date.startOfDay().toMSecsSinceEpoch(),
                             m_date.addDays(1).startOfDay().toMSecsSinceEpoch());
    m_selection.clear();
    reload();
}

void DayPage::goToDay(qint64 dayStartMs)
{
    const QDate d = QDateTime::fromMSecsSinceEpoch(dayStartMs, Qt::LocalTime).date();
    setDate(d);
}

// ── 日期导航 ────────────────────────────────────────────────
void DayPage::onPrevDay()
{
    setDate(m_date.addDays(-1));
}

void DayPage::onNextDay()
{
    setDate(m_date.addDays(1));
}

void DayPage::onToday()
{
    setDate(QDate::currentDate());
}

void DayPage::toggleSelectMode(bool on)
{
    m_timeline->setSelectMode(on);
    setStatus(on ? QStringLiteral("选择模式：在时间线上拖拽/双击选中时间段，Ctrl+拖拽多选")
                 : QString());
}

void DayPage::onSelModeChanged(int idx)
{
    switch (idx) {
    case 0:
        selectRanges(activeRanges());
        setStatus(QStringLiteral("已全选当天活动"));
        break;
    case 1:
        selectRanges(untaggedRanges());
        setStatus(QStringLiteral("已选中所有未标记时间段"));
        break;
    case 2: {
        m_showOnlyUntagged = !m_showOnlyUntagged;
        m_updating = true;
        rebuildDetails();
        m_updating = false;
        setStatus(m_showOnlyUntagged ? QStringLiteral("仅显示未标记活动")
                                     : QStringLiteral("已取消仅显示未标记"));
        break;
    }
    }
}

// ── 刷新 ────────────────────────────────────────────────────
void DayPage::reload()
{
    m_updating = true;
    rebuildTagsLane();
    rebuildDetails();
    rebuildSummary();
    m_updating = false;
    refreshStatus();
}

void DayPage::rebuildTagsLane()
{
    QList<TimelineLane> lanes = generateTimelineLanes(m_date);
    TimelineLane tagsLane;
    tagsLane.name = QStringLiteral("Tags");
    const qint64 dayStart = m_date.startOfDay().toMSecsSinceEpoch();
    const qint64 dayEnd = m_date.addDays(1).startOfDay().toMSecsSinceEpoch();
    for (const auto &seg : m_store->segmentsInRange(dayStart, dayEnd)) {
        TimelineEvent ev;
        ev.startMs = seg.startMs;
        ev.endMs = seg.endMs;
        ev.label = seg.tags.join(QStringLiteral(", "));
        ev.color = m_store->segmentColor(seg);
        ev.detail = seg.notes;
        tagsLane.events.append(ev);
    }
    lanes.append(tagsLane);

    // AutoTags lane（规则计算，随规则变化实时重算）
    const auto hits = AutoTagEngine::compute(m_store, dayStart, dayEnd, lanes);
    if (!hits.isEmpty()) {
        TimelineLane autoLane;
        autoLane.name = QStringLiteral("AutoTags");
        for (const auto &h : hits)
            autoLane.events.append(h.event);
        lanes.append(autoLane);
    }
    m_timeline->setLanes(lanes);
}

void DayPage::rebuildDetails()
{
    m_details.clear();
    if (!m_store)
        return;
    const qint64 dayStart = m_date.startOfDay().toMSecsSinceEpoch();
    const qint64 dayEnd = m_date.addDays(1).startOfDay().toMSecsSinceEpoch();

    // 活动事件
    const QList<TimelineLane> lanes = generateTimelineLanes(m_date);
    for (const auto &lane : lanes) {
        const bool isTagLike =
            lane.name.contains(QStringLiteral("window")) || lane.name.contains(QStringLiteral("web"));
        for (const auto &ev : lane.events) {
            if (ev.endMs <= dayStart || ev.startMs >= dayEnd)
                continue;
            ActivityInfo info;
            info.title = ev.label;
            info.group = ev.label;
            info.startMs = ev.startMs;
            info.endMs = ev.endMs;
            info.isTagSegment = false;
            m_details.append(info);
            Q_UNUSED(isTagLike);
        }
    }
    // 标签段
    for (const auto &seg : m_store->segmentsInRange(dayStart, dayEnd)) {
        ActivityInfo info;
        info.title = seg.tags.join(QStringLiteral(", "));
        info.group = seg.tags.isEmpty() ? QString() : seg.tags.first();
        info.startMs = seg.startMs;
        info.endMs = seg.endMs;
        info.notes = seg.notes;
        info.billable = seg.billable;
        info.isTagSegment = true;
        info.tagId = seg.id;
        m_details.append(info);
    }
    std::sort(m_details.begin(), m_details.end(),
              [](const ActivityInfo &a, const ActivityInfo &b) { return a.startMs < b.startMs; });

    // 过滤
    QList<ActivityInfo> shown;
    for (const auto &info : m_details) {
        ActivityRow row;
        row.title = info.title;
        row.group = info.group;
        row.startMs = info.startMs;
        row.endMs = info.endMs;
        row.notes = info.notes;
        row.billable = info.billable;
        if (m_filter.isActive() && !m_filter.matches(row))
            continue;
        if (m_showOnlyUntagged) {
            if (info.isTagSegment)
                continue; // 标签段不属于未标记
            const qint64 tagged = m_store->taggedTimeInRange(info.startMs, info.endMs);
            if (tagged >= (info.endMs - info.startMs))
                continue; // 完全被覆盖
        }
        shown.append(info);
    }

    m_details = shown; // 与表格行对齐（过滤后）
    m_detailsTable->setRowCount(shown.size());
    for (int r = 0; r < shown.size(); ++r) {
        const ActivityInfo &info = shown[r];
        auto *check = new QTableWidgetItem;
        check->setFlags(Qt::ItemIsUserCheckable | Qt::ItemIsEnabled | Qt::ItemIsSelectable);
        check->setCheckState(rowInSelection(info) ? Qt::Checked : Qt::Unchecked);
        m_detailsTable->setItem(r, 0, check);
        auto *title = new QTableWidgetItem(info.title);
        if (info.isTagSegment)
            title->setForeground(m_store->segmentColor(
                m_store->find(info.tagId) ? *m_store->find(info.tagId) : TagSegment{}));
        else
            title->setForeground(QColor(QStringLiteral("#e6e6e6")));
        m_detailsTable->setItem(r, 1, title);
        m_detailsTable->setItem(r, 2, new QTableWidgetItem(info.group));
        const QDateTime st(QDateTime::fromMSecsSinceEpoch(info.startMs, Qt::LocalTime));
        m_detailsTable->setItem(r, 3,
                                new QTableWidgetItem(st.time().toString(QStringLiteral("HH:mm"))));
        m_detailsTable->setItem(r, 4,
                                new QTableWidgetItem(QDateTime::fromMSecsSinceEpoch(info.endMs, Qt::LocalTime)
                                                         .time()
                                                         .toString(QStringLiteral("HH:mm"))));
        m_detailsTable->setItem(
            r, 5, new QTableWidgetItem(formatDuration((info.endMs - info.startMs) / 1000)));
        const QString notes =
            info.notes + (info.billable ? QStringLiteral("  [$]") : QString());
        m_detailsTable->setItem(r, 6, new QTableWidgetItem(notes));
    }
    m_detailsTable->resizeColumnsToContents();
    // Title 列给一个合理下限，避免内容很短时过窄；其余列按内容宽度
    m_detailsTable->setColumnWidth(1, qMax(m_detailsTable->columnWidth(1), 160));
}

void DayPage::rebuildSummary()
{
    if (!m_summaryTable)
        return;
    // 按 group 聚合 window/web 事件
    QMap<QString, QPair<qint64, int>> agg; // group -> (durationMs, count)
    const QList<TimelineLane> lanes = generateTimelineLanes(m_date);
    for (const auto &lane : lanes) {
        if (!lane.name.contains(QStringLiteral("window")) && !lane.name.contains(QStringLiteral("web")))
            continue;
        for (const auto &ev : lane.events) {
            auto &p = agg[ev.label];
            p.first += (ev.endMs - ev.startMs);
            ++p.second;
        }
    }
    m_summaryTable->setRowCount(agg.size());
    int r = 0;
    for (auto it = agg.constBegin(); it != agg.constEnd(); ++it, ++r) {
        auto *check = new QTableWidgetItem;
        check->setFlags(Qt::ItemIsUserCheckable | Qt::ItemIsEnabled | Qt::ItemIsSelectable);
        check->setCheckState(Qt::Unchecked);
        m_summaryTable->setItem(r, 0, check);
        auto *group = new QTableWidgetItem(it.key());
        group->setForeground(colorForString(it.key()));
        m_summaryTable->setItem(r, 1, group);
        m_summaryTable->setItem(r, 2,
                                new QTableWidgetItem(formatDuration(it.value().first / 1000)));
        m_summaryTable->setItem(r, 3, new QTableWidgetItem(QString::number(it.value().second)));
    }
    m_summaryTable->resizeColumnsToContents();
    m_summaryTable->setColumnWidth(1, qMax(m_summaryTable->columnWidth(1), 140));

    qint64 total = 0;
    for (auto it = agg.constBegin(); it != agg.constEnd(); ++it)
        total += it.value().first;
    m_bottomSummary->setText(QStringLiteral("Total tracked: %1")
                                 .arg(formatDuration(total / 1000)));
}

void DayPage::refreshStatus()
{
    if (!m_statusLabel)
        return;
    m_dateLabel->setText(m_date.toString(QStringLiteral("yyyy-MM-dd ddd")));
    qint64 selMs = 0;
    for (const auto &r : m_selection)
        selMs += (r.second - r.first);
    m_statusLabel->setText(QStringLiteral("已选 %1 段 · %2")
                               .arg(m_selection.size())
                               .arg(formatDuration(selMs / 1000)));
}

// ── 选区与联动 ──────────────────────────────────────────────
QList<QPair<qint64, qint64>> DayPage::selectedRanges() const
{
    return m_selection;
}

QList<QPair<qint64, qint64>> DayPage::activeRanges() const
{
    QList<QPair<qint64, qint64>> out;
    const QList<TimelineLane> lanes = generateTimelineLanes(m_date);
    for (const auto &lane : lanes) {
        if (lane.name.contains(QStringLiteral("window")) || lane.name.contains(QStringLiteral("web")))
            for (const auto &ev : lane.events)
                out.append({ev.startMs, ev.endMs});
    }
    return mergeRanges(out);
}

QList<QPair<qint64, qint64>> DayPage::untaggedRanges() const
{
    const qint64 dayStart = m_date.startOfDay().toMSecsSinceEpoch();
    const qint64 dayEnd = m_date.addDays(1).startOfDay().toMSecsSinceEpoch();
    QList<QPair<qint64, qint64>> tagged;
    for (const auto &s : m_store->segmentsInRange(dayStart, dayEnd))
        tagged.append({s.startMs, s.endMs});
    return subtractRanges(activeRanges(), tagged);
}

void DayPage::selectRanges(const QList<QPair<qint64, qint64>> &ranges, bool append)
{
    if (!append)
        m_selection = ranges;
    else
        m_selection.append(ranges);
    m_selection = mergeRanges(m_selection);
    m_timeline->setSelection(m_selection);
    m_updating = true;
    syncCheckboxes();
    m_updating = false;
    refreshStatus();
}

bool DayPage::rowInSelection(const ActivityInfo &info) const
{
    for (const auto &r : m_selection)
        if (info.startMs >= r.first && info.endMs <= r.second)
            return true;
    return false;
}

void DayPage::syncCheckboxes()
{
    // Details 勾选同步（由 timeline 交互触发时）
    const int rows = m_detailsTable->rowCount();
    for (int r = 0; r < rows; ++r) {
        if (r >= m_details.size())
            break;
        const bool sel = rowInSelection(m_details[r]);
        QTableWidgetItem *item = m_detailsTable->item(r, 0);
        if (item)
            item->setCheckState(sel ? Qt::Checked : Qt::Unchecked);
    }
}

void DayPage::onTimelineSelection(const QList<QPair<qint64, qint64>> &ranges)
{
    m_selection = ranges;
    m_updating = true;
    syncCheckboxes();
    m_updating = false;
    refreshStatus();
}

void DayPage::onDetailsItemChanged(QTableWidgetItem *item)
{
    if (m_updating || !item || item->column() != 0)
        return;
    // 从勾选行重建选区
    QList<QPair<qint64, qint64>> ranges;
    const int rows = m_detailsTable->rowCount();
    for (int r = 0; r < rows; ++r) {
        QTableWidgetItem *chk = m_detailsTable->item(r, 0);
        if (chk && chk->checkState() == Qt::Checked && r < m_details.size())
            ranges.append({m_details[r].startMs, m_details[r].endMs});
    }
    m_selection = mergeRanges(ranges);
    m_timeline->setSelection(m_selection);
    refreshStatus();
}

void DayPage::onSummaryItemChanged(QTableWidgetItem *item)
{
    if (m_updating || !item || item->column() != 0)
        return;
    // 勾选某组 → 选择该组所有事件
    QList<QPair<qint64, qint64>> ranges;
    const int rows = m_summaryTable->rowCount();
    for (int r = 0; r < rows; ++r) {
        QTableWidgetItem *chk = m_summaryTable->item(r, 0);
        if (!chk || chk->checkState() != Qt::Checked)
            continue;
        const QString group = m_summaryTable->item(r, 1)->text();
        const QList<TimelineLane> lanes = generateTimelineLanes(m_date);
        for (const auto &lane : lanes) {
            if (!lane.name.contains(QStringLiteral("window")) &&
                !lane.name.contains(QStringLiteral("web")))
                continue;
            for (const auto &ev : lane.events)
                if (ev.label == group)
                    ranges.append({ev.startMs, ev.endMs});
        }
    }
    m_selection = mergeRanges(ranges);
    m_timeline->setSelection(m_selection);
    m_updating = true;
    syncCheckboxes();
    m_updating = false;
    refreshStatus();
}

void DayPage::onDetailsDoubleClicked(int row, int)
{
    if (row < 0 || row >= m_details.size())
        return;
    const ActivityInfo &info = m_details[row];
    selectRanges({{info.startMs, info.endMs}});
}

// ── 打标签 ──────────────────────────────────────────────────
void DayPage::onAddTag()
{
    if (!m_store)
        return;
    const auto ranges = selectedRanges();
    if (ranges.isEmpty()) {
        setStatus(QStringLiteral("请先在时间线上选择时间段（拖拽/双击/勾选明细）"));
        return;
    }
    AddTagDialog dlg(m_store, ranges.first().first, ranges.last().second, this);
    if (dlg.exec() != QDialog::Accepted)
        return;
    const QStringList tags = dlg.tags();
    if (tags.isEmpty())
        return;
    const bool billable = dlg.billable();
    const QString notes = dlg.notes();
    int count = 0;
    for (const auto &r : ranges) {
        m_store->addSegment(r.first, r.second, tags, notes, billable);
        ++count;
    }
    setStatus(QStringLiteral("已为 %1 段时间打上标签").arg(count));
    reload();
    emit tagsChanged();
}

void DayPage::onOpenTagEditor()
{
    if (!m_store)
        return;
    TagEditorDialog dlg(m_store, this);
    connect(&dlg, &TagEditorDialog::tagsChanged, this, &DayPage::reload);
    dlg.exec();
}

void DayPage::onOpenAdvancedSearch()
{
    if (!m_store)
        return;
    AdvancedSearchDialog dlg(m_store, this);
    connect(&dlg, &AdvancedSearchDialog::jumpToDay, this, &DayPage::goToDay);
    connect(&dlg, &AdvancedSearchDialog::tagsChanged, this, &DayPage::reload);
    dlg.exec();
}

void DayPage::onOpenAutoTag()
{
    if (!m_store)
        return;
    AutoTagDialog dlg(m_store, this);
    connect(&dlg, &AutoTagDialog::rulesChanged, this, &DayPage::reload);
    dlg.exec();
}

void DayPage::onCopyAutotags()
{
    if (!m_store)
        return;
    const qint64 dayStart = m_date.startOfDay().toMSecsSinceEpoch();
    const qint64 dayEnd = m_date.addDays(1).startOfDay().toMSecsSinceEpoch();
    const auto hits = AutoTagEngine::compute(m_store, dayStart, dayEnd,
                                             generateTimelineLanes(m_date));
    if (hits.isEmpty()) {
        setStatus(QStringLiteral("今天没有自动标签可复制，请先在「自动标签」里建规则"));
        return;
    }
    QMessageBox box(this);
    box.setWindowTitle(QStringLiteral("Copy autotags to tags"));
    box.setText(QStringLiteral("把 %1 段自动标签复制为手工标签。与已有标签冲突时：")
                    .arg(hits.size()));
    auto *fillBtn = box.addButton(QStringLiteral("仅未标记（Fill untagged）"), QMessageBox::AcceptRole);
    auto *ignoreBtn = box.addButton(QStringLiteral("允许重叠（Ignore existing）"), QMessageBox::AcceptRole);
    auto *overwriteBtn = box.addButton(QStringLiteral("覆盖已有（Overwrite）"), QMessageBox::AcceptRole);
    box.addButton(QMessageBox::Cancel);
    box.exec();
    QAbstractButton *clicked = box.clickedButton();
    if (clicked == nullptr || clicked == box.button(QMessageBox::Cancel))
        return;

    int count = 0;
    for (const auto &h : hits) {
        const qint64 a = h.event.startMs;
        const qint64 b = h.event.endMs;
        if (b <= a)
            continue;
        // 标签文本已是组合（逗号分隔）
        QStringList tags;
        for (const auto &t : h.event.label.split(QLatin1Char(','), Qt::SkipEmptyParts)) {
            const QString s = t.trimmed();
            if (!s.isEmpty())
                tags << s;
        }
        if (tags.isEmpty())
            continue;
        if (clicked == overwriteBtn) {
            const auto existing = m_store->segmentsInRange(a, b);
            for (const auto &seg : existing)
                m_store->removeSegment(seg.id);
            m_store->addSegment(a, b, tags, QString(), false);
            ++count;
        } else if (clicked == ignoreBtn) {
            m_store->addSegment(a, b, tags, QString(), false);
            ++count;
        } else { // fill untagged
            const qint64 tagged = m_store->taggedTimeInRange(a, b);
            if (tagged < (b - a)) {
                m_store->addSegment(a, b, tags, QString(), false);
                ++count;
            }
        }
    }
    setStatus(QStringLiteral("已复制 %1 段自动标签为手工标签").arg(count));
    reload();
    emit tagsChanged();
}

void DayPage::onOpenTiming()
{
    if (!m_store)
        return;
    TimingDialog dlg(m_store, this);
    connect(&dlg, &TimingDialog::tagsChanged, this, &DayPage::reload);
    dlg.exec();
}

void DayPage::onTagAway()
{
    // Away 窗口简化：选中当天所有未标记时间段 → Add tag
    selectRanges(untaggedRanges());
    if (m_selection.isEmpty()) {
        setStatus(QStringLiteral("今天没有未标记的时间段（离开/空闲时段）"));
        return;
    }
    onAddTag();
}

void DayPage::toggleUntagged()
{
    const bool toUntagged = (m_stack->currentIndex() == 0);
    if (toUntagged) {
        const QDate today = QDate::currentDate();
        m_untaggedView->setRange(m_date.addDays(-m_date.day() + 1 - 30), today);
        m_stack->setCurrentIndex(1);
    } else {
        m_stack->setCurrentIndex(0);
    }
}

void DayPage::onFilterEdited(const QString &text)
{
    m_filter.parse(text);
    m_updating = true;
    rebuildDetails();
    m_updating = false;
}

// ── 键盘快捷键（标签快捷键） ────────────────────────────────
void DayPage::applyShortcutKey(int key)
{
    if (!m_store)
        return;
    const QString combo = m_store->shortcutTag(QChar(key).toLower());
    if (combo.isEmpty())
        return;
    const auto ranges = selectedRanges();
    if (ranges.isEmpty()) {
        setStatus(QStringLiteral("请先选择时间段再按标签快捷键"));
        return;
    }
    QStringList tags;
    for (const auto &t : combo.split(QLatin1Char(','), Qt::SkipEmptyParts)) {
        const QString s = t.trimmed();
        if (!s.isEmpty())
            tags << s;
    }
    if (tags.isEmpty())
        return;
    for (const auto &r : ranges)
        m_store->addSegment(r.first, r.second, tags, QString(),
                            m_store->newTagsBillableByDefault());
    setStatus(QStringLiteral("已用快捷键打标签：%1").arg(combo));
    reload();
    emit tagsChanged();
}

void DayPage::keyPressEvent(QKeyEvent *event)
{
    const QString text = event->text();
    if (event->modifiers() == Qt::NoModifier && text.size() == 1) {
        const QChar c = text.at(0);
        if (c.isLetterOrNumber()) {
            applyShortcutKey(c.unicode());
            return;
        }
    }
    QWidget::keyPressEvent(event);
}

void DayPage::setStatus(const QString &msg)
{
    if (m_statusLabel)
        m_statusLabel->setText(msg);
}

} // namespace awqtui
