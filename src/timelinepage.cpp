// timelinepage.cpp —— ActivityWatch Timeline / Tockler 风格可交互时间线页
#include "timelinepage.h"
#include "ui_timelinepage.h"

#include "apiclient.h"
#include "awdatastore.h"
#include "charts.h"
#include "mockdata.h"
#include "theme.h"
#include "timelinewidget.h"

#include <QComboBox>
#include <QDateTime>
#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkReply>
#include <QPushButton>
#include <QVBoxLayout>

namespace awqtui {

TimelinePage::TimelinePage(ApiClient *api, QWidget *parent)
    : QWidget(parent), m_api(api), m_date(QDate::currentDate())
{
    qDebug() << "[TimelinePage] ctor start";
    qDebug() << "[TimelinePage] calling buildUi...";
    buildUi();
    applyTheme();
    qDebug() << "[TimelinePage] buildUi done, calling reloadData...";
    reloadData();
    qDebug() << "[TimelinePage] ctor done";
}

void TimelinePage::applyTheme()
{
    setStyleSheet(QStringLiteral(R"(
        QFrame#StatCard {
            background: %1;
            border: 1px solid %2;
            border-radius: 8px;
        }
        QLabel#StatLabel { color: %4; font-size: 11px; padding: 8px 12px 0; }
        QLabel#StatValue { color: %3; font-size: 18px; font-weight: 700; padding: 2px 12px 10px; }
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
        QComboBox {
            background: %6; border: 1px solid %2; border-radius: 6px;
            padding: 4px 8px; color: %5; font-size: 12px; min-width: 100px;
        }
        QComboBox:hover { border-color: %7; }
        QComboBox QAbstractItemView { background: %6; border: 1px solid %2; selection-background-color: %7; }
    )")
                                  .arg(kColorBgElev, kColorBorder, kColorFg, kColorFgMuted,
                                       kColorFgSoft, kColorBgElev2, kColorAccent, kColorHover));
    if (m_dateLabel)
        m_dateLabel->setStyleSheet(
            QStringLiteral("color: %1; font-size: 14px; font-weight: 600; padding: 0 4px;")
                .arg(kColorFg));
}

void TimelinePage::buildUi()
{
    // 静态布局来自 Qt Designer（timelinepage.ui -> ui_timelinepage.h）
    ui = new Ui::TimelinePage;
    ui->setupUi(this);

    // 主题样式角色（applyTheme 的 QSS 按 objectName 选择器匹配；.ui 中名称保持唯一）
    for (auto *c : {ui->StatCard, ui->afkCard, ui->firstCard, ui->lastCard})
        c->setObjectName(QStringLiteral("StatCard"));
    for (auto *l : {ui->StatLabel, ui->afkLabel, ui->firstLabel, ui->lastLabel})
        l->setObjectName(QStringLiteral("StatLabel"));
    for (auto *v : {ui->totalTracked, ui->afkTime, ui->firstActivity, ui->lastActivity})
        v->setObjectName(QStringLiteral("StatValue"));
    for (auto *l : {ui->intervalLabel, ui->showLabel, ui->eventsLabel, ui->hintLabel})
        l->setObjectName(QStringLiteral("ToolbarLabel"));
    for (auto *b : {ui->prevBtn, ui->nextBtn})
        b->setObjectName(QStringLiteral("NavArrow"));
    for (auto *b : {ui->todayBtn, ui->resetBtn})
        b->setObjectName(QStringLiteral("ToolBtn"));

    // 主题色相关样式（kColor* 随主题切换，无法烘焙进 .ui）
    m_dateLabel = ui->dateLabel;
    m_dateLabel->setStyleSheet(
        QStringLiteral("color: %1; font-size: 14px; font-weight: 600; padding: 0 4px;")
            .arg(kColorFg));
    ui->hintLabel->setStyleSheet(
        QStringLiteral("color: %1; font-size: 11px; font-style: italic;").arg(kColorMuted2));

    // ── 成员别名：业务逻辑沿用 m_* 指针 ──
    m_prevBtn = ui->prevBtn;
    m_nextBtn = ui->nextBtn;
    m_todayBtn = ui->todayBtn;
    m_intervalCombo = ui->intervalCombo;
    m_showLastCombo = ui->showLastCombo;
    m_eventsLabel = ui->eventsLabel;
    m_resetBtn = ui->resetBtn;
    m_totalTracked = ui->totalTracked;
    m_afkTime = ui->afkTime;
    m_firstActivity = ui->firstActivity;
    m_lastActivity = ui->lastActivity;

    // 时间线控件本体在 .ui 中，仅设置运行时行为
    m_timeline = ui->timeline;
    m_timeline->setLaneHeight(44);

    // ── 信号连接 ──
    connect(m_prevBtn, &QPushButton::clicked, this, &TimelinePage::onPrevDay);
    connect(m_nextBtn, &QPushButton::clicked, this, &TimelinePage::onNextDay);
    connect(m_todayBtn, &QPushButton::clicked, this, &TimelinePage::onToday);
    connect(m_resetBtn, &QPushButton::clicked, this, &TimelinePage::onResetView);
    connect(m_showLastCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this](int idx) {
        const qint64 base = QDateTime(m_date, QTime(0, 0), Qt::LocalTime).toMSecsSinceEpoch();
        qint64 range = 86400000LL;
        switch (idx) {
        case 0: range = 86400000LL; break;
        case 1: range = 43200000LL; break;
        case 2: range = 21600000LL; break;
        case 3: range = 172800000LL; break;
        case 4: range = 7 * 86400000LL; break;
        }
        m_timeline->setTimeRange(base, base + range);
    });
    connect(m_timeline, &TimelineWidget::timeRangeChanged, this, &TimelinePage::onRangeChanged);
}

void TimelinePage::setDate(const QDate &date)
{
    m_date = date;
    reloadData();
}

void TimelinePage::refresh()
{
    reloadData();
}

void TimelinePage::onPrevDay()
{
    m_date = m_date.addDays(-1);
    reloadData();
}

void TimelinePage::onNextDay()
{
    m_date = m_date.addDays(1);
    reloadData();
}

void TimelinePage::onToday()
{
    m_date = QDate::currentDate();
    reloadData();
}

void TimelinePage::onResetView()
{
    m_timeline->resetView();
}

void TimelinePage::onRangeChanged(qint64, qint64)
{
    // 可以在这里更新当前可见范围的统计，暂留空
}

void TimelinePage::reloadData()
{
    const QString weekday = m_date.toString(QStringLiteral("ddd"));
    m_dateLabel->setText(m_date.toString(QStringLiteral("yyyy-MM-dd ")) + weekday);

    const qint64 base = QDateTime(m_date, QTime(0, 0), Qt::LocalTime).toMSecsSinceEpoch();
    m_timeline->setTimeRange(base, base + 86400000LL);

    if (!m_api) {
        m_lanes = generateTimelineLanes(m_date);
        m_timeline->setLanes(m_lanes);
        int totalEvents = 0;
        for (const auto &lane : m_lanes) totalEvents += lane.events.size();
        m_eventsLabel->setText(QStringLiteral("Events shown: %1").arg(totalEvents));
        updateStats();
        return;
    }

    m_loading = true;
    m_eventsLabel->setText(QStringLiteral("Events shown: loading…"));
    QNetworkReply *reply = m_api->getBuckets();
    connect(reply, &QNetworkReply::finished, this, &TimelinePage::onBucketsLoaded);
}

void TimelinePage::onBucketsLoaded()
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

void TimelinePage::fetchAllEvents()
{
    m_eventsMap.clear();
    m_pendingEvents = m_buckets.size();

    const qint64 dayStart = QDateTime(m_date, QTime(0, 0), Qt::LocalTime).toMSecsSinceEpoch();
    const qint64 dayEnd = dayStart + 86400000LL;

    for (const BucketInfo &b : m_buckets) {
        QNetworkReply *reply = m_api->getEvents(b.id, dayStart, dayEnd);
        reply->setProperty("bucketId", b.id);
        connect(reply, &QNetworkReply::finished, this, &TimelinePage::onEventLoaded);
    }
}

void TimelinePage::onEventLoaded()
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
        m_timeline->setLanes(m_lanes);
        int totalEvents = 0;
        for (const auto &lane : m_lanes)
            totalEvents += lane.events.size();
        m_eventsLabel->setText(QStringLiteral("Events shown: %1").arg(totalEvents));
        updateStats();
    }
}

void TimelinePage::showEmptyState(const QString &msg)
{
    m_loading = false;
    m_lanes.clear();
    m_timeline->setLanes({});
    m_eventsLabel->setText(QStringLiteral("Events shown: 0"));
    m_totalTracked->setText(QStringLiteral("—"));
    m_afkTime->setText(QStringLiteral("—"));
    m_firstActivity->setText(QStringLiteral("—"));
    m_lastActivity->setText(QStringLiteral("—"));
    qWarning() << "[TimelinePage]" << msg;
}

void TimelinePage::updateStats()
{
    qint64 activeMs = 0;
    qint64 afkMs = 0;
    qint64 firstActive = -1;
    qint64 lastActive = -1;

    for (const auto &lane : m_lanes) {
        if (lane.name != QStringLiteral("afk-status")) continue;
        for (const auto &ev : lane.events) {
            if (ev.label == QStringLiteral("not-afk")) {
                activeMs += ev.endMs - ev.startMs;
                if (firstActive < 0 || ev.startMs < firstActive) firstActive = ev.startMs;
                if (ev.endMs > lastActive) lastActive = ev.endMs;
            } else {
                afkMs += ev.endMs - ev.startMs;
            }
        }
    }

    m_totalTracked->setText(formatDuration(activeMs / 1000));
    m_afkTime->setText(formatDuration(afkMs / 1000));
    if (firstActive >= 0) {
        m_firstActivity->setText(QDateTime::fromMSecsSinceEpoch(firstActive, Qt::LocalTime).toString(QStringLiteral("HH:mm:ss")));
    } else {
        m_firstActivity->setText(QStringLiteral("—"));
    }
    if (lastActive >= 0) {
        m_lastActivity->setText(QDateTime::fromMSecsSinceEpoch(lastActive, Qt::LocalTime).toString(QStringLiteral("HH:mm:ss")));
    } else {
        m_lastActivity->setText(QStringLiteral("—"));
    }
}

} // namespace awqtui
