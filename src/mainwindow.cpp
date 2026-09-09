// mainwindow.cpp
#include "mainwindow.h"
#include "ui_mainwindow.h"

#include "activitypage.h"
#include "apiclient.h"
#include "appsettings.h"
#include "config.h"
#include "daypage.h"
#include "focusstore.h"
#include "focuswidgets.h"
#include "focuscharts.h"
#include "globalshortcut.h"
#include "inboxpage.h"
#include "inboxsettingspage.h"
#include "localstore.h"
#include "mdnsdiscovery.h"
#include "models.h"
#include "settingsdialog.h"
#include "statspage.h"
#include "syncpage.h"
#include "syncdetailspage.h"
#include "cloudbackuppage.h"
#include "querypage.h"
#include "d1syncpage.h"
#include "tagstore.h"
#include "theme.h"
#include "todopage.h"
#include "todostore.h"
#include "watcher.h"

#include <QApplication>
#include <QButtonGroup>
#include <QCloseEvent>
#include <QComboBox>
#include <QEasingCurve>
#include <QEvent>
#include <QFont>
#include <QFontMetrics>
#include <QFrame>
#include <QGraphicsOpacityEffect>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QKeySequence>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QPropertyAnimation>
#include <QPushButton>
#include <QScreen>
#include <QScrollArea>
#include <QSizePolicy>
#include <QSpinBox>
#include <QSystemTrayIcon>
#include <QTabWidget>
#include <QTextEdit>
#include <QTimer>
#include <QToolButton>
#include <QVBoxLayout>
#include <QWheelEvent>

#ifdef Q_OS_WIN
#include <windows.h>
#include <dwmapi.h>
#pragma comment(lib, "dwmapi.lib")
#endif

#include <cmath>

namespace awqtui {

// 左侧导航宽度（缩放前基准 px）：窄栏仅图标 / 展开显示图标+文字
static const int kNavCollapsedPx = 56;
static const int kNavExpandedPx = 148;

// 最小窗口尺寸：保证收件箱工具栏（搜索框 240 + 排序 108 + 按钮组）与卡片
// 头部「⋯」菜单按钮不被压出可视区；数值随 UI 缩放（si），并按当前屏幕钳制
// （高倍缩放或小屏时不超过屏幕可用区域，避免窗口大于屏幕无法完整显示）。
static void applyWindowMinimumSize(QMainWindow *win)
{
    QSize sz(si(980), si(600));
    if (const QScreen *s = win->screen())
        sz = sz.boundedTo(s->availableGeometry().size());
    win->setMinimumSize(sz);
}


// 缩放吸附档位：仅 gFixSnapZoom 开启时使用，把缩放吸附到"干净"倍率，
// 避免非整数缩放导致控件落在亚像素位置、1px 边框发虚。关闭时保留自由缩放（1.15 倍步进）。
static qreal snapZoom(qreal z)
{
    static const qreal kSteps[] = {0.50, 0.75, 1.00, 1.25, 1.50, 1.75,
                                   2.00, 2.25, 2.50, 2.75, 3.00};
    qreal best = z;
    qreal bestDist = 1e9;
    for (const qreal s : kSteps) {
        const qreal d = qAbs(s - z);
        if (d < bestDist) {
            bestDist = d;
            best = s;
        }
    }
    return best;
}

MainWindow::MainWindow(const QString &serverUrl, QWidget *parent) : QMainWindow(parent)
{
    m_api = new ApiClient(this);
    if (!serverUrl.isEmpty())
        m_api->setBaseUrl(serverUrl);

    // 启动内置 watcher：当前窗口（1s 心跳）+ AFK 状态（10s 心跳），上报到 aw-server
    auto *windowWatcher = new WindowWatcher(m_api, this);
    windowWatcher->start();
    auto *afkWatcher = new AfkWatcher(m_api, this);
    afkWatcher->start();
    m_mdns = new MdnsDiscovery(this);
    m_tagStore = new TagStore;
    m_tagStore->load();
    m_todoStore = new TodoApiStore(m_api, this);
    m_todoStore->load();
    // 专注数据：本地优先（focus_local.json）；后续接 Rust /focus 端点时换 FocusApiStore
    m_focusStore = new FocusStore(this);
    m_focusStore->load();
    // 收件箱本地存储（离线优先）
    m_localStore = new LocalStore;
    m_localStore->load();

    // 界面效果配置：先于 buildUi 载入，确保页面在创建时就按配置渲染
    const UiEffects fx = loadUiEffects();
    gShadowLevel = fx.shadowLevel;
    gGlassLevel = fx.glassLevel;
    gFxAnimations = fx.animations;
    gDwmBackdrop = fx.dwmBackdrop;
    gFixEdgeLowContrast = fx.fixEdgeLowContrast;
    gFixGlassOpaque = fx.fixGlassOpaque;
    gFixSnapZoom = fx.fixSnapZoom;
    gFixShadowAdaptive = fx.fixShadowAdaptive;

    // 左侧导航展开状态：读取上次设置（默认收起 → 窄栏图标模式）
    m_navExpanded = !loadNavCollapsed();

    // 页面缩放：先于 buildUi 载入，确保设置编辑组件里的滑块初始化为已保存的比例
    m_zoom = loadUiZoom();
    // 缩放对齐开启时，把历史保存的非整数缩放吸附到干净档位并持久化（避免边缘发虚）
    if (gFixSnapZoom) {
        const qreal snapped = snapZoom(m_zoom);
        if (!qFuzzyCompare(snapped, m_zoom)) {
            m_zoom = snapped;
            saveUiZoom(m_zoom);
        }
    }

    buildUi();
    // 按读取到的状态应用窄栏/展开（buildUi 默认构建窄栏）
    setNavExpanded(m_navExpanded);

    applyUiScale();
    qApp->installEventFilter(this);

    // 键盘 Ctrl± 缩放防抖：连按/长按时累计目标值，停顿约 80ms 后才真正 applyUiScale
    m_zoomInputTimer = new QTimer(this);
    m_zoomInputTimer->setSingleShot(true);
    m_zoomInputTimer->setInterval(80);
    connect(m_zoomInputTimer, &QTimer::timeout, this, [this] {
        if (m_zoomPending >= 0.0) {
            const qreal target = m_zoomPending;
            m_zoomPending = -1.0;
            setZoom(target, false); // 落位：吸附 + 持久化 + 重建当前可见页
        }
    });

    // 全局热键：注册到主窗口 HWND，应用失焦/最小化时仍能触发（WM_HOTKEY -> nativeEvent）
    m_hotkey = new GlobalHotkey(this);
    connect(m_hotkey, &GlobalHotkey::activated, this, &MainWindow::onGlobalHotkey);
    // 窗口 show() 之后再注册：构造函数里 winId() 的 HWND 尚未完全就绪，
    // RegisterHotKey 会失败（GetLastError==0）。
    QTimer::singleShot(0, this, [this] { applyShortcuts(); });

    QTimer *t = new QTimer(this);
    connect(t, &QTimer::timeout, this, &MainWindow::updateStatus);
    t->start(30000);
    QTimer::singleShot(0, this, &MainWindow::updateStatus);
    // 窗口显示后应用 DWM 系统背景（Mica/Acrylic），需 HWND 就绪
    QTimer::singleShot(0, this, &MainWindow::applyDwmBackdrop);

    // 系统托盘：程序图标（makeAppIcon，无外部资源），左键切换显示/隐藏，右键菜单
    setupTray();
}

MainWindow::~MainWindow()
{
    delete m_tagStore;
    delete m_localStore;
    delete ui;
}

// 系统托盘：图标用 makeAppIcon()（对齐 aw-android-native 启动图标：白底圆 + 琥珀黄盘 + 青绿时钟），
// 已含 16/32px 托盘尺寸，零外部资源。
// 交互：左键/双击切换显示隐藏；右键菜单「显示/隐藏主窗口」「退出」；关窗默认最小化到托盘。
void MainWindow::setupTray()
{
    if (!QSystemTrayIcon::isSystemTrayAvailable())
        return;
    m_tray = new QSystemTrayIcon(this);
    m_tray->setIcon(makeAppIcon());
    m_tray->setToolTip(QStringLiteral("aw-qtui · %1").arg(QString::fromUtf8(gTheme->name)));

    auto *menu = new QMenu(this);
    auto *toggleAct = menu->addAction(QStringLiteral("显示 / 隐藏主窗口"));
    menu->addSeparator();
    auto *settingsAct = menu->addAction(QStringLiteral("设置"));
    menu->addSeparator();
    auto *quitAct = menu->addAction(QStringLiteral("退出"));
    m_tray->setContextMenu(menu);

    const auto toggleWindow = [this] {
        if (isVisible() && !isMinimized())
            hide();
        else
            wakeUpAndShow();
    };
    connect(toggleAct, &QAction::triggered, this, toggleWindow);
    connect(settingsAct, &QAction::triggered, this, &MainWindow::openSettings);
    connect(quitAct, &QAction::triggered, this, [this] {
        m_trayExiting = true;
        qApp->quit();
    });
    connect(m_tray, &QSystemTrayIcon::activated, this,
            [toggleWindow](QSystemTrayIcon::ActivationReason reason) {
                if (reason == QSystemTrayIcon::Trigger || reason == QSystemTrayIcon::DoubleClick)
                    toggleWindow();
            });

    m_tray->show();
}

// 关窗最小化到托盘：保持后台运行（全局热键 / 服务端看护照常工作）；托盘菜单「退出」才真正退出。
void MainWindow::closeEvent(QCloseEvent *event)
{
    if (m_tray && m_tray->isVisible() && !m_trayExiting) {
        hide();
        event->ignore();
        return;
    }
    QMainWindow::closeEvent(event);
}

void MainWindow::buildUi()
{
    // 静态外壳来自 Qt Designer（mainwindow.ui -> ui_mainwindow.h）：
    // 中央布局 + NavSidebar/navLay 容器 + 页面栈（4 个容器页已声明在栈内）。
    // 导航按钮/折叠分组与真实页面均为运行时动态构建（图标字形、构造参数依赖）。
    ui = new Ui::MainWindow;
    ui->setupUi(this);
    auto *navLay = ui->navLay;

    // ---- 左侧导航：四分组 ----
    m_nav = ui->NavSidebar;
    m_nav->setFixedWidth(si(kNavCollapsedPx));

    // 展开/收起切换按钮（顶部留白呼吸，不加分隔线；图标随展开态在 updateNavIcons 重绘）
    m_navToggle = new QToolButton;
    m_navToggle->setObjectName(QStringLiteral("NavToggle"));
    m_navToggle->setToolTip(QStringLiteral("展开导航"));
    m_navToggle->setCursor(Qt::PointingHandCursor);
    m_navToggle->setProperty("expanded", false);
    navLay->addWidget(m_navToggle);
    connect(m_navToggle, &QToolButton::clicked, this, [this] { setNavExpanded(!m_navExpanded); });

    // 可折叠分组
    struct NavSection {
        QToolButton *header;
        QWidget *box;
        QVBoxLayout *layout;
    };
    auto makeSection = [&navLay](const QString &title, bool expanded) {
        auto *header = new QToolButton;
        header->setObjectName(QStringLiteral("NavSection"));
        header->setText(title);
        header->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
        header->setCheckable(true);
        header->setChecked(expanded);
        // chevron 用 Segoe 图标字体字形（折叠 ▶ / 展开 ▼），随 toggled 换图
        header->setIcon(glyphIcon(expanded ? glyph::ChevDown : glyph::ChevRight,
                                  QColor(kColorMuted2), si(12)));
        header->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);

        auto *box = new QWidget;
        // 命名给全局 QSS：分组容器必须透明，否则不透明底色会在玻璃侧栏上形成色块接缝
        box->setObjectName(QStringLiteral("NavSectionBox"));
        auto *lay = new QVBoxLayout(box);
        lay->setContentsMargins(0, 0, 0, 0);
        lay->setSpacing(4);

        QObject::connect(header, &QToolButton::toggled, box, &QWidget::setVisible);
        QObject::connect(header, &QToolButton::toggled, header, [header](bool on) {
            header->setIcon(glyphIcon(on ? glyph::ChevDown : glyph::ChevRight,
                                      QColor(kColorMuted2), si(12)));
        });

        navLay->addWidget(header);
        navLay->addWidget(box);
        box->setVisible(expanded);
        return NavSection{header, box, lay};
    };

    // 导航按钮工厂：图标用 Segoe 图标字体字形渲染（颜色随选中态在 updateNavIcons 重绘）
    auto makeNavBtn = [this](const QString &glyph, const char *label) -> QPushButton * {
        auto *b = new QPushButton;
        b->setObjectName(QStringLiteral("NavBtn"));
        b->setCheckable(true);
        b->setCursor(Qt::PointingHandCursor);
        b->setProperty("navGlyph", glyph);
        b->setProperty("navLabel", QString::fromUtf8(label));
        b->setProperty("expanded", false);
        b->setToolTip(QString::fromUtf8(label));
        b->setIconSize(QSize(si(18), si(18)));
        m_navButtons.append(b);
        return b;
    };

    // ---- 分组 1：Inbox ----
    // 回收站与「收件箱设置」均收纳进「设置」页，侧边栏只保留收件箱入口
    NavSection inboxSec = makeSection(QStringLiteral("笔记"), true);
    m_navInbox = makeNavBtn(glyph::Inbox, "笔记");
    inboxSec.layout->addWidget(m_navInbox);

    // ---- 分组 2：任务 ----
    // 统计类视图合并为单个「专注统计」入口（页内子标签切换），减少侧边栏图标数量
    NavSection todoSec = makeSection(QStringLiteral("任务"), true);
    m_navTodo = makeNavBtn(glyph::Checkbox, "TODO");
    m_navFocusStats = makeNavBtn(glyph::BarChart, "专注");
    todoSec.layout->addWidget(m_navTodo);
    todoSec.layout->addWidget(m_navFocusStats);

    // ---- 分组 3：活动 ----
    // 6 个视图合并为单个「活动」入口（页内子标签切换）
    NavSection awSec = makeSection(QStringLiteral("活动"), false);
    m_navActivity = makeNavBtn(glyph::Recent, "活动");
    awSec.layout->addWidget(m_navActivity);

    // ---- 分组 4：同步 ----
    // 同步详情并入局域网同步页内子标签
    NavSection syncSec = makeSection(QStringLiteral("同步"), false);
    m_navSync = makeNavBtn(glyph::Sync, "局域网同步");
    m_navD1Sync = makeNavBtn(glyph::Cloud, "D1 云同步");
    m_navCloudBackup = makeNavBtn(glyph::Save, "云备份（冷备）");
    syncSec.layout->addWidget(m_navSync);
    syncSec.layout->addWidget(m_navD1Sync);
    syncSec.layout->addWidget(m_navCloudBackup);

    // 窄栏模式：隐藏分组标题，图标平铺
    m_navSectionHeaders = {inboxSec.header, todoSec.header, awSec.header, syncSec.header};
    for (auto *h : m_navSectionHeaders) {
        h->setVisible(false);
        h->setChecked(true);
    }

    // 按钮组（单选）
    auto *grp = new QButtonGroup(this);
    grp->setExclusive(true);
    for (auto *b : m_navButtons)
        grp->addButton(b);

    // 连接信号
    connect(m_navInbox, &QPushButton::clicked, this, [this] { switchPage(PAGE_INBOX); });
    connect(m_navTodo, &QPushButton::clicked, this, [this] { switchPage(PAGE_TODO); });
    connect(m_navFocusStats, &QPushButton::clicked, this, [this] { switchPage(PAGE_FOCUS_STATS); });
    connect(m_navActivity, &QPushButton::clicked, this, [this] { switchPage(PAGE_ACTIVITY); });
    connect(m_navSync, &QPushButton::clicked, this, [this] { switchPage(PAGE_SYNC); });
    connect(m_navD1Sync, &QPushButton::clicked, this, [this] { switchPage(PAGE_D1_SYNC); });
    connect(m_navCloudBackup, &QPushButton::clicked, this, [this] { switchPage(PAGE_CLOUD_BACKUP); });

    navLay->addStretch(1);

    // 底部固定区：分隔线 + 设置入口（现代应用惯例，全局操作置底）
    auto *bottomSep = new QFrame;
    bottomSep->setFrameShape(QFrame::HLine);
    bottomSep->setStyleSheet(scaleQss(QStringLiteral("background: %1; max-height: 1px;").arg(kColorBorder)));
    navLay->addWidget(bottomSep);
    m_navSettings = makeNavBtn(glyph::Settings, "设置");
    navLay->addWidget(m_navSettings);
    connect(m_navSettings, &QPushButton::clicked, this, [this] { switchPage(PAGE_SETTINGS); });

    // ---- 页面堆栈 ----
    // .ui 中 pageStack 已声明 4 个容器页（pageSettings/pageFocusStats/pageActivity/PageSync），
    // 它们占紧凑索引 0..3，与页面枚举 (SETTINGS=1/FOCUS_STATS=4/ACTIVITY=5/SYNC=6) 不对应。
    // 先移除这 4 个容器页，再按「枚举→栈索引」紧凑映射重建（见下），否则侧边栏切页会错位。
    m_stack = ui->pageStack;
    m_settingsTabs = ui->settingsTabs;
    m_focusTabs = ui->focusTabs;
    m_awTabs = ui->awTabs;
    m_syncTabs = ui->syncTabs;

    // 创建页面
    m_inbox = new InboxPage(m_api);
    m_inboxSettings = new InboxSettingsPage(m_localStore);
    m_todo = new TodoPage(m_todoStore);
    m_timerPage = new FocusTimerPage(m_focusStore, m_todoStore);
    m_overviewPage = new FocusOverviewPage(m_focusStore);
    m_detailPage = new FocusDetailPage(m_focusStore);
    m_weekPage = new FocusWeekPage(m_focusStore);
    m_heatmapPage = new FocusHeatmapPage(m_focusStore);
    m_bestPage = new FocusBestPage(m_focusStore);
    m_calendarPage = new FocusCalendarPage(m_focusStore, m_todoStore);
    m_memorialPage = new FocusMemorialPage(m_focusStore);

    // 装入 .ui 中声明好的子标签 host 容器（Tab 标题/顺序由 .ui 决定）
    ui->focusRecordHostLay->addWidget(m_overviewPage);
    ui->focusDetailHostLay->addWidget(m_detailPage);
    ui->focusWeekHostLay->addWidget(m_weekPage);
    ui->focusHeatmapHostLay->addWidget(m_heatmapPage);
    ui->focusBestHostLay->addWidget(m_bestPage);
    ui->focusCalendarHostLay->addWidget(m_calendarPage);
    ui->focusMemorialHostLay->addWidget(m_memorialPage);
    // 计时专注并入「专注」统计页，作为第一个子标签
    m_focusTabs->insertTab(0, m_timerPage, QStringLiteral("🍅 计时"));
    styleSubTabs(m_focusTabs);
    // 专注容器内切换子标签时，把该页按当前缩放补齐（switchPage 只覆盖到容器层）
    connect(m_focusTabs, &QTabWidget::currentChanged, this, [this] {
        if (m_currentPage == PAGE_FOCUS_STATS)
            scaleCurrentView();
    });

    m_activity = new ActivityPage(m_api);
    m_day = new DayPage(m_api, m_tagStore);
    m_stats = new StatsPage(m_api, m_tagStore);
    m_sync = new SyncPage(m_api, m_mdns);
    // 配对请求到达 → 系统托盘气泡（用户不在同步页也能第一时间知道）
    connect(m_sync, &SyncPage::pairRequestReceived, this, [this](const QString &deviceName) {
        if (m_tray)
            m_tray->showMessage(QStringLiteral("局域网同步"),
                                QStringLiteral("设备「%1」想与本机配对，请在同步页确认").arg(deviceName),
                                QSystemTrayIcon::Information, 6000);
    });
    m_d1Sync = new D1SyncPage(m_api);
    m_syncDetails = new SyncDetailsPage(m_api);
    m_cloudBackup = new CloudBackupPage(m_api);
    m_query = new QueryPage(m_api);

    ui->awActivityHostLay->addWidget(m_activity);
    ui->awDayHostLay->addWidget(m_day);
    ui->awStatsHostLay->addWidget(m_stats);
    ui->awQueryHostLay->addWidget(m_query);
    styleSubTabs(m_awTabs);

    ui->syncHostLay->addWidget(m_sync);
    ui->syncDetailsHostLay->addWidget(m_syncDetails);
    styleSubTabs(m_syncTabs);

    ui->inboxHostLay->addWidget(m_inboxSettings);
    // 通用设置 Tab：内嵌设置编辑组件（原设置对话框内容），运行时构建
    buildSettingsEditor();
    styleSubTabs(m_settingsTabs);

    // 其余 8 个页面按「枚举→栈索引」紧凑映射重建：先移开 .ui 容器的 4 个页面，
    // 再从空栈 addWidget 依次追加（栈索引自 0 连续递增），登记各页枚举对应的栈索引。
    // 注意：不能用 addWidget 按枚举值 1:1 摆放，因为 PAGE_FOCUS_TIMER=3 无独立页面，
    // 栈里不存在该槽位 —— 统一用 m_pageToStack 映射切页（见 switchPage）。
    QWidget *settingsContainer = qobject_cast<QWidget *>(ui->settingsTabs->parentWidget());
    QWidget *focusContainer   = qobject_cast<QWidget *>(ui->focusTabs->parentWidget());
    QWidget *awContainer      = qobject_cast<QWidget *>(ui->awTabs->parentWidget());
    QWidget *syncContainer    = qobject_cast<QWidget *>(ui->syncTabs->parentWidget());
    m_stack->removeWidget(settingsContainer);
    m_stack->removeWidget(focusContainer);
    m_stack->removeWidget(awContainer);
    m_stack->removeWidget(syncContainer);

    auto reg = [this](int page, QWidget *w) {
        m_pageToStack.insert(page, m_stack->count());
        m_stack->addWidget(w);
    };
    reg(PAGE_INBOX, m_inbox);               // 栈 0
    reg(PAGE_SETTINGS, settingsContainer);  // 栈 1
    reg(PAGE_TODO, m_todo);                 // 栈 2
    reg(PAGE_FOCUS_STATS, focusContainer);  // 栈 3
    reg(PAGE_ACTIVITY, awContainer);        // 栈 4
    reg(PAGE_SYNC, syncContainer);          // 栈 5
    reg(PAGE_D1_SYNC, m_d1Sync);            // 栈 6
    reg(PAGE_CLOUD_BACKUP, m_cloudBackup);  // 栈 7

    connect(m_inbox, &InboxPage::settingsRequested, this, &MainWindow::openSettings);

    // SyncDetailsPage：「返回同步」切到容器内第一个子标签 + 转发日志信号
    connect(m_syncDetails, &SyncDetailsPage::backToSync, this, [this] { m_syncTabs->setCurrentIndex(0); });
    connect(m_syncDetails, &SyncDetailsPage::logMessage, m_sync, &SyncPage::logMessage);

    // 默认显示收件箱
    switchPage(PAGE_INBOX);

    // 右上角缩放百分比提示（缩放后短暂显示）
    m_zoomBadge = new QLabel(this);
    m_zoomBadge->setStyleSheet(
        QStringLiteral("background: rgba(0,0,0,0.72); color: white; border: 1px solid %1; "
                       "border-radius: 4px; padding: 3px 10px; font-size: 12px;")
            .arg(kColorBorder));
    m_zoomBadge->hide();

    setWindowTitle(QStringLiteral("aw-qtui — ActivityWatch 客户端"));
    resize(1280, 820);
    applyWindowMinimumSize(this);
}

// 左侧导航在「窄栏（仅图标）」与「展开（图标+文字）」之间切换：
// 默认窄栏，点击顶部 ☰ 展开、点击 « 收起。分组标题窄栏时隐藏、展开时显示；
// 两种模式下分组都保持展开，保证全部 Tab（含 Activity Watch 组）图标/文字可见。
void MainWindow::setNavExpanded(bool expanded)
{
    m_navExpanded = expanded;
    saveNavCollapsed(!expanded);

    for (auto *h : m_navSectionHeaders) {
        h->setVisible(expanded);
        h->setChecked(true);
    }

    // 按钮内容：窄栏只显示图标（居中），展开显示图标 + 文字（左对齐）
    for (auto *b : m_navButtons) {
        const QString label = b->property("navLabel").toString();
        b->setText(expanded ? label : QString());
        b->setProperty("expanded", expanded);
        b->style()->unpolish(b);
        b->style()->polish(b);
    }

    if (m_navToggle) {
        m_navToggle->setToolTip(expanded ? QStringLiteral("收起导航") : QStringLiteral("展开导航"));
        m_navToggle->setProperty("expanded", expanded);
        m_navToggle->style()->unpolish(m_navToggle);
        m_navToggle->style()->polish(m_navToggle);
    }

    updateNavIcons();

    if (m_nav) {
        const int target = si(expanded ? kNavExpandedPx : kNavCollapsedPx);
        if (gFxAnimations && m_nav->isVisible() && m_nav->width() != target) {
            // 展开/收起动画：先解除 fixedWidth 约束，再动画 maximumWidth
            const int start = m_nav->width();
            m_nav->setMinimumWidth(0);
            m_nav->setMaximumWidth(QWIDGETSIZE_MAX);
            auto *anim = new QPropertyAnimation(m_nav, "maximumWidth", m_nav);
            anim->setDuration(200);
            anim->setStartValue(start);
            anim->setEndValue(target);
            anim->setEasingCurve(QEasingCurve::OutCubic);
            connect(anim, &QPropertyAnimation::finished, this, [this, expanded, target] {
                m_nav->setFixedWidth(target);
                if (auto *nl = qobject_cast<QVBoxLayout *>(m_nav->layout())) {
                    nl->setContentsMargins(0, si(expanded ? 16 : 12), 0, si(12));
                    nl->setSpacing(si(expanded ? 4 : 2));
                }
                m_nav->layout()->activate();
                m_nav->update();
            });
            anim->start(QAbstractAnimation::DeleteWhenStopped);
        } else {
            m_nav->setFixedWidth(target);
            if (auto *nl = qobject_cast<QVBoxLayout *>(m_nav->layout())) {
                nl->setContentsMargins(0, si(expanded ? 16 : 12), 0, si(12));
                nl->setSpacing(si(expanded ? 4 : 2));
            }
            m_nav->layout()->activate();
            m_nav->update();
        }
    }
}

// 导航图标重绘：Segoe 字形渲染为 QIcon（选中 accent / 未选中 muted）。
// 触发点：选中页变化（switchPage）、窄栏/展开切换（setNavExpanded）、缩放变化（applyUiScale）
void MainWindow::updateNavIcons()
{
    const int px = si(18);
    for (auto *b : m_navButtons) {
        const QString glyphStr = b->property("navGlyph").toString();
        if (glyphStr.isEmpty())
            continue;
        b->setIconSize(QSize(si(18), si(18)));
        b->setIcon(glyphIcon(glyphStr, b->isChecked() ? QColor(kColorAccent)
                                                      : QColor(kColorFgMuted), px));
    }
    // 展开/收起切换按钮：窄栏汉堡菜单，展开左箭头（收起）——图标比导航项大一号
    if (m_navToggle) {
        m_navToggle->setIconSize(QSize(si(20), si(20)));
        m_navToggle->setIcon(glyphIcon(m_navExpanded ? glyph::ChevLeft : glyph::Menu,
                                       QColor(kColorFgMuted), si(20)));
    }
    // 分组头 chevron 与缩放尺寸
    for (auto *h : m_navSectionHeaders) {
        h->setIconSize(QSize(si(12), si(12)));
        h->setIcon(glyphIcon(h->isChecked() ? glyph::ChevDown : glyph::ChevRight,
                             QColor(kColorMuted2), si(12)));
    }
}

void MainWindow::switchPage(int index)
{
    if (index < 0 || index >= PAGE_COUNT)
        return;
    // 页面枚举 → 栈索引映射（PAGE_FOCUS_TIMER 无独立页，未登记 → 不切页）
    const auto it = m_pageToStack.constFind(index);
    if (it == m_pageToStack.constEnd())
        return;
    const int stackIndex = it.value();
    m_stack->setCurrentIndex(stackIndex);
    m_currentPage = index;
    // 切到新页：把之前延迟重建的页面按当前缩放补齐
    scaleCurrentView();

    // 更新导航按钮状态
    m_navInbox->setChecked(index == PAGE_INBOX);
    m_navSettings->setChecked(index == PAGE_SETTINGS);
    m_navTodo->setChecked(index == PAGE_TODO);
    m_navFocusStats->setChecked(index == PAGE_FOCUS_STATS);
    m_navActivity->setChecked(index == PAGE_ACTIVITY);
    m_navSync->setChecked(index == PAGE_SYNC);
    m_navD1Sync->setChecked(index == PAGE_D1_SYNC);
    m_navCloudBackup->setChecked(index == PAGE_CLOUD_BACKUP);
    updateNavIcons();

    // 页面特定处理
    if (index == PAGE_SYNC) {
        // 进入局域网同步界面：启动 UDP 广播发现 + 网络环境自动开启同步 + 定时刷新
        m_sync->onEnteredSyncPage();
    } else if (m_prevPage == PAGE_SYNC) {
        // 从同步页切走时停止广播与定时刷新（避免后台偷偷广播）
        if (m_api)
            m_api->discoveryStop();
        m_sync->stopRefresh();
    }
    if (index == PAGE_TODO)
        m_todo->refresh();

    // 记录当前页为「上一页」，供下次切换判断是否离开同步页
    m_prevPage = index;

    // 淡入动画
    if (gFxAnimations) {
        if (QWidget *page = m_stack->widget(stackIndex)) {
            auto *eff = new QGraphicsOpacityEffect(page);
            eff->setOpacity(0.0);
            page->setGraphicsEffect(eff);
            auto *anim = new QPropertyAnimation(eff, "opacity", page);
            anim->setDuration(150);
            anim->setStartValue(0.0);
            anim->setEndValue(1.0);
            anim->setEasingCurve(QEasingCurve::OutCubic);
            connect(anim, &QPropertyAnimation::finished, page, [page] {
                page->setGraphicsEffect(nullptr);
            });
            anim->start(QAbstractAnimation::DeleteWhenStopped);
        }
    }
}

// 容器页子标签样式：随主题/缩放重建（专注统计与 ActivityWatch 容器共用）
void MainWindow::styleSubTabs(QTabWidget *tabs)
{
    if (!tabs)
        return;
    // 下划线标签：选中项 accent 下划线直接压在 pane 顶边线上（浏览器式），
    // 消除旧「悬浮胶囊 + 圆角描边框」在标签栏与内容之间的接缝
    tabs->setStyleSheet(QStringLiteral(
        "QTabWidget::pane { border: none; border-top: 1px solid %1; top: 0; background: transparent; }"
        "QTabBar { background: transparent; }"
        "QTabBar::tab { background: transparent; color: %2; padding: %3px %4px; margin-right: %5px;"
        "  border: none; border-bottom: 2px solid transparent; font-size: %6px; }"
        "QTabBar::tab:selected { color: %7; border-bottom: 2px solid %7; }"
        "QTabBar::tab:hover:!selected { color: %8; }")
        .arg(kColorBorder)
        .arg(kColorFgMuted)
        .arg(si(7))
        .arg(si(14))
        .arg(si(4))
        .arg(si(13))
        .arg(kColorAccent)
        .arg(kColorFg));
}

void MainWindow::updateStatus()
{
    // 设备名称/操作系统已移入设置对话框，此处仅维持同步心跳
    m_sync->heartbeat();
}

QStringList MainWindow::applyShortcuts()
{
    const ShortcutConfig cfg = loadShortcuts();
    void *hwnd = reinterpret_cast<void *>(quintptr(winId()));
    qDebug() << "[MainWindow] applyShortcuts hwnd=" << quintptr(hwnd)
             << "add=" << cfg.addNote.toString() << "inbox=" << cfg.showInbox.toString();
    QStringList failed;

    auto tryRegister = [&](int id, const QKeySequence &seq, const QString &label) {
        // 空序列在 setHotKey 内按“禁用”处理并返回 true，因此这里只会收集真实失败
        if (!m_hotkey->setHotKey(id, seq, hwnd))
            failed << QStringLiteral("%1 (%2)").arg(label, seq.toString(QKeySequence::NativeText));
    };
    tryRegister(kHotkeyAddNoteId, cfg.addNote, QStringLiteral("添加记录"));
    tryRegister(kHotkeyShowInboxId, cfg.showInbox, QStringLiteral("唤醒并跳转收件箱"));

    if (!failed.isEmpty())
        qWarning() << "[MainWindow] 全局热键注册失败:" << failed.join(QLatin1String("; "));
    return failed;
}

void MainWindow::openSettings()
{
    // 编辑快捷键期间暂停全局热键：避免录入“当前已绑定”的组合时误触发动作
    m_hotkey->suspendAll();

    const QString curTheme = gTheme ? QString::fromLatin1(gTheme->id) : QStringLiteral("midnight");
    const QString curIconId = gAppIcon ? QLatin1String(gAppIcon->id) : QStringLiteral("amber");
    const UiEffects curFx = loadUiEffects();
    SettingsDialog dlg(loadShortcuts(), curTheme, curFx, curIconId, m_zoom, this);
    if (dlg.exec() != QDialog::Accepted) {
        applyShortcuts(); // 取消：恢复原注册
        return;
    }
    applySettingsValues(*dlg.widget());
}

void MainWindow::applySettingsValues(const SettingsWidget &w)
{
    const QString curTheme = gTheme ? QString::fromLatin1(gTheme->id) : QStringLiteral("midnight");
    const QString curIconId = gAppIcon ? QLatin1String(gAppIcon->id) : QStringLiteral("amber");
    // 主题变更：应用并持久化
    const QString newTheme = w.themeId();
    const UiEffects newFx = w.uiEffects();
    const bool fxChanged = newFx.shadowLevel != gShadowLevel || newFx.glassLevel != gGlassLevel
                           || newFx.animations != gFxAnimations || newFx.dwmBackdrop != gDwmBackdrop
                           || newFx.fixEdgeLowContrast != gFixEdgeLowContrast
                           || newFx.fixGlassOpaque != gFixGlassOpaque
                           || newFx.fixSnapZoom != gFixSnapZoom
                           || newFx.fixShadowAdaptive != gFixShadowAdaptive;
    if (newTheme != curTheme)
        saveThemeId(newTheme);
    // 程序图标变更：更新全局变体并持久化，窗口 / 托盘图标立即切换
    const QString newIconId = w.appIconId();
    if (newIconId != curIconId) {
        gAppIcon = findAppIcon(newIconId);
        saveAppIconId(newIconId);
        setWindowIcon(makeAppIcon());
        if (m_tray)
            m_tray->setIcon(makeAppIcon());
    }
    if (fxChanged) {
        // 界面效果变更：更新全局配置并持久化
        gShadowLevel = newFx.shadowLevel;
        gGlassLevel = newFx.glassLevel;
        gFxAnimations = newFx.animations;
        gDwmBackdrop = newFx.dwmBackdrop;
        gFixEdgeLowContrast = newFx.fixEdgeLowContrast;
        gFixGlassOpaque = newFx.fixGlassOpaque;
        gFixSnapZoom = newFx.fixSnapZoom;
        gFixShadowAdaptive = newFx.fixShadowAdaptive;
        saveUiEffects(newFx);
        // DWM 背景变化需要立即应用/撤销
        applyDwmBackdrop();
        // 缩放对齐开关变化：立即把当前缩放重新吸附到干净档位（或恢复原值）
        setZoom(m_zoom, false);
    }
    if (newTheme != curTheme || fxChanged)
        applyTheme(newTheme); // 重建全局 QSS + 页面内联样式（阴影/玻璃/动画随之生效）

    saveShortcuts(w.config());
    const QStringList failed = applyShortcuts();

    // 界面缩放比：滑块取值（含缩放对齐吸附）作为新的目标比例
    setZoom(w.zoom(), false);

    // 同步设置（sync_inbox / sync_activity / sync_todo）保存到 aw-server-rust
    const SyncSettingsConfig syncCfg = w.syncSettings();
    QNetworkReply *rg = m_api->getSyncConfig();
    connect(rg, &QNetworkReply::finished, this, [this, rg, syncCfg] {
        QJsonDocument doc;
        QString err;
        SyncConfig cfg;
        if (ApiClient::parseReply(rg, &doc, &err)) {
            cfg = SyncConfig::fromJson(doc.object());
        }
        rg->deleteLater();

        // 更新同步范围设置（其他字段保留）
        cfg.syncInbox = syncCfg.syncInbox;
        cfg.syncActivity = syncCfg.syncActivity;
        cfg.syncTodo = syncCfg.syncTodo;

        QNetworkReply *rp = m_api->setSyncConfig(cfg.toJson());
        connect(rp, &QNetworkReply::finished, this, [this, rp] {
            rp->deleteLater();
        });
    });

    // 冲突提示用非模态 toast：短暂显示后自动消失，不阻塞、无需点击
    if (!failed.isEmpty())
        showToast(QStringLiteral("快捷键冲突：%1（可能已被其它程序占用，已保存但暂不生效）")
                      .arg(failed.join(QLatin1Char(' '))));
}

// 设置组件触发的缩放（内嵌设置实时预览）：应用缩放并让滑块回显吸附后的实际值，避免因缩放对齐造成错位
void MainWindow::applySettingsZoom(SettingsWidget *ed, double v)
{
    setZoom(v, false);
    if (ed)
        ed->setZoomValue(m_zoom);
}

// 在设置页「通用设置」Tab 内构建设置编辑组件 + 保存按钮（把原设置对话框内容内嵌）
void MainWindow::buildSettingsEditor()
{
    const QString curTheme = gTheme ? QString::fromLatin1(gTheme->id) : QStringLiteral("midnight");
    const QString curIconId = gAppIcon ? QLatin1String(gAppIcon->id) : QStringLiteral("amber");
    const UiEffects curFx = loadUiEffects();
    m_settingsEditor = new SettingsWidget(loadShortcuts(), curTheme, curFx, curIconId, m_zoom, this);
    // 内嵌设置：拖动滑块实时预览缩放（经防抖），并同步滑块与吸附后的实际值
    connect(m_settingsEditor, &SettingsWidget::zoomChanged, this,
            [this](double v) { applySettingsZoom(m_settingsEditor, v); });

    auto *scroll = new QScrollArea;
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setWidget(m_settingsEditor);

    auto *saveBtn = new QPushButton(QStringLiteral("保存并应用"));
    saveBtn->setObjectName(QStringLiteral("PrimaryBtn"));
    saveBtn->setCursor(Qt::PointingHandCursor);
    saveBtn->setMinimumHeight(si(34));
    saveBtn->setMaximumWidth(si(180));
    connect(saveBtn, &QPushButton::clicked, this, &MainWindow::doSaveSettings);

    auto *lay = new QVBoxLayout;
    lay->setContentsMargins(12, 12, 12, 12);
    lay->setSpacing(10);
    lay->addWidget(scroll, 1);
    auto *btnRow = new QHBoxLayout;
    btnRow->addStretch(1);
    btnRow->addWidget(saveBtn);
    lay->addLayout(btnRow);
    auto *host = new QWidget;
    host->setLayout(lay);
    ui->generalHostLay->addWidget(host);
}

// 应用后按当前设置重建编辑组件，同步新主题/图标/效果的配色与已保存值
void MainWindow::rebuildSettingsEditor()
{
    if (QLayoutItem *item = ui->generalHostLay->takeAt(0)) {
        if (QWidget *w = item->widget())
            w->deleteLater();
        delete item;
    }
    buildSettingsEditor();
}

void MainWindow::doSaveSettings()
{
    if (!m_settingsEditor)
        return;
    const QString err = SettingsWidget::validate(m_settingsEditor->config());
    if (!err.isEmpty()) {
        QMessageBox::warning(this, QStringLiteral("快捷键无效"), err);
        return;
    }
    applySettingsValues(*m_settingsEditor);
    // 应用后重建编辑器：让主题/图标/效果的内联配色与控件取值保持最新
    rebuildSettingsEditor();
}

void MainWindow::applyTheme(const QString &themeId)
{
    const Theme *t = findTheme(themeId);
    if (!t)
        return;
    gTheme = t;
    applyThemeColors(*t);
    applyUiScale(); // 重建全局 QSS（按缩放）+ 缩放字体 + 导航 + inbox/todo 页面
    styleSubTabs(m_focusTabs);    // 专注统计子标签按新主题重建
    styleSubTabs(m_awTabs);       // ActivityWatch 子标签按新主题重建
    styleSubTabs(m_syncTabs);     // 同步子标签按新主题重建
    styleSubTabs(m_settingsTabs); // 设置子标签按新主题重建

    // 页面级内联样式按新主题重建
    if (m_inbox)
        m_inbox->applyUiScale();
    if (m_todo)
        m_todo->applyUiScale();
    if (m_activity)
        m_activity->applyTheme();
    if (m_day)
        m_day->applyTheme();
    if (m_stats)
        m_stats->applyTheme();
    if (m_inboxSettings)
        m_inboxSettings->applyUiScale();
    if (m_timerPage)
        m_timerPage->applyUiScale();
    if (m_overviewPage)
        m_overviewPage->applyUiScale();
    if (m_detailPage)
        m_detailPage->applyUiScale();
    if (m_weekPage)
        m_weekPage->applyUiScale();
    if (m_heatmapPage)
        m_heatmapPage->applyUiScale();
    if (m_bestPage)
        m_bestPage->applyUiScale();
    if (m_calendarPage)
        m_calendarPage->applyUiScale();
    if (m_memorialPage)
        m_memorialPage->applyUiScale();

    // 强制顶层窗口重绘，刷新自绘的图表 / 时间轴等控件
    for (QWidget *w : QApplication::topLevelWidgets())
        w->update();
    // 托盘图标随主题刷新（makeAppIcon 固定设计，与主题无关）
    if (m_tray) {
        m_tray->setIcon(makeAppIcon());
        m_tray->setToolTip(QStringLiteral("aw-qtui · %1").arg(QString::fromUtf8(gTheme->name)));
    }
    qDebug() << "[MainWindow] theme applied:" << t->id;
}

void MainWindow::wakeUpAndShow()
{
    if (isMinimized())
        showNormal();
    show();
    raise();
    activateWindow();
    raiseWindowToFront(this);
}

void MainWindow::onGlobalHotkey(int id)
{
    qDebug() << "[MainWindow] global hotkey activated id=" << id;
    switch (id) {
    case kHotkeyAddNoteId:
        // 添加记录：只弹出新建笔记对话框，不唤醒/调出主窗口
        // （主窗口隐藏/最小化时保持后台运行，对话框作为模态顶层窗口独立弹出）
        switchPage(PAGE_INBOX);
        m_inbox->openNewNote();
        break;
    case kHotkeyShowInboxId:
        // 唤醒并跳转收件箱
        wakeUpAndShow();
        switchPage(PAGE_INBOX);
        break;
    default:
        break;
    }
}

bool MainWindow::nativeEvent(const QByteArray &eventType, void *message, qintptr *result)
{
#ifdef Q_OS_WIN
    if (m_hotkey && m_hotkey->handleMessage(message)) {
        if (result)
            *result = 0;
        return true;
    }
    // 最大化时把窗口尺寸钳制到屏幕可用区域，避免无边框/细边框窗口的右侧与底部
    // 被 Windows 最大化边框延伸到屏幕外（导致笔记卡片「⋯」按钮切出可视区）。
    // 用 MonitorFromWindow 获取窗口实际所在显示器（比 QWidget::screen() 更可靠，
    // 副屏/DPI 差异时不会返回错误屏幕）。mi.rcWork 是排除任务栏的工作区。
    if (eventType == QByteArrayLiteral("windows_generic_MSG")) {
        const auto *msg = static_cast<const MSG *>(message);
        if (msg->message == WM_GETMINMAXINFO) {
            if (HWND hwnd = msg->hwnd) {
                HMONITOR hMon = MonitorFromWindow(hwnd, MONITOR_DEFAULTTONEAREST);
                MONITORINFO mi = { sizeof(mi) };
                if (GetMonitorInfo(hMon, &mi)) {
                    auto *mmi = reinterpret_cast<MINMAXINFO *>(msg->lParam);
                    mmi->ptMaxPosition.x = mi.rcWork.left;
                    mmi->ptMaxPosition.y = mi.rcWork.top;
                    mmi->ptMaxSize.x = mi.rcWork.right - mi.rcWork.left;
                    mmi->ptMaxSize.y = mi.rcWork.bottom - mi.rcWork.top;
                    // 不钳制 ptMaxTrackSize：保留系统默认，允许用户手动拖拽跨屏
                    if (result)
                        *result = 0;
                    return true;
                }
            }
        }
    }
    Q_UNUSED(eventType);
#else
    Q_UNUSED(eventType);
    Q_UNUSED(message);
    Q_UNUSED(result);
#endif
    return QMainWindow::nativeEvent(eventType, message, result);
}

void MainWindow::keyPressEvent(QKeyEvent *event)
{
    // 快捷键：1-9 切页，F5 刷新当前页
    switch (event->key()) {
    case Qt::Key_1: switchPage(PAGE_INBOX); return;
    case Qt::Key_2: switchPage(PAGE_SETTINGS); return;
    case Qt::Key_3: switchPage(PAGE_TODO); return;
    case Qt::Key_4: switchPage(PAGE_FOCUS_STATS); return;
    case Qt::Key_5: switchPage(PAGE_FOCUS_STATS); return;
    case Qt::Key_6: switchPage(PAGE_ACTIVITY); return;
    case Qt::Key_7: switchPage(PAGE_SYNC); return;
    case Qt::Key_8: switchPage(PAGE_D1_SYNC); return;
    case Qt::Key_9: switchPage(PAGE_CLOUD_BACKUP); return;
    case Qt::Key_F5:
        // AW 容器页：刷新当前子标签
        if (m_currentPage == PAGE_ACTIVITY) {
            switch (m_awTabs->currentIndex()) {
            case 0: m_activity->refresh(); break;
            case 1: m_day->refresh(); break;
            case 2: m_stats->refresh(); break;
            case 3: m_query->refresh(); break;
            }
            return;
        }
        if (m_currentPage == PAGE_SYNC) {
            // 同步容器页：按当前子标签分发刷新
            if (m_syncTabs->currentIndex() == 0)
                m_sync->refreshDevices();
            else
                m_syncDetails->refreshLogs();
            return;
        }
        if (m_currentPage == PAGE_INBOX) m_inbox->refreshAll();
        else if (m_currentPage == PAGE_TODO) m_todo->refresh();
        return;
    default: break;
    }
    QMainWindow::keyPressEvent(event);
}

bool MainWindow::eventFilter(QObject *obj, QEvent *event)
{
    // 缩放快捷键：Ctrl+加 / Ctrl+减 / Ctrl+0 复位。
    // 滚轮缩放已移除：按住 Ctrl 滚动会重建整页（Inbox 列表全部重渲染）导致卡顿。
    // 只要焦点不在「快捷键录入框」（避免把 Ctrl+加/减 录进全局热键），Ctrl+± 即全局生效，
    // 无论是否处于文本输入态，保证在主窗口任意页面都能调节比例。
    if (event->type() == QEvent::KeyPress && !qobject_cast<ShortcutEdit *>(obj)) {
        auto *ke = static_cast<QKeyEvent *>(event);
        const bool ctrl = ke->modifiers().testFlag(Qt::ControlModifier);
        const int key = ke->key();
        if (ctrl && (key == Qt::Key_Plus || key == Qt::Key_Equal || key == Qt::Key_Minus
                     || key == Qt::Key_0)) {
            if (key == Qt::Key_Plus || key == Qt::Key_Equal)
                queueZoomBy(1.15);
            else if (key == Qt::Key_Minus)
                queueZoomBy(1.0 / 1.15);
            else
                setZoom(1.0, false);
            ke->accept();
            return true;
        }
    }
    return QMainWindow::eventFilter(obj, event);
}

void MainWindow::setZoom(qreal zoom, bool underMouse)
{
    Q_UNUSED(underMouse);
    // 缩放对齐开启时先吸附到干净档位
    if (gFixSnapZoom)
        zoom = snapZoom(zoom);
    zoom = qBound(0.3, zoom, 3.0);
    if (qFuzzyCompare(zoom, m_zoom))
        return;
    m_zoom = zoom;
    qDebug() << "[MainWindow] setZoom" << m_zoom;
    applyUiScale();
    saveUiZoom(m_zoom);
    showZoomBadge();
}

// 键盘 Ctrl+加/减：目标值先累计到 m_zoomPending 并防抖，停顿后才真正落位重建。
// 长按/连按时只做加减算术（零窗口重建），真正重建合并到约 80ms 一次，兼顾流畅与反馈
void MainWindow::queueZoomBy(qreal factor)
{
    if (m_zoomPending < 0.0)
        m_zoomPending = m_zoom; // 以当前已生效的比例为基准开始累计
    m_zoomPending = qBound(0.3, m_zoomPending * factor, 3.0);
    if (m_zoomInputTimer)
        m_zoomInputTimer->start();
}

// 只为当前可见页面应用缩放样式（延迟重建的其余页在 switchPage / 子标签切换时通过本函数补齐）
void MainWindow::scaleCurrentView()
{
    switch (m_currentPage) {
    case PAGE_INBOX:
        if (m_inbox) m_inbox->applyUiScale();
        break;
    case PAGE_SETTINGS:
        if (m_inboxSettings) m_inboxSettings->applyUiScale();
        break;
    case PAGE_TODO:
        if (m_todo) m_todo->applyUiScale();
        break;
    case PAGE_FOCUS_STATS: {
        // 专注容器：仅重建当前激活子标签对应的页面（tab 下标由 .ui 顺序决定，故用指针互比）
        QWidget *w = m_focusTabs ? m_focusTabs->currentWidget() : nullptr;
        if (w == m_timerPage) m_timerPage->applyUiScale();
        else if (w == m_overviewPage) m_overviewPage->applyUiScale();
        else if (w == m_detailPage) m_detailPage->applyUiScale();
        else if (w == m_weekPage) m_weekPage->applyUiScale();
        else if (w == m_heatmapPage) m_heatmapPage->applyUiScale();
        else if (w == m_bestPage) m_bestPage->applyUiScale();
        else if (w == m_calendarPage) m_calendarPage->applyUiScale();
        else if (w == m_memorialPage) m_memorialPage->applyUiScale();
        break;
    }
    case PAGE_ACTIVITY:
        if (m_activity) m_activity->applyUiScale();
        break;
    default:
        // 同步 / D1 / 云备份等页未登记到整页重建列表，仅靠全局 QSS/字体缩放
        break;
    }
}

void MainWindow::applyUiScale()
{
    gUiScale = m_zoom;

    // 重新生成全局 QSS
    qApp->setStyleSheet(scaleQss(gGlobalQss));

    // 同步放大基准字体
    QFont f = qApp->font();
    f.setPixelSize(qRound(13 * m_zoom));
    qApp->setFont(f);

    // 左侧导航
    if (m_nav) {
        m_nav->setFixedWidth(si(m_navExpanded ? kNavExpandedPx : kNavCollapsedPx));
        auto *nl = qobject_cast<QVBoxLayout *>(m_nav->layout());
        if (nl) {
            nl->setContentsMargins(0, si(m_navExpanded ? 16 : 12), 0, si(12));
            nl->setSpacing(si(m_navExpanded ? 4 : 2));
        }
        updateNavIcons();
    }

    // 页面级缩放样式：只重建当前可见页，其余页延迟到切回时（switchPage/子标签切换）再应用，
    // 避免 Ctrl± 或拖动滑块时对所有页面（尤其收件箱整表）反复全量重建导致卡顿
    scaleCurrentView();
}

void MainWindow::applyDwmBackdrop()
{
#ifdef Q_OS_WIN
    HWND hwnd = (HWND)winId();
    if (!hwnd)
        return;
    // DWMWA_SYSTEMBACKDROP_TYPE = 38（Win11 22H2+）
    // DWMSBT_AUTO=0, DWMSBT_NONE=1, DWMSBT_MAINWINDOW=2(Mica), DWMSBT_TRANSIENTWINDOW=3(Acrylic)
    // 玻璃背景使用 Acrylic(3)：对窗口背后内容做模糊 + 调色，呈现系统级玻璃质感
    const DWORD type = gDwmBackdrop ? 3 : 1;
    HRESULT hr = DwmSetWindowAttribute(hwnd, 38, &type, sizeof(type));
    if (gDwmBackdrop && SUCCEEDED(hr)) {
        setAttribute(Qt::WA_TranslucentBackground, true);
        // alpha=1 极淡背景：Qt 会在重绘时用它填充整个区域（正确擦除旧像素，避免残影），
        // 但 alpha=1 人眼几乎不可见，DWM Acrylic 玻璃仍能透出。
        // 不能用 background: transparent —— Qt 会跳过背景绘制，导致旧帧残留（残影）。
        const QString faint = QStringLiteral("background: rgba(0,0,0,1);");
        if (auto *cw = centralWidget())
            cw->setStyleSheet(faint);
        if (m_stack)
            m_stack->setStyleSheet(faint);
    } else {
        setAttribute(Qt::WA_TranslucentBackground, false);
        if (auto *cw = centralWidget())
            cw->setStyleSheet(QString());
        if (m_stack)
            m_stack->setStyleSheet(QString());
    }
#else
    Q_UNUSED(this);
#endif
}

// DWM 透明背景下，Qt 的 backing store 可能不擦除旧像素导致残影；
// 此处每次 paintEvent 都用透明色填充整个窗口，强制清除 backing store。
void MainWindow::paintEvent(QPaintEvent *event)
{
    if (gDwmBackdrop) {
        QPainter p(this);
        p.setCompositionMode(QPainter::CompositionMode_Clear);
        p.fillRect(rect(), Qt::transparent);
        p.end();
    }
    QMainWindow::paintEvent(event);
}

// DWM 透明背景下调整窗口大小时，强制全窗口重绘，避免边缘残影。
void MainWindow::resizeEvent(QResizeEvent *event)
{
    QMainWindow::resizeEvent(event);
    if (gDwmBackdrop)
        update();
}

void MainWindow::showZoomBadge()
{
    if (!m_zoomBadge)
        return;
    m_zoomBadge->setText(QStringLiteral("%1%").arg(qRound(m_zoom * 100)));
    m_zoomBadge->adjustSize();
    const QWidget *cw = centralWidget();
    const int w = cw ? cw->width() : width();
    m_zoomBadge->move(w - m_zoomBadge->width() - 16, 14);
    m_zoomBadge->show();
    m_zoomBadge->raise();
    QTimer::singleShot(1200, m_zoomBadge, [this] { m_zoomBadge->hide(); });
}

void MainWindow::showToast(const QString &text, int ms)
{
    if (!m_toast) {
        m_toast = new QLabel(this);
        m_toast->setWordWrap(true);
        m_toast->setStyleSheet(
            QStringLiteral("background: rgba(0,0,0,0.78); color: #ffffff; "
                           "border: 1px solid %1; border-radius: 6px; "
                           "padding: 8px 14px; font-size: 13px;")
                .arg(kColorBorder));
        m_toast->setMaximumWidth(400);
    }
    m_toast->setText(text);
    // 按最大宽度换行后计算实际尺寸（含 padding/border）
    const QFontMetrics fm(m_toast->font());
    const QRect br = fm.boundingRect(QRect(0, 0, m_toast->maximumWidth() - 30, 2000),
                                     Qt::TextWordWrap, text);
    m_toast->resize(qMin(br.width(), m_toast->maximumWidth() - 30) + 30, br.height() + 20);
    const QWidget *cw = centralWidget();
    const int w = cw ? cw->width() : width();
    m_toast->move(w - m_toast->width() - 16, 48);
    m_toast->show();
    m_toast->raise();
    QTimer::singleShot(ms, m_toast, [this] { m_toast->hide(); });
}

} // namespace awqtui
