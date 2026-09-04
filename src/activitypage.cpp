// activitypage.cpp
#include "activitypage.h"

#include "apiclient.h"
#include "awdatastore.h"
#include "charts.h"
#include "mockdata.h"
#include "theme.h"

#include <QDateTime>
#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkReply>
#include <QPushButton>
#include <QScrollArea>
#include <QTabWidget>
#include <QVBoxLayout>

namespace awqtui {

static QFrame *createCard(QWidget *parent = nullptr)
{
    auto *card = new QFrame(parent);
    card->setObjectName(QStringLiteral("Card"));
    return card;
}

static QLabel *createCardTitle(const QString &text, QWidget *parent = nullptr)
{
    auto *lbl = new QLabel(text, parent);
    lbl->setObjectName(QStringLiteral("CardTitle"));
    return lbl;
}

ActivityPage::ActivityPage(ApiClient *api, QWidget *parent)
    : QWidget(parent), m_api(api), m_dateStart(QDate::currentDate()), m_dateEnd(QDate::currentDate()),
      m_rangeLabel(QStringLiteral("Today"))
{
    qDebug() << "[ActivityPage] ctor start";
    applyTheme();
    qDebug() << "[ActivityPage] stylesheet set, calling buildUi...";
    buildUi();
    applyTheme();
    qDebug() << "[ActivityPage] buildUi done, calling reloadData...";
    reloadData();
    qDebug() << "[ActivityPage] ctor done";
}

void ActivityPage::applyTheme()
{
    setStyleSheet(QStringLiteral(R"(
        QFrame#Card {
            background: %1;
            border: 1px solid %2;
            border-radius: 8px;
        }
        QLabel#CardTitle {
            color: %3;
            font-size: 13px;
            font-weight: 600;
            padding: 10px 12px 2px;
        }
        QLabel#ToolbarLabel { color: %4; font-size: 12px; }
        QPushButton#NavArrow {
            background: transparent; border: 1px solid %2; border-radius: 4px;
            padding: 4px 10px; color: %5; font-size: 14px;
        }
        QPushButton#NavArrow:hover { background: %6; border-color: %7; }
        QPushButton#ToolBtn {
            background: %6; border: 1px solid %2; border-radius: 6px;
            padding: 5px 12px; color: %5; font-size: 12px;
        }
        QPushButton#ToolBtn:hover { background: %8; border-color: %7; }
        QPushButton#ChipBtn {
            background: transparent; border: 1px solid %2; border-radius: 14px;
            padding: 4px 14px; color: %4; font-size: 12px; min-width: 36px;
        }
        QPushButton#ChipBtn:hover { background: %6; border-color: %7; color: %5; }
        QPushButton#ChipBtn:checked { background: %7; border-color: %7; color: %9; font-weight: 600; }
        QTabWidget::pane { border: 1px solid %2; border-radius: 8px; background: %9; }
        QTabBar::tab {
            background: transparent; color: %4; padding: 8px 18px;
            border: none; border-bottom: 2px solid transparent; font-size: 12px;
        }
        QTabBar::tab:selected { color: %7; border-bottom-color: %7; }
        QTabBar::tab:hover { color: %3; }
    )")
                                  .arg(kColorBgElev, kColorBorder, kColorFg, kColorFgMuted,
                                       kColorFgSoft, kColorBgElev2, kColorAccent, kColorHover,
                                       kColorBg));
    if (m_dateLabel)
        m_dateLabel->setStyleSheet(
            QStringLiteral("color: %1; font-size: 14px; font-weight: 600; padding: 0 4px;")
                .arg(kColorFg));
}

void ActivityPage::buildUi()
{
    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(16, 12, 16, 12);
    root->setSpacing(10);

    // ── 顶部工具栏 ──
    auto *toolbar = new QHBoxLayout;
    toolbar->setSpacing(8);

    m_prevBtn = new QPushButton(QStringLiteral("◀"));
    m_prevBtn->setObjectName(QStringLiteral("NavArrow"));
    m_dateLabel = new QLabel;
    m_nextBtn = new QPushButton(QStringLiteral("▶"));
    m_nextBtn->setObjectName(QStringLiteral("NavArrow"));
    m_todayBtn = new QPushButton(QStringLiteral("Today"));
    m_todayBtn->setObjectName(QStringLiteral("ToolBtn"));

    connect(m_prevBtn, &QPushButton::clicked, this, &ActivityPage::onPrevDay);
    connect(m_nextBtn, &QPushButton::clicked, this, &ActivityPage::onNextDay);
    connect(m_todayBtn, &QPushButton::clicked, this, &ActivityPage::onToday);

    toolbar->addWidget(m_prevBtn);
    toolbar->addWidget(m_dateLabel);
    toolbar->addWidget(m_nextBtn);
    toolbar->addWidget(m_todayBtn);
    toolbar->addSpacing(16);

    // 日期 chips
    m_chipToday = new QPushButton(QStringLiteral("Today"));
    m_chipToday->setObjectName(QStringLiteral("ChipBtn"));
    m_chipToday->setCheckable(true);
    m_chipToday->setChecked(true);
    m_chipYesterday = new QPushButton(QStringLiteral("Yesterday"));
    m_chipYesterday->setObjectName(QStringLiteral("ChipBtn"));
    m_chipYesterday->setCheckable(true);
    m_chipLast7 = new QPushButton(QStringLiteral("Last 7 days"));
    m_chipLast7->setObjectName(QStringLiteral("ChipBtn"));
    m_chipLast7->setCheckable(true);
    m_chipLast30 = new QPushButton(QStringLiteral("Last 30 days"));
    m_chipLast30->setObjectName(QStringLiteral("ChipBtn"));
    m_chipLast30->setCheckable(true);
    m_chipAll = new QPushButton(QStringLiteral("All"));
    m_chipAll->setObjectName(QStringLiteral("ChipBtn"));
    m_chipAll->setCheckable(true);

    connect(m_chipToday, &QPushButton::clicked, this, &ActivityPage::onDateChipToday);
    connect(m_chipYesterday, &QPushButton::clicked, this, &ActivityPage::onDateChipYesterday);
    connect(m_chipLast7, &QPushButton::clicked, this, &ActivityPage::onDateChipLast7);
    connect(m_chipLast30, &QPushButton::clicked, this, &ActivityPage::onDateChipLast30);
    connect(m_chipAll, &QPushButton::clicked, this, &ActivityPage::onDateChipAll);

    toolbar->addWidget(m_chipToday);
    toolbar->addWidget(m_chipYesterday);
    toolbar->addWidget(m_chipLast7);
    toolbar->addWidget(m_chipLast30);
    toolbar->addWidget(m_chipAll);
    toolbar->addSpacing(16);

    m_hostLabel = new QLabel;
    m_hostLabel->setObjectName(QStringLiteral("ToolbarLabel"));
    m_activeLabel = new QLabel;
    m_activeLabel->setObjectName(QStringLiteral("ToolbarLabel"));
    toolbar->addWidget(m_hostLabel);
    toolbar->addSpacing(12);
    toolbar->addWidget(m_activeLabel);
    toolbar->addStretch(1);

    auto *filtersBtn = new QPushButton(QStringLiteral("⚙ Filters"));
    filtersBtn->setObjectName(QStringLiteral("ToolBtn"));
    auto *refreshBtn = new QPushButton(QStringLiteral("↻ Refresh"));
    refreshBtn->setObjectName(QStringLiteral("ToolBtn"));
    connect(refreshBtn, &QPushButton::clicked, this, &ActivityPage::refresh);
    auto *newViewBtn = new QPushButton(QStringLiteral("+ New view"));
    newViewBtn->setObjectName(QStringLiteral("ToolBtn"));
    toolbar->addWidget(filtersBtn);
    toolbar->addWidget(refreshBtn);
    toolbar->addWidget(newViewBtn);

    root->addLayout(toolbar);

    // ── 24 小时活跃柱状图 ──
    auto *barsCard = createCard();
    auto *barsLay = new QVBoxLayout(barsCard);
    barsLay->setContentsMargins(12, 8, 12, 8);
    barsLay->addWidget(createCardTitle(QStringLiteral("Activity over time")));
    m_hourlyBars = new HourlyActivityBars;
    barsLay->addWidget(m_hourlyBars);
    root->addWidget(barsCard);

    // ── 标签页 ──
    m_tabs = new QTabWidget;

    // === Summary 页 ===
    auto *summaryWidget = new QWidget;
    auto *summaryRoot = new QVBoxLayout(summaryWidget);
    summaryRoot->setContentsMargins(10, 12, 10, 10);
    summaryRoot->setSpacing(10);

    // 上排：三个 Top
    auto *topRow = new QHBoxLayout;
    topRow->setSpacing(10);

    auto *appsCard = createCard();
    auto *appsLay = new QVBoxLayout(appsCard);
    appsLay->setContentsMargins(4, 0, 8, 8);
    appsLay->addWidget(createCardTitle(QStringLiteral("Top Applications")));
    m_topApps = new HorizontalBarChart;
    m_topApps->setLabelWidth(130);
    appsLay->addWidget(m_topApps, 1);
    topRow->addWidget(appsCard, 1);

    auto *titlesCard = createCard();
    auto *titlesLay = new QVBoxLayout(titlesCard);
    titlesLay->setContentsMargins(4, 0, 8, 8);
    titlesLay->addWidget(createCardTitle(QStringLiteral("Top Window Titles")));
    m_topTitles = new HorizontalBarChart;
    m_topTitles->setLabelWidth(170);
    titlesLay->addWidget(m_topTitles, 1);
    topRow->addWidget(titlesCard, 1);

    auto *catsCard = createCard();
    auto *catsLay = new QVBoxLayout(catsCard);
    catsLay->setContentsMargins(4, 0, 8, 8);
    catsLay->addWidget(createCardTitle(QStringLiteral("Top Categories")));
    m_topCats = new HorizontalBarChart;
    m_topCats->setLabelWidth(130);
    catsLay->addWidget(m_topCats, 1);
    topRow->addWidget(catsCard, 1);

    summaryRoot->addLayout(topRow);

    // 下排：CategoryBars + CategoryTree + Donut
    auto *bottomRow = new QHBoxLayout;
    bottomRow->setSpacing(10);

    auto *cbCard = createCard();
    auto *cbLay = new QVBoxLayout(cbCard);
    cbLay->setContentsMargins(4, 0, 8, 8);
    cbLay->addWidget(createCardTitle(QStringLiteral("Timeline (Barchart)")));
    m_catBars = new CategoryBars;
    cbLay->addWidget(m_catBars, 1);
    bottomRow->addWidget(cbCard, 2);

    auto *treeCard = createCard();
    auto *treeLay = new QVBoxLayout(treeCard);
    treeLay->setContentsMargins(4, 0, 8, 8);
    treeLay->addWidget(createCardTitle(QStringLiteral("Category Tree")));
    m_catTree = new HorizontalBarChart;
    m_catTree->setLabelWidth(160);
    treeLay->addWidget(m_catTree, 1);
    bottomRow->addWidget(treeCard, 1);

    auto *donutCard = createCard();
    auto *donutLay = new QVBoxLayout(donutCard);
    donutLay->setContentsMargins(4, 0, 8, 8);
    donutLay->addWidget(createCardTitle(QStringLiteral("Category Sunburst")));
    m_donut = new DonutChart;
    donutLay->addWidget(m_donut, 1);
    bottomRow->addWidget(donutCard, 1);

    summaryRoot->addLayout(bottomRow, 1);
    m_tabs->addTab(summaryWidget, QStringLiteral("Summary"));

    // === Window 页 ===
    auto *winWidget = new QWidget;
    auto *winRoot = new QHBoxLayout(winWidget);
    winRoot->setContentsMargins(10, 12, 10, 10);
    winRoot->setSpacing(10);

    auto *winAppsCard = createCard();
    auto *winAppsLay = new QVBoxLayout(winAppsCard);
    winAppsLay->setContentsMargins(4, 0, 8, 8);
    winAppsLay->addWidget(createCardTitle(QStringLiteral("Top Applications")));
    m_winApps = new HorizontalBarChart;
    m_winApps->setLabelWidth(150);
    winAppsLay->addWidget(m_winApps, 1);
    winRoot->addWidget(winAppsCard, 1);

    auto *winTitlesCard = createCard();
    auto *winTitlesLay = new QVBoxLayout(winTitlesCard);
    winTitlesLay->setContentsMargins(4, 0, 8, 8);
    winTitlesLay->addWidget(createCardTitle(QStringLiteral("Top Window Titles")));
    m_winTitles = new HorizontalBarChart;
    m_winTitles->setLabelWidth(200);
    winTitlesLay->addWidget(m_winTitles, 1);
    winRoot->addWidget(winTitlesCard, 1);

    m_tabs->addTab(winWidget, QStringLiteral("Window"));

    // === Browser 页 ===
    auto *browserWidget = new QWidget;
    auto *browserRoot = new QHBoxLayout(browserWidget);
    browserRoot->setContentsMargins(10, 12, 10, 10);
    browserRoot->setSpacing(10);

    auto *domCard = createCard();
    auto *domLay = new QVBoxLayout(domCard);
    domLay->setContentsMargins(4, 0, 8, 8);
    domLay->addWidget(createCardTitle(QStringLiteral("Top Domains")));
    m_topDomains = new HorizontalBarChart;
    m_topDomains->setLabelWidth(160);
    domLay->addWidget(m_topDomains, 1);
    browserRoot->addWidget(domCard, 1);

    auto *urlCard = createCard();
    auto *urlLay = new QVBoxLayout(urlCard);
    urlLay->setContentsMargins(4, 0, 8, 8);
    urlLay->addWidget(createCardTitle(QStringLiteral("Top URLs")));
    m_topUrls = new HorizontalBarChart;
    m_topUrls->setLabelWidth(180);
    urlLay->addWidget(m_topUrls, 1);
    browserRoot->addWidget(urlCard, 1);

    m_tabs->addTab(browserWidget, QStringLiteral("Browser"));

    // === Editor 页 ===
    auto *editorWidget = new QWidget;
    auto *editorRoot = new QVBoxLayout(editorWidget);
    editorRoot->setContentsMargins(10, 12, 10, 10);

    auto *edCard = createCard();
    auto *edLay = new QVBoxLayout(edCard);
    edLay->setContentsMargins(4, 0, 8, 8);
    edLay->addWidget(createCardTitle(QStringLiteral("Top Editor Files")));
    m_editorFiles = new HorizontalBarChart;
    m_editorFiles->setLabelWidth(260);
    edLay->addWidget(m_editorFiles, 1);
    editorRoot->addWidget(edCard, 1);

    m_tabs->addTab(editorWidget, QStringLiteral("Editor"));

    // === Trends 页（多日聚合） ===
    auto *trendsWidget = new QWidget;
    auto *trendsRoot = new QVBoxLayout(trendsWidget);
    trendsRoot->setContentsMargins(10, 12, 10, 10);
    trendsRoot->setSpacing(10);

    // 占位符（无数据时显示）
    m_trendPlaceholder = createCard();
    auto *phLay = new QVBoxLayout(m_trendPlaceholder);
    phLay->setContentsMargins(20, 20, 20, 20);
    auto *phLabel = new QLabel(QStringLiteral("Select a date range to view trends\n(Today / Yesterday / Last 7 days / Last 30 days / All)"));
    phLabel->setObjectName(QStringLiteral("ToolbarLabel"));
    phLabel->setAlignment(Qt::AlignCenter);
    phLay->addWidget(phLabel);
    trendsRoot->addWidget(m_trendPlaceholder);

    // Top Applications (multi-day aggregate)
    auto *tAppsCard = createCard();
    auto *tAppsLay = new QVBoxLayout(tAppsCard);
    tAppsLay->setContentsMargins(4, 0, 8, 8);
    tAppsLay->addWidget(createCardTitle(QStringLiteral("Top Applications (Trend)")));
    m_trendApps = new HorizontalBarChart;
    m_trendApps->setLabelWidth(150);
    tAppsLay->addWidget(m_trendApps, 1);
    trendsRoot->addWidget(tAppsCard, 1);

    // Top Categories (multi-day aggregate)
    auto *tCatsCard = createCard();
    auto *tCatsLay = new QVBoxLayout(tCatsCard);
    tCatsLay->setContentsMargins(4, 0, 8, 8);
    tCatsLay->addWidget(createCardTitle(QStringLiteral("Top Categories (Trend)")));
    m_trendCats = new HorizontalBarChart;
    m_trendCats->setLabelWidth(150);
    tCatsLay->addWidget(m_trendCats, 1);
    trendsRoot->addWidget(tCatsCard, 1);

    // Daily breakdown
    auto *tDailyCard = createCard();
    auto *tDailyLay = new QVBoxLayout(tDailyCard);
    tDailyLay->setContentsMargins(4, 0, 8, 8);
    tDailyLay->addWidget(createCardTitle(QStringLiteral("Daily Activity")));
    m_trendDaily = new HorizontalBarChart;
    m_trendDaily->setLabelWidth(100);
    tDailyLay->addWidget(m_trendDaily, 1);
    trendsRoot->addWidget(tDailyCard, 1);

    m_tabs->addTab(trendsWidget, QStringLiteral("Trends"));

    root->addWidget(m_tabs, 1);
}

void ActivityPage::setDate(const QDate &date)
{
    m_dateStart = date;
    m_dateEnd = date;
    m_rangeLabel = QStringLiteral("Today");
    uncheckAllChips();
    if (m_chipToday) m_chipToday->setChecked(true);
    reloadData();
}

void ActivityPage::refresh()
{
    reloadData();
}

void ActivityPage::onPrevDay()
{
    m_dateStart = m_dateStart.addDays(-1);
    m_dateEnd = m_dateStart;
    m_rangeLabel = m_dateStart.toString(QStringLiteral("yyyy-MM-dd"));
    uncheckAllChips();
    reloadData();
}

void ActivityPage::onNextDay()
{
    m_dateStart = m_dateStart.addDays(1);
    m_dateEnd = m_dateStart;
    m_rangeLabel = m_dateStart.toString(QStringLiteral("yyyy-MM-dd"));
    uncheckAllChips();
    reloadData();
}

void ActivityPage::onToday()
{
    m_dateStart = QDate::currentDate();
    m_dateEnd = QDate::currentDate();
    m_rangeLabel = QStringLiteral("Today");
    uncheckAllChips();
    if (m_chipToday) m_chipToday->setChecked(true);
    reloadData();
}

void ActivityPage::onDateChipToday()
{
    uncheckAllChips();
    m_chipToday->setChecked(true);
    m_dateStart = QDate::currentDate();
    m_dateEnd = QDate::currentDate();
    m_rangeLabel = QStringLiteral("Today");
    reloadData();
}

void ActivityPage::onDateChipYesterday()
{
    uncheckAllChips();
    m_chipYesterday->setChecked(true);
    m_dateStart = QDate::currentDate().addDays(-1);
    m_dateEnd = m_dateStart;
    m_rangeLabel = QStringLiteral("Yesterday");
    reloadData();
}

void ActivityPage::onDateChipLast7()
{
    uncheckAllChips();
    m_chipLast7->setChecked(true);
    m_dateEnd = QDate::currentDate();
    m_dateStart = m_dateEnd.addDays(-6);
    m_rangeLabel = QStringLiteral("Last 7 days");
    reloadData();
}

void ActivityPage::onDateChipLast30()
{
    uncheckAllChips();
    m_chipLast30->setChecked(true);
    m_dateEnd = QDate::currentDate();
    m_dateStart = m_dateEnd.addDays(-29);
    m_rangeLabel = QStringLiteral("Last 30 days");
    reloadData();
}

void ActivityPage::onDateChipAll()
{
    uncheckAllChips();
    m_chipAll->setChecked(true);
    m_dateStart = QDate(2020, 1, 1);
    m_dateEnd = QDate::currentDate();
    m_rangeLabel = QStringLiteral("All time");
    reloadData();
}

void ActivityPage::uncheckAllChips()
{
    if (m_chipToday) m_chipToday->setChecked(false);
    if (m_chipYesterday) m_chipYesterday->setChecked(false);
    if (m_chipLast7) m_chipLast7->setChecked(false);
    if (m_chipLast30) m_chipLast30->setChecked(false);
    if (m_chipAll) m_chipAll->setChecked(false);
}

void ActivityPage::reloadData()
{
    const QString dateText = (m_dateStart == m_dateEnd)
        ? m_dateStart.toString(QStringLiteral("yyyy-MM-dd ddd"))
        : m_dateStart.toString(QStringLiteral("yyyy-MM-dd")) + QStringLiteral(" → ") +
          m_dateEnd.toString(QStringLiteral("yyyy-MM-dd"));
    m_dateLabel->setText(dateText);

    if (!m_api) {
        // Mock 模式：用 mockdata 生成
        m_lanes = generateTimelineLanes(m_dateStart);
        updateUiFromLanes();
        updateTrendsFromLanes();
        return;
    }

    m_loading = true;
    m_hostLabel->setText(QStringLiteral("Host: loading…"));
    m_activeLabel->setText(QStringLiteral("time active: —"));

    QNetworkReply *reply = m_api->getBuckets();
    connect(reply, &QNetworkReply::finished, this, &ActivityPage::onBucketsLoaded);
}

void ActivityPage::onBucketsLoaded()
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
    m_hostLabel->setText(QStringLiteral("Host: %1").arg(m_buckets.first().hostname));
    fetchAllEvents();
}

void ActivityPage::fetchAllEvents()
{
    m_eventsMap.clear();
    m_pendingEvents = m_buckets.size();

    const qint64 dayStart = QDateTime(m_dateStart, QTime(0, 0), Qt::LocalTime).toMSecsSinceEpoch();
    const qint64 dayEnd = QDateTime(m_dateEnd, QTime(23, 59, 59), Qt::LocalTime).toMSecsSinceEpoch() + 1;

    for (const BucketInfo &b : m_buckets) {
        QNetworkReply *reply = m_api->getEvents(b.id, dayStart, dayEnd);
        reply->setProperty("bucketId", b.id);
        connect(reply, &QNetworkReply::finished, this, &ActivityPage::onEventLoaded);
    }
}

void ActivityPage::onEventLoaded()
{
    auto *reply = qobject_cast<QNetworkReply *>(sender());
    if (!reply)
        return;
    const QString bucketId = reply->property("bucketId").toString();
    QJsonDocument doc;
    QString err;
    if (ApiClient::parseReply(reply, &doc, &err)) {
        m_eventsMap[bucketId] = doc.array();
    } else {
        qWarning() << "[ActivityPage] events failed for" << bucketId << ":" << err;
    }
    reply->deleteLater();

    if (--m_pendingEvents <= 0) {
        m_loading = false;
        updateUiFromLanes();
        updateTrendsFromLanes();
    }
}

void ActivityPage::updateUiFromLanes()
{
    m_lanes = buildLanes(m_buckets, m_eventsMap);

    const QList<qint64> hourly = hourlyFromLanes(m_lanes);
    qint64 totalActive = 0;
    for (qint64 s : hourly)
        totalActive += s;
    m_activeLabel->setText(QStringLiteral("time active: %1").arg(formatDuration(totalActive)));

    m_hourlyBars->setData(hourly);

    m_topApps->setItems(topAppsFromLanes(m_lanes, 7));
    m_topTitles->setItems(topTitlesFromLanes(m_lanes, 7));
    const QList<BarItem> cats = topCategoriesFromLanes(m_lanes, 6);
    m_topCats->setItems(cats);
    m_catBars->setData(hourly, computeHourlyCategories());
    m_catTree->setItems(categoryTreeFromLanes(m_lanes));
    m_donut->setItems(cats);

    m_winApps->setItems(topAppsFromLanes(m_lanes, 10));
    m_winTitles->setItems(topTitlesFromLanes(m_lanes, 10));

    m_topDomains->setItems(topDomainsFromLanes(m_lanes, 8));
    m_topUrls->setItems(topUrlsFromLanes(m_lanes, 8));

    m_editorFiles->setItems(mockEditorFiles(10));
}

void ActivityPage::updateTrendsFromLanes()
{
    // 多日范围时显示趋势数据
    if (m_dateStart == m_dateEnd) {
        // 单日：趋势图显示该日数据
        m_trendApps->setItems(topAppsFromLanes(m_lanes, 10));
        m_trendCats->setItems(topCategoriesFromLanes(m_lanes, 8));

        QList<BarItem> daily;
        BarItem d;
        d.label = m_dateStart.toString(QStringLiteral("MM-dd"));
        d.valueSeconds = 0;
        for (const auto &lane : m_lanes) {
            for (const auto &ev : lane.events) {
                d.valueSeconds += (ev.endMs - ev.startMs) / 1000;
            }
        }
        d.color = kColorAccent;
        daily.append(d);
        m_trendDaily->setItems(daily);
    } else {
        // 多日：按天聚合（这里简化处理，实际应逐日请求）
        m_trendApps->setItems(topAppsFromLanes(m_lanes, 10));
        m_trendCats->setItems(topCategoriesFromLanes(m_lanes, 8));

        // 按天聚合总时长
        QHash<QString, qint64> dayDur;
        for (const auto &lane : m_lanes) {
            for (const auto &ev : lane.events) {
                const QDate d = QDateTime::fromMSecsSinceEpoch(ev.startMs, Qt::LocalTime).date();
                dayDur[d.toString(QStringLiteral("yyyy-MM-dd"))] += (ev.endMs - ev.startMs) / 1000;
            }
        }
        QList<BarItem> daily;
        QStringList keys = dayDur.keys();
        std::sort(keys.begin(), keys.end());
        for (const QString &k : keys) {
            BarItem d;
            d.label = k.right(5);  // MM-dd
            d.valueSeconds = dayDur[k];
            d.color = kColorAccent;
            daily.append(d);
        }
        m_trendDaily->setItems(daily);
    }
}

void ActivityPage::showEmptyState(const QString &msg)
{
    m_loading = false;
    m_lanes.clear();
    m_hostLabel->setText(QStringLiteral("Host: —"));
    m_activeLabel->setText(QStringLiteral("time active: —"));
    m_hourlyBars->setData(QList<qint64>(24, 0));
    m_topApps->setItems({});
    m_topTitles->setItems({});
    m_topCats->setItems({});
    m_catBars->setData(QList<qint64>(24, 0), QStringList(24, QStringLiteral("Uncategorized")));
    m_catTree->setItems({});
    m_donut->setItems({});
    m_winApps->setItems({});
    m_winTitles->setItems({});
    m_topDomains->setItems({});
    m_topUrls->setItems({});
    m_editorFiles->setItems({});
    m_trendApps->setItems({});
    m_trendCats->setItems({});
    m_trendDaily->setItems({});
    qWarning() << "[ActivityPage]" << msg;
}

QStringList ActivityPage::computeHourlyCategories() const
{
    QStringList result(24, QStringLiteral("Uncategorized"));
    for (const auto &lane : m_lanes) {
        if (!lane.name.startsWith(QStringLiteral("aw-watcher-window"))) continue;
        QVector<qint64> hourDur(24, 0);
        QVector<QString> hourCat(24);
        for (const auto &ev : lane.events) {
            if (ev.label == QStringLiteral("idle")) continue;
            qint64 cur = ev.startMs;
            while (cur < ev.endMs) {
                const QDateTime dt = QDateTime::fromMSecsSinceEpoch(cur, Qt::LocalTime);
                const int h = dt.time().hour();
                const qint64 hourEnd = QDateTime(dt.date(), QTime(h, 59, 59, 999), Qt::LocalTime).toMSecsSinceEpoch();
                const qint64 segEnd = qMin(ev.endMs, hourEnd + 1);
                hourDur[h] += (segEnd - cur) / 1000;
                if (hourDur[h] > 0 && (hourCat[h].isEmpty() || hourDur[h] > 0)) {
                    hourCat[h] = ev.category;
                }
                cur = segEnd;
            }
        }
        for (int h = 0; h < 24; ++h) {
            if (!hourCat[h].isEmpty()) result[h] = hourCat[h];
        }
    }
    return result;
}

QList<BarItem> ActivityPage::mockEditorFiles(int limit) const
{
    static const QVector<QPair<QString, qint64>> files = {
        {QStringLiteral("src/timelinewidget.cpp"), 7200},
        {QStringLiteral("src/activitypage.cpp"), 5400},
        {QStringLiteral("src/charts.cpp"), 4200},
        {QStringLiteral("src/mockdata.cpp"), 3600},
        {QStringLiteral("src/mainwindow.cpp"), 2400},
        {QStringLiteral("CMakeLists.txt"), 1800},
        {QStringLiteral("src/timelinewidget.h"), 1500},
        {QStringLiteral("src/charts.h"), 1200},
        {QStringLiteral("src/activitypage.h"), 900},
        {QStringLiteral("src/theme.h"), 600},
    };
    QList<BarItem> items;
    for (int i = 0; i < qMin(limit, files.size()); ++i) {
        BarItem b;
        b.label = files[i].first;
        b.valueSeconds = files[i].second;
        b.color = colorForString(files[i].first);
        items.append(b);
    }
    return items;
}

} // namespace awqtui
