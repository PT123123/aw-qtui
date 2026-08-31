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

private:
    QLabel *m_dot;
    QLabel *m_label;
};

// ------------------------------------------------------------------ //
// 笔记卡片（MoeMemos 风格）：头部（相对时间 + 置顶/同步图标 + ⋯ 菜单）+ Markdown 内容
class NoteCard : public QFrame
{
    Q_OBJECT
public:
    explicit NoteCard(const Note &note, bool pinned = false, QWidget *parent = nullptr);
    Note note() const { return m_note; }
signals:
    void editRequested(qint64 id);
    void deleteRequested(qint64 id);
    void commentRequested(qint64 id);
    void togglePinnedRequested(qint64 id);
    void taskToggled(qint64 id, const QString &content);

private:
    void onLinkActivated(const QString &link);

    Note m_note;
    bool m_pinned = false;
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

private:
    QListWidget *m_list;
    QPlainTextEdit *m_input;
    qint64 m_noteId;
};

} // namespace awqtui
