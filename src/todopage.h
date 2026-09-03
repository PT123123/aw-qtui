// todopage.h —— Todo 页（参照 TickTick / Super Productivity）
//
// 布局：左侧「收集箱/今天/最近 7 天/全部 + 清单」导航，中间任务列表（快速添加 +
// 已完成折叠区），右侧详情面板（标题/完成/清单/优先级/截止/重复/标签/备注/子任务）。
//
// 注意：专注模块（计时、热力图、日历等）现在在主侧边栏中直接导航，
// 由 MainWindow 的主堆栈统一管理。TodoPage 不再内嵌专注模块页面。
#pragma once

#include <QHash>
#include <QList>
#include <QWidget>

#include "todomodels.h"

class QCheckBox;
class QComboBox;
class QDateEdit;
class QGraphicsDropShadowEffect;
class QLabel;
class QLineEdit;
class QListWidget;
class QPlainTextEdit;
class QPushButton;
class QTimer;
class QToolButton;
class QVBoxLayout;

namespace awqtui {

class FocusSource;
class TodoSource;

// 任务行控件（复选框 + 标题 + 优先级/期限/标签元信息），点击整行选中
class TodoTaskRow : public QWidget
{
    Q_OBJECT
public:
    explicit TodoTaskRow(const TodoTask &task, const QString &dotColor, QWidget *parent = nullptr);
    qint64 taskId() const { return m_taskId; }
    void setHighlighted(bool on);

signals:
    void selected(qint64 taskId);
    void toggleRequested(qint64 taskId, bool completed);

protected:
    void mousePressEvent(QMouseEvent *event) override;

private:
    qint64 m_taskId;
    bool m_highlighted = false;
};

class TodoPage : public QWidget
{
    Q_OBJECT
public:
    explicit TodoPage(TodoSource *source, QWidget *parent = nullptr);
    void refresh();
    void applyUiScale();

private slots:
    void onDataChanged();
    void onQuickAdd();
    void onNewList();
    void onRenameList(qint64 listId);
    void onDeleteList(qint64 listId);
    void onToggleRequested(qint64 id, bool completed);
    void onTaskDelete();
    void onSubtaskAdd();

private:
    enum ViewKind { ViewInbox, ViewToday, ViewNext7, ViewAll, ViewList };

    void buildUi();
    void applyPageStyles();
    void rebuildSidebar();
    void rebuildList();
    void selectView(ViewKind kind, qint64 listId = 0);
    void reloadListCombo();
    void reloadSubtaskList();
    void loadDetail(qint64 id);
    void clearDetail();
    void commitDetail();
    void setRowHighlight(qint64 id);
    void setViewButtonsChecked();
    QString viewTitle() const;

    QList<TodoTask> visibleTasks() const;
    static bool taskLessThan(const TodoTask &a, const TodoTask &b);
    QWidget *makeRow(const TodoTask &task);
    QWidget *makeSubtaskRow(const TodoSubtask &s);

    TodoSource *m_source;
    QList<TodoList> m_lists;
    QList<TodoTask> m_tasks;
    QHash<qint64, QString> m_listColors;
    ViewKind m_view = ViewInbox;
    qint64 m_viewList = 0;
    qint64 m_selectedTask = 0;
    bool m_showCompleted = false;

    // 侧栏
    QWidget *m_sidebar;
    QWidget *m_listsBox;
    QVBoxLayout *m_listsLay;
    QList<QToolButton *> m_viewBtns;
    QList<QToolButton *> m_listBtns;
    QPushButton *m_newListBtn;

    // 中间任务列表的浮动表面
    QWidget *m_surface = nullptr;
    QGraphicsDropShadowEffect *m_surfaceShadow = nullptr;

    // 列表区
    QLabel *m_viewTitle;
    QLabel *m_viewCount;
    QLineEdit *m_quickAdd;
    QListWidget *m_list;
    QPushButton *m_completedBtn;
    QLabel *m_progress;

    // 详情面板
    QWidget *m_detailPanel;
    QLabel *m_detailEmpty;
    QWidget *m_detailBody;
    QLineEdit *m_dTitle;
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
};

} // namespace awqtui
