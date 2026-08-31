// timelinepage.cpp
#include "timelinepage.h"

#include "charts.h"
#include "mockdata.h"
#include "timelinewidget.h"

#include <QComboBox>
#include <QDateTime>
#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QVBoxLayout>

namespace awqtui {

static QFrame *createStatCard(QWidget *parent = nullptr)
{
    auto *card = new QFrame(parent);
    card->setObjectName(QStringLiteral("StatCard"));
    return card;
}

TimelinePage::TimelinePage(QWidget *parent) : QWidget(parent), m_date(QDate::currentDate())
{
    qDebug() << "[TimelinePage] ctor start";
    setStyleSheet(QStringLiteral(R"(
        QFrame#StatCard {
            background: #22262c;
            border: 1px solid #343a44;
            border-radius: 8px;
        }
        QLabel#StatLabel { color: #9aa4b0; font-size: 11px; padding: 8px 12px 0; }
        QLabel#StatValue { color: #e6e6e6; font-size: 18px; font-weight: 700; padding: 2px 12px 10px; }
        QLabel#ToolbarLabel { color: #9aa4b0; font-size: 12px; }
        QPushButton#NavArrow {
            background: transparent; border: 1px solid #343a44; border-radius: 4px;
            padding: 4px 10px; color: #c8cdd4; font-size: 14px;
        }
        QPushButton#NavArrow:hover { background: #2a2f37; border-color: #4c8bf5; }
        QPushButton#ToolBtn {
            background: #2a2f37; border: 1px solid #343a44; border-radius: 6px;
            padding: 5px 12px; color: #c8cdd4; font-size: 12px;
        }
        QPushButton#ToolBtn:hover { background: #30363f; border-color: #4c8bf5; }
        QComboBox {
            background: #2a2f37; border: 1px solid #343a44; border-radius: 6px;
            padding: 4px 8px; color: #c8cdd4; font-size: 12px; min-width: 100px;
        }
        QComboBox:hover { border-color: #4c8bf5; }
        QComboBox QAbstractItemView { background: #2a2f37; border: 1px solid #343a44; selection-background-color: #4c8bf5; }
    )"));

    qDebug() << "[TimelinePage] stylesheet set, calling buildUi...";
    buildUi();
    qDebug() << "[TimelinePage] buildUi done, calling reloadData...";
    reloadData();
    qDebug() << "[TimelinePage] ctor done";
}

void TimelinePage::buildUi()
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
    m_dateLabel->setStyleSheet(QStringLiteral("color: #e6e6e6; font-size: 14px; font-weight: 600; padding: 0 4px;"));
    m_nextBtn = new QPushButton(QStringLiteral("▶"));
    m_nextBtn->setObjectName(QStringLiteral("NavArrow"));
    m_todayBtn = new QPushButton(QStringLiteral("Today"));
    m_todayBtn->setObjectName(QStringLiteral("ToolBtn"));

    connect(m_prevBtn, &QPushButton::clicked, this, &TimelinePage::onPrevDay);
    connect(m_nextBtn, &QPushButton::clicked, this, &TimelinePage::onNextDay);
    connect(m_todayBtn, &QPushButton::clicked, this, &TimelinePage::onToday);

    toolbar->addWidget(m_prevBtn);
    toolbar->addWidget(m_dateLabel);
    toolbar->addWidget(m_nextBtn);
    toolbar->addWidget(m_todayBtn);
    toolbar->addSpacing(20);

    // Interval mode
    auto *intervalLabel = new QLabel(QStringLiteral("Interval mode:"));
    intervalLabel->setObjectName(QStringLiteral("ToolbarLabel"));
    m_intervalCombo = new QComboBox;
    m_intervalCombo->addItems({QStringLiteral("Last duration"), QStringLiteral("Merged duration"), QStringLiteral("First event")});
    toolbar->addWidget(intervalLabel);
    toolbar->addWidget(m_intervalCombo);
    toolbar->addSpacing(12);

    // Show last
    auto *showLabel = new QLabel(QStringLiteral("Show last:"));
    showLabel->setObjectName(QStringLiteral("ToolbarLabel"));
    m_showLastCombo = new QComboBox;
    m_showLastCombo->addItems({QStringLiteral("24h"), QStringLiteral("12h"), QStringLiteral("6h"), QStringLiteral("48h"), QStringLiteral("7d")});
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
    toolbar->addWidget(showLabel);
    toolbar->addWidget(m_showLastCombo);
    toolbar->addSpacing(12);

    m_eventsLabel = new QLabel;
    m_eventsLabel->setObjectName(QStringLiteral("ToolbarLabel"));
    toolbar->addWidget(m_eventsLabel);
    toolbar->addStretch(1);

    auto *hintLabel = new QLabel(QStringLiteral("Drag to pan and scroll to zoom"));
    hintLabel->setObjectName(QStringLiteral("ToolbarLabel"));
    hintLabel->setStyleSheet(QStringLiteral("color: #6b7280; font-size: 11px; font-style: italic;"));
    toolbar->addWidget(hintLabel);
    toolbar->addSpacing(8);

    m_resetBtn = new QPushButton(QStringLiteral("⟲ Reset view"));
    m_resetBtn->setObjectName(QStringLiteral("ToolBtn"));
    connect(m_resetBtn, &QPushButton::clicked, this, &TimelinePage::onResetView);
    toolbar->addWidget(m_resetBtn);

    root->addLayout(toolbar);

    // ── 时间线 ──
    m_timeline = new TimelineWidget;
    m_timeline->setLaneHeight(44);
    connect(m_timeline, &TimelineWidget::timeRangeChanged, this, &TimelinePage::onRangeChanged);
    root->addWidget(m_timeline, 1);

    // ── 底部统计卡片（Tockler 风格） ──
    auto *statsRow = new QHBoxLayout;
    statsRow->setSpacing(10);

    auto *totalCard = createStatCard();
    auto *totalLay = new QVBoxLayout(totalCard);
    totalLay->setContentsMargins(0, 0, 0, 0);
    auto *totalLbl = new QLabel(QStringLiteral("Total tracked"));
    totalLbl->setObjectName(QStringLiteral("StatLabel"));
    m_totalTracked = new QLabel(QStringLiteral("—"));
    m_totalTracked->setObjectName(QStringLiteral("StatValue"));
    totalLay->addWidget(totalLbl);
    totalLay->addWidget(m_totalTracked);
    statsRow->addWidget(totalCard, 1);

    auto *afkCard = createStatCard();
    auto *afkLay = new QVBoxLayout(afkCard);
    afkLay->setContentsMargins(0, 0, 0, 0);
    auto *afkLbl = new QLabel(QStringLiteral("AFK"));
    afkLbl->setObjectName(QStringLiteral("StatLabel"));
    m_afkTime = new QLabel(QStringLiteral("—"));
    m_afkTime->setObjectName(QStringLiteral("StatValue"));
    afkLay->addWidget(afkLbl);
    afkLay->addWidget(m_afkTime);
    statsRow->addWidget(afkCard, 1);

    auto *firstCard = createStatCard();
    auto *firstLay = new QVBoxLayout(firstCard);
    firstLay->setContentsMargins(0, 0, 0, 0);
    auto *firstLbl = new QLabel(QStringLiteral("First activity"));
    firstLbl->setObjectName(QStringLiteral("StatLabel"));
    m_firstActivity = new QLabel(QStringLiteral("—"));
    m_firstActivity->setObjectName(QStringLiteral("StatValue"));
    firstLay->addWidget(firstLbl);
    firstLay->addWidget(m_firstActivity);
    statsRow->addWidget(firstCard, 1);

    auto *lastCard = createStatCard();
    auto *lastLay = new QVBoxLayout(lastCard);
    lastLay->setContentsMargins(0, 0, 0, 0);
    auto *lastLbl = new QLabel(QStringLiteral("Last activity"));
    lastLbl->setObjectName(QStringLiteral("StatLabel"));
    m_lastActivity = new QLabel(QStringLiteral("—"));
    m_lastActivity->setObjectName(QStringLiteral("StatValue"));
    lastLay->addWidget(lastLbl);
    lastLay->addWidget(m_lastActivity);
    statsRow->addWidget(lastCard, 1);

    root->addLayout(statsRow);
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

    m_lanes = generateTimelineLanes(m_date);
    m_timeline->setLanes(m_lanes);

    const qint64 base = QDateTime(m_date, QTime(0, 0), Qt::LocalTime).toMSecsSinceEpoch();
    m_timeline->setTimeRange(base, base + 86400000LL);

    // 事件计数
    int totalEvents = 0;
    for (const auto &lane : m_lanes) totalEvents += lane.events.size();
    m_eventsLabel->setText(QStringLiteral("Events shown: %1").arg(totalEvents));

    updateStats();
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
