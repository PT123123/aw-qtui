// mainwindow.cpp
#include "mainwindow.h"

#include "activitypage.h"
#include "apiclient.h"
#include "appsettings.h"
#include "config.h"
#include "daypage.h"
#include "focusstore.h"
#include "globalshortcut.h"
#include "inboxpage.h"
#include "mdnsdiscovery.h"
#include "settingsdialog.h"
#include "statspage.h"
#include "syncpage.h"
#include "tagstore.h"
#include "theme.h"
#include "timelinepage.h"
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
#include <QGraphicsOpacityEffect>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QKeySequence>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QPlainTextEdit>
#include <QPropertyAnimation>
#include <QPushButton>
#include <QSizePolicy>
#include <QSpinBox>
#include <QSystemTrayIcon>
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
    buildUi();
    // 按读取到的状态应用窄栏/展开（buildUi 默认构建窄栏）
    setNavExpanded(m_navExpanded);

    // 页面缩放：应用上次保存的比例（0.3~3.0），并安装全局事件过滤器拦截 Ctrl+滚轮 / +- 键
    m_zoom = loadUiZoom();
    // 缩放对齐开启时，把历史保存的非整数缩放吸附到干净档位并持久化（避免边缘发虚）
    if (gFixSnapZoom) {
        const qreal snapped = snapZoom(m_zoom);
        if (!qFuzzyCompare(snapped, m_zoom)) {
            m_zoom = snapped;
            saveUiZoom(m_zoom);
        }
    }
    applyUiScale();
    qApp->installEventFilter(this);

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

    // 系统托盘：emoji 图标（复用主题 emoji，无外部资源），左键切换显示/隐藏，右键菜单
    setupTray();
}

MainWindow::~MainWindow()
{
    delete m_tagStore;
}

// 系统托盘：图标用固定 kAppEmoji(🌿) + 白色圆形背景（makeEmojiIcon 默认 WhiteCircle，已含 16/32px 托盘尺寸），零外部资源。
// 交互：左键/双击切换显示隐藏；右键菜单「显示/隐藏主窗口」「退出」；关窗默认最小化到托盘。
void MainWindow::setupTray()
{
    if (!QSystemTrayIcon::isSystemTrayAvailable())
        return;
    m_tray = new QSystemTrayIcon(this);
    m_tray->setIcon(makeEmojiIcon(QString::fromUtf8(kAppEmoji)));
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
    auto *central = new QWidget;
    auto *root = new QHBoxLayout(central);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(0);

    // ---- 左侧导航：默认窄栏（仅图标），点击 ☰ 展开显示图标+文字 ----
    m_nav = new QWidget;
    m_nav->setObjectName(QStringLiteral("NavSidebar"));
    m_nav->setFixedWidth(si(kNavCollapsedPx));
    auto *nav = m_nav;
    auto *navLay = new QVBoxLayout(nav);
    navLay->setContentsMargins(0, si(12), 0, si(12));
    navLay->setSpacing(si(2));

    // 展开/收起切换按钮：窄栏时显示 ☰，展开后显示 «
    m_navToggle = new QToolButton;
    m_navToggle->setObjectName(QStringLiteral("NavToggle"));
    m_navToggle->setText(QStringLiteral("☰"));
    m_navToggle->setToolTip(QStringLiteral("展开导航"));
    m_navToggle->setCursor(Qt::PointingHandCursor);
    m_navToggle->setProperty("expanded", false);
    navLay->addWidget(m_navToggle);
    connect(m_navToggle, &QToolButton::clicked, this, [this] { setNavExpanded(!m_navExpanded); });

    // 可折叠分组（展开态可用）：header 点击展开/收起，容器内放导航按钮
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
        header->setArrowType(expanded ? Qt::DownArrow : Qt::RightArrow);
        header->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);

        auto *box = new QWidget;
        auto *lay = new QVBoxLayout(box);
        lay->setContentsMargins(0, 0, 0, 0);
        lay->setSpacing(4);

        QObject::connect(header, &QToolButton::toggled, box, &QWidget::setVisible);
        QObject::connect(header, &QToolButton::toggled, header, [header](bool on) {
            header->setArrowType(on ? Qt::DownArrow : Qt::RightArrow);
        });

        navLay->addWidget(header);
        navLay->addWidget(box);
        box->setVisible(expanded);
        return NavSection{header, box, lay};
    };

    // 导航按钮统一工厂：记录 emoji 与 label，窄栏只显示 emoji（居中），展开显示 emoji + 文字
    auto makeNavBtn = [this](const char *emoji, const char *label) -> QPushButton * {
        auto *b = new QPushButton;
        b->setObjectName(QStringLiteral("NavBtn"));
        b->setCheckable(true);
        b->setCursor(Qt::PointingHandCursor);
        b->setProperty("navEmoji", QString::fromUtf8(emoji));
        b->setProperty("navLabel", QString::fromUtf8(label));
        b->setProperty("expanded", false); // 初始窄栏：仅图标
        b->setToolTip(QString::fromUtf8(label));
        b->setText(QString::fromUtf8(emoji));
        m_navButtons.append(b);
        return b;
    };

    // ---- 分组 1：收件箱 ----
    NavSection inboxSec = makeSection(QStringLiteral("收件箱"), /*expanded*/ true);

    m_navInbox = makeNavBtn("📥", "收件箱");
    m_navTodo = makeNavBtn("☑", "任务");
    m_navSync = makeNavBtn("⇄", "局域网同步");
    inboxSec.layout->addWidget(m_navInbox);
    inboxSec.layout->addWidget(m_navTodo);
    inboxSec.layout->addWidget(m_navSync);

    // ---- 分组 2：ACTIVITYWATCH ----
    NavSection awSec = makeSection(QStringLiteral("ACTIVITYWATCH"), /*expanded*/ false);

    m_navActivity = makeNavBtn("📊", "Activity");
    m_navTimeline = makeNavBtn("⏱", "Timeline");
    m_navDay = makeNavBtn("🏷", "标签 Day");
    m_navStats = makeNavBtn("📈", "统计");
    awSec.layout->addWidget(m_navActivity);
    awSec.layout->addWidget(m_navTimeline);
    awSec.layout->addWidget(m_navDay);
    awSec.layout->addWidget(m_navStats);

    // 窄栏模式下隐藏分组标题、全部图标平铺显示（分组展开状态由 setNavExpanded 统一管理）
    m_navSectionHeaders = {inboxSec.header, awSec.header};
    inboxSec.header->setVisible(false);
    awSec.header->setVisible(false);
    inboxSec.header->setChecked(true);
    awSec.header->setChecked(true);

    auto *grp = new QButtonGroup(this);
    grp->addButton(m_navActivity);
    grp->addButton(m_navTimeline);
    grp->addButton(m_navInbox);
    grp->addButton(m_navTodo);
    grp->addButton(m_navSync);
    grp->addButton(m_navDay);
    grp->addButton(m_navStats);

    connect(m_navActivity, &QPushButton::clicked, this, [this] { switchPage(0); });
    connect(m_navTimeline, &QPushButton::clicked, this, [this] { switchPage(1); });
    connect(m_navInbox, &QPushButton::clicked, this, [this] { switchPage(2); });
    connect(m_navTodo, &QPushButton::clicked, this, [this] { switchPage(3); });
    connect(m_navSync, &QPushButton::clicked, this, [this] { switchPage(4); });
    connect(m_navDay, &QPushButton::clicked, this, [this] { switchPage(5); });
    connect(m_navStats, &QPushButton::clicked, this, [this] { switchPage(6); });

    navLay->addStretch(1);

    root->addWidget(nav);

    // ---- 页面堆栈 ----
    m_stack = new QStackedWidget;
    qDebug() << "[MainWindow] creating ActivityPage...";
    m_activity = new ActivityPage(m_api);
    qDebug() << "[MainWindow] ActivityPage created";
    qDebug() << "[MainWindow] creating TimelinePage...";
    m_timeline = new TimelinePage(m_api);
    qDebug() << "[MainWindow] TimelinePage created";
    qDebug() << "[MainWindow] creating InboxPage...";
    m_inbox = new InboxPage(m_api);
    qDebug() << "[MainWindow] InboxPage created";
    connect(m_inbox, &InboxPage::settingsRequested, this, &MainWindow::openSettings);
    qDebug() << "[MainWindow] creating TodoPage...";
    m_todo = new TodoPage(m_todoStore, m_focusStore);
    qDebug() << "[MainWindow] TodoPage created";
    qDebug() << "[MainWindow] creating SyncPage...";
    m_sync = new SyncPage(m_api, m_mdns);
    qDebug() << "[MainWindow] SyncPage created";
    qDebug() << "[MainWindow] creating DayPage...";
    m_day = new DayPage(m_api, m_tagStore);
    qDebug() << "[MainWindow] DayPage created";
    qDebug() << "[MainWindow] creating StatsPage...";
    m_stats = new StatsPage(m_api, m_tagStore);
    qDebug() << "[MainWindow] StatsPage created";
    m_stack->addWidget(m_activity);   // 0
    m_stack->addWidget(m_timeline);   // 1
    m_stack->addWidget(m_inbox);      // 2
    m_stack->addWidget(m_todo);       // 3
    m_stack->addWidget(m_sync);       // 4
    m_stack->addWidget(m_day);        // 5
    m_stack->addWidget(m_stats);      // 6
    qDebug() << "[MainWindow] all pages added to stack";
    root->addWidget(m_stack, 1);

    // 默认落在收件箱：activitywatch 分组默认收起，启动时导航保持可见/高亮一致
    switchPage(2);

    setCentralWidget(central);

    // 右上角缩放百分比提示（缩放后短暂显示）
    m_zoomBadge = new QLabel(this);
    m_zoomBadge->setStyleSheet(
        QStringLiteral("background: rgba(0,0,0,0.72); color: white; border: 1px solid %1; "
                       "border-radius: 4px; padding: 3px 10px; font-size: 12px;")
            .arg(kColorBorder));
    m_zoomBadge->hide();

    setWindowTitle(QStringLiteral("aw-qtui — ActivityWatch 客户端"));
    resize(1280, 820);
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

    // 按钮文字：窄栏只显示 emoji（居中），展开显示 emoji + 文字（左对齐）
    for (auto *b : m_navButtons) {
        const QString emoji = b->property("navEmoji").toString();
        const QString label = b->property("navLabel").toString();
        b->setText(expanded ? emoji + QStringLiteral("  ") + label : emoji);
        b->setProperty("expanded", expanded);
        b->style()->unpolish(b);
        b->style()->polish(b);
    }

    if (m_navToggle) {
        m_navToggle->setText(expanded ? QStringLiteral("«") : QStringLiteral("☰"));
        m_navToggle->setToolTip(expanded ? QStringLiteral("收起导航") : QStringLiteral("展开导航"));
        m_navToggle->setProperty("expanded", expanded);
        m_navToggle->style()->unpolish(m_navToggle);
        m_navToggle->style()->polish(m_navToggle);
    }

    if (m_nav) {
        m_nav->setFixedWidth(si(expanded ? kNavExpandedPx : kNavCollapsedPx));
        if (auto *nl = qobject_cast<QVBoxLayout *>(m_nav->layout())) {
            nl->setContentsMargins(0, si(expanded ? 16 : 12), 0, si(12));
            nl->setSpacing(si(expanded ? 4 : 2));
        }
        m_nav->layout()->activate();
        m_nav->update();
    }
}

void MainWindow::switchPage(int index)
{
    m_stack->setCurrentIndex(index);
    m_navActivity->setChecked(index == 0);
    m_navTimeline->setChecked(index == 1);
    m_navInbox->setChecked(index == 2);
    m_navTodo->setChecked(index == 3);
    m_navSync->setChecked(index == 4);
    m_navDay->setChecked(index == 5);
    m_navStats->setChecked(index == 6);
    if (index == 4)
        m_sync->refreshDevices();
    if (index == 3)
        m_todo->refresh();

    // 切页淡入（受全局动画开关控制；动画结束即移除透明度效果，避免长期合成分开销）
    if (gFxAnimations) {
        if (QWidget *page = m_stack->widget(index)) {
            auto *eff = new QGraphicsOpacityEffect(page);
            eff->setOpacity(0.0);
            page->setGraphicsEffect(eff);
            auto *anim = new QPropertyAnimation(eff, "opacity", page);
            anim->setDuration(150);
            anim->setStartValue(0.0);
            anim->setEndValue(1.0);
            anim->setEasingCurve(QEasingCurve::OutCubic);
            // 注意：setGraphicsEffect(nullptr) 会同步删除原效果，此处不要再对 eff 做 deleteLater
            connect(anim, &QPropertyAnimation::finished, page, [page] {
                page->setGraphicsEffect(nullptr);
            });
            anim->start(QAbstractAnimation::DeleteWhenStopped);
        }
    }
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
    const UiEffects curFx = loadUiEffects();
    SettingsDialog dlg(loadShortcuts(), curTheme, curFx, this);
    if (dlg.exec() != QDialog::Accepted) {
        applyShortcuts(); // 取消：恢复原注册
        return;
    }
    // 主题变更：应用并持久化
    const QString newTheme = dlg.themeId();
    const UiEffects newFx = dlg.uiEffects();
    const bool fxChanged = newFx.shadowLevel != gShadowLevel || newFx.glassLevel != gGlassLevel
                           || newFx.animations != gFxAnimations || newFx.dwmBackdrop != gDwmBackdrop
                           || newFx.fixEdgeLowContrast != gFixEdgeLowContrast
                           || newFx.fixGlassOpaque != gFixGlassOpaque
                           || newFx.fixSnapZoom != gFixSnapZoom
                           || newFx.fixShadowAdaptive != gFixShadowAdaptive;
    if (newTheme != curTheme)
        saveThemeId(newTheme);
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

    saveShortcuts(dlg.config());
    const QStringList failed = applyShortcuts();
    // 冲突提示用非模态 toast：短暂显示后自动消失，不阻塞、无需点击
    if (!failed.isEmpty())
        showToast(QStringLiteral("快捷键冲突：%1（可能已被其它程序占用，已保存但暂不生效）")
                      .arg(failed.join(QLatin1Char(' '))));
}

void MainWindow::applyTheme(const QString &themeId)
{
    const Theme *t = findTheme(themeId);
    if (!t)
        return;
    gTheme = t;
    applyThemeColors(*t);
    applyUiScale(); // 重建全局 QSS（按缩放）+ 缩放字体 + 导航 + inbox/todo 页面

    // 页面级内联样式按新主题重建
    if (m_activity)
        m_activity->applyTheme();
    if (m_timeline)
        m_timeline->applyTheme();
    if (m_day)
        m_day->applyTheme();
    if (m_stats)
        m_stats->applyTheme();

    // 强制顶层窗口重绘，刷新自绘的图表 / 时间轴等控件
    for (QWidget *w : QApplication::topLevelWidgets())
        w->update();
    // 托盘图标随主题刷新（固定 kAppEmoji + 白底圆，与主题无关）
    if (m_tray) {
        m_tray->setIcon(makeEmojiIcon(QString::fromUtf8(kAppEmoji)));
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
        switchPage(2);
        m_inbox->openNewNote();
        break;
    case kHotkeyShowInboxId:
        // 唤醒并跳转收件箱
        wakeUpAndShow();
        switchPage(2);
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
    // 快捷键：1-5 切页，F5 刷新当前页
    switch (event->key()) {
    case Qt::Key_1: switchPage(0); return;
    case Qt::Key_2: switchPage(1); return;
    case Qt::Key_3: switchPage(2); return;
    case Qt::Key_4: switchPage(3); return;
    case Qt::Key_5: switchPage(4); return;
    case Qt::Key_6: switchPage(5); return;
    case Qt::Key_7: switchPage(6); return;
    case Qt::Key_F5:
        if (m_stack->currentIndex() == 0) m_activity->refresh();
        else if (m_stack->currentIndex() == 1) m_timeline->refresh();
        else if (m_stack->currentIndex() == 2) m_inbox->refreshAll();
        else if (m_stack->currentIndex() == 3) m_todo->refresh();
        else if (m_stack->currentIndex() == 4) m_sync->refreshDevices();
        else if (m_stack->currentIndex() == 5) m_day->refresh();
        else if (m_stack->currentIndex() == 6) m_stats->refresh();
        return;
    default: break;
    }
    QMainWindow::keyPressEvent(event);
}

bool MainWindow::eventFilter(QObject *obj, QEvent *event)
{
    // 缩放仅支持快捷键：Ctrl+加 / Ctrl+减 / Ctrl+0 复位。
    // 滚轮缩放已移除：按住 Ctrl 滚动会重建整页（Inbox 列表全部重渲染）导致卡顿。
    if (event->type() == QEvent::KeyPress && isInsideZoomable(obj)) {
        auto *ke = static_cast<QKeyEvent *>(event);
        const bool ctrl = ke->modifiers().testFlag(Qt::ControlModifier);
        const int key = ke->key();
        if (ctrl) {
            bool handled = true;
            if (key == Qt::Key_Plus || key == Qt::Key_Equal) {
                zoomBy(1.15, false);
            } else if (key == Qt::Key_Minus) {
                zoomBy(1.0 / 1.15, false);
            } else if (key == Qt::Key_0) {
                setZoom(1.0, false);
            } else {
                handled = false;
            }
            if (handled) {
                ke->accept();
                return true;
            }
        }
    }
    return QMainWindow::eventFilter(obj, event);
}

void MainWindow::zoomBy(qreal factor, bool underMouse)
{
    Q_UNUSED(underMouse);
    setZoom(m_zoom * factor, false);
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

void MainWindow::applyUiScale()
{
    gUiScale = m_zoom;

    // 重新生成全局 QSS：把 Npx 字号/间距按缩放比放大，让控件重新布局（文字不糊）
    qApp->setStyleSheet(scaleQss(gGlobalQss));

    // 同步放大基准字体（与全局 QSS 基准 13px 一致），未内联字号的控件随之重排
    QFont f = qApp->font();
    f.setPixelSize(qRound(13 * m_zoom));
    qApp->setFont(f);

    // 左侧导航：宽度、间距随缩放比与展开状态调整（导航按钮样式来自全局 QSS 已缩放）
    if (m_nav) {
        m_nav->setFixedWidth(si(m_navExpanded ? kNavExpandedPx : kNavCollapsedPx));
        auto *nl = qobject_cast<QVBoxLayout *>(m_nav->layout());
        if (nl) {
            nl->setContentsMargins(0, si(m_navExpanded ? 16 : 12), 0, si(12));
            nl->setSpacing(si(m_navExpanded ? 4 : 2));
        }
    }

    // 页面级缩放样式（目前重点：Inbox 页）
    if (m_inbox)
        m_inbox->applyUiScale();
    if (m_todo)
        m_todo->applyUiScale();
}

void MainWindow::applyDwmBackdrop()
{
#ifdef Q_OS_WIN
    HWND hwnd = (HWND)winId();
    if (!hwnd)
        return;
    // DWMWA_SYSTEMBACKDROP_TYPE = 38（Win11 22H2+）
    // DWMSBT_AUTO=0, DWMSBT_NONE=1, DWMSBT_MAINWINDOW=2(Mica), DWMSBT_TRANSIENTWINDOW=3(Acrylic)
    const DWORD type = gDwmBackdrop ? 2 : 1;
    HRESULT hr = DwmSetWindowAttribute(hwnd, 38, &type, sizeof(type));
    if (gDwmBackdrop && SUCCEEDED(hr)) {
        setAttribute(Qt::WA_TranslucentBackground, true);
        // alpha=1 极淡背景：Qt 会在重绘时用它填充整个区域（正确擦除旧像素，避免残影），
        // 但 alpha=1 人眼几乎不可见，DWM Mica 背景仍能透出。
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

bool MainWindow::isInsideZoomable(QObject *obj) const
{
    auto *w = qobject_cast<QWidget *>(obj);
    // 主窗口内容为普通 QWidget 层级，window() 即主窗口；对话框等顶层窗口除外
    return w && w->window() == static_cast<const QWidget *>(this);
}

bool MainWindow::isTextEditingWidget(const QWidget *w) const
{
    if (!w)
        return false;
    return qobject_cast<const QLineEdit *>(w) || qobject_cast<const QTextEdit *>(w)
        || qobject_cast<const QPlainTextEdit *>(w) || qobject_cast<const QComboBox *>(w)
        || qobject_cast<const QSpinBox *>(w);
}

} // namespace awqtui
