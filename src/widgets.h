// widgets.h —— 通用控件：TagChip / NoteCard / StatusBadge / NoteEditorDialog / CommentsDialog
#pragma once

#include <QAbstractItemDelegate>
#include <QGraphicsDropShadowEffect>
#include <QCache>
#include <QDialog>
#include <QFrame>
#include <QLabel>
#include <QListWidget>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QStandardItemModel>
#include <QWidget>
#include <QVector>

#include <functional>

#include "models.h"

class QLineEdit;
class QMouseEvent;
class QModelIndex;
class QScreen;
class QVBoxLayout;

namespace awqtui {

// ISO8601 -> 本地时间 "YYYY-MM-DD HH:MM"
QString formatLocal(const QString &iso, const QString &fmt = QStringLiteral("yyyy-MM-dd HH:mm"));
// ISO8601 -> 相对时间（对齐 MoeMemos：刚刚 / N 分钟前 / N 小时前 / N 天前 / 日期）
QString formatRelative(const QString &iso);
// 从纯文本提取 #tag
QStringList extractTags(const QString &text);

// 屏幕底部居中的短暂气泡提示（淡入 → 停留 → 淡出后自毁，约 1.8s）。
// 独立无边框置顶工具窗：不抢焦点、不进任务栏/Alt+Tab，主窗口隐藏时也可见；
// anchorScreen 为空时取光标所在屏。用于快捷输入提交后的「已发送」等反馈。
void showToast(const QString &text, QScreen *anchorScreen = nullptr);

// 带一个操作按钮的气泡（默认「撤销」，3s 后淡出自毁），用于「完成任务 → 后悔」这类可回退操作。
// 与 showToast 同为独立置顶工具窗，但额外做了两件事：
//   1. 鼠标停在气泡上时暂停倒计时（3s 内点中一个小按钮太紧）；
//   2. 同一时刻只保留一个（新气泡顶掉旧的）—— 撤销的时效窗口本来就极短。
// onAction 在按钮点击后调用，回调里请用 QPointer 保护宿主对象（气泡活得可能比它久）。
//
// onExpire（可选）在倒计时真正走完、开始淡出时调用，**鼠标悬停冻结期间不会调用**，点按钮走
// onAction 时也不会调用。适合「到期即提交」的延迟删除：撤销窗口被悬停延长时，提交也同步延后，
// 不会出现「气泡还在、点撤销却已经生效了」的错位。
void showActionToast(const QString &text, const QString &actionText,
                     std::function<void()> onAction, int ms = 3000,
                     QScreen *anchorScreen = nullptr,
                     std::function<void()> onExpire = nullptr);

// ------------------------------------------------------------------ //
// 连接状态徽标
class StatusBadge : public QWidget
{
    Q_OBJECT
public:
    enum class State { Connected, Syncing, Disconnected, Error, Unknown };
    explicit StatusBadge(QWidget *parent = nullptr);
    void setState(State s, const QString &text = QString());
    // 界面缩放：按当前全局缩放因子重应用字体样式
    void applyUiScale();

private:
    void applyStyle();
    QLabel *m_dot;
    QLabel *m_label;
    QString m_color;
};

// ------------------------------------------------------------------ //
// 虚拟化池：固定大小 NoteCard 回收站，O(visible) 滚动时复用而非重建
class NoteCard; // 前向声明（NoteCard 定义在 CardPool/CardDelegate 之后）
class CardPool
{
public:
    explicit CardPool(int maxSize = 3) : m_maxSize(maxSize) {}
    ~CardPool();
    // 从池中取一张卡（空或从池尾弹），绑定 note
    NoteCard *acquire(const Note &note, bool pinned);
    // 归还一张卡到池中（满了则删除）
    void release(NoteCard *card);
    // 把所有当前在使用的卡归还池中（不舍弃，用于整屏刷新前复用）
    void releaseAll(const QMap<int, NoteCard *> &active);
    // 清空池中所有卡（不舍弃已池化卡片）
    void discardAll();
    // 清空池中所有卡并删除（彻底销毁）
    void clear();
    int count() const { return m_pool.size(); }

private:
    const int m_maxSize;
    QVector<NoteCard *> m_pool; // 后进先出（最近用过的放后面，优先回收旧的）
};

// ------------------------------------------------------------------ //
// 虚拟化代理：把 QListView 的标准模型映射为池化的 NoteCard widget
// 真正只创建「可见行数 + 上下缓冲」个卡片，复用池中卡片绑定不同数据
class CardDelegate : public QAbstractItemDelegate
{
    Q_OBJECT
public:
    explicit CardDelegate(CardPool *pool, QObject *parent = nullptr);
    // 池尺寸 = 可见行数 + 上下缓冲
    void setBufferSize(int rows);

    // QAbstractItemDelegate 接口
    void paint(QPainter *, const QStyleOptionViewItem &, const QModelIndex &) const override;
    QSize sizeHint(const QStyleOptionViewItem &, const QModelIndex &) const override;

    // 绑定卡片的信号连接（由 InboxPage 调用一次，之后池内卡片复用时信号自动保持）
    void connectCard(NoteCard *card);
    // 返回当前活跃卡片（row -> NoteCard 映射），用于 applyClientFilter 结束后归还
    QMap<int, NoteCard *> activeCards() const { return m_activeCards; }

signals:
    void cardClicked(qint64 id, Qt::KeyboardModifiers mods);

private:
    CardPool *m_pool;
    int m_bufferRows = 3; // 上下各缓冲行数
    // 当前正在使用的卡片（仅 visible+buffer 个，row -> card）
    QMap<int, NoteCard *> m_activeCards;
};

// ------------------------------------------------------------------ //
// 笔记卡片（MoeMemos 风格）：头部（相对时间 + 置顶/同步图标 + ⋯ 菜单）+ Markdown 内容
class NoteCard : public QFrame
{
    Q_OBJECT
public:
    explicit NoteCard(const Note &note, bool pinned = false, QWidget *parent = nullptr);
    Note note() const { return m_note; }
    // 虚拟化池回收时，重新绑定到另一张笔记（保留完整 widget tree，只替换内容）
    void setNote(const Note &note, bool pinned);
    // 在内容下方注入「被评论/被引用笔记」的预览（灰色小字、100 字截断），点击可跳转
    void setParentReference(qint64 parentId, const QString &preview);
    // 跳转定位时的视觉反馈：边框高亮闪烁后恢复
    void flashHighlight();

    // ---- 多选模式（笔记页工具栏「选择」）----
    // 开启后：左侧出现圆形勾选框、右上角 ⋯ 隐藏、正文关闭文本交互，
    // 于是「点卡片任意空白处」都会落到本控件上（见 mousePressEvent）。
    void setSelectionMode(bool on);
    void setChecked(bool on);
    bool isChecked() const { return m_checked; }

signals:
    void editRequested(qint64 id);
    void deleteRequested(qint64 id);
    void commentRequested(qint64 id);
    void togglePinnedRequested(qint64 id);
    void taskToggled(qint64 id, const QString &content);
    void parentReferenceClicked(qint64 parentId);
    // 查看详细信息（元信息 + 历史版本，服务端 GET /inbox/notes/<id>/history）
    void detailsRequested(qint64 id);
    // 点击正文里的 #标签（层级 tag 的每段可点，参数为「到该段为止的路径」）
    void tagClicked(const QString &path);
    // ⋯ 菜单「转为待办」：先建 Todo 再删原笔记（NoteTodoConverter 语义）
    void convertToTodoRequested(qint64 id);
    // 多选模式下点击卡片（modifiers 交给宿主实现 Shift 范围选 / Ctrl 加选）
    void selectionClicked(qint64 id, Qt::KeyboardModifiers mods);

protected:
    bool eventFilter(QObject *obj, QEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    bool event(QEvent *event) override;

private:
    void onLinkActivated(const QString &link);
    void refreshCheckStyle();

    Note m_note;
    bool m_pinned = false;
    QString m_baseStyle;          // 初始样式，供 flashHighlight 恢复
    qint64 m_parentId = 0;        // 被评论/被引用笔记 id（0 = 无引用预览）
    QLabel *m_parentRef = nullptr;

    QVBoxLayout *m_col = nullptr;    // 内容列（header + 正文 + 引用预览）
    QLabel *m_check = nullptr;       // 多选模式的圆形勾选框
    QWidget *m_menuBtn = nullptr;    // 右上角 ⋯（多选模式下隐藏）
    QLabel *m_content = nullptr;     // 正文标签（多选模式下关闭文本交互）
    QLabel *m_timeLabel = nullptr;   // 头部时间标签
    QLabel *m_pinLabel = nullptr;    // 置顶图标
    QLabel *m_conflictLabel = nullptr; // 冲突警告图标
    QLabel *m_pendingLabel = nullptr;  // 待同步图标
    bool m_selectMode = false;
    bool m_checked = false;
    QGraphicsDropShadowEffect *m_shadow = nullptr; // hover-only：仅悬浮时创建，节省 GPU 资源
};

// ------------------------------------------------------------------ //
// 笔记编辑器对话框（新建/编辑），带 #标签 联想
class NoteEditorDialog : public QDialog
{
    Q_OBJECT
public:
    explicit NoteEditorDialog(const QString &initial = QString(), const QStringList &existingTags = {},
                              const QString &title = QStringLiteral("新建笔记"), QWidget *parent = nullptr);
    QString text() const;

protected:
    bool eventFilter(QObject *obj, QEvent *event) override;

private slots:
    void updateSuggestions();
    void applySuggestion(QListWidgetItem *item);

private:
    void hideSuggestions();
    QPlainTextEdit *m_editor;
    QListWidget *m_suggest;
    QStringList m_existingTags;
    QStringList m_suggestions;
    int m_suggestionIndex = -1;
};

// ------------------------------------------------------------------ //
// 评论对话框
class CommentsDialog : public QDialog
{
    Q_OBJECT
public:
    explicit CommentsDialog(qint64 noteId, QWidget *parent = nullptr);
    void setComments(const QList<Comment> &comments);
    QString commentText() const;

protected:
    void showEvent(QShowEvent *event) override;

private:
    QListWidget *m_list;
    QPlainTextEdit *m_input;
    qint64 m_noteId;
};

// ------------------------------------------------------------------ //
// 批量标签对话框：给选中的多条笔记统一添加或移除某个标签。
// 顶部为「添加 / 移除」模式切换；中部为可搜索的标签列表；添加模式额外提供新标签输入框。
class TagPickerDialog : public QDialog
{
    Q_OBJECT
public:
    enum class Mode { Add, Remove };
    // known：标签池（添加模式的候选，来自编辑器联想列表）；noteTags：选中笔记标签的并集
    // （移除模式的候选）；noteCount：选中的笔记条数（仅用于文案）
    TagPickerDialog(const QStringList &known, const QStringList &noteTags, int noteCount,
                    QWidget *parent = nullptr);
    Mode mode() const { return m_mode; }
    // 要添加/移除的标签（添加模式允许是列表里没有的新标签）
    QString tag() const;

private:
    void setMode(Mode m);
    void refreshList();
    void acceptCurrent();

    Mode m_mode = Mode::Add;
    QStringList m_known;
    QStringList m_noteTags;
    int m_noteCount = 0;
    QLabel *m_title = nullptr;
    QPushButton *m_tabAdd = nullptr;
    QPushButton *m_tabRemove = nullptr;
    QLineEdit *m_newTag = nullptr;
    QLineEdit *m_search = nullptr;
    QListWidget *m_list = nullptr;
};

// ------------------------------------------------------------------ //
// 笔记详细信息对话框：上半部分为笔记元信息（ID / 添加时间 / 更新时间 / 同步时间 /
// 来源设备 / 版本 / 标签 / 状态 / 内容长度），下半部分为历史版本列表
// （自原「历史版本」对话框迁移而来，GET /inbox/notes/<id>/history）。
// 「恢复此版本」把选中版本的内容回填到笔记正文。
class NoteDetailsDialog : public QDialog
{
    Q_OBJECT
public:
    explicit NoteDetailsDialog(const Note &note, QWidget *parent = nullptr);
    // 填充历史版本列表（在线拉取成功后回填）
    void setHistory(const QList<NoteHistory> &items);
    // 历史版本不可用（离线 / 本地未同步 / 获取失败）时在版本列表区显示原因
    void setHistoryUnavailable(const QString &reason);
    // 回填「来源设备」的友好名称（异步从已配对设备列表解析后调用）
    void setDeviceName(const QString &name);

signals:
    // 请求把选中历史版本的内容恢复到笔记正文
    void restoreRequested(qint64 noteId, const QString &content);
    // 「转为待办」：先建 Todo 再删原笔记
    void convertRequested(qint64 noteId);
    // 点击标签面包屑的某一段（层级 tag，参数为「到该段为止的路径」）
    void tagClicked(const QString &path);

private slots:
    void onCurrentRowChanged(int row);
    void onRestoreClicked();
    void onCopyClicked();

private:
    QLabel *m_deviceValue = nullptr;  // 来源设备值控件（setDeviceName 回填目标）
    QLabel *m_tagsValue = nullptr;    // 标签面包屑（每段可点，按路径筛选）
    QString m_deviceId;               // 原始 device_id（tooltip 展示）
    QListWidget *m_list = nullptr;
    QPlainTextEdit *m_preview = nullptr;
    QPushButton *m_btnRestore = nullptr;
    QList<NoteHistory> m_items;
    qint64 m_noteId = 0;
};

} // namespace awqtui
