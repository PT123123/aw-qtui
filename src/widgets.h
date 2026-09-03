// widgets.h —— 通用控件：TagChip / NoteCard / StatusBadge / NoteEditorDialog / CommentsDialog
#pragma once

#include <QDialog>
#include <QFrame>
#include <QLabel>
#include <QListWidget>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QWidget>

#include "models.h"

namespace awqtui {

// ISO8601 -> 本地时间 "YYYY-MM-DD HH:MM"
QString formatLocal(const QString &iso, const QString &fmt = QStringLiteral("yyyy-MM-dd HH:mm"));
// ISO8601 -> 相对时间（对齐 MoeMemos：刚刚 / N 分钟前 / N 小时前 / N 天前 / 日期）
QString formatRelative(const QString &iso);
// 从纯文本提取 #tag
QStringList extractTags(const QString &text);

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
// 笔记卡片（MoeMemos 风格）：头部（相对时间 + 置顶/同步图标 + ⋯ 菜单）+ Markdown 内容
class NoteCard : public QFrame
{
    Q_OBJECT
public:
    explicit NoteCard(const Note &note, bool pinned = false, QWidget *parent = nullptr);
    Note note() const { return m_note; }
    // 在内容下方注入「被评论/被引用笔记」的预览（灰色小字、100 字截断），点击可跳转
    void setParentReference(qint64 parentId, const QString &preview);
    // 跳转定位时的视觉反馈：边框高亮闪烁后恢复
    void flashHighlight();
signals:
    void editRequested(qint64 id);
    void deleteRequested(qint64 id);
    void commentRequested(qint64 id);
    void togglePinnedRequested(qint64 id);
    void taskToggled(qint64 id, const QString &content);
    void parentReferenceClicked(qint64 parentId);
    // 查看历史版本（服务端 GET /inbox/notes/<id>/history）
    void historyRequested(qint64 id);

protected:
    bool eventFilter(QObject *obj, QEvent *event) override;

private:
    void onLinkActivated(const QString &link);

    Note m_note;
    bool m_pinned = false;
    QString m_baseStyle;          // 初始样式，供 flashHighlight 恢复
    qint64 m_parentId = 0;        // 被评论/被引用笔记 id（0 = 无引用预览）
    QLabel *m_parentRef = nullptr;
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
// 笔记历史版本对话框（GET /inbox/notes/<id>/history）
// 左侧版本列表，右侧只读预览；「恢复此版本」把选中版本的内容回填到笔记。
class NoteHistoryDialog : public QDialog
{
    Q_OBJECT
public:
    explicit NoteHistoryDialog(qint64 noteId, QWidget *parent = nullptr);
    void setHistory(const QList<NoteHistory> &items);

signals:
    // 请求把选中历史版本的内容恢复到笔记正文
    void restoreRequested(qint64 noteId, const QString &content);

private slots:
    void onCurrentRowChanged(int row);
    void onRestoreClicked();
    void onCopyClicked();

private:
    QListWidget *m_list = nullptr;
    QPlainTextEdit *m_preview = nullptr;
    QPushButton *m_btnRestore = nullptr;
    QList<NoteHistory> m_items;
    qint64 m_noteId = 0;
};

} // namespace awqtui
