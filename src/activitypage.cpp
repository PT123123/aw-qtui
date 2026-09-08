// activitypage.cpp
#include "activitypage.h"
#include "ui_activitypage.h"

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

ActivityPage::~ActivityPage()
{
    delete ui;
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
    // 静态布局来自 Qt Designer（activitypage.ui -> ui_activitypage.h）
    ui = new Ui::ActivityPage;
    ui->setupUi(this);

    // ── 成员别名：业务逻辑沿用 m_* 指针，静态布局归属 .ui 文件 ──
    m_prevBtn = ui->prevBtn;
    m_dateLabel = ui->dateLabel;
    m_nextBtn = ui->nextBtn;
    m_todayBtn = ui->todayBtn;
    m_chipToday = ui->chipToday;
    m_chipYesterday = ui->chipYesterday;
    m_chipLast7 = ui->chipLast7;
    m_chipLast30 = ui->chipLast30;
    m_chipAll = ui->chipAll;
    m_hostLabel = ui->hostLabel;
    m_activeLabel = ui->activeLabel;
    m_hourlyBars = ui->hourlyBars;
    m_tabs = ui->tabs;
    m_topApps = ui->topApps;
    m_topTitles = ui->topTitles;
    m_topCats = ui->topCats;
    m_catBars = ui->catBars;
    m_catTree = ui->catTree;
    m_donut = ui->donut;
    m_winApps = ui->winApps;
    m_winTitles = ui->winTitles;
    m_topDomains = ui->topDomains;
    m_topUrls = ui->topUrls;
    m_editorFiles = ui->editorFiles;
    m_trendPlaceholder = ui->trendPlaceholder;
    m_trendApps = ui->trendApps;
    m_trendCats = ui->trendCats;
    m_trendDaily = ui->trendDaily;

    // 主题样式角色（applyTheme 的 QSS 按 objectName 选择器匹配）
    for (QFrame *card : {ui->barsCard, ui->appsCard, ui->titlesCard, ui->catsCard,
                         ui->cbCard, ui->treeCard, ui->donutCard, ui->winAppsCard,
                         ui->winTitlesCard, ui->domCard, ui->urlCard, ui->edCard,
                         ui->trendPlaceholder})
        card->setObjectName(QStringLiteral("Card"));
    for (QLabel *title : {ui->barsTitle, ui->appsTitle, ui->titlesTitle, ui->catsTitle,
                          ui->cbTitle, ui->treeTitle, ui->donutTitle, ui->winAppsTitle,
                          ui->winTitlesTitle, ui->domTitle, ui->urlTitle, ui->edTitle,
                          ui->tAppsTitle, ui->tCatsTitle, ui->tDailyTitle})
        title->setObjectName(QStringLiteral("CardTitle"));
    m_prevBtn->setObjectName(QStringLiteral("NavArrow"));
    m_nextBtn->setObjectName(QStringLiteral("NavArrow"));
    m_todayBtn->setObjectName(QStringLiteral("ToolBtn"));
    m_chipToday->setObjectName(QStringLiteral("ChipBtn"));
    m_chipYesterday->setObjectName(QStringLiteral("ChipBtn"));
    m_chipLast7->setObjectName(QStringLiteral("ChipBtn"));
    m_chipLast30->setObjectName(QStringLiteral("ChipBtn"));
    m_chipAll->setObjectName(QStringLiteral("ChipBtn"));
    m_hostLabel->setObjectName(QStringLiteral("ToolbarLabel"));
    m_activeLabel->setObjectName(QStringLiteral("ToolbarLabel"));
    ui->filtersBtn->setObjectName(QStringLiteral("ToolBtn"));
    ui->refreshBtn->setObjectName(QStringLiteral("ToolBtn"));
    ui->newViewBtn->setObjectName(QStringLiteral("ToolBtn"));
    ui->phLabel->setObjectName(QStringLiteral("ToolbarLabel"));

    // 图表参数（自定义方法，非 Q_PROPERTY，无法进 .ui）
    m_topApps->setLabelWidth(130);
    m_topTitles->setLabelWidth(170);
    m_topCats->setLabelWidth(130);
    m_catTree->setLabelWidth(160);
    m_winApps->setLabelWidth(150);
    m_winTitles->setLabelWidth(200);
    m_topDomains->setLabelWidth(160);
    m_topUrls->setLabelWidth(180);
    m_editorFiles->setLabelWidth(260);
    m_trendApps->setLabelWidth(150);
    m_trendCats->setLabelWidth(150);
    m_trendDaily->setLabelWidth(100);

    // 信号连接
    connect(m_prevBtn, &QPushButton::clicked, this, &ActivityPage::onPrevDay);
    connect(m_nextBtn, &QPushButton::clicked, this, &ActivityPage::onNextDay);
    connect(m_todayBtn, &QPushButton::clicked, this, &ActivityPage::onToday);
    connect(m_chipToday, &QPushButton::clicked, this, &ActivityPage::onDateChipToday);
    connect(m_chipYesterday, &QPushButton::clicked, this, &ActivityPage::onDateChipYesterday);
    connect(m_chipLast7, &QPushButton::clicked, this, &ActivityPage::onDateChipLast7);
    connect(m_chipLast30, &QPushButton::clicked, this, &ActivityPage::onDateChipLast30);
    connect(m_chipAll, &QPushButton::clicked, this, &ActivityPage::onDateChipAll);
    connect(ui->refreshBtn, &QPushButton::clicked, this, &ActivityPage::refresh);
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
