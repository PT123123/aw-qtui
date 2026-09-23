// todoboard.h —— 任务页「平铺（看板）」视图
//
// 平铺 = 把左侧清单横排成列，每列显示该清单的未完成任务；
// 卡片可以用鼠标直接拖到别的列（跨清单移动）。拖拽中卡片以「2px accent 环 + 柔和投影」
// 的浮层跟随光标，目标列整列点亮，并在插入位留出 accent 占位框让同列卡片让位。
//
// 分层：
//   TodoFadeButton  悬停浮现的图标按钮（几何常驻、靠透明度显隐，避免布局跳动）
//   TodoBoardCard   一张卡片（勾选框 + 标题 + 元信息 + 悬停浮现的 ⋯）
//   TodoBoardColumn 一列 = 一个清单（列头 + 卡片区 + 底部「添加任务」）
//   TodoBoardView   板（横向滚动容器，持有所有列）
//
// 拖拽协议：QDrag + mime "application/x-awqtui-taskids"，载荷为文本
//   from=<listId>;h=<卡片高>;ids=<id>[,<id>…]
// from 是发起拖拽时所在的清单，用于判定「拖回原列不放行」——本视图不支持列内手动排序，
// 列内顺序统一由排序模式决定，与列表视图保持一致。
#pragma once

#include <QColor>
#include <QGraphicsOpacityEffect>
#include <QHash>
#include <QList>
#include <QPointer>
#include <QPropertyAnimation>
#include <QPixmap>
#include <QScrollArea>
#include <QToolButton>
#include <QWidget>

#include <functional>

#include "todomodels.h"

class QCheckBox;
class QDragEnterEvent;
class QDragLeaveEvent;
class QDragMoveEvent;
class QDropEvent;
class QHBoxLayout;
class QLabel;
class QLineEdit;

class QVBoxLayout;

namespace awqtui {

// ── 拖拽协议 ─────────────────────────────────────────────
QString todoBoardMimeType();
QString encodeBoardDrag(qint64 fromListId, int cardHeight, const QList<qint64> &ids);
bool decodeBoardDrag(const QString &payload, qint64 *fromListId, int *cardHeight,
                     QList<qint64> *ids);

// 拖拽进行中标志：拖拽期间禁止重建看板（列被 delete 会让 QDrag 的落点悬垂）
inline bool &boardDragActive()
{
    static bool active = false;
    return active;
}

// 抓取渲染：把卡片画成「卡片本体 + 2px accent 环 + 柔和投影」的拖拽浮层。
// padOut 返回四边留白，供 QDrag::setHotSpot 折算抓取点。
QPixmap renderBoardDragPixmap(QWidget *card, int *padOut);

// ── 悬停浮现的图标按钮 ───────────────────────────────────
// 强制占位（sizeHint 不变），只用透明度显隐：否则鼠标一移上去列头/行尾控件位移会"跳"。
class TodoFadeButton : public QToolButton
{
    Q_OBJECT
public:
    explicit TodoFadeButton(QWidget *parent = nullptr);
    void fadeTo(qreal to, int ms = 140);
    void setEngaged(bool on);   // 立即切换显隐（不播动画）

private:
    QGraphicsOpacityEffect *m_fx = nullptr;
    QPointer<QPropertyAnimation> m_anim;
};

// ── 落点占位框（accent 虚线，高度用动画展开/收起，让同列其他卡片自然让位） ──
class TodoBoardGap : public QWidget
{
    Q_OBJECT
    Q_PROPERTY(int gapHeight READ gapHeight WRITE setGapHeight)
public:
    explicit TodoBoardGap(QWidget *parent = nullptr);
    int gapHeight() const { return m_gapHeight; }
    void setGapHeight(int h);

protected:
    void paintEvent(QPaintEvent *event) override;

private:
    int m_gapHeight = 0;
};

// ── 看板卡片 ─────────────────────────────────────────────
class TodoBoardCard : public QWidget
{
    Q_OBJECT
    Q_PROPERTY(qreal hoverT READ hoverT WRITE setHoverT)
    Q_PROPERTY(qreal ringT READ ringT WRITE setRingT)
public:
    explicit TodoBoardCard(const TodoTask &task, QWidget *parent = nullptr);
    qint64 taskId() const { return m_id; }
    void setListId(qint64 id) { m_listId = id; }   // 拖拽载荷需要知道来源清单

    qreal hoverT() const { return m_hoverT; }
    void setHoverT(qreal v);
    qreal ringT() const { return m_ringT; }
    void setRingT(qreal v);

    void setDimmed(bool on);   // 拖拽中：留在原位的卡片变暗
    void playSettle();         // 刚落位：accent 环淡出（对应视频里落位后仍带一圈蓝）

signals:
    void activated(qint64 id);
    void toggleRequested(qint64 id, bool completed);
    void menuRequested(qint64 id, const QPoint &globalPos);

protected:
    void paintEvent(QPaintEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    void enterEvent(QEnterEvent *event) override;
    void leaveEvent(QEvent *event) override;

private:
    void animTo(const char *prop, qreal to, int ms);

    qint64 m_id = 0;
    qint64 m_listId = 0;
    QCheckBox *m_chk = nullptr;
    QLabel *m_title = nullptr;
    QWidget *m_meta = nullptr;
    TodoFadeButton *m_more = nullptr;
    QPoint m_press;
    bool m_pressed = false;
    bool m_dimmed = false;
    qreal m_hoverT = 0.0;
    qreal m_ringT = 0.0;
    QPointer<QPropertyAnimation> m_hoverAnim;
    QPointer<QPropertyAnimation> m_ringAnim;
};

// ── 看板列（= 一个清单） ─────────────────────────────────
class TodoBoardColumn : public QWidget
{
    Q_OBJECT
public:
    explicit TodoBoardColumn(qint64 listId, QWidget *parent = nullptr);

    qint64 listId() const { return m_listId; }
    // 重建列内容（列头文案/颜色 + 卡片 + 计数）。settleTaskId 命中的卡片播落位动画。
    void setContent(const QString &name, const QColor &color, bool inbox,
                    const QList<TodoTask> &tasks, qint64 settleTaskId);

signals:
    void taskActivated(qint64 id);
    void taskToggleRequested(qint64 id, bool completed);
    void taskMenuRequested(qint64 id, const QPoint &globalPos);
    void taskDropped(qint64 id, qint64 listId);
    void quickAdd(qint64 listId, const QString &title);
    void listMenuRequested(qint64 listId, const QPoint &globalPos);

protected:
    void dragEnterEvent(QDragEnterEvent *event) override;
    void dragMoveEvent(QDragMoveEvent *event) override;
    void dragLeaveEvent(QDragLeaveEvent *event) override;
    void dropEvent(QDropEvent *event) override;
    void enterEvent(QEnterEvent *event) override;
    void leaveEvent(QEvent *event) override;
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    void applyStyle();
    void setDropActive(bool on);
    void setColHover(bool on);
    QList<TodoBoardCard *> cardList() const;
    int dropIndexAt(int cardsY) const;   // m_cards 局部 y → 插入位
    void moveGapTo(int index, int cardH);
    void hideGap(bool animated);
    void startQuickAdd();
    void endQuickAdd(bool commit);

    qint64 m_listId = 0;
    QString m_color;

    QLabel *m_dot = nullptr;
    QLabel *m_name = nullptr;
    QLabel *m_count = nullptr;
    TodoFadeButton *m_addTop = nullptr;
    TodoFadeButton *m_menu = nullptr;

    QScrollArea *m_scroll = nullptr;
    QWidget *m_cards = nullptr;
    QVBoxLayout *m_cardsLay = nullptr;
    TodoBoardGap *m_gap = nullptr;
    QPointer<QPropertyAnimation> m_gapAnim;
    int m_gapIndex = -1;

    QToolButton *m_addTask = nullptr;
    QLineEdit *m_addEdit = nullptr;

    bool m_hovered = false;
    bool m_dropActive = false;
};

// ── 板 ───────────────────────────────────────────────────
class TodoBoardView : public QScrollArea
{
    Q_OBJECT
public:
    explicit TodoBoardView(QWidget *parent = nullptr);

    // listColors: listId → hex（收集箱不在此表内，用中性灰点）
    // less: 列内排序（复用 TodoPage::taskLessThan，保证与列表视图同序）
    void setData(const QList<TodoList> &lists, const QList<TodoTask> &tasks,
                 const QHash<qint64, QString> &listColors, qint64 settleTaskId,
                 const std::function<bool(const TodoTask &, const TodoTask &)> &less);

signals:
    void taskActivated(qint64 id);
    void taskToggleRequested(qint64 id, bool completed);
    void taskMenuRequested(qint64 id, const QPoint &globalPos);
    void listMenuRequested(qint64 listId, const QPoint &globalPos);
    void taskDropped(qint64 id, qint64 listId);
    void quickAddRequested(qint64 listId, const QString &title);

private:
    void rebuildColumns(const QList<TodoList> &lists, const QHash<qint64, QString> &listColors);
    // 视图自身的缩放相关部件：横向滚动条样式（sp()）与列间距（si()）都在构造时定死，
    // 缩放变化必须重算（rebuildColumns 在比例变化时会调用它）
    void applyMetrics();

    QWidget *m_canvas = nullptr;
    QHBoxLayout *m_lay = nullptr;
    QList<TodoBoardColumn *> m_cols;
    qreal m_builtScale = -1.0;   // m_cols 是按哪个 gUiScale 建的（-1 = 还没有列）
};

} // namespace awqtui
