// inboxpage.h —— 收件箱页
#pragma once

#include <QList>
#include <QSet>
#include <QWidget>

#include "localstore.h"
#include "models.h"
#include "widgets.h" // StatusBadge 完整定义（嵌套枚举 State）

class QComboBox;
class QLineEdit;
class QListWidget;
class QStackedLayout;
class QTimer;
class QVBoxLayout;

namespace awqtui {

class ApiClient;
class StatusBadge;

class InboxPage : public QWidget
{
    Q_OBJECT
public:
    explicit InboxPage(ApiClient *api, QWidget *parent = nullptr);

    void loadNotes(bool reset = true);
    void loadTags();
    void loadDetailedTags();
    void refreshAll();

    int noteCount() const { return m_notes.size(); }
    QStringList selectedTags() const { return m_selectedTags; }
    QString searchTerm() const;
    StatusBadge *badge() const { return m_badge; }

signals:
    void noteCountChanged(int n);

private slots:
    void onSearchChanged();
    void onSortChanged();
    void onRefresh();
    void onTagToggled();
    void onNewNote();
    void onEditNote(qint64 id);
    void onDeleteNote(qint64 id);
    void onComment(qint64 id);
    void onTogglePinned(qint64 id);
    void onTaskToggled(qint64 id, const QString &content);
    void onScroll();
    void onCopyAll();

private:
    void buildUi();
    void rebuildTagSidebar();
    void applyClientFilter();
    void appendNotes(const QList<Note> &notes, bool clear);
    void setStatus(StatusBadge::State s, const QString &text = QString());
    QWidget *makeCard(const Note &n);
    // 把新内容应用到笔记（在线 PUT / 离线本地），供编辑与任务勾选共用
    void applyContent(qint64 id, const QString &text);

    // ---- 离线优先（本地存储） ----
    void renderLocal();               // 服务端不可用时，用本地缓存渲染（含客户端过滤/排序）
    void rebuildTagsFromLocal();      // 离线时从本地笔记统计标签
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
    void updateOfflineBadge();
    bool isOffline() const { return !m_online; }

    ApiClient *m_api;
    LocalStore m_store;
    bool m_online = true;             // 上次服务端请求是否成功
    QTimer *m_reconnect = nullptr;
    int m_pendingPush = 0;            // 正在补推的数量（并发计数）
    int m_reqGen = 0;                 // 请求代际：切离线/重置时递增，丢弃迟到回包
    QSet<QString> m_inflightComments; // 正在由 submitComment 直接推送的评论时间戳，避免补推重复 POST
    QList<Note> m_notes;
    QList<DetailedTag> m_tags;
    QStringList m_selectedTags;

    QLineEdit *m_search;
    QComboBox *m_sort;
    StatusBadge *m_badge;
    QPushButton *m_btnSidebar;
    QWidget *m_tagPanel;
    QListWidget *m_tagList;
    QListWidget *m_list;
    QPushButton *m_fab;
    QStackedLayout *m_stack;

    int m_offset = 0;
    int m_limit = 20;
    bool m_hasMore = true;
    bool m_loading = false;
    int m_sidebarWidth = 180;
    bool m_sidebarVisible = true;
};

} // namespace awqtui
