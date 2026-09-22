// inboxpage.h —— 收件箱页
#pragma once

#include <QElapsedTimer>
#include <QList>
#include <QPointer>
#include <QSet>
#include <QWidget>

#include "localstore.h"
#include "models.h"
#include "widgets.h" // StatusBadge 完整定义（嵌套枚举 State）

class QComboBox;
class QGraphicsDropShadowEffect;
class QKeyEvent;
class QLineEdit;
class QLabel;
class QListWidget;
class QPushButton;
class QStackedLayout;
class QTimer;
class QToolButton;
class QTreeWidget;
class QTreeWidgetItem;
class QVBoxLayout;

// Qt Designer 布局（inboxpage.ui），全局命名空间
namespace Ui { class InboxPage; }

namespace awqtui {

class ApiClient;
class StatusBadge;
class NoteEditorDialog;

class InboxPage : public QWidget
{
    Q_OBJECT
public:
    explicit InboxPage(ApiClient *api, QWidget *parent = nullptr);
    ~InboxPage() override;

    void loadNotes(bool reset = true);
    void loadTags();
    void loadDetailedTags();
    void loadTagTree();
    void refreshAll();

    int noteCount() const { return m_notes.size(); }
    // 当前层级标签筛选路径（空 = 无筛选）；兼容旧接口：返回单元素列表
    QStringList selectedTags() const
    {
        return m_currentTag.isEmpty() ? QStringList() : QStringList{ m_currentTag };
    }
    QString currentTagPath() const { return m_currentTag; }
    QString searchTerm() const;
    StatusBadge *badge() const { return m_badge; }

    // 全局热键“添加记录”入口：等同点击右下角 ＋
    void openNewNote();
    // 界面缩放：按当前全局缩放因子重应用本页所有 Npx 样式/固定尺寸，并重渲染列表
    void applyUiScale();

signals:
    void noteCountChanged(int n);
    // 用户点击工具栏 ⚙，请求打开设置界面（由 MainWindow 响应）
    void settingsRequested();

protected:
    // Esc 退出多选（与任务页一致）
    void keyPressEvent(QKeyEvent *event) override;

private slots:
    void onSearchChanged();
    void onSortChanged();
    void onRefresh();
    void onTagTreeItemClicked(QTreeWidgetItem *item, int column);
    void onNewNote();
    void onEditNote(qint64 id);
    void onDeleteNote(qint64 id);
    void onComment(qint64 id);
    void onTogglePinned(qint64 id);
    void onNoteDetails(qint64 id);
    void onConvertToTodo(qint64 id);
    void onTaskToggled(qint64 id, const QString &content);
    void onParentReferenceClicked(qint64 parentId);
    void onScroll();
    void onCopyAll();

private:
    // Qt Designer 生成的布局对象（inboxpage.ui -> ui_inboxpage.h）
    Ui::InboxPage *ui = nullptr;
    void buildUi();
    void applyStyles();               // 内联主题样式（buildUi 与 applyUiScale 共用）
    void rebuildTagTree();            // 按 m_tagRoots 重建侧栏层级标签树并恢复当前选中
    void updateFilterBar();           // 同步筛选面包屑条（显示/隐藏、文案、↑ 可用性）
    void applyTagFilterPath(const QString &path); // 进入/切换/清除层级标签筛选（空 = 清除）
    static QList<TagNode> buildTagTreeFromExact(const QMap<QString, qint64> &exact);
    // force=true 时无条件重建（缩放变化后卡片需按新比例重取样式）
    void applyClientFilter(bool force = false);
    void appendNotes(const QList<Note> &notes, bool reset);
    void setStatus(StatusBadge::State s, const QString &text = QString());
    // 可见笔记中按 id 找卡片（多选时同步勾选态用）
    NoteCard *findCard(qint64 id) const;
    // 内存笔记（含被乐观修改的字段），找不到返回 nullptr
    Note *findNote(qint64 id);

    // ---- 多选（工具栏「选择」按钮） ----
    void setSelectMode(bool on);
    void updateBulkBar();               // 已选计数 + 按钮可用态
    void applySelectionToCards();       // 重建后把选择模式与勾选态套到新卡片
    void pruneSelection();              // 丢弃已不存在笔记的选择残留
    void selectAllToggle();
    // 点卡片（多选模式）：mods 带 Shift = 范围选，其余为切换单条
    void onCardSelectionClicked(qint64 id, Qt::KeyboardModifiers mods);
    void bulkDelete();
    void bulkSetPinned(bool pinned);
    void bulkEditTags();
    QList<qint64> selectedIds() const;  // 按当前可见顺序返回

    // ---- 删除：免确认 + 3 秒撤销浮条（到期才真正提交） ----
    void deleteNotes(const QList<qint64> &ids);
    void commitPendingDelete();         // 撤销窗口到期：真正提交（在线 DELETE / 离线 tombstone）
    void undoPendingDelete();           // 点「撤销」：原样恢复，顺序不变
    void tombstoneLocal(qint64 id);     // 离线或提交失败时的本地删除（不触发列表重建）
    void refreshTagTree();              // 侧栏标签树：在线用服务端口径，离线用本地统计

    // ---- 转为待办：免确认 + 3 秒撤销浮条（到期才真正提交服务端两步转换） ----
    // 与删除同构：撤销窗口内只把笔记从视图隐藏，m_notes / LocalStore 一律不动，
    // 撤销即原样回来（id、评论、历史版本、顺序全不变）；窗口内关掉程序 = 没转。
    void commitPendingConvert();        // 撤销窗口到期：createTodo → deleteNote
    void undoPendingConvert();          // 点「撤销」：解除隐藏，笔记原位回来
    // 转换未完成（第一步建待办失败 / 第二步删原笔记失败）：解除隐藏并如实提示
    void abortPendingConvert(qint64 id, const QString &message);
    // 取笔记原文（内存列表 → 本地镜像）供两步转换使用；找不到返回 false
    bool lookupNote(qint64 id, Note *out) const;

    // ---- 重建时保持滚动位置 ----
    struct ScrollAnchor {
        qint64 id = 0;              // 重建前视口顶部那条笔记
        int delta = 0;              // 该卡片顶边相对视口顶端的像素偏移
        QList<qint64> fallback;     // 锚点被删时的后备（锚点之后的若干条）
    };
    ScrollAnchor captureScrollAnchor() const;
    void restoreScrollAnchor(const ScrollAnchor &a);
    // 把新内容应用到笔记（在线 PUT / 离线本地），供编辑与任务勾选共用
    void applyContent(qint64 id, const QString &text);
    // 生成被评论/被引用笔记的预览文本（100 字截断、去除常见 markdown 标记）
    QString parentPreview(qint64 parentId) const;
    // 滚动定位到指定笔记所在卡片并高亮闪烁
    void jumpToNote(qint64 id);

    // ---- 离线优先（本地存储） ----
    void renderLocal();               // 服务端不可用时，用本地缓存渲染（含客户端过滤/排序）
    void rebuildTagsFromLocal();      // 离线时从本地笔记统计标签
    void rebuildFlatTagsFromTree();   // 在线：标签树拍平重建联想池 m_tags（编辑器 #tag 联想）
    void createLocal(const QString &content, const QStringList &tags);
    void updateLocal(qint64 id, const QString &content, const QStringList &tags);
    void deleteLocal(qint64 id);
    void pushDirty();                 // 重连成功后把本地待同步改动补推服务端
    void pushDone();
    void pushFailed();
    void pushComments();              // 把待同步评论逐条补推服务端
    void submitComment(qint64 noteId, const QString &text); // 评论提交（离线落本地+入队）
    void tryReconnect();              // 定时探测服务端是否恢复
    void startReconnect();
    void triggerLanPull();            // 刷新时后台静默逐台触发局域网同步（成功后追加一轮加载）
    void updateOfflineBadge();
    bool isOffline() const { return !m_online; }

    ApiClient *m_api;
    LocalStore m_store;
    bool m_online = true;             // 上次服务端请求是否成功
    QTimer *m_reconnect = nullptr;
    int m_pendingPush = 0;            // 正在补推的数量（并发计数）
    int m_reqGen = 0;                 // 请求代际：切离线/重置时递增，丢弃迟到回包
    QSet<QString> m_inflightComments; // 正在由 submitComment 直接推送的评论时间戳，避免补推重复 POST
    // 局域网拉取（刷新顺带触发）：在途标志 + 节流（连续 F5 不重复打同步）
    bool m_lanPullInflight = false;
    QElapsedTimer m_lanPullThrottle;
    QList<Note> m_notes;
    QList<DetailedTag> m_tags;        // 扁平标签（编辑器联想用）
    QList<TagNode> m_tagRoots;        // 层级标签树（服务端 /tags/tree 或本地构建）
    QString m_currentTag;             // 当前筛选路径（空 = 无筛选；?tag= 段边界前缀匹配）
    // 当前渲染列表的笔记 id 顺序（与 m_list 逐项对应），供「跳转到被评论笔记」定位
    QList<qint64> m_visibleIds;
    // 上次实际渲染的笔记内容签名：相同则跳过重建，避免同步轮询把列表清空重画导致闪动
    QString m_renderSig;
    // 上次实际渲染的标签树签名：相同则跳过重建，避免侧栏标签树闪动/收起
    QString m_tagTreeSig;
    // 待跳转目标：目标笔记被过滤掉时先清过滤重载，渲染完成后消费
    qint64 m_pendingJumpId = 0;

    QLineEdit *m_search;
    QComboBox *m_sort;
    StatusBadge *m_badge;
    QPushButton *m_btnSidebar;
    QPushButton *m_btnRefresh;
    QPushButton *m_btnCopy;
    QPushButton *m_btnClear;
    QLabel *m_tagTitle;
    QLabel *m_title;
    QLabel *m_emptyIcon;
    QLabel *m_emptyText;
    QLabel *m_emptyHint;
    QWidget *m_tagPanel;
    QTreeWidget *m_tagTree;
    // 层级标签筛选面包屑条（仅筛选时显示）
    QWidget *m_filterBar = nullptr;
    QLabel *m_filterText = nullptr;
    QPushButton *m_btnFilterUp = nullptr;
    QPushButton *m_btnFilterClear = nullptr;
    QListWidget *m_list;
    CardPool *m_cardPool = nullptr; // 虚拟化 widget 池（可见行数 + 缓冲）
    QPushButton *m_fab;
    QStackedLayout *m_stack;
    // 悬浮 + 按钮的投影阴影（受全局阴影开关控制，运行时增删）
    QGraphicsDropShadowEffect *m_fabShadow = nullptr;
    // 新建笔记对话框的单例指针：避免全局热键重复触发时弹出多个窗口
    QPointer<NoteEditorDialog> m_newNoteDialog;
    // 是否给本次重建的卡片列表加入场淡入（仅刷新/初次加载时置真，过滤/翻页时不加）
    bool m_animateCards = false;

    // ---- 多选 ----
    bool m_selectMode = false;
    QSet<qint64> m_selected;
    qint64 m_selectAnchor = 0;        // Shift 范围选的锚点（上一次点过的笔记）
    QPushButton *m_btnSelect = nullptr;
    QWidget *m_bulkBar = nullptr;
    QLabel *m_bulkCount = nullptr;
    QToolButton *m_bulkSelectAll = nullptr;
    QToolButton *m_bulkPin = nullptr;
    QToolButton *m_bulkUnpin = nullptr;
    QToolButton *m_bulkTag = nullptr;
    QToolButton *m_bulkDelete = nullptr;
    QToolButton *m_bulkCancel = nullptr;

    // ---- 待提交删除（撤销窗口内）：已从视图隐藏、尚未落库，撤销可原样恢复 ----
    QList<qint64> m_pendingDel;
    int m_pendingDelCommits = 0;        // 在途的在线删除请求数，归零后刷新一次侧栏标签树
    // ---- 待提交/在途的「转为待办」 ----
    // m_pendingConv：撤销窗口内、尚未提交的（是 m_convHidden 的子集）
    // m_convHidden：整个转换期间都要隐藏的（含请求在途），保证后台刷新不会把这条又拉回列表
    QList<qint64> m_pendingConv;
    QSet<qint64> m_convHidden;
    // 恢复滚动位置期间屏蔽 onScroll 的分页加载（setValue/scrollToItem 会触发 valueChanged）
    bool m_restoringScroll = false;
    bool m_paginationAppend = false;   // true = 当前 appendNotes 为翻页追加，不走 applyClientFilter

    int m_offset = 0;
    int m_limit = 20;
    bool m_hasMore = true;
    bool m_loading = false;
    bool m_rebuilding = false;      // applyClientFilter 重入保护：addItem 触发滚动条 valueChanged → onScroll → loadNotes → renderLocal 会同步重入，内层 clear() 会删掉外层刚 addItem 的 item 造成 use-after-free
    int m_sidebarWidth = 180;
    bool m_sidebarVisible = true;
};

} // namespace awqtui
