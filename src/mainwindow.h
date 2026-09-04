// mainwindow.h —— 主窗口：左侧导航 + 页面堆栈
#pragma once

#include <QMainWindow>
#include <QStackedWidget>
#include <QStringList>

class QCloseEvent;
class QEvent;
class QKeyEvent;
class QLabel;
class QPushButton;
class QSystemTrayIcon;
class QToolButton;
class QWheelEvent;

namespace awqtui {

class ApiClient;
class MdnsDiscovery;
class GlobalHotkey;
class TagStore;
class TodoSource;
class FocusSource;
class LocalStore;
class ActivityPage;
class TimelinePage;
class DayPage;
class StatsPage;
class InboxPage;
class InboxSettingsPage;
class TrashPage;
class SyncPage;
class D1SyncPage;
class StopwatchPage;
class QueryPage;
class SyncDetailsPage;
class TodoPage;
class FocusTimerPage;
class FocusOverviewPage;
class FocusDetailPage;
class FocusWeekPage;
class FocusHeatmapPage;
class FocusBestPage;
class FocusCalendarPage;
class FocusMemorialPage;

class MainWindow : public QMainWindow
{
    Q_OBJECT
public:
    explicit MainWindow(const QString &serverUrl = QString(), QWidget *parent = nullptr);
    ~MainWindow() override;

    InboxPage *inboxPage() const { return m_inbox; }
    InboxSettingsPage *inboxSettingsPage() const { return m_inboxSettings; }
    TrashPage *trashPage() const { return m_trash; }
    SyncPage *syncPage() const { return m_sync; }
    D1SyncPage *d1SyncPage() const { return m_d1Sync; }
    StopwatchPage *stopwatchPage() const { return m_stopwatch; }
    QueryPage *queryPage() const { return m_query; }
    SyncDetailsPage *syncDetailsPage() const { return m_syncDetails; }
    ActivityPage *activityPage() const { return m_activity; }
    TimelinePage *timelinePage() const { return m_timeline; }
    DayPage *dayPage() const { return m_day; }
    StatsPage *statsPage() const { return m_stats; }
    TodoPage *todoPage() const { return m_todo; }

    // 专注模块页面（Todo 内部用）
    FocusTimerPage *focusTimerPage() const { return m_timerPage; }

    void switchPage(int index);

    // 当前页面缩放比（1.0 = 100%）
    qreal zoomScale() const { return m_zoom; }

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

private:
    void buildUi();
    // 左侧导航在「窄栏（仅图标）」与「展开（图标+文字）」之间切换，并持久化状态
    void setNavExpanded(bool expanded);
    // 系统托盘：emoji 图标（复用主题 emoji + accent，无外部资源文件），
    // 左键切换显示/隐藏，右键菜单（显示/隐藏、退出），关窗最小化到托盘
    void setupTray();
    void updateStatus();
    // 读取配置并注册全部全局热键；返回未能注册的快捷键描述列表（空 = 全部成功）
    QStringList applyShortcuts();
    // 打开设置对话框：编辑期间暂停热键，保存/取消后重新注册
    void openSettings();
    // 全局热键唤醒：还原/置前窗口并激活
    void wakeUpAndShow();
    // 页面缩放：以 factor 倍率放大/缩小整体 UI（Ctrl+滚轮 / +/-）
    void zoomBy(qreal factor, bool underMouse);
    // 设置绝对缩放比并应用（0.3 ~ 3.0），持久化并显示右下角百分比提示
    void setZoom(qreal zoom, bool underMouse);
    // 把当前缩放比落到位：重生成全局 QSS + 放大基准字体，并通知页面重应用其缩放样式
    void applyUiScale();
    // Windows DWM 系统背景（Mica/Acrylic）：开启时窗口背景透明让 DWM 模糊透出，失败静默回退
    void applyDwmBackdrop();
    // 右下角短暂显示当前缩放百分比
    void showZoomBadge();
    // 非模态 toast 提示：右上角短暂显示后自动消失，无需用户点击，ms 为显示时长
    void showToast(const QString &text, int ms = 4000);
    // 事件目标是否属于可缩放的主窗口内容区
    bool isInsideZoomable(QObject *obj) const;
    // 焦点控件是否为文本输入类（此时无修饰 +/- 应交给输入，不做缩放）
    bool isTextEditingWidget(const QWidget *w) const;

    // 页面索引枚举
    enum {
        PAGE_INBOX = 0,
        PAGE_INBOX_SETTINGS,
        PAGE_TRASH,
        PAGE_TODO,
        PAGE_FOCUS_TIMER,
        PAGE_FOCUS_OVERVIEW,
        PAGE_FOCUS_DETAIL,
        PAGE_FOCUS_WEEK,
        PAGE_FOCUS_HEATMAP,
        PAGE_FOCUS_BEST,
        PAGE_FOCUS_CALENDAR,
        PAGE_FOCUS_MEMORIAL,
        PAGE_ACTIVITY,
        PAGE_TIMELINE,
        PAGE_DAY,
        PAGE_STATS,
        PAGE_SYNC,
        PAGE_D1_SYNC,
        PAGE_STOPWATCH,
        PAGE_QUERY,
        PAGE_SYNC_DETAILS,
        PAGE_COUNT
    };

    ApiClient *m_api = nullptr;
    MdnsDiscovery *m_mdns = nullptr;
    GlobalHotkey *m_hotkey = nullptr;
    TagStore *m_tagStore = nullptr;
    TodoSource *m_todoStore = nullptr;
    FocusSource *m_focusStore = nullptr;
    LocalStore *m_localStore = nullptr;
    InboxPage *m_inbox = nullptr;
    InboxSettingsPage *m_inboxSettings = nullptr;
    TrashPage *m_trash = nullptr;
    ActivityPage *m_activity = nullptr;
    TimelinePage *m_timeline = nullptr;
    SyncPage *m_sync = nullptr;
    D1SyncPage *m_d1Sync = nullptr;
    DayPage *m_day = nullptr;
    StatsPage *m_stats = nullptr;
    TodoPage *m_todo = nullptr;
    StopwatchPage *m_stopwatch = nullptr;
    QueryPage *m_query = nullptr;
    SyncDetailsPage *m_syncDetails = nullptr;
    QStackedWidget *m_stack = nullptr;
    // 左侧导航栏（缩放时按比例调整宽度）
    QWidget *m_nav = nullptr;
    // 左侧导航：展开/收起切换按钮；全部导航按钮与分组标题（用于窄栏/展开两种状态切换）
    QToolButton *m_navToggle = nullptr;
    QList<QPushButton *> m_navButtons;
    QList<QToolButton *> m_navSectionHeaders;
    bool m_navExpanded = false; // 默认收起（窄栏图标模式）
    // 页面缩放：当前缩放比（1.0 = 100%）与右下角百分比提示
    qreal m_zoom = 1.0;
    QLabel *m_zoomBadge = nullptr;
    QLabel *m_toast = nullptr;
    // 左侧导航按钮
    QPushButton *m_navInbox = nullptr;
    QPushButton *m_navInboxSettings = nullptr;
    QPushButton *m_navTrash = nullptr;
    QPushButton *m_navTodo = nullptr;
    QPushButton *m_navTimer = nullptr;
    QPushButton *m_navOverview = nullptr;
    QPushButton *m_navDetail = nullptr;
    QPushButton *m_navWeek = nullptr;
    QPushButton *m_navHeatmap = nullptr;
    QPushButton *m_navBest = nullptr;
    QPushButton *m_navCalendar = nullptr;
    QPushButton *m_navMemorial = nullptr;
    QPushButton *m_navActivity = nullptr;
    QPushButton *m_navTimeline = nullptr;
    QPushButton *m_navDay = nullptr;
    QPushButton *m_navStats = nullptr;
    QPushButton *m_navSync = nullptr;
    QPushButton *m_navD1Sync = nullptr;
    QPushButton *m_navSyncDetails = nullptr;
    QPushButton *m_navStopwatch = nullptr;
    QPushButton *m_navQuery = nullptr;
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
};

} // namespace awqtui
