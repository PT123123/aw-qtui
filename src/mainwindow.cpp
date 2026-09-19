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
#include "syncservice.h"
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
#include <QFileInfo>
#include <QFont>
#include <QFontMetrics>
#include <QFrame>
#include <QGraphicsOpacityEffect>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QJsonDocument>
#include <QJsonObject>
#include <QKeyEvent>
#include <QKeySequence>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QProcess>
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
#include <shellscalingapi.h>  // GetDpiForMonitor：按目标显示器取有效 DPI（旧文档里的 shcore.h 在本 SDK 无此名）
#pragma comment(lib, "dwmapi.lib")
// shcore.lib 由 CMakeLists 链接
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
    // 无界面常驻的局域网同步引擎：独立于同步页生命周期（页控件销毁/重建照常工作）
    m_syncService = new SyncService(m_api, this);
    // 配对请求到达 → 系统托盘气泡（无论是否打开同步页都能第一时间知道）
    connect(m_syncService, &SyncService::pairRequestReceived, this,
            [this](const QString &deviceName) {
                if (m_tray)
                    m_tray->showMessage(QStringLiteral("局域网同步"),
                                        QStringLiteral("设备「%1」想与本机配对，请在同步页确认").arg(deviceName),
                                        QSystemTrayIcon::Information, 6000);
            });
    m_tagStore = new TagStore;
    m_tagStore->load();
    m_todoStore = new TodoApiStore(m_api, this);
    // 首次拉取交给下面 TodoPage 构造（常驻页，一定会建）；这里不再 load，
    // 否则启动即两轮全量拉取。
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
    // 远端变更监视：服务端每有远端数据落地就递增 revision，这里低频轮询，值变了才刷新。
    // 15 秒既有「即时感」，又不会在连续同步时反复重建列表。
    m_remoteTimer = new QTimer(this);
    m_remoteTimer->setInterval(15000);
    connect(m_remoteTimer, &QTimer::timeout, this, &MainWindow::pollRemoteChanges);
    m_remoteTimer->start();
    QTimer::singleShot(3000, this, &MainWindow::pollRemoteChanges); // 首次只取基线，不刷新
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

// 系统托盘：图标用 makeAppIcon()（透明底绿色描边：折角文档 + L 形时钟指针），
// 已含 16/32px 托盘尺寸，零外部资源。
// 交互：左键/双击切换显示隐藏；右键菜单「显示/隐藏主窗口」「退出」；关窗默认最小化到托盘。
void MainWindow::setupTray()
{
    if (!QSystemTrayIcon::isSystemTrayAvailable())
        return;
    m_tray = new QSystemTrayIcon(this);
    m_tray->setIcon(makeAppIcon());
    m_tray->setToolTip(QStringLiteral("aw-qtui v%1 · %2").arg(kAppVersion, QString::fromUtf8(gTheme->name)));

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

    // ---- 分组 2：待办（分组标题避开与下方「任务」按钮同名） ----
    // 统计类视图合并为单个「专注统计」入口（页内子标签切换），减少侧边栏图标数量
    NavSection todoSec = makeSection(QStringLiteral("待办"), true);
    m_navTodo = makeNavBtn(glyph::Checkbox, "任务");
    m_navFocusStats = makeNavBtn(glyph::BarChart, "专注");
    todoSec.layout->addWidget(m_navTodo);
    todoSec.layout->addWidget(m_navFocusStats);

    // ---- 分组 3：活动 ----
    // 6 个视图合并为单个「活动」入口（页内子标签切换）
    NavSection awSec = makeSection(QStringLiteral("活动"), false);
    m_navActivity = makeNavBtn(glyph::Recent, "活动");
    awSec.layout->addWidget(m_navActivity);

    // ---- 分组 4：同步 ----
    // 局域网同步 / 详情 / D1云 / 冷备 合并为单个「同步」入口（页内子标签切换）
    NavSection syncSec = makeSection(QStringLiteral("同步"), false);
    m_navSync = makeNavBtn(glyph::Sync, "同步");
    syncSec.layout->addWidget(m_navSync);

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

    // 创建页面：仅构造常驻页（收件箱 / 任务）。
    // 活动/专注/同步/设置容器在此不构造子页面 —— 属「可淘汰页」，首次点入才经
    // ensure*Pages 懒建，切走时由 leaveEvictable 销毁其整个子页面 widget 树（见下）。
    // 同步容器的后台引擎（SyncService）已在构造函数创建并常驻，不因页控件回收而停摆。
    m_inbox = new InboxPage(m_api);
    m_todo = new TodoPage(m_todoStore);
    // 任务详细信息里的「来源设备」需要查 /devices 把 device_id 换成设备名
    m_todo->setApiClient(m_api);

    styleSubTabs(m_syncTabs);
    // 同步容器内切换子标签时，把该页按当前缩放补齐
    connect(m_syncTabs, &QTabWidget::currentChanged, this, [this] {
        if (m_currentPage == PAGE_SYNC)
            scaleCurrentView();
    });

    // 活动容器（懒建）：切子标签时释放上一停留页驻留数据、切回重拉。
    // 子页面构建期为 nullptr（尚未懒建），此处一律空转；真切页时 m_currentPage==PAGE_ACTIVITY
    // 且子页面必然已由 enterEvictable 建好，故以下可直接用。
    connect(m_awTabs, &QTabWidget::currentChanged, this, [this] {
        if (m_currentPage != PAGE_ACTIVITY)
            return;
        if (!m_activity) // 尚未懒建或已离开销毁
            return;
        m_activity->releaseWeight();
        m_day->releaseWeight();
        m_stats->releaseWeight();
        m_query->releaseWeight();
        scaleCurrentView();
        switch (m_awTabs->currentIndex()) {
        case 0: m_activity->refresh(); break;
        case 1: m_day->refresh(); break;
        case 2: m_stats->refresh(); break;
        case 3: m_query->refresh(); break;
        default: break;
        }
    });

    // 专注容器（懒建）：同上 —— 切子标签释放驻留数据、切回重拉。
    connect(m_focusTabs, &QTabWidget::currentChanged, this, [this] {
        if (m_currentPage != PAGE_FOCUS_STATS)
            return;
        if (!m_timerPage) // 尚未懒建或已离开销毁
            return;
        m_detailPage->releaseWeight();
        m_weekPage->releaseWeight();
        m_heatmapPage->releaseWeight();
        m_bestPage->releaseWeight();
        m_calendarPage->releaseWeight();
        m_memorialPage->releaseWeight();
        scaleCurrentView();
        switch (m_focusTabs->currentIndex()) {
        case 0: m_timerPage->refresh(); break;
        case 1: m_overviewPage->refresh(); break;
        case 2: m_detailPage->refresh(); break;
        case 3: m_weekPage->refresh(); break;
        case 4: m_heatmapPage->refresh(); break;
        case 5: m_bestPage->refresh(); break;
        case 6: m_calendarPage->refresh(); break;
        case 7: m_memorialPage->refresh(); break;
        default: break;
        }
    });

    // 收件箱设置与通用设置编辑组件改为懒建（settings 容器属可淘汰页，见 ensureSettingsPages）
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

    connect(m_inbox, &InboxPage::settingsRequested, this, &MainWindow::openSettings);
    // SyncDetailsPage 与 SyncPage 的互相接线在 ensureSyncPages 懒建时建立（见下）

    // 默认显示收件箱
    switchPage(PAGE_INBOX);

    // 右上角缩放百分比提示（缩放后短暂显示）
    m_zoomBadge = new QLabel(this);
    m_zoomBadge->setStyleSheet(
        QStringLiteral("background: rgba(0,0,0,0.72); color: white; border: 1px solid %1; "
                       "border-radius: 4px; padding: 3px 10px; font-size: 12px;")
            .arg(kColorBorder));
    m_zoomBadge->hide();

    setWindowTitle(QStringLiteral("aw-qtui v%1 — ActivityWatch 客户端").arg(kAppVersion));
    resize(1280, 820);
    applyWindowMinimumSize(this);
}

// ==================================================================== //
// 可淘汰页生命周期：懒建 + 离开即回收 + LRU 有界缓存。
// 常驻页（收件箱/任务）始终构造、绝不淘汰。同步页与设置页虽无后台逻辑驻留，但同步容器
// 的后台代码（心跳/发现/配对提醒）已抽入无界面常驻的 SyncService，因此两容器都属可淘汰：
// 首次点入才经 ensure*Pages 构造其全部子页面，一旦切走就把整个 widget 树 delete 回收，
// 重建时还原其子标签现场（m_savedSubtab）。
// ==================================================================== //
bool MainWindow::isResidentPage(int page) const
{
    return page == PAGE_INBOX || page == PAGE_TODO;
}

bool MainWindow::ensureActivityPages()
{
    if (m_activity) // 已懒建
        return true;
    m_activity = new ActivityPage(m_api);
    m_day = new DayPage(m_api, m_tagStore);
    m_stats = new StatsPage(m_api, m_tagStore);
    m_query = new QueryPage(m_api);
    ui->awActivityHostLay->addWidget(m_activity);
    ui->awDayHostLay->addWidget(m_day);
    ui->awStatsHostLay->addWidget(m_stats);
    ui->awQueryHostLay->addWidget(m_query);
    styleSubTabs(m_awTabs);
    // 还原上次离开时的子标签现场
    if (m_savedSubtab.contains(PAGE_ACTIVITY))
        m_awTabs->setCurrentIndex(m_savedSubtab.value(PAGE_ACTIVITY));
    return true;
}

void MainWindow::releaseActivityPages()
{
    if (m_awTabs && m_activity)
        m_savedSubtab[PAGE_ACTIVITY] = m_awTabs->currentIndex();
    if (m_activity) { ui->awActivityHostLay->removeWidget(m_activity); m_activity->deleteLater(); m_activity = nullptr; }
    if (m_day)      { ui->awDayHostLay->removeWidget(m_day);      m_day->deleteLater();      m_day = nullptr; }
    if (m_stats)    { ui->awStatsHostLay->removeWidget(m_stats);   m_stats->deleteLater();    m_stats = nullptr; }
    if (m_query)    { ui->awQueryHostLay->removeWidget(m_query);   m_query->deleteLater();    m_query = nullptr; }
}

bool MainWindow::ensureFocusPages()
{
    if (m_timerPage) // 已懒建
        return true;
    m_timerPage = new FocusTimerPage(m_focusStore, m_todoStore);
    m_overviewPage = new FocusOverviewPage(m_focusStore);
    m_detailPage = new FocusDetailPage(m_focusStore, m_todoStore);
    m_weekPage = new FocusWeekPage(m_focusStore);
    m_heatmapPage = new FocusHeatmapPage(m_focusStore);
    m_bestPage = new FocusBestPage(m_focusStore);
    m_calendarPage = new FocusCalendarPage(m_focusStore, m_todoStore);
    m_memorialPage = new FocusMemorialPage(m_focusStore);
    // 计时专注并入「专注」统计页，作为第一个子标签（.ui 的 7 个统计 host 依次后移）
    m_focusTabs->insertTab(0, m_timerPage, QStringLiteral("🍅 计时"));
    ui->focusRecordHostLay->addWidget(m_overviewPage);
    ui->focusDetailHostLay->addWidget(m_detailPage);
    ui->focusWeekHostLay->addWidget(m_weekPage);
    ui->focusHeatmapHostLay->addWidget(m_heatmapPage);
    ui->focusBestHostLay->addWidget(m_bestPage);
    ui->focusCalendarHostLay->addWidget(m_calendarPage);
    ui->focusMemorialHostLay->addWidget(m_memorialPage);
    styleSubTabs(m_focusTabs);
    // 还原上次离开时的子标签现场
    if (m_savedSubtab.contains(PAGE_FOCUS_STATS))
        m_focusTabs->setCurrentIndex(m_savedSubtab.value(PAGE_FOCUS_STATS));
    return true;
}

void MainWindow::releaseFocusPages()
{
    if (m_focusTabs && m_timerPage)
        m_savedSubtab[PAGE_FOCUS_STATS] = m_focusTabs->currentIndex();
    // 计时子标签经 insertTab 挂入 QTabWidget，其余经 hostLay 挂入，移除方式不同
    if (m_timerPage) { m_focusTabs->removeTab(m_focusTabs->indexOf(m_timerPage)); m_timerPage->deleteLater(); m_timerPage = nullptr; }
    if (m_overviewPage)  { ui->focusRecordHostLay->removeWidget(m_overviewPage);  m_overviewPage->deleteLater();  m_overviewPage = nullptr; }
    if (m_detailPage)    { ui->focusDetailHostLay->removeWidget(m_detailPage);    m_detailPage->deleteLater();    m_detailPage = nullptr; }
    if (m_weekPage)      { ui->focusWeekHostLay->removeWidget(m_weekPage);        m_weekPage->deleteLater();      m_weekPage = nullptr; }
    if (m_heatmapPage)   { ui->focusHeatmapHostLay->removeWidget(m_heatmapPage);  m_heatmapPage->deleteLater();   m_heatmapPage = nullptr; }
    if (m_bestPage)      { ui->focusBestHostLay->removeWidget(m_bestPage);        m_bestPage->deleteLater();      m_bestPage = nullptr; }
    if (m_calendarPage)  { ui->focusCalendarHostLay->removeWidget(m_calendarPage); m_calendarPage->deleteLater(); m_calendarPage = nullptr; }
    if (m_memorialPage)  { ui->focusMemorialHostLay->removeWidget(m_memorialPage); m_memorialPage->deleteLater(); m_memorialPage = nullptr; }
}

// 同步容器：懒建 4 个子页（局域网同步/详情/D1云/冷备）+ 还原上次子标签现场
bool MainWindow::ensureSyncPages()
{
    if (m_sync) // 已懒建
        return true;
    m_sync = new SyncPage(m_api, m_syncService);
    m_d1Sync = new D1SyncPage(m_api);
    m_syncDetails = new SyncDetailsPage(m_api);
    m_cloudBackup = new CloudBackupPage(m_api);
    ui->syncHostLay->addWidget(m_sync);
    ui->syncDetailsHostLay->addWidget(m_syncDetails);
    ui->d1HostLay->addWidget(m_d1Sync);
    ui->cloudBackupHostLay->addWidget(m_cloudBackup);
    styleSubTabs(m_syncTabs);
    // SyncDetailsPage：「返回同步」切到容器内第一个子标签 + 转发日志到同步页
    connect(m_syncDetails, &SyncDetailsPage::backToSync, this, [this] { m_syncTabs->setCurrentIndex(0); });
    connect(m_syncDetails, &SyncDetailsPage::logMessage, m_sync, &SyncPage::logMessage);
    // 还原上次离开时的子标签现场
    if (m_savedSubtab.contains(PAGE_SYNC))
        m_syncTabs->setCurrentIndex(m_savedSubtab.value(PAGE_SYNC));
    return true;
}

void MainWindow::releaseSyncPages()
{
    if (m_syncTabs && m_sync)
        m_savedSubtab[PAGE_SYNC] = m_syncTabs->currentIndex();
    if (m_sync)       { ui->syncHostLay->removeWidget(m_sync);       m_sync->deleteLater();       m_sync = nullptr; }
    if (m_d1Sync)     { ui->d1HostLay->removeWidget(m_d1Sync);       m_d1Sync->deleteLater();     m_d1Sync = nullptr; }
    if (m_syncDetails){ ui->syncDetailsHostLay->removeWidget(m_syncDetails); m_syncDetails->deleteLater(); m_syncDetails = nullptr; }
    if (m_cloudBackup){ ui->cloudBackupHostLay->removeWidget(m_cloudBackup); m_cloudBackup->deleteLater(); m_cloudBackup = nullptr; }
}

// 设置容器：懒建「收件箱设置」+「通用设置」内嵌编辑器 + 还原上次子标签现场
bool MainWindow::ensureSettingsPages()
{
    if (m_inboxSettings) // 已懒建
        return true;
    m_inboxSettings = new InboxSettingsPage(m_localStore);
    ui->inboxHostLay->addWidget(m_inboxSettings);
    buildSettingsEditor();
    if (m_savedSubtab.contains(PAGE_SETTINGS))
        m_settingsTabs->setCurrentIndex(m_savedSubtab.value(PAGE_SETTINGS));
    return true;
}

void MainWindow::releaseSettingsPages()
{
    if (m_settingsTabs && m_inboxSettings)
        m_savedSubtab[PAGE_SETTINGS] = m_settingsTabs->currentIndex();
    if (m_inboxSettings) { ui->inboxHostLay->removeWidget(m_inboxSettings); m_inboxSettings->deleteLater(); m_inboxSettings = nullptr; }
    // 回收内嵌设置编辑器整树（含 SettingsWidget；rebuildSettingsEditor 同样用它）
    if (QLayoutItem *item = ui->generalHostLay->takeAt(0)) {
        if (QWidget *w = item->widget())
            w->deleteLater();
        delete item;
    }
    m_settingsEditor = nullptr;
    m_settingsEditorHost = nullptr;
}

void MainWindow::enterEvictable(int page)
{
    if (!isResidentPage(page)) {
        // 懒建子页面 + 在重建时由 ensure 内部还原子标签
        if (page == PAGE_ACTIVITY)   ensureActivityPages();
        else if (page == PAGE_FOCUS_STATS) ensureFocusPages();
        else if (page == PAGE_SYNC)  ensureSyncPages();
        else if (page == PAGE_SETTINGS) ensureSettingsPages();
    }
    // LRU 有界缓存：记当前页为最近使用；超过常驻上限则淘汰最久未用者（恒不淘汰当前页）
    m_residentEvictable.removeAll(page);
    m_residentEvictable.prepend(page);
    while (m_residentEvictable.size() > m_evictableCap) {
        const int victim = m_residentEvictable.takeLast();
        if (victim == page) { m_residentEvictable.prepend(page); break; } // 避免误淘汰当前页
        if (victim == PAGE_ACTIVITY)   releaseActivityPages();
        else if (victim == PAGE_FOCUS_STATS) releaseFocusPages();
        else if (victim == PAGE_SYNC)  releaseSyncPages();
        else if (victim == PAGE_SETTINGS) releaseSettingsPages();
    }
}

void MainWindow::leaveEvictable(int page)
{
    if (page == PAGE_ACTIVITY)   releaseActivityPages();
    else if (page == PAGE_FOCUS_STATS) releaseFocusPages();
    else if (page == PAGE_SYNC)  releaseSyncPages();
    else if (page == PAGE_SETTINGS) releaseSettingsPages();
    m_residentEvictable.removeAll(page);
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

    // 进入可淘汰页（活动/专注/同步/设置）：懒建其子页面并还原子标签现场。
    // 必须在设 m_currentPage 之前处理，避免子标签 currentChanged 的
    // m_currentPage==PAGE_X 守卫在本页仍显示时被误触发而重复刷新。
    if (index == PAGE_ACTIVITY || index == PAGE_FOCUS_STATS || index == PAGE_SYNC
        || index == PAGE_SETTINGS)
        enterEvictable(index);

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
    updateNavIcons();

    // 离开上一可淘汰页：保存子标签并回收其整个子页面 widget 树（懒建的逆操作）
    if (m_prevPage != index && (m_prevPage == PAGE_ACTIVITY || m_prevPage == PAGE_FOCUS_STATS
                                 || m_prevPage == PAGE_SYNC || m_prevPage == PAGE_SETTINGS))
        leaveEvictable(m_prevPage);

    // 页面特定处理
    if (index == PAGE_SYNC) {
        // 进入局域网同步界面：启动 UDP 广播发现 + 引擎「局域网自动开启同步」+ 立即刷新
        if (m_sync)
            m_sync->onEnteredSyncPage();
    } else if (m_prevPage == PAGE_SYNC) {
        // 从同步页切走：停掉服务端发现广播。
        // discoveryStop 桌面端服务端会当 no-op —— 桌面端设备发现必须常驻，
        // 否则离开页面就等于关掉广播/监听，对端换 IP、换 device id、上下线全都感知不到
        // （见 aw-sync-rust manager.rs 的 discovery_persistent）。该分支只对 Android 端服务端生效。
        if (m_api)
            m_api->discoveryStop();
    }
    if (index == PAGE_ACTIVITY) {
        // 进入活动容器：懒建已完成，现重拉当前子标签
        switch (m_awTabs->currentIndex()) {
        case 0: m_activity->refresh(); break;
        case 1: m_day->refresh(); break;
        case 2: m_stats->refresh(); break;
        case 3: m_query->refresh(); break;
        default: break;
        }
    }
    if (index == PAGE_FOCUS_STATS) {
        // 进入专注容器：懒建已完成，现重拉当前子标签
        switch (m_focusTabs->currentIndex()) {
        case 0: m_timerPage->refresh(); break;
        case 1: m_overviewPage->refresh(); break;
        case 2: m_detailPage->refresh(); break;
        case 3: m_weekPage->refresh(); break;
        case 4: m_heatmapPage->refresh(); break;
        case 5: m_bestPage->refresh(); break;
        case 6: m_calendarPage->refresh(); break;
        case 7: m_memorialPage->refresh(); break;
        default: break;
        }
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
    // 设备名称/操作系统已移入设置对话框，此处仅维持同步心跳（无界面引擎常驻）
    if (m_syncService)
        m_syncService->heartbeat();
}

// 远端变更监视：轮询 GET /api/0/sync/revision。
// 修订号只在本机业务库被「远端」改动过（快照合并有新应用/归档）时递增，所以值变了
// 就说明有远端数据落地 → 静默刷新列表。没有它的时候，服务端就算已经把手机的数据拉
// 回来了，界面也不会重载，用户看到的和「根本没同步」一模一样。
void MainWindow::pollRemoteChanges()
{
    if (!m_api)
        return;
    // 用户正在弹窗里编辑/确认时不打断，等下一轮
    if (QApplication::activeModalWidget())
        return;

    QNetworkReply *r = m_api->getSyncRevision();
    connect(r, &QNetworkReply::finished, this, [this, r] {
        r->deleteLater();
        QJsonDocument doc;
        QString err;
        if (!ApiClient::parseReply(r, &doc, &err))
            return; // 服务端不可用或旧版无此端点：静默忽略，不影响其它功能
        const qint64 rev =
            doc.object().value(QStringLiteral("revision")).toVariant().toLongLong();
        if (m_remoteRevision < 0) { // 首次：只记录基线，避免启动时白刷一遍
            m_remoteRevision = rev;
            return;
        }
        if (rev == m_remoteRevision)
            return;
        m_remoteRevision = rev;
        // 走轻量刷新入口：不用 refreshAll —— 它会再触发一轮局域网拉取，而数据已经落地了
        if (m_inbox) {
            m_inbox->loadTagTree();
            m_inbox->loadNotes(true);
        }
        if (m_todo)
            m_todo->refresh();
        // 活动页刷新较重（图表重建）：只在正显示时刷
        if (m_activity && m_currentPage == PAGE_ACTIVITY)
            m_activity->refresh();
    });
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
    m_settingsEditorHost = host; // 供 releaseSettingsPages / rebuildSettingsEditor 整树回收
    ui->generalHostLay->addWidget(host);
}

// 应用后按当前设置重建编辑组件，同步新主题/图标/效果的配色与已保存值
void MainWindow::rebuildSettingsEditor()
{
    if (QWidget *w = m_settingsEditorHost) {
        QLayoutItem *item = ui->generalHostLay->takeAt(ui->generalHostLay->indexOf(w));
        if (item) {
            if (w) {
                w->deleteLater();
            }
            delete item;
        }
    } else if (QLayoutItem *item = ui->generalHostLay->takeAt(0)) { // 兼容性兜底
        if (QWidget *w = item->widget())
            w->deleteLater();
        delete item;
    }
    m_settingsEditor = nullptr;
    m_settingsEditorHost = nullptr;
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
        m_tray->setToolTip(QStringLiteral("aw-qtui v%1 · %2").arg(kAppVersion, QString::fromUtf8(gTheme->name)));
    }
    qDebug() << "[MainWindow] theme applied:" << t->id;
}

// 窗户外屏自愈：多屏环境（混合 DPI、副屏被拔掉、系统首次摆放跑偏）下，窗口可能被摆在
// **所有显示器之外**。此时进程活着、消息循环正常、show() 也已执行，但用户什么都看不到；
// 而托盘 / 单实例的「置前」只做 show() + raise()，不会把窗口挪回来 —— 症状就是「界面一直出不来」。
// 判据与处置都刻意保守：只要窗口还能被抓住就绝不移动它（用户自己摆的位置永远优先）。
void MainWindow::ensureOnScreen()
{
    // 必须在窗口「可见」时判：最小化/隐藏期间系统会把窗口挪到 (-32000,-32000)，
    // 那是正常的停靠位置，不是跑到屏外 —— 否则每次托盘唤醒都会把用户的窗口重新居中。
    if (!isVisible())
        return;
    const QList<QScreen *> screens = QGuiApplication::screens();
    if (screens.isEmpty())
        return;

    const QRect frame = frameGeometry();
    // 只挨着一点点（比如标题栏整个落到屏外）同样抓不住窗口，所以要判「顶部条带」与某块屏
    // 可用区域的交叠面积，而不是简单 intersects()。
    const QRect strip(frame.left(), frame.top(), frame.width(), qMin(si(48), frame.height()));
    for (const QScreen *s : screens) {
        const QRect hit = strip.intersected(s->availableGeometry());
        if (hit.width() >= si(80) && hit.height() >= si(16))
            return; // 抓得住 → 保持原样
    }

    QScreen *home = screen();
    if (!home)
        home = QGuiApplication::primaryScreen();
    if (!home)
        return;

    const bool wasMax = isMaximized();
    if (wasMax)
        showNormal();
    const QRect avail = home->availableGeometry();
    const QSize sz = size().boundedTo(avail.size());
    const QPoint pos(avail.left() + (avail.width() - sz.width()) / 2,
                     avail.top() + (avail.height() - sz.height()) / 2);
    resize(sz);
    move(pos);
    if (wasMax)
        showMaximized();
    qInfo().noquote() << "[ui] 窗口原本落在所有显示器之外，已移回" << home->name() << pos;
}

// 首次显示后复核摆放结果：系统给的初始位置本身可能就是屏外，所以要等这一轮布局落定
// （singleShot(0)）再判 —— 在 show() 之前判会读到尚未被系统改写的位置，等于没查。
void MainWindow::showEvent(QShowEvent *event)
{
    QMainWindow::showEvent(event);
    if (m_geomChecked)
        return;
    m_geomChecked = true;
    QTimer::singleShot(0, this, [this] { ensureOnScreen(); });
}

void MainWindow::wakeUpAndShow()
{
    if (isMinimized()) {
        // 最大化中被最小化：还原时要回到最大化，否则托盘/热键唤醒一次就悄悄丢掉最大化态
        if (windowState().testFlag(Qt::WindowMaximized))
            showMaximized();
        else
            showNormal();
    }
    // 修位置放在 show() 之后：只有窗口真正可见时，frameGeometry() 才是系统摆放的
    // 真实结果（隐藏期间读到的可能是停靠位置）。此时若发现它整个在屏外，就挪回来。
    show();
    raise();
    activateWindow();
    raiseWindowToFront(this);
    ensureOnScreen();
}

// 跨屏移动 / 改缩放比后重算最小尺寸：applyWindowMinimumSize 读的是窗口当前所在屏，
// 只在构造时算一次会把旧屏的钳制值带到新屏上（小屏/高缩放屏上最小尺寸可能大于工作区）。
void MainWindow::changeEvent(QEvent *event)
{
    if (event->type() == QEvent::WindowStateChange)
        applyWindowMinimumSize(this);
    QMainWindow::changeEvent(event);
}

// 单实例让位支撑：同版本被再次启动时，请求方要求把本窗口拉到前台
void MainWindow::raiseToFront()
{
    wakeUpAndShow();
}

// 单实例让位支撑：更新版本请求本实例退出。
// 顺序很重要 —— 先把用户还没落库的编辑冲刷掉，再拉起新版，最后才 quit。
// 退出前拉起新版是幂等的：若新版其实已经在跑，它会命中「同版本静默退出」规则，
// 不会叠加出第二个实例（见 singleinstance.cpp 的 ExitSameVersion 分支）。
void MainWindow::requestQuitForYield(const QString &newerExe)
{
    qInfo().noquote() << "[yield] 收到新版让位请求，准备优雅退出。请求方:" << newerExe;

    // 1) 冲刷 debounce 中的编辑（任务标题/备注 250ms 定时提交）
    if (m_todo)
        m_todo->flushPendingEdits();

    // 2) 走与托盘「退出」相同的路径：置位后 closeEvent 不再拦截成最小化到托盘
    m_trayExiting = true;

    // 3) 退出前确保新版在跑（不改变窗口几何/状态，纯接管）
    if (!newerExe.isEmpty()) {
        const QFileInfo fi(newerExe);
        if (fi.exists() && fi.isFile()) {
            if (!QProcess::startDetached(fi.absoluteFilePath(), QStringList()))
                qWarning().noquote() << "[yield] 新版拉起失败:" << fi.absoluteFilePath();
        } else {
            qWarning().noquote() << "[yield] 新版 exe 不存在，跳过拉起:" << newerExe;
        }
    }

    qApp->quit();
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
    // 最大化几何修正（多屏 / 混合 DPI）。三块屏缩放比不一致时，最大化偏离全部出在这里：
    // 1) 必须先让 DefWindowProc 填满 MINMAXINFO，取回系统默认的 ptMinPosition / ptMaxTrackSize
    //    （旧实现在此处直接 return，结构体其余字段留在未初始化状态，行为随 Windows 何时发消息而变）。
    // 2) Windows 对带边框窗口把「整个窗口矩形」当作最大化结果，而该矩形比可见区域多出
    //    四周约 7-8px 的不可见 resize 边框 —— 所以要按边框把 rcWork 向外扩，
    //    旧实现原样写入 rcWork，等于内容四周各内缩一个边框宽（右侧/底部被切掉的由来）。
    // 3) 边框宽与 DPI 一律按「即将最大化到的那个显示器」取，不能用进程默认值。
    // 4) Qt 的最小尺寸是逻辑像素，折算成该屏物理像素再交给系统，并钳进工作区内。
    if (eventType == QByteArrayLiteral("windows_generic_MSG")) {
        const auto *msg = static_cast<const MSG *>(message);
        if (msg->message == WM_GETMINMAXINFO && msg->hwnd) {
            HWND hwnd = msg->hwnd;
            const HMONITOR hMon = MonitorFromWindow(hwnd, MONITOR_DEFAULTTONEAREST);
            MONITORINFO mi = { sizeof(mi) };
            if (hMon && GetMonitorInfo(hMon, &mi)) {
                DefWindowProcW(hwnd, msg->message, msg->wParam, msg->lParam);
                auto *mmi = reinterpret_cast<MINMAXINFO *>(msg->lParam);

                UINT dpiX = 96, dpiY = 96;
                if (FAILED(GetDpiForMonitor(hMon, MDT_EFFECTIVE_DPI, &dpiX, &dpiY)) || !dpiX || !dpiY)
                    dpiX = dpiY = 96;
                // 只有 SM_CXPADDEDBORDER 一个索引（没有 SM_CYPADDEDBORDER），
                // 垂直方向同样叠加它。
                const int bx = GetSystemMetricsForDpi(dpiX, SM_CXSIZEFRAME)
                             + GetSystemMetricsForDpi(dpiX, SM_CXPADDEDBORDER);
                const int by = GetSystemMetricsForDpi(dpiY, SM_CYSIZEFRAME)
                             + GetSystemMetricsForDpi(dpiX, SM_CXPADDEDBORDER);
                const LONG workW = mi.rcWork.right - mi.rcWork.left;
                const LONG workH = mi.rcWork.bottom - mi.rcWork.top;

                mmi->ptMaxPosition.x = mi.rcWork.left - bx;
                mmi->ptMaxPosition.y = mi.rcWork.top - by;
                mmi->ptMaxSize.x = workW + bx * 2;
                mmi->ptMaxSize.y = workH + by * 2;

                const qreal dpr = qreal(dpiX) / 96.0;
                const QSize minSz = minimumSize();
                mmi->ptMinTrackSize.x = LONG(qMin<qreal>(minSz.width() * dpr, workW));
                mmi->ptMinTrackSize.y = LONG(qMin<qreal>(minSz.height() * dpr, workH));

                if (result)
                    *result = 0;
                return true;
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
    case Qt::Key_8: switchPage(PAGE_SYNC); m_syncTabs->setCurrentIndex(2); return; // D1云
    case Qt::Key_9: switchPage(PAGE_SYNC); m_syncTabs->setCurrentIndex(3); return; // 冷备
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
            // 同步容器页：按当前子标签分发刷新（页面为懒建，此刻必已建好）
            if (!m_sync)
                return;
            switch (m_syncTabs->currentIndex()) {
            case 0: m_sync->refreshDevices(); break;
            case 1: m_syncDetails->refreshLogs(); break;
            case 2: m_d1Sync->refreshStatus(); break;
            case 3: m_cloudBackup->refreshStatus(); break;
            }
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
    case PAGE_SYNC: {
        // 同步容器：当前激活子标签的页面补缩放
        QWidget *w = m_syncTabs ? m_syncTabs->currentWidget() : nullptr;
        if (w == m_sync) { /* SyncPage 无 applyUiScale，靠全局 QSS */ }
        else if (w == m_syncDetails) m_syncDetails->applyTheme();
        else if (w == m_d1Sync) m_d1Sync->applyUiScale();
        else if (w == m_cloudBackup) m_cloudBackup->applyUiScale();
        break;
    }
    default:
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
