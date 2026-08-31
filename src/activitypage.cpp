// activitypage.cpp
#include "activitypage.h"

#include "charts.h"
#include "mockdata.h"
#include "theme.h"

#include <QDateTime>
#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
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

ActivityPage::ActivityPage(QWidget *parent) : QWidget(parent), m_date(QDate::currentDate())
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

    root->addWidget(m_tabs, 1);
}

void ActivityPage::setDate(const QDate &date)
{
    m_date = date;
    reloadData();
}

void ActivityPage::refresh()
{
    reloadData();
}

void ActivityPage::onPrevDay()
{
    m_date = m_date.addDays(-1);
    reloadData();
}

void ActivityPage::onNextDay()
{
    m_date = m_date.addDays(1);
    reloadData();
}

void ActivityPage::onToday()
{
    m_date = QDate::currentDate();
    reloadData();
}

void ActivityPage::reloadData()
{
    // 日期标签
    const QString weekday = m_date.toString(QStringLiteral("ddd"));
    m_dateLabel->setText(m_date.toString(QStringLiteral("yyyy-MM-dd ")) + weekday);

    m_hostLabel->setText(QStringLiteral("Host: DESKTOP-QTUI"));

    // 生成数据
    m_lanes = generateTimelineLanes(m_date);
    const QList<qint64> hourly = generateHourlyActivity(m_date);
    qint64 totalActive = 0;
    for (qint64 s : hourly) totalActive += s;
    m_activeLabel->setText(QStringLiteral("time active: %1").arg(formatDuration(totalActive)));

    m_hourlyBars->setData(hourly);

    // Summary
    m_topApps->setItems(generateTopApps(m_date, 7));
    m_topTitles->setItems(generateTopTitles(m_date, 7));
    const QList<BarItem> cats = generateTopCategories(m_date, 6);
    m_topCats->setItems(cats);
    m_catBars->setData(hourly, computeHourlyCategories());
    m_catTree->setItems(generateCategoryTree(m_date));
    m_donut->setItems(cats);

    // Window
    m_winApps->setItems(generateTopApps(m_date, 10));
    m_winTitles->setItems(generateTopTitles(m_date, 10));

    // Browser
    m_topDomains->setItems(computeTopDomains(8));
    m_topUrls->setItems(computeTopUrls(8));

    // Editor
    m_editorFiles->setItems(mockEditorFiles(10));
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
                    // 记录该小时时长最长的分类：简化为最后一个非空分类
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

QList<BarItem> ActivityPage::computeTopDomains(int limit) const
{
    QHash<QString, qint64> dur;
    for (const auto &lane : m_lanes) {
        if (!lane.name.startsWith(QStringLiteral("aw-watcher-web"))) continue;
        for (const auto &ev : lane.events) {
            dur[ev.label] += (ev.endMs - ev.startMs) / 1000;
        }
    }
    QList<BarItem> items;
    for (auto it = dur.constBegin(); it != dur.constEnd(); ++it) {
        BarItem b;
        b.label = it.key();
        b.valueSeconds = it.value();
        b.color = colorForString(it.key());
        items.append(b);
    }
    std::sort(items.begin(), items.end(), [](const BarItem &a, const BarItem &b) {
        return a.valueSeconds > b.valueSeconds;
    });
    if (items.size() > limit) items = items.mid(0, limit);
    return items;
}

QList<BarItem> ActivityPage::computeTopUrls(int limit) const
{
    // mock：在域名后加路径
    QList<BarItem> domains = computeTopDomains(limit * 2);
    static const QStringList paths = {
        QStringLiteral("/"), QStringLiteral("/search?q=qt"), QStringLiteral("/activitywatch/activitywatch"),
        QStringLiteral("/watch"), QStringLiteral("/questions/12345"), QStringLiteral("/trending"),
        QStringLiteral("/feed"), QStringLiteral("/user/profile"), QStringLiteral("/docs/api"),
    };
    QList<BarItem> result;
    for (int i = 0; i < domains.size() && result.size() < limit; ++i) {
        BarItem b = domains[i];
        b.label = domains[i].label + paths[i % paths.size()];
        b.valueSeconds = qMax<qint64>(60, domains[i].valueSeconds * (0.4 + 0.3 * (i % 3)));
        result.append(b);
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
