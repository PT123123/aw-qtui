// mainwindow.cpp
#include "mainwindow.h"

#include "activitypage.h"
#include "apiclient.h"
#include "config.h"
#include "daypage.h"
#include "inboxpage.h"
#include "mdnsdiscovery.h"
#include "statspage.h"
#include "syncpage.h"
#include "tagstore.h"
#include "theme.h"
#include "timelinepage.h"

#include <QButtonGroup>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QPushButton>
#include <QTimer>
#include <QVBoxLayout>

namespace awqtui {

MainWindow::MainWindow(const QString &serverUrl, QWidget *parent) : QMainWindow(parent)
{
    m_api = new ApiClient(this);
    if (!serverUrl.isEmpty())
        m_api->setBaseUrl(serverUrl);
    m_mdns = new MdnsDiscovery(this);
    m_tagStore = new TagStore;
    m_tagStore->load();
    buildUi();

    QTimer *t = new QTimer(this);
    connect(t, &QTimer::timeout, this, &MainWindow::updateStatus);
    t->start(30000);
    QTimer::singleShot(0, this, &MainWindow::updateStatus);
}

MainWindow::~MainWindow()
{
    delete m_tagStore;
}

void MainWindow::buildUi()
{
    auto *central = new QWidget;
    auto *root = new QHBoxLayout(central);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(0);

    // ---- 左侧导航 ----
    auto *nav = new QWidget;
    nav->setObjectName(QStringLiteral("NavSidebar"));
    nav->setFixedWidth(200);
    auto *navLay = new QVBoxLayout(nav);
    navLay->setContentsMargins(0, 16, 0, 12);
    navLay->setSpacing(4);

    auto *brand = new QLabel(QStringLiteral("aw · qtui"));
    brand->setStyleSheet(QStringLiteral("font-size: 20px; font-weight: 700; color: white; padding: 0 16px 12px;"));
    navLay->addWidget(brand);

    // 分组标签：ActivityWatch
    auto *awSection = new QLabel(QStringLiteral("ACTIVITYWATCH"));
    awSection->setStyleSheet(QStringLiteral("color: #6b7280; font-size: 10px; font-weight: 700; padding: 8px 16px 4px; letter-spacing: 1px;"));
    navLay->addWidget(awSection);

    m_navActivity = new QPushButton(QStringLiteral("📊  Activity"));
    m_navTimeline = new QPushButton(QStringLiteral("⏱  Timeline"));
    m_navActivity->setObjectName(QStringLiteral("NavBtn"));
    m_navTimeline->setObjectName(QStringLiteral("NavBtn"));
    m_navActivity->setCheckable(true);
    m_navTimeline->setCheckable(true);
    m_navActivity->setChecked(true);
    navLay->addWidget(m_navActivity);
    navLay->addWidget(m_navTimeline);

    // 分组标签：Inbox & Sync
    auto *inboxSection = new QLabel(QStringLiteral("INBOX & SYNC"));
    inboxSection->setStyleSheet(QStringLiteral("color: #6b7280; font-size: 10px; font-weight: 700; padding: 12px 16px 4px; letter-spacing: 1px;"));
    navLay->addWidget(inboxSection);

    m_navInbox = new QPushButton(QStringLiteral("📥  收件箱"));
    m_navSync = new QPushButton(QStringLiteral("⇄  局域网同步"));
    m_navInbox->setObjectName(QStringLiteral("NavBtn"));
    m_navSync->setObjectName(QStringLiteral("NavBtn"));
    m_navInbox->setCheckable(true);
    m_navSync->setCheckable(true);
    navLay->addWidget(m_navInbox);
    navLay->addWidget(m_navSync);

    // 分组标签：时间标签（ManicTime 特性移植）
    auto *tagSection = new QLabel(QStringLiteral("TIME TAGS"));
    tagSection->setStyleSheet(QStringLiteral("color: #6b7280; font-size: 10px; font-weight: 700; padding: 12px 16px 4px; letter-spacing: 1px;"));
    navLay->addWidget(tagSection);

    m_navDay = new QPushButton(QStringLiteral("🏷  标签 Day"));
    m_navDay->setObjectName(QStringLiteral("NavBtn"));
    m_navDay->setCheckable(true);
    navLay->addWidget(m_navDay);

    m_navStats = new QPushButton(QStringLiteral("📈  统计"));
    m_navStats->setObjectName(QStringLiteral("NavBtn"));
    m_navStats->setCheckable(true);
    navLay->addWidget(m_navStats);

    auto *grp = new QButtonGroup(this);
    grp->addButton(m_navActivity);
    grp->addButton(m_navTimeline);
    grp->addButton(m_navInbox);
    grp->addButton(m_navSync);
    grp->addButton(m_navDay);
    grp->addButton(m_navStats);

    connect(m_navActivity, &QPushButton::clicked, this, [this] { switchPage(0); });
    connect(m_navTimeline, &QPushButton::clicked, this, [this] { switchPage(1); });
    connect(m_navInbox, &QPushButton::clicked, this, [this] { switchPage(2); });
    connect(m_navSync, &QPushButton::clicked, this, [this] { switchPage(3); });
    connect(m_navDay, &QPushButton::clicked, this, [this] { switchPage(4); });
    connect(m_navStats, &QPushButton::clicked, this, [this] { switchPage(5); });

    navLay->addStretch(1);

    m_deviceLabel = new QLabel;
    m_deviceLabel->setWordWrap(true);
    m_deviceLabel->setStyleSheet(QStringLiteral("color: %1; font-size: 11px; padding: 0 16px;")
                                     .arg(kColorFgMuted));
    navLay->addWidget(m_deviceLabel);
    root->addWidget(nav);

    // ---- 页面堆栈 ----
    m_stack = new QStackedWidget;
    qDebug() << "[MainWindow] creating ActivityPage...";
    m_activity = new ActivityPage;
    qDebug() << "[MainWindow] ActivityPage created";
    qDebug() << "[MainWindow] creating TimelinePage...";
    m_timeline = new TimelinePage;
    qDebug() << "[MainWindow] TimelinePage created";
    qDebug() << "[MainWindow] creating InboxPage...";
    m_inbox = new InboxPage(m_api);
    qDebug() << "[MainWindow] InboxPage created";
    qDebug() << "[MainWindow] creating SyncPage...";
    m_sync = new SyncPage(m_api, m_mdns);
    qDebug() << "[MainWindow] SyncPage created";
    qDebug() << "[MainWindow] creating DayPage...";
    m_day = new DayPage(m_tagStore);
    qDebug() << "[MainWindow] DayPage created";
    qDebug() << "[MainWindow] creating StatsPage...";
    m_stats = new StatsPage(m_tagStore);
    qDebug() << "[MainWindow] StatsPage created";
    m_stack->addWidget(m_activity);   // 0
    m_stack->addWidget(m_timeline);   // 1
    m_stack->addWidget(m_inbox);      // 2
    m_stack->addWidget(m_sync);       // 3
    m_stack->addWidget(m_day);        // 4
    m_stack->addWidget(m_stats);      // 5
    qDebug() << "[MainWindow] all pages added to stack";
    root->addWidget(m_stack, 1);

    setCentralWidget(central);
    setWindowTitle(QStringLiteral("aw-qtui — ActivityWatch 客户端"));
    resize(1280, 820);
}

void MainWindow::switchPage(int index)
{
    m_stack->setCurrentIndex(index);
    m_navActivity->setChecked(index == 0);
    m_navTimeline->setChecked(index == 1);
    m_navInbox->setChecked(index == 2);
    m_navSync->setChecked(index == 3);
    m_navDay->setChecked(index == 4);
    m_navStats->setChecked(index == 5);
    if (index == 3)
        m_sync->refreshDevices();
}

void MainWindow::updateStatus()
{
    const QString dev = QStringLiteral("设备 %1\n%2 · %3")
                            .arg(deviceId(), hostname(), platform());
    m_deviceLabel->setText(dev);
    m_sync->heartbeat();
}

void MainWindow::keyPressEvent(QKeyEvent *event)
{
    // 快捷键：1-5 切页，F5 刷新当前页
    switch (event->key()) {
    case Qt::Key_1: switchPage(0); return;
    case Qt::Key_2: switchPage(1); return;
    case Qt::Key_3: switchPage(2); return;
    case Qt::Key_4: switchPage(3); return;
    case Qt::Key_5: switchPage(4); return;
    case Qt::Key_6: switchPage(5); return;
    case Qt::Key_F5:
        if (m_stack->currentIndex() == 0) m_activity->refresh();
        else if (m_stack->currentIndex() == 1) m_timeline->refresh();
        else if (m_stack->currentIndex() == 2) m_inbox->refreshAll();
        else if (m_stack->currentIndex() == 4) m_day->refresh();
        else if (m_stack->currentIndex() == 5) m_stats->refresh();
        return;
    default: break;
    }
    QMainWindow::keyPressEvent(event);
}

} // namespace awqtui
