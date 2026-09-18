// mainwindow.h —— 主窗口：左侧导航 + 页面堆栈
#pragma once

#include <QHash>
#include <QList>
#include <QMainWindow>
#include <QStackedWidget>
#include <QStringList>

class QCloseEvent;
class QEvent;
class QKeyEvent;
class QLabel;
class QPushButton;
class QSystemTrayIcon;
class QTabWidget;
class QTimer;
class QToolButton;
class QWheelEvent;

// Qt Designer 布局（mainwindow.ui），全局命名空间
namespace Ui { class MainWindow; }

namespace awqtui {

class ApiClient;
class MdnsDiscovery;
class GlobalHotkey;
class TagStore;
class TodoSource;
class FocusSource;
class LocalStore;
class ActivityPage;
class DayPage;
class StatsPage;
class InboxPage;
class InboxSettingsPage;
class SyncPage;
class SyncService;
class D1SyncPage;
class QueryPage;
class SyncDetailsPage;
class CloudBackupPage;
class TodoPage;
class FocusTimerPage;
class FocusOverviewPage;
class FocusDetailPage;
class FocusWeekPage;
class FocusHeatmapPage;
class FocusBestPage;
class FocusCalendarPage;
class FocusMemorialPage;
class SettingsWidget;

class MainWindow : public QMainWindow
{
    Q_OBJECT
public:
    explicit MainWindow(const QString &serverUrl = QString(), QWidget *parent = nullptr);
    ~MainWindow() override;

    InboxPage *inboxPage() const { return m_inbox; }
    InboxSettingsPage *inboxSettingsPage() const { return m_inboxSettings; }
    SyncPage *syncPage() const { return m_sync; }
    D1SyncPage *d1SyncPage() const { return m_d1Sync; }
    QueryPage *queryPage() const { return m_query; }
    SyncDetailsPage *syncDetailsPage() const { return m_syncDetails; }
    CloudBackupPage *cloudBackupPage() const { return m_cloudBackup; }
    ActivityPage *activityPage() const { return m_activity; }
    DayPage *dayPage() const { return m_day; }
    StatsPage *statsPage() const { return m_stats; }
    TodoPage *todoPage() const { return m_todo; }

    // 专注模块页面（Todo 内部用）
    FocusTimerPage *focusTimerPage() const { return m_timerPage; }

    void switchPage(int index);

    // 当前页面缩放比（1.0 = 100%）
    qreal zoomScale() const { return m_zoom; }

    // ── 单实例让位支撑（见 src/singleinstance.h）──
    // 已有同版本实例被再次启动时，把本窗口唤醒到前台
    void raiseToFront();
    // 被更新版本请求让位：flush 待提交编辑 → 退出前拉起新版 → 优雅退出。
    // newerExe 为空则只退出（不改变「谁负责拉起」的责任）。
    void requestQuitForYield(const QString &newerExe);

    // 应用指定主题：更新语义色/全局 QSS、刷新页面内联样式与自绘控件并重绘
    void applyTheme(const QString &themeId);

protected:
    void keyPressEvent(QKeyEvent *event) override;
    void paintEvent(QPaintEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;
    bool nativeEvent(const QByteArray &eventType, void *message, qintptr *result) override;
    bool eventFilter(QObject *obj, QEvent *event) override;
    void closeEvent(QCloseEvent *event) override;

private slots:
    void onGlobalHotkey(int id);
    // 轮询服务端数据修订号：远端（手机/其它设备）数据落地后静默刷新列表，
    // 否则局域网同步拉到的内容要等用户操作界面才可见 —— 看起来就像「没同步」。
    void pollRemoteChanges();

private:
    // Qt Designer-generated layout object (mainwindow.ui -> ui_mainwindow.h)
    Ui::MainWindow *ui = nullptr;
    void buildUi();
    // 左侧导航在「窄栏（仅图标）」与「展开（图标+文字）」之间切换，并持久化状态
    void setNavExpanded(bool expanded);
    // 导航图标重绘：选中 accent / 未选中 muted，随选中态、展开态与缩放变化调用
    void updateNavIcons();
    // 系统托盘：emoji 图标（复用主题 emoji + accent，无外部资源文件），
    // 左键切换显示/隐藏，右键菜单（显示/隐藏、退出），关窗最小化到托盘
    void setupTray();
    void updateStatus();
    // 读取配置并注册全部全局热键；返回未能注册的快捷键描述列表（空 = 全部成功）
    QStringList applyShortcuts();
    // 打开设置对话框：编辑期间暂停热键，保存/取消后重新注册
    void openSettings();
    // 在设置页「通用设置」Tab 内构建设置编辑组件 + 保存按钮（把原设置对话框内嵌）
    void buildSettingsEditor();
    // 应用后按当前设置重建编辑组件，同步新主题/图标/效果的配色与已保存值
    void rebuildSettingsEditor();
    // 「通用设置」内嵌编辑器的保存按钮处理：校验 + 应用
    void doSaveSettings();
    // 应用内嵌/对话框编辑器携带的值：主题/图标/效果/快捷键/同步设置
    void applySettingsValues(const SettingsWidget &w);
    // 内嵌设置滑块触发的缩放：应用并回显吸附后的实际值
    void applySettingsZoom(SettingsWidget *ed, double v);
    // 全局热键唤醒：还原/置前窗口并激活
    void wakeUpAndShow();
    // 页面缩放：以 factor 倍率放大/缩小整体 UI（Ctrl+/-）
    // 设置绝对缩放比并应用（0.3 ~ 3.0），持久化并显示右下角百分比提示
    void setZoom(qreal zoom, bool underMouse);
    // 只为当前可见页面应用缩放样式（其余页延迟到切回时再应用，缓解 Ctrl± 整页重建卡顿）
    void scaleCurrentView();
    // 键盘 Ctrl+加/减：把新目标累计到待应用值并防抖，停顿后再真正重建（避免长按逐键重建）
    void queueZoomBy(qreal factor);
    // 把当前缩放比落到位：重生成全局 QSS + 放大基准字体，并通知页面重应用其缩放样式
    void applyUiScale();
    // Windows DWM 系统背景（Mica/Acrylic）：开启时窗口背景透明让 DWM 模糊透出，失败静默回退
    void applyDwmBackdrop();
    // 右下角短暂显示当前缩放百分比
    void showZoomBadge();
    // 非模态 toast 提示：右上角短暂显示后自动消失，无需用户点击，ms 为显示时长
    void showToast(const QString &text, int ms = 4000);

    // 页面索引枚举
    enum {
        PAGE_INBOX = 0,
        PAGE_SETTINGS,    // 设置：收件箱设置 + 通用设置（子标签容器）
        PAGE_TODO,
        PAGE_FOCUS_TIMER,
        PAGE_FOCUS_STATS, // 专注统计：7 个统计视图的子标签容器
        PAGE_ACTIVITY,    // ActivityWatch：6 个视图的子标签容器
        PAGE_SYNC,        // 同步：局域网同步/详情/D1云/冷备 的子标签容器
        PAGE_COUNT
    };

    // ── 可淘汰页生命周期（懒建 + 离开即回收 + LRU 有界缓存）──
    // 常驻 Residet（不可淘汰）：收件箱 / 任务。
    //    同步的「后台代码」（心跳/设备轮询/去抖自动推送/配对检测）由独立的无界面
    //    SyncService（m_syncService，随主窗口常驻）承担，故同步页控件可整体淘汰；
    //    设置页亦无后台逻辑，同样可淘汰。
    // 可淘汰 Evitable（懒建 + 离开销毁）：活动容器、专注容器、同步容器、设置容器。
    //    切到容器页才构造子页面（懒建）；一旦切走，把子页面整个 widget 树 delete 回收
    //    （较 B 方案只清数据更进一步：控件本身也被释放），并在重建时还原其子标签现场。
    bool isResidentPage(int page) const;
    // 进入可淘汰页：懒建子页面 + 恢复子标签 + 记录最近使用；超出常驻上限时淘汰最久未用页
    void enterEvictable(int page);
    // 离开可淘汰页：保存子标签并销毁其全部子页面（常驻页/当前页不受影响）
    void leaveEvictable(int page);
    // 活动容器子页面构造/销毁（构造幂等：已建则直接返回；销毁幂等：未建则空操作）
    bool ensureActivityPages();
    void releaseActivityPages();
    // 专注容器子页面构造/销毁
    bool ensureFocusPages();
    void releaseFocusPages();
    // 同步容器（局域网同步/详情/D1云/冷备）子页面构造/销毁；后台引擎常驻不受影响
    bool ensureSyncPages();
    void releaseSyncPages();
    // 设置容器（收件箱设置/通用设置）子页面构造/销毁
    bool ensureSettingsPages();
    void releaseSettingsPages();
    // 活动/专注容器的子标签现场（销毁前存、重建后还原，跨销毁保留）
    QHash<int, int> m_savedSubtab;
    // 可淘汰容器常驻上限（LRU 有界缓存；进入新可淘汰页且超过上限时淘汰最久未用者）
    int m_evictableCap = 1;
    // 可淘汰容器的最近使用顺序（front = 最近使用）；恒不淘汰当前页与常驻页
    QList<int> m_residentEvictable;

    ApiClient *m_api = nullptr;
    MdnsDiscovery *m_mdns = nullptr;
    // 无界面常驻的局域网同步引擎：独立于同步页生命周期（即使页控件被销毁也照常工作）
    SyncService *m_syncService = nullptr;
    GlobalHotkey *m_hotkey = nullptr;
    TagStore *m_tagStore = nullptr;
    TodoSource *m_todoStore = nullptr;
    FocusSource *m_focusStore = nullptr;
    LocalStore *m_localStore = nullptr;
    InboxPage *m_inbox = nullptr;
    InboxSettingsPage *m_inboxSettings = nullptr;
    ActivityPage *m_activity = nullptr;
    SyncPage *m_sync = nullptr;
    D1SyncPage *m_d1Sync = nullptr;
    DayPage *m_day = nullptr;
    StatsPage *m_stats = nullptr;
    TodoPage *m_todo = nullptr;
    QueryPage *m_query = nullptr;
    SyncDetailsPage *m_syncDetails = nullptr;
    CloudBackupPage *m_cloudBackup = nullptr;
    QStackedWidget *m_stack = nullptr;
    // 左侧导航栏（缩放时按比例调整宽度）
    QWidget *m_nav = nullptr;
    // 左侧导航：展开/收起切换按钮；全部导航按钮与分组标题（用于窄栏/展开两种状态切换）
    QToolButton *m_navToggle = nullptr;
    QList<QPushButton *> m_navButtons;
    QList<QToolButton *> m_navSectionHeaders;
    bool m_navExpanded = false; // 默认收起（窄栏图标模式）
    // 上一次显示的页面索引（用于检测「离开局域网同步页」以停止广播）
    int m_prevPage = PAGE_INBOX;
    // 页面枚举 → QStackedWidget 栈索引映射。因 PAGE_FOCUS_TIMER 无独立页面，
    // 栈索引与页面枚举并不一致（栈索引 0..7 连续，枚举有跳号），切页须经此映射。
    QHash<int, int> m_pageToStack;
    // 当前显示页的枚举值（用于 F5 按当前页分发刷新）
    int m_currentPage = PAGE_INBOX;
    // 页面缩放：当前缩放比（1.0 = 100%）与右下角百分比提示
    qreal m_zoom = 1.0;
    QLabel *m_zoomBadge = nullptr;
    QLabel *m_toast = nullptr;
    // 键盘 Ctrl± 缩放防抖：长按/连按时先累计到待应用值，停顿后才真正 applyUiScale
    QTimer *m_zoomInputTimer = nullptr;
    qreal m_zoomPending = -1.0; // <0 表示当前无待应用的目标
    // 左侧导航按钮
    QPushButton *m_navInbox = nullptr;
    QPushButton *m_navSettings = nullptr;
    QPushButton *m_navTodo = nullptr;
    QPushButton *m_navTimer = nullptr;
    QPushButton *m_navFocusStats = nullptr;
    QPushButton *m_navActivity = nullptr;
    QPushButton *m_navSync = nullptr;
    // 专注统计页内部的子标签容器（7 个统计视图共用一个导航入口）
    QTabWidget *m_focusTabs = nullptr;
    // ActivityWatch 容器页内部的子标签容器（6 个视图共用一个导航入口）
    QTabWidget *m_awTabs = nullptr;
    // 同步容器页内部的子标签容器（局域网同步 / 详情 / D1云 / 冷备 共用一个导航入口）
    QTabWidget *m_syncTabs = nullptr;
    // 设置容器页内部的子标签容器（收件箱设置 / 通用设置共用一个导航入口）
    QTabWidget *m_settingsTabs = nullptr;
    // 「通用设置」Tab 内嵌的设置编辑组件（原设置对话框内容）
    SettingsWidget *m_settingsEditor = nullptr;
    // 内嵌设置编辑组件的宿主容器（releaseSettingsPages 时整树回收）
    QWidget *m_settingsEditorHost = nullptr;
    // 子标签样式（随主题/缩放重建），专注统计与 ActivityWatch 容器共用
    void styleSubTabs(QTabWidget *tabs);
    // 专注模块页面指针（Todo 内部持有，这里也存一份供快捷键/刷新用）
    FocusTimerPage *m_timerPage = nullptr;
    FocusOverviewPage *m_overviewPage = nullptr;
    FocusDetailPage *m_detailPage = nullptr;
    FocusWeekPage *m_weekPage = nullptr;
    FocusHeatmapPage *m_heatmapPage = nullptr;
    FocusBestPage *m_bestPage = nullptr;
    FocusCalendarPage *m_calendarPage = nullptr;
    FocusMemorialPage *m_memorialPage = nullptr;
    // 系统托盘
    QSystemTrayIcon *m_tray = nullptr;
    bool m_trayExiting = false;   // 托盘菜单「退出」置位：关窗不再拦截
    // 远端变更监视：轮询 /api/0/sync/revision。初始为 -1（首次只记录基线，不刷新）
    QTimer *m_remoteTimer = nullptr;
    qint64 m_remoteRevision = -1;
};

} // namespace awqtui
