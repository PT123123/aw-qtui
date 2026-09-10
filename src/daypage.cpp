// daypage.cpp —— Day 视图实现（编辑模式 + 可选时间范围）
#include "daypage.h"
#include "ui_daypage.h"

#include <QComboBox>
#include <QButtonGroup>
#include <QDateEdit>
#include <QDateTime>
#include <QHeaderView>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QMap>
#include <QMessageBox>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkReply>
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
#include "apiclient.h"
#include "autotagdialog.h"
#include "autotagengine.h"
#include "awdatastore.h"
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

DayPage::DayPage(ApiClient *api, TagStore *store, QWidget *parent)
    : QWidget(parent), m_api(api), m_store(store)
{
    m_end = QDate::currentDate();
    m_start = m_end;
    buildUi();
    reload();
}

DayPage::~DayPage()
{
    delete ui;
}

void DayPage::applyTheme()
{
    setStyleSheet(QStringLiteral(R"(
        QLabel#ToolbarLabel { color: %4; font-size: 12px; }
        QComboBox {
            background: %6; border: 1px solid %2; border-radius: 6px;
            padding: 4px 8px; color: %5; font-size: 12px; min-width: 100px;
        }
        QComboBox:hover { border-color: %7; }
        QComboBox QAbstractItemView { background: %6; border: 1px solid %2; selection-background-color: %7; }
        QDateEdit {
            background: %6; border: 1px solid %2; border-radius: 6px;
            padding: 4px 8px; color: %5; font-size: 12px;
        }
        QDateEdit:hover { border-color: %7; }
        QDateEdit::drop-down { border: none; width: 18px; }
        QPushButton#RangeBtn {
            background: %6; border: 1px solid %2; border-radius: 6px;
            padding: 4px 12px; color: %5; font-size: 12px;
        }
        QPushButton#RangeBtn:hover { background: %8; border-color: %7; }
        QPushButton#RangeBtn:checked {
            background: %7; border-color: %7; color: white; font-weight: 600;
        }
    )")
                                  .arg(kColorBgElev, kColorBorder, kColorFg, kColorFgMuted,
                                       kColorFgSoft, kColorBgElev2, kColorAccent, kColorHover));
    if (m_dateLabel)
        m_dateLabel->setStyleSheet(
            QStringLiteral("color:%1;font-size:15px;font-weight:600;").arg(kColorFg));
    if (m_statusLabel)
        m_statusLabel->setStyleSheet(QStringLiteral("color:%1;font-size:11px;").arg(kColorMuted2));
    if (m_bottomSummary)
        m_bottomSummary->setStyleSheet(
            QStringLiteral("color:%1;font-size:11px;").arg(kColorFgMuted));
    if (m_untaggedScroll)
        m_untaggedScroll->setStyleSheet(
            QStringLiteral("QScrollArea{background:%1;border:none;}").arg(kColorChartBg));
    // 重建表格项前景色（主题色）
    reload();
}

void DayPage::buildUi()
{
    // 静态布局来自 Qt Designer（daypage.ui -> ui_daypage.h）
    ui = new Ui::DayPage;
    ui->setupUi(this);

    // 主题样式角色（全局 QSS 按 objectName 选择器匹配；.ui 中名称保持唯一）
    for (auto *b : {ui->prevBtn, ui->nextBtn, ui->todayBtn, ui->selectToggle, ui->resetBtn,
                    ui->tagEditorBtn, ui->autoTagBtn, ui->copyAutotagBtn,
                    ui->untaggedBtn, ui->awayBtn, ui->timingBtn, ui->advSearchBtn})
        b->setObjectName(QStringLiteral("ToolBtn"));
    ui->addTagBtn->setObjectName(QStringLiteral("PrimaryBtn"));
    for (auto *b : {ui->rangeBtn1d, ui->rangeBtn7d})
        b->setObjectName(QStringLiteral("RangeBtn"));
    for (auto *l : {ui->rangeLabel, ui->startDateLabel, ui->eventsLabel, ui->hintLabel})
        l->setObjectName(QStringLiteral("ToolbarLabel"));

    // 主题色相关样式（kColor* 随主题切换，无法烘焙进 .ui）
    m_dateLabel = ui->dateLabel;
    m_dateLabel->setStyleSheet(QStringLiteral("color:%1;font-size:15px;font-weight:600;").arg(kColorFg));
    m_statusLabel = ui->statusLabel;
    m_statusLabel->setStyleSheet(QStringLiteral("color:%1;font-size:11px;").arg(kColorMuted2));
    m_bottomSummary = ui->bottomSummary;
    m_bottomSummary->setStyleSheet(QStringLiteral("color:%1;font-size:11px;").arg(kColorFgMuted));
    m_untaggedScroll = ui->untaggedScroll;
    m_untaggedScroll->setStyleSheet(QStringLiteral("QScrollArea{background:%1;border:none;}").arg(kColorChartBg));
    ui->hintLabel->setStyleSheet(
        QStringLiteral("color: %1; font-size: 11px; font-style: italic;").arg(kColorMuted2));

    // ── 成员别名：业务逻辑沿用 m_* 指针 ──
    m_prevBtn = ui->prevBtn;
    m_nextBtn = ui->nextBtn;
    m_todayBtn = ui->todayBtn;
    m_selectToggle = ui->selectToggle;
    m_selModeCombo = ui->selModeCombo;
    m_addTagBtn = ui->addTagBtn;
    m_tagEditorBtn = ui->tagEditorBtn;
    m_autoTagBtn = ui->autoTagBtn;
    m_copyAutotagBtn = ui->copyAutotagBtn;
    m_untaggedBtn = ui->untaggedBtn;
    m_awayBtn = ui->awayBtn;
    m_timingBtn = ui->timingBtn;
    m_advSearchBtn = ui->advSearchBtn;
    m_filterEdit = ui->filterEdit;
    m_stack = ui->stack;
    m_timeline = ui->timeline;
    m_bottomTabs = ui->bottomTabs;
    m_detailsTable = ui->detailsTable;
    m_summaryTable = ui->summaryTable;
    m_eventsLabel = ui->eventsLabel;
    m_rangeBtn1d = ui->rangeBtn1d;
    m_rangeBtn7d = ui->rangeBtn7d;
    m_startDateEdit = ui->startDateEdit;

    // 时间范围：排他按钮组（1天 / 7天）；选择起始日期则两者都不选中（自定义）
    auto *rangeGroup = new QButtonGroup(this);
    rangeGroup->setExclusive(true);
    rangeGroup->addButton(m_rangeBtn1d);
    rangeGroup->addButton(m_rangeBtn7d);

    const QDate today = QDate::currentDate();
    m_startDateEdit->setDateRange(today.addYears(-5), today);
    m_startDateEdit->setDate(m_start);

    // ── 运行时行为：分割器拉伸、表格列宽与表头交互模式、未标记视图 ──
    ui->split->setStretchFactor(0, 3);
    ui->split->setStretchFactor(1, 2);
    for (QTableWidget *t : {m_detailsTable, m_summaryTable}) {
        t->horizontalHeader()->setSectionResizeMode(QHeaderView::Interactive);
        t->horizontalHeader()->setMinimumSectionSize(48);
        t->setColumnWidth(0, 28);
    }
    m_untaggedView = new UntaggedView(m_store);
    m_untaggedScroll->setWidget(m_untaggedView);

    // 时间线默认显示今天
    m_timeline->setTimeRange(rangeStartMs(), rangeEndMs());

    // ── 连接 ──
    connect(m_prevBtn, &QPushButton::clicked, this, &DayPage::onPrevDay);
    connect(m_nextBtn, &QPushButton::clicked, this, &DayPage::onNextDay);
    connect(m_todayBtn, &QPushButton::clicked, this, &DayPage::onToday);
    connect(m_rangeBtn1d, &QPushButton::clicked, this, &DayPage::onRange1Day);
    connect(m_rangeBtn7d, &QPushButton::clicked, this, &DayPage::onRange7Days);
    connect(m_startDateEdit, &QDateEdit::dateChanged, this, &DayPage::onStartDateChanged);
    connect(ui->resetBtn, &QPushButton::clicked, this, [this] { m_timeline->resetView(); });
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

    m_timeline->setSelectMode(false);
    updateRangeWidgets();
}

void DayPage::setDate(const QDate &date)
{
    m_end = date;
    applyRangeMode();
    m_timeline->setTimeRange(rangeStartMs(), rangeEndMs());
    m_selection.clear();
    reload();
}

void DayPage::goToDay(qint64 dayStartMs)
{
    const QDate d = QDateTime::fromMSecsSinceEpoch(dayStartMs, Qt::LocalTime).date();
    setDate(d);
}

// ── 日期导航与时间范围 ─────────────────────────────────────
void DayPage::onPrevDay()
{
    m_start = m_start.addDays(-1);
    m_end = m_end.addDays(-1);
    m_timeline->setTimeRange(rangeStartMs(), rangeEndMs());
    m_selection.clear();
    reload();
}

void DayPage::onNextDay()
{
    const QDate today = QDate::currentDate();
    if (m_end >= today)
        return; // 不允许越过今天
    m_start = m_start.addDays(1);
    m_end = m_end.addDays(1);
    m_timeline->setTimeRange(rangeStartMs(), rangeEndMs());
    m_selection.clear();
    reload();
}

void DayPage::onToday()
{
    setDate(QDate::currentDate());
}

void DayPage::onRange1Day()
{
    m_rangeMode = Range1Day;
    applyRangeMode();
    m_timeline->setTimeRange(rangeStartMs(), rangeEndMs());
    m_selection.clear();
    reload();
}

void DayPage::onRange7Days()
{
    m_rangeMode = Range7Days;
    applyRangeMode();
    m_timeline->setTimeRange(rangeStartMs(), rangeEndMs());
    m_selection.clear();
    reload();
}

void DayPage::onStartDateChanged(const QDate &date)
{
    if (m_updating)
        return; // 代码回填控件时不响应
    m_rangeMode = RangeCustom;
    m_start = date;
    const QDate today = QDate::currentDate();
    m_end = (date > today) ? date : today;
    m_timeline->setTimeRange(rangeStartMs(), rangeEndMs());
    m_selection.clear();
    reload();
}

void DayPage::applyRangeMode()
{
    switch (m_rangeMode) {
    case Range7Days:
        m_start = m_end.addDays(-6);
        break;
    case RangeCustom:
        // m_start 来自日期编辑器；m_end 保持（不早于 m_start）
        if (m_start > m_end)
            m_end = m_start;
        break;
    case Range1Day:
    default:
        m_start = m_end;
        break;
    }
    updateRangeWidgets();
}

void DayPage::updateRangeWidgets()
{
    const QSignalBlocker b1(m_rangeBtn1d);
    const QSignalBlocker b2(m_rangeBtn7d);
    const QSignalBlocker b3(m_startDateEdit);
    m_rangeBtn1d->setChecked(m_rangeMode == Range1Day);
    m_rangeBtn7d->setChecked(m_rangeMode == Range7Days);
    m_startDateEdit->setDate(m_start);

    if (m_rangeMode == Range1Day || m_start == m_end)
        m_dateLabel->setText(m_end.toString(QStringLiteral("yyyy-MM-dd ddd")));
    else
        m_dateLabel->setText(QStringLiteral("%1 ~ %2")
                                 .arg(m_start.toString(QStringLiteral("MM-dd")),
                                      m_end.toString(QStringLiteral("MM-dd"))));
}

qint64 DayPage::rangeStartMs() const
{
    return QDateTime(m_start, QTime(0, 0), Qt::LocalTime).toMSecsSinceEpoch();
}

qint64 DayPage::rangeEndMs() const
{
    return QDateTime(m_end, QTime(0, 0), Qt::LocalTime).toMSecsSinceEpoch() + 86400000LL;
}

// ── 刷新 ────────────────────────────────────────────────────
void DayPage::reload()
{
    updateRangeWidgets();
    if (!m_api) {
        // 离线：按范围内每天生成模拟数据并按 lane 名合并
        m_lanes.clear();
        QMap<QString, int> laneIdx;
        for (QDate d = m_start; d <= m_end; d = d.addDays(1)) {
            for (const TimelineLane &lane : generateTimelineLanes(d)) {
                int idx = laneIdx.value(lane.name, -1);
                if (idx < 0) {
                    m_lanes.append(lane);
                    laneIdx.insert(lane.name, m_lanes.size() - 1);
                } else {
                    m_lanes[idx].events.append(lane.events);
                }
            }
        }
        m_updating = true;
        rebuildTagsLane();
        rebuildDetails();
        rebuildSummary();
        m_updating = false;
        refreshStatus();
        int totalEvents = 0;
        for (const auto &lane : m_lanes)
            totalEvents += lane.events.size();
        if (m_eventsLabel)
            m_eventsLabel->setText(QStringLiteral("Events shown: %1").arg(totalEvents));
        return;
    }

    m_loading = true;
    setStatus(QStringLiteral("加载中…"));
    QNetworkReply *reply = m_api->getBuckets();
    connect(reply, &QNetworkReply::finished, this, &DayPage::onBucketsLoaded);
}

void DayPage::onBucketsLoaded()
{
    auto *reply = qobject_cast<QNetworkReply *>(sender());
    if (!reply)
        return;
    QJsonDocument doc;
    QString err;
    if (!ApiClient::parseReply(reply, &doc, &err)) {
        showEmptyState(QStringLiteral("Failed to load buckets: %1").arg(err));
        reply->deleteLater();
        return;
    }
    reply->deleteLater();

    m_buckets = parseBuckets(doc.object());
    if (m_buckets.isEmpty()) {
        showEmptyState(QStringLiteral("No buckets — run aw-watcher to start tracking."));
        return;
    }
    fetchAllEvents();
}

void DayPage::fetchAllEvents()
{
    m_eventsMap.clear();
    m_pendingEvents = m_buckets.size();

    const qint64 dayStart = rangeStartMs();
    const qint64 dayEnd = rangeEndMs();

    for (const BucketInfo &b : m_buckets) {
        QNetworkReply *reply = m_api->getEvents(b.id, dayStart, dayEnd);
        reply->setProperty("bucketId", b.id);
        connect(reply, &QNetworkReply::finished, this, &DayPage::onEventLoaded);
    }
}

void DayPage::onEventLoaded()
{
    auto *reply = qobject_cast<QNetworkReply *>(sender());
    if (!reply)
        return;
    const QString bucketId = reply->property("bucketId").toString();
    QJsonDocument doc;
    QString err;
    if (ApiClient::parseReply(reply, &doc, &err)) {
        m_eventsMap[bucketId] = doc.array();
    }
    reply->deleteLater();

    if (--m_pendingEvents <= 0) {
        m_loading = false;
        m_lanes = buildLanes(m_buckets, m_eventsMap);
        m_updating = true;
        rebuildTagsLane();
        rebuildDetails();
        rebuildSummary();
        m_updating = false;
        refreshStatus();
        int totalEvents = 0;
        for (const auto &lane : m_lanes)
            totalEvents += lane.events.size();
        if (m_eventsLabel)
            m_eventsLabel->setText(QStringLiteral("Events shown: %1").arg(totalEvents));
    }
}

void DayPage::showEmptyState(const QString &msg)
{
    m_loading = false;
    m_lanes.clear();
    m_updating = true;
    rebuildTagsLane();
    rebuildDetails();
    rebuildSummary();
    m_updating = false;
    setStatus(msg);
    if (m_eventsLabel)
        m_eventsLabel->setText(QStringLiteral("Events shown: 0"));
    qWarning() << "[DayPage]" << msg;
}

void DayPage::rebuildTagsLane()
{
    // 在已加载的数据行（真实或离线模拟）基础上追加 Tags / AutoTags 行
    QList<TimelineLane> lanes = m_lanes;
    TimelineLane tagsLane;
    tagsLane.name = QStringLiteral("Tags");
    const qint64 dayStart = rangeStartMs();
    const qint64 dayEnd = rangeEndMs();
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
    const qint64 dayStart = rangeStartMs();
    const qint64 dayEnd = rangeEndMs();

    // 活动事件
    const QList<TimelineLane> &lanes = m_lanes;
    for (const auto &lane : lanes) {
        for (const auto &ev : lane.events) {
            if (ev.endMs <= dayStart || ev.startMs >= dayEnd)
                continue;
            ActivityInfo info;
            info.title = ev.detail.isEmpty() ? ev.label : ev.detail;
            info.group = ev.label;
            info.startMs = ev.startMs;
            info.endMs = ev.endMs;
            info.isTagSegment = false;
            m_details.append(info);
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
    // 跨天时 Start/End 附带日期，避免歧义
    const QString timeFmt =
        (m_start == m_end) ? QStringLiteral("HH:mm") : QStringLiteral("MM-dd HH:mm");
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
            title->setForeground(QColor(kColorFg));
        m_detailsTable->setItem(r, 1, title);
        m_detailsTable->setItem(r, 2, new QTableWidgetItem(info.group));
        const QDateTime st(QDateTime::fromMSecsSinceEpoch(info.startMs, Qt::LocalTime));
        m_detailsTable->setItem(r, 3,
                                new QTableWidgetItem(st.time().toString(timeFmt)));
        m_detailsTable->setItem(r, 4,
                                new QTableWidgetItem(QDateTime::fromMSecsSinceEpoch(info.endMs, Qt::LocalTime)
                                                         .time()
                                                         .toString(timeFmt)));
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
    const QList<TimelineLane> &lanes = m_lanes;
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
    const QList<TimelineLane> &lanes = m_lanes;
    for (const auto &lane : lanes) {
        if (lane.name.contains(QStringLiteral("window")) || lane.name.contains(QStringLiteral("web")))
            for (const auto &ev : lane.events)
                out.append({ev.startMs, ev.endMs});
    }
    return mergeRanges(out);
}

QList<QPair<qint64, qint64>> DayPage::untaggedRanges() const
{
    const qint64 dayStart = rangeStartMs();
    const qint64 dayEnd = rangeEndMs();
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
        const QList<TimelineLane> &lanes = m_lanes;
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
    const qint64 dayStart = rangeStartMs();
    const qint64 dayEnd = rangeEndMs();
    const auto hits = AutoTagEngine::compute(m_store, dayStart, dayEnd, m_lanes);
    if (hits.isEmpty()) {
        setStatus(QStringLiteral("当前范围内没有自动标签可复制，请先在「自动标签」里建规则"));
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
    // Away 窗口简化：选中当前范围内所有未标记时间段 → Add tag
    selectRanges(untaggedRanges());
    if (m_selection.isEmpty()) {
        setStatus(QStringLiteral("当前范围内没有未标记的时间段（离开/空闲时段）"));
        return;
    }
    onAddTag();
}

void DayPage::toggleUntagged()
{
    const bool toUntagged = (m_stack->currentIndex() == 0);
    if (toUntagged) {
        const QDate today = QDate::currentDate();
        m_untaggedView->setRange(m_end.addDays(-m_end.day() + 1 - 30), today);
        m_stack->setCurrentIndex(1);
    } else {
        m_stack->setCurrentIndex(0);
    }
}

void DayPage::onSelModeChanged(int idx)
{
    switch (idx) {
    case 0:
        selectRanges(activeRanges());
        setStatus(QStringLiteral("已全选当前范围活动"));
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

void DayPage::toggleSelectMode(bool on)
{
    m_timeline->setSelectMode(on);
    setStatus(on ? QStringLiteral("选择模式：在时间线上拖拽/双击选中时间段，Ctrl+拖拽多选")
                 : QString());
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
