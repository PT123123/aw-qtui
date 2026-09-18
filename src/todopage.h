// todopage.h —— Todo 页（参照 TickTick 三栏式：左侧列表导航 + 任务列表 + 详情面板）
//
// 布局：左侧 listNav 导航栏（智能清单 收集箱/今天/最近7天/全部 + 自定义清单 + 已完成），
// 中间任务列表（快速添加 + 已完成折叠区），右侧详情面板（标题/完成/清单/优先级/截止/重复/标签/备注/子任务）。
//
// 注意：专注模块（计时、热力图、日历等）现在在主侧边栏中直接导航，
// 由 MainWindow 的主堆栈统一管理。TodoPage 不再内嵌专注模块页面。
#pragma once

#include <QHash>
#include <QList>
#include <QSet>
#include <QWidget>

#include "todomodels.h"

class QCheckBox;
class QComboBox;
class QDateEdit;
class QGraphicsDropShadowEffect;
class QLabel;
class QLineEdit;
class QListWidget;
class QMenu;
class QPlainTextEdit;
class QPushButton;
class QTimer;
class QToolButton;
class QVBoxLayout;

// Qt Designer 布局（todopage.ui），全局命名空间
namespace Ui { class TodoPage; }

namespace awqtui {

class FocusSource;
class TodoSource;

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

private:
    void applyStyle();
    void renderIcon();

    QLabel *m_icon = nullptr;
    QLabel *m_name = nullptr;
    QLabel *m_count = nullptr;
    QString m_glyph;
    bool m_selected = false;
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

protected:
    void mousePressEvent(QMouseEvent *event) override;

private:
    void applyRowStyle();

    qint64 m_taskId;
    QCheckBox *m_chk = nullptr;      // 完成勾选框（非多选模式下可见）
    QCheckBox *m_selChk = nullptr;   // 多选选择框（多选模式下可见）
    QLabel *m_title = nullptr;       // 标题（可换行，heightForWidth 需要）
    int m_rightW = 0;                // 右侧信息簇宽度（标题可用宽度 = 行宽 - 固定占位）
    bool m_hasMeta = false;          // 标题下方是否有标签/清单胶囊行
    bool m_highlighted = false;
    bool m_selected = false;
    bool m_multi = false;
    bool m_rowStyled = false;   // 行底/hover 样式是否已应用（保证首次即应用 :hover）
};

class TodoPage : public QWidget
{
    Q_OBJECT
public:
    // 任务排序模式（对齐 Android menu_todo 排序子菜单，持久化到 awqtui.ini todo/sortMode）
    enum class SortMode { Default = 0, NewestFirst = 1, Reverse = 2, ByPriority = 3, ByDue = 4 };

    explicit TodoPage(TodoSource *source, QWidget *parent = nullptr);
    ~TodoPage() override;
    void refresh();
    void applyUiScale();
    // 统一几何：页面栅格 / 卡片内边距 / 三栏内边距 / 详情栏固定宽度（buildUi 与 applyUiScale 共用）
    void applyMetrics();
    // 退出前冲刷：把 debounce（250ms）中的标题/备注编辑立即落库。
    // 单实例让位 / 正常退出都必须在调 qApp->quit() 之前调用，否则会吃掉用户最后一次输入。
    void flushPendingEdits();

private slots:
    void onDataChanged();
    void onQuickAdd();
    void onNewList();
    void onRenameList(qint64 listId);
    void onDeleteList(qint64 listId);
    void onToggleRequested(qint64 id, bool completed);
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
    // 多选辅助
    void setMultiSelect(bool on);
    QList<qint64> selectedTaskIds() const;
    QList<qint64> rowTaskIds() const;        // 当前列表里实际渲染出的任务 id
    void syncRowSelection();                 // 仅刷新行选中态（不重建列表）
    void updateBulkBar();                    // 刷新批量条文案与可用状态
    void pruneSelection();                   // 丢弃已被删除任务的选择残留

    QList<TodoTask> visibleTasks() const;
    static bool taskLessThan(const TodoTask &a, const TodoTask &b, SortMode mode);
    QWidget *makeRow(const TodoTask &task);
    QWidget *makeSubtaskRow(const TodoSubtask &s);
    TodoNavItem *makeNavItem(const QString &name);

    TodoSource *m_source;
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
    QLineEdit *m_quickAdd;
    QListWidget *m_list;
    QPushButton *m_completedBtn;
    QLabel *m_progress;

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
};

} // namespace awqtui
