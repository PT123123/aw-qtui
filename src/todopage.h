// todopage.h —— Todo 页（参照 TickTick 三栏式：左侧列表导航 + 任务列表 + 详情面板）
//
// 布局：左侧 listNav 导航栏（智能清单 收集箱/今天/最近7天/全部 + 自定义清单 + 已完成），
// 中间任务列表（快速添加 + 已完成折叠区），右侧详情面板（标题/完成/清单/优先级/截止/重复/标签/备注/子任务）。
//
// 注意：专注模块（计时、热力图、日历等）现在在主侧边栏中直接导航，
// 由 MainWindow 的主堆栈统一管理。TodoPage 不再内嵌专注模块页面。
#pragma once

#include <QDialog>
#include <QHash>
#include <QList>
#include <QSet>
#include <QWidget>

#include "todomodels.h"

class QCheckBox;
class QComboBox;
class QContextMenuEvent;
class QDateEdit;
class QGraphicsDropShadowEffect;
class QHBoxLayout;
class QLabel;
class QLayout;
class QLineEdit;
class QListWidget;
class QMenu;
class QPainter;
class QPaintEvent;
class QPlainTextEdit;
class QPushButton;
class QTimer;
class QToolButton;
class QVBoxLayout;

// Qt Designer 布局（todopage.ui），全局命名空间
namespace Ui { class TodoPage; }

namespace awqtui {

class ApiClient;
class FocusSource;
class TodoSource;
class TodoBoardView;
class TodoFadeButton;

// 导航项控件（图标/圆点 + 名称 + 右对齐计数），点击整行选中，仿 TickTick 左侧列表栏
class TodoNavItem : public QWidget
{
    Q_OBJECT
public:
    explicit TodoNavItem(QWidget *parent = nullptr);
    void setText(const QString &name);
    void setGlyph(const QString &glyph);   // Segoe 图标字形
    void setColorDot(const QColor &c);     // 清单彩色圆点（与字形互斥，调用后覆盖字形）
    void setCount(int n);
    void setSelected(bool on);
    bool isSelected() const { return m_selected; }

signals:
    void clicked();

protected:
    void mousePressEvent(QMouseEvent *event) override;
    void enterEvent(QEnterEvent *event) override;
    void leaveEvent(QEvent *event) override;
    void showEvent(QShowEvent *event) override;

private:
    void applyStyle();
    void renderIcon();
    void setHovered(bool on);
    // 鼠标移到项内图标/名称/计数上时本项不应退出 hover；清单项每次数据变化都被重建，
    // 新实例收不到 Enter —— 两种情况都靠「光标是否仍在自身矩形内」重新判定。
    void refreshHoverFromCursor();

    QLabel *m_icon = nullptr;
    QLabel *m_name = nullptr;
    QLabel *m_count = nullptr;
    QString m_glyph;
    bool m_selected = false;
    bool m_hovered = false;
    bool m_dot = false;
    QColor m_dotColor;
};

// 任务行控件（圆形勾选框 + 可换行标题 + 标题下标签胶囊 + 右侧优先级/期限/清单圆点），点击整行选中
class TodoTaskRow : public QWidget
{
    Q_OBJECT
public:
    // listName 非空时在元信息行显示清单名胶囊（多清单视图用，颜色取自清单色）
    explicit TodoTaskRow(const TodoTask &task, const QString &dotColor,
                         const QString &listName = QString(), QWidget *parent = nullptr);
    // 虚拟化池回收时，重新绑定到另一任务（保留完整 widget tree，只替换内容）
    void setTask(const TodoTask &task, const QString &dotColor, const QString &listName);
    qint64 taskId() const { return m_taskId; }
    void setHighlighted(bool on);
    // 多选模式：行首显示选择框、隐藏完成框，点击整行切换选中（不再打开详情）
    void setMultiSelectMode(bool on);
    void setSelected(bool on);
    bool isSelected() const { return m_selected; }

    // 长标题换行：报告行高随视口宽度变化（配合 ItemWidgetRelayoutFilter）
    bool hasHeightForWidth() const override { return true; }
    int heightForWidth(int w) const override;

signals:
    void selected(qint64 taskId);
    void toggleRequested(qint64 taskId, bool completed);
    void selectionToggled(qint64 taskId, bool selected);
    void menuRequested(qint64 taskId, const QPoint &globalPos);

protected:
    void mousePressEvent(QMouseEvent *event) override;
    // 右键弹任务菜单（与「⋯」同一个菜单）。以前任何按键都会 emit selected，
    // 于是右键也会把详情栏切过去、整行高亮闪一下。
    void contextMenuEvent(QContextMenuEvent *event) override;
    void enterEvent(QEnterEvent *event) override;
    void leaveEvent(QEvent *event) override;
    bool eventFilter(QObject *watched, QEvent *event) override;
    // 完成划线的自绘层（QSS 的 text-decoration 没有动画，只能自己画）
    void paintEvent(QPaintEvent *event) override;

private:
    void applyRowStyle();
    void setHovered(bool on);
    // 勾选框 / ⋯ 按钮会截走 hover：鼠标移到它们上面时行收到 Leave，整行高亮闪掉。
    // 所以 hover 不用 QSS 的 :hover 伪态，统一按「光标是否仍在行矩形内」判定。
    void refreshHoverFromCursor();
    // 点「完成」时先播划线动画，动画跑完再把完成状态写回数据层
    void playCompleteAnimation();
    void paintStrike(QPainter &p);
    // 池复用时按指针重建「标签胶囊行」/「右侧信息簇」。
    // ⚠ 不能用 parentWidget()->layout()->itemAt(n) 按下标取容器：行外层布局的顺序是
    //   [m_selChk, m_chk, mid, right, m_more]，而下标写法假设 mid 在 1、right 在 2，
    //   一错就把 mid（装着 m_title 的 QVBoxLayout）当成右侧簇清空 → delete 掉 m_title，
    //   行还活着、m_title 已悬空 → 下次 heightForWidth/绘制取字体即崩（启动建列表必现）。
    void rebuildPills(const TodoTask &task, const QString &dotColor, const QString &listName);
    void rebuildRightCluster(const TodoTask &task, const QString &dotColor);

    qint64 m_taskId;
    QCheckBox *m_chk = nullptr;      // 完成勾选框（非多选模式下可见）
    QCheckBox *m_selChk = nullptr;   // 多选选择框（多选模式下可见）
    QLabel *m_title = nullptr;       // 标题（可换行，heightForWidth 需要）
    TodoFadeButton *m_more = nullptr; // 悬停浮现的 ⋯（几何常驻，避免标题宽度跳动）
    QHBoxLayout *m_lay = nullptr;     // 行外层横向布局（插回右侧簇时用 m_more 定位）
    QVBoxLayout *m_midLay = nullptr;  // 中间列：标题 + 胶囊行（m_title 恒为第 0 项）
    QHBoxLayout *m_pillLay = nullptr; // 胶囊行（无标签时整个摘掉，不留空布局占 spacing）
    QHBoxLayout *m_rightLay = nullptr; // 右侧信息簇（无内容时整个摘掉）
    int m_rightW = 0;                // 右侧信息簇宽度（标题可用宽度 = 行宽 - 固定占位）
    bool m_hasMeta = false;          // 标题下方是否有标签/清单胶囊行
    bool m_highlighted = false;
    bool m_selected = false;
    bool m_multi = false;
    bool m_rowStyled = false;   // 行底/hover 样式是否已应用（保证首次即应用）
    bool m_hovered = false;     // 光标是否在行内（含压在子控件之上）
    qreal m_strike = 0.0;       // 完成划线进度 0..1（paintEvent 用）
    bool m_completing = false;  // 完成动画进行中：屏蔽重复点击 / 重复提交
};

// ------------------------------------------------------------------ //
// 任务详细信息对话框：与笔记详情同口径的元信息表（来源设备 / 同步状态 / 版本 / 时间线）
// + 备注原文。服务端 todos 表带 device_id / version / synced_at，故「从哪端传来」可考。
class TaskDetailsDialog : public QDialog
{
    Q_OBJECT
public:
    explicit TaskDetailsDialog(const TodoTask &task, const QString &listName, bool showRecurrence,
                               QWidget *parent = nullptr);
    // 回填「来源设备」：device_id → 已配对设备的别名/名称 + 端类型（异步解析后调用）
    void setDeviceName(const QString &name);

private:
    QLabel *m_deviceValue = nullptr;
    QString m_deviceId;
};

class TodoPage : public QWidget
{
    Q_OBJECT
public:
    // 任务排序模式（对齐 Android menu_todo 排序子菜单，持久化到 awqtui.ini todo/sortMode）
    enum class SortMode { Default = 0, NewestFirst = 1, Reverse = 2, ByPriority = 3, ByDue = 4 };

    explicit TodoPage(TodoSource *source, QWidget *parent = nullptr);
    ~TodoPage() override;
    // 向服务端重拉一轮（任务/清单全量）。切页、F5、远端修订号轮询都走这里；
    // 界面重绘由 store 的 dataChanged 驱动，内容未变时 renderSignature 会跳过重建。
    void refresh();
    void applyUiScale();
    // 统一几何：页面栅格 / 卡片内边距 / 三栏内边距 / 详情栏固定宽度（buildUi 与 applyUiScale 共用）
    void applyMetrics();
    // 退出前冲刷：把 debounce（250ms）中的标题/备注编辑立即落库。
    // 单实例让位 / 正常退出都必须在调 qApp->quit() 之前调用，否则会吃掉用户最后一次输入。
    void flushPendingEdits();

    // 详细信息里的「来源设备」要把 device_id 解析成设备名，需要走 /devices；
    // 本地数据源不注入（此时详情只显示原始 id）。
    void setApiClient(ApiClient *api) { m_api = api; }

    // 排序谓词（列表视图与平铺看板共用，保证两种视图同序）
    static bool taskLessThan(const TodoTask &a, const TodoTask &b, SortMode mode);

private slots:
    void onDataChanged();
    void onQuickAdd();
    void onNewList();
    void onRenameList(qint64 listId);
    void onDeleteList(qint64 listId);
    void onToggleRequested(qint64 id, bool completed);
    // 完成任务后的「撤销」气泡（3s 内可回退）
    void showUndoToast(qint64 id, const QString &title);
    void onTaskDelete();
    void onSubtaskAdd();
    // 多选 / 批量操作
    void onMultiToggled(bool on);
    void onSelectionToggled(qint64 id, bool selected);
    void onBulkSelectAll();
    void onBulkSetCompleted(bool completed);
    void onBulkDelete();
    void onBulkMove(qint64 listId);
    void onBulkPriority(int priority);
    void onBulkDue(const QString &dueDate);

private:
    enum ViewKind { ViewInbox, ViewToday, ViewNext7, ViewAll, ViewList, ViewDone };

    // Qt Designer 生成的布局对象（todopage.ui -> ui_todopage.h）
    Ui::TodoPage *ui = nullptr;
    void buildUi();
    void applyPageStyles();
    void buildBulkBar();
    // 左侧导航栏
    void buildNav();
    void rebuildNavLists();   // 自定义清单列表（跟随 lists 变化重建）
    void updateNavCounts();   // 各导航项任务计数
    void setNavChecked();     // 同步导航选中态
    void rebuildList();
    // 列表渲染内容签名：与上次相同说明界面不会变，可直接跳过重建
    QString renderSignature() const;
    void selectView(ViewKind kind, qint64 listId = 0);
    void reloadListCombo();
    void reloadSubtaskList();
    void loadDetail(qint64 id);
    void clearDetail();
    void commitDetail();
    void setRowHighlight(qint64 id);
    QString viewTitle() const;

    // ── 平铺（看板）视图 ──
    void buildViewToggle();            // 列表头部「列表 / 平铺」分段开关
    void buildBoardView();             // 创建板并接信号
    void setBoardMode(bool on);        // 切换视图（含两侧控件显隐 + 持久化）
    void rebuildBoard();               // 用当前数据刷新板
    void updateDetailVisibility();     // 详情栏可见性（平铺下默认收起、点卡片临时展开）
    void onBoardTaskMenu(qint64 id, const QPoint &globalPos);
    void onBoardListMenu(qint64 listId, const QPoint &globalPos);
    void onTaskRowMenu(qint64 id, const QPoint &globalPos);   // 列表行 ⋯ 菜单（与看板共用）
    void showTaskDetails(qint64 id);   // 详细信息对话框（元信息 + 来源设备解析）
    // 多选辅助
    void setMultiSelect(bool on);
    QList<qint64> selectedTaskIds() const;
    QList<qint64> rowTaskIds() const;        // 当前列表里实际渲染出的任务 id
    void syncRowSelection();                 // 仅刷新行选中态（不重建列表）
    void updateBulkBar();                    // 刷新批量条文案与可用状态
    void pruneSelection();                   // 丢弃已被删除任务的选择残留

    QList<TodoTask> visibleTasks() const;
    // 搜索框当前词（已 trim + 转小写）；空 = 未启用搜索
    QString searchNeedle() const;
    // 标题 / 备注 / 标签 / 所属清单名 任一命中即保留（子串匹配、不区分大小写）
    bool matchesSearch(const TodoTask &t, const QString &needle) const;

    QWidget *makeSubtaskRow(const TodoSubtask &s);
    TodoNavItem *makeNavItem(const QString &name);

    TodoSource *m_source;
    ApiClient *m_api = nullptr;   // 仅用于详细信息里的设备名解析（可为空）
    QList<TodoList> m_lists;
    QList<TodoTask> m_tasks;
    QHash<qint64, QString> m_listColors;
    ViewKind m_view = ViewInbox;
    qint64 m_viewList = 0;
    SortMode m_sort = SortMode::Default;
    qint64 m_selectedTask = 0;
    bool m_showCompleted = false;
    int m_detailW = 0;   // 详情面板展开宽度（随缩放变化）

    // 左侧列表导航
    QWidget *m_listNav = nullptr;
    TodoNavItem *m_navInbox = nullptr;   // 收集箱
    TodoNavItem *m_navToday = nullptr;   // 今天
    TodoNavItem *m_navNext7 = nullptr;   // 最近 7 天
    TodoNavItem *m_navAll = nullptr;     // 全部
    TodoNavItem *m_navDone = nullptr;    // 已完成
    QLabel *m_navListLabel = nullptr;    // 「清单」分组小标题
    QWidget *m_navListsBox = nullptr;    // 自定义清单项容器
    QVBoxLayout *m_navListsLay = nullptr;
    QToolButton *m_newListBtn = nullptr; // ＋ 新建清单
    QList<TodoNavItem *> m_navListItems; // 当前清单项（供计数更新 / 清理）

    // 中间任务列表的浮动表面
    QWidget *m_surface = nullptr;
    QGraphicsDropShadowEffect *m_surfaceShadow = nullptr;

    // 列表区
    QLabel *m_viewTitle;
    QLabel *m_viewCount;
    QComboBox *m_sortBox = nullptr;   // 排序模式（默认/最近添加/倒序/按优先级/按截止日期）
    QLineEdit *m_search = nullptr;    // 当前视图内的任务搜索（纯内存过滤）
    QLineEdit *m_quickAdd;
    QListWidget *m_list;
    QPushButton *m_completedBtn;
    QLabel *m_progress;

    // 平铺（看板）视图：列表列位置用显隐切换（列表控件与板互斥显示）
    TodoBoardView *m_board = nullptr;
    QWidget *m_viewSeg = nullptr;        // 「列表 / 平铺」分段开关
    QToolButton *m_segList = nullptr;
    QToolButton *m_segBoard = nullptr;
    bool m_boardMode = false;
    bool m_detailWanted = true;          // 平铺视图下详情栏是否临时展开
    qint64 m_settleTask = 0;             // 刚落位的任务：重建看板时播 accent 环淡出
    bool m_dragRefreshPending = false;   // 拖拽期间被跳过的刷新，正在轮询补做
    int m_dragRefreshTries = 0;

    // 多选：列表头部入口按钮 + 列表下方批量操作条
    QToolButton *m_multiBtn = nullptr;
    QWidget *m_bulkBar = nullptr;
    QLabel *m_bulkCount = nullptr;
    QToolButton *m_bulkSelectAll = nullptr;
    QToolButton *m_bulkDone = nullptr;
    QToolButton *m_bulkUndone = nullptr;
    QToolButton *m_bulkMoveBtn = nullptr;
    QToolButton *m_bulkPriorityBtn = nullptr;
    QToolButton *m_bulkDueBtn = nullptr;
    QToolButton *m_bulkDeleteBtn = nullptr;
    QMenu *m_bulkMoveMenu = nullptr;
    QMenu *m_bulkPriorityMenu = nullptr;
    QMenu *m_bulkDueMenu = nullptr;
    bool m_multi = false;              // 是否处于多选模式
    QSet<qint64> m_selectedIds;        // 多选模式下已勾选的任务 id

    // 详情面板
    QWidget *m_detailPanel = nullptr;
    QLabel *m_detailEmpty;
    QWidget *m_detailBody;
    QPlainTextEdit *m_dTitle;
    QCheckBox *m_dDone;
    QComboBox *m_dList;
    QComboBox *m_dPriority;
    QCheckBox *m_dHasDue;
    QDateEdit *m_dDue;
    QComboBox *m_dRecur;
    QLineEdit *m_dTags;
    QPlainTextEdit *m_dNotes;
    QListWidget *m_dSubs;
    QLineEdit *m_dSubAdd;
    QPushButton *m_dDelete;
    QTimer *m_commitTimer;
    bool m_loadingDetail = false;
    bool m_animateNext = true;
    // 上次实际渲染的内容签名：同步轮询触发的 dataChanged 若内容没变，
    // 直接跳过整表重建，避免列表闪动与滚动位置跳回顶部
    QString m_renderSig;

    // ── widget 池（O(1) 滚动复用）─────────────────────────────────────
    class TodoPool
    {
    public:
        explicit TodoPool(TodoPage *page, int maxSize = 3) : m_page(page), m_maxSize(maxSize) {}
        // 从池中取一行（空或从池尾弹），绑定任务数据
        TodoTaskRow *acquire(const TodoTask &task, const QString &dotColor,
                             const QString &listName, bool multi, bool selected);
        // 归还一行到池中（满了则删除）
        void release(TodoTaskRow *row);
        // 把所有当前在使用的行归还池中（不舍弃，用于整屏刷新前复用）
        void releaseAll(const QMap<int, TodoTaskRow *> &active);
        // 清空池中所有行（不舍弃已池化行）
        void discardAll();
        // 清空池中所有行并删除（彻底销毁）
        void clear();
        int count() const { return m_pool.size(); }

    private:
        TodoPage *m_page;
        const int m_maxSize;
        QVector<TodoTaskRow *> m_pool; // 后进先出（最近用过的放后面，优先回收旧的）
    };
    TodoPool *m_cardPool = nullptr;
};

} // namespace awqtui
