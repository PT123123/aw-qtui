// widgets.cpp
#include "widgets.h"

#include "mdrender.h"
#include "theme.h"

#include <QApplication>
#include <QClipboard>
#include <QDateTime>
#include <QDesktopServices>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QListWidgetItem>
#include <QMenu>
#include <QRegularExpression>
#include <QStyle>
#include <QToolButton>
#include <QUrl>
#include <QVBoxLayout>

#include <utility>

namespace awqtui {

QString formatLocal(const QString &iso, const QString &fmt)
{
    if (iso.isEmpty())
        return QStringLiteral("--");
    QDateTime dt = QDateTime::fromString(iso, Qt::ISODate);
    if (!dt.isValid())
        return iso.left(16);
    dt = dt.toLocalTime();
    return dt.toString(fmt);
}

QStringList extractTags(const QString &text)
{
    QStringList out;
    static const QRegularExpression re(QStringLiteral("#([^\\s#]+)"));
    auto it = re.globalMatch(text);
    while (it.hasNext()) {
        const auto m = it.next();
        if (!m.captured(1).isEmpty())
            out << m.captured(1);
    }
    return out;
}

QString formatRelative(const QString &iso)
{
    if (iso.isEmpty())
        return QStringLiteral("--");
    QDateTime dt = QDateTime::fromString(iso, Qt::ISODate);
    if (!dt.isValid())
        return iso.left(16);
    dt = dt.toLocalTime();
    const qint64 secs = dt.secsTo(QDateTime::currentDateTime());
    if (secs < 60)
        return QStringLiteral("刚刚");
    if (secs < 3600)
        return QStringLiteral("%1 分钟前").arg(secs / 60);
    if (secs < 86400)
        return QStringLiteral("%1 小时前").arg(secs / 3600);
    if (secs < 604800)
        return QStringLiteral("%1 天前").arg(secs / 86400);
    return dt.toString(QStringLiteral("yyyy-MM-dd"));
}

// ------------------------------------------------------------------ //
// StatusBadge
StatusBadge::StatusBadge(QWidget *parent) : QWidget(parent)
{
    auto *lay = new QHBoxLayout(this);
    lay->setContentsMargins(0, 0, 0, 0);
    lay->setSpacing(6);
    m_dot = new QLabel(QStringLiteral("●"));
    m_dot->setStyleSheet(QStringLiteral("color: %1; font-size: 12px;").arg(kColorFgMuted));
    m_label = new QLabel(QStringLiteral("未知"));
    m_label->setStyleSheet(QStringLiteral("color: %1; font-size: 12px;").arg(kColorFgMuted));
    lay->addWidget(m_dot);
    lay->addWidget(m_label);
    setState(State::Unknown);
}

void StatusBadge::setState(State s, const QString &text)
{
    const char *color = kColorFgMuted;
    QString label = text;
    switch (s) {
    case State::Connected:
        color = kColorOk;
        if (label.isEmpty())
            label = QStringLiteral("已连接");
        break;
    case State::Syncing:
        color = kColorWarn;
        if (label.isEmpty())
            label = QStringLiteral("同步中…");
        break;
    case State::Disconnected:
    case State::Error:
        color = kColorDanger;
        if (label.isEmpty())
            label = (s == State::Error) ? QStringLiteral("出错") : QStringLiteral("已断开");
        break;
    default:
        if (label.isEmpty())
            label = QStringLiteral("未知");
        break;
    }
    m_dot->setStyleSheet(QStringLiteral("color: %1; font-size: 12px;").arg(color));
    m_label->setText(label);
    m_label->setStyleSheet(QStringLiteral("color: %1; font-size: 12px;").arg(color));
    setToolTip(label);
}

// ------------------------------------------------------------------ //
// NoteCard —— MoeMemos 风格卡片：头部（相对时间 + 置顶/状态图标 + ⋯ 菜单）+ Markdown 内容
NoteCard::NoteCard(const Note &note, bool pinned, QWidget *parent)
    : QFrame(parent), m_note(note), m_pinned(pinned)
{
    setObjectName(QStringLiteral("NoteCard"));
    setStyleSheet(QStringLiteral(
        "QFrame#NoteCard { background: %1; border: 1px solid %2; border-radius: 10px; }"
        "QFrame#NoteCard:hover { border-color: %3; }")
                      .arg(kColorBgElev, kColorBorder, kColorAccent));

    auto *lay = new QVBoxLayout(this);
    lay->setContentsMargins(14, 10, 10, 12);
    lay->setSpacing(6);

    // ---- 头部行：时间（左）+ 状态图标 + ⋯ 菜单（右） ----
    auto *header = new QHBoxLayout;
    header->setSpacing(6);

    auto *time = new QLabel(formatRelative(note.updatedAt.isEmpty() ? note.createdAt : note.updatedAt));
    time->setToolTip(QStringLiteral("创建 %1\n更新 %2")
                         .arg(formatLocal(note.createdAt), formatLocal(note.updatedAt)));
    time->setStyleSheet(QStringLiteral("color: %1; font-size: 11px; background: transparent; border: none;")
                            .arg(kColorFgMuted));
    header->addWidget(time);

    if (m_pinned) {
        auto *pin = new QLabel(QStringLiteral("⚑"));
        pin->setToolTip(QStringLiteral("已置顶"));
        pin->setStyleSheet(QStringLiteral("color: %1; font-size: 13px; background: transparent; border: none;")
                               .arg(kColorWarn));
        header->addWidget(pin);
    }
    if (note.conflict) {
        auto *warn = new QLabel(QStringLiteral("⚠"));
        warn->setToolTip(QStringLiteral("存在同步冲突"));
        warn->setStyleSheet(QStringLiteral("color: %1; font-size: 12px; background: transparent; border: none;")
                                .arg(kColorWarn));
        header->addWidget(warn);
    }
    if (!note.pendingOp.isEmpty()) {
        const bool del = note.pendingOp == QLatin1String("delete");
        auto *pend = new QLabel(del ? QStringLiteral("🗑") : QStringLiteral("⏳"));
        pend->setToolTip(del ? QStringLiteral("待同步删除") : QStringLiteral("待同步"));
        pend->setStyleSheet(QStringLiteral("color: %1; font-size: 12px; background: transparent; border: none;")
                                .arg(kColorWarn));
        header->addWidget(pend);
    }

    header->addStretch(1);

    auto *menuBtn = new QToolButton;
    menuBtn->setText(QStringLiteral("⋯"));
    menuBtn->setCursor(Qt::PointingHandCursor);
    menuBtn->setPopupMode(QToolButton::InstantPopup);
    menuBtn->setFixedSize(30, 26);
    menuBtn->setStyleSheet(QStringLiteral(
        "QToolButton { background: transparent; border: none; border-radius: 5px;"
        " color: %1; font-size: 16px; font-weight: 700; }"
        "QToolButton:hover { background: %2; color: %3; }")
                              .arg(kColorFgMuted, kColorBgElev2, kColorFg));
    auto *menu = new QMenu(menuBtn);
    menu->addAction(m_pinned ? QStringLiteral("取消置顶") : QStringLiteral("置顶"), this,
                    [this] { emit togglePinnedRequested(m_note.id); });
    menu->addAction(QStringLiteral("复制内容"), this, [this] {
        QApplication::clipboard()->setText(m_note.content);
    });
    menu->addAction(QStringLiteral("编辑"), this, [this] { emit editRequested(m_note.id); });
    menu->addAction(QStringLiteral("评论"), this, [this] { emit commentRequested(m_note.id); });
    auto *del = menu->addAction(QStringLiteral("删除"), this, [this] { emit deleteRequested(m_note.id); });
    del->setIcon(QApplication::style()->standardIcon(QStyle::SP_TrashIcon));
    menuBtn->setMenu(menu);
    header->addWidget(menuBtn);

    lay->addLayout(header);

    // ---- 内容：完整 Markdown + #标签 高亮 + 可点击任务清单 ----
    const MarkdownRenderResult md = renderMarkdown(note.content);
    auto *content = new QLabel(md.html);
    content->setWordWrap(true);
    content->setTextFormat(Qt::RichText);
    content->setTextInteractionFlags(Qt::TextBrowserInteraction);
    content->setOpenExternalLinks(false); // 手动分发：任务切换 / 外链
    content->setStyleSheet(QStringLiteral("color: %1; background: transparent; border: none; font-size: 14px;")
                               .arg(kColorFg));
    connect(content, &QLabel::linkActivated, this, &NoteCard::onLinkActivated);
    lay->addWidget(content);
}

void NoteCard::onLinkActivated(const QString &link)
{
    if (link.startsWith(QLatin1String("awtask://"))) {
        bool ok = false;
        const int idx = link.mid(9).toInt(&ok);
        if (!ok)
            return;
        QStringList lines = m_note.content.split(QLatin1Char('\n'));
        static const QRegularExpression taskRe(QStringLiteral("^\\s*[-*+]\\s+\\[[ xX]\\]"));
        int count = 0;
        for (int i = 0; i < lines.size(); ++i) {
            if (!taskRe.match(lines[i]).hasMatch())
                continue;
            if (count == idx) {
                const int pos = lines[i].indexOf(QLatin1Char('['));
                if (pos < 0 || pos + 1 >= lines[i].size())
                    return;
                const bool checked = lines[i].at(pos + 1) == QLatin1Char('x')
                                     || lines[i].at(pos + 1) == QLatin1Char('X');
                lines[i].replace(pos + 1, 1, checked ? QLatin1Char(' ') : QLatin1Char('x'));
                emit taskToggled(m_note.id, lines.join(QLatin1Char('\n')));
                return;
            }
            ++count;
        }
        return;
    }
    const QUrl url(link);
    if (url.isValid())
        QDesktopServices::openUrl(url);
}

// ------------------------------------------------------------------ //
// NoteEditorDialog
NoteEditorDialog::NoteEditorDialog(const QString &initial, const QStringList &existingTags,
                                   const QString &title, QWidget *parent)
    : QDialog(parent), m_existingTags(existingTags)
{
    setWindowTitle(title);
    setModal(true);
    resize(560, 320);

    auto *lay = new QVBoxLayout(this);
    lay->setSpacing(10);

    auto *hint = new QLabel(QStringLiteral("提示：# 输入标签，Ctrl+Enter 或 Alt+S 提交，Esc 取消"));
    hint->setStyleSheet(QStringLiteral("color: %1; font-size: 11px;").arg(kColorFgMuted));
    lay->addWidget(hint);

    m_editor = new QPlainTextEdit;
    m_editor->setPlaceholderText(QStringLiteral("写点什么… 用 #标签 归类"));
    m_editor->setPlainText(initial);
    m_editor->setStyleSheet(QStringLiteral(
        "QPlainTextEdit { background: %1; border: 1px solid %2; border-radius: 6px;"
        " padding: 10px; font-size: 14px; }")
                                .arg(kColorBgElev, kColorBorder));
    lay->addWidget(m_editor, 1);

    m_suggest = new QListWidget;
    m_suggest->setMaximumHeight(120);
    m_suggest->setVisible(false);
    m_suggest->setStyleSheet(QStringLiteral(
        "QListWidget { background: #2a2f37; border: 1px solid %1; border-radius: 6px; }"
        "QListWidget::item { padding: 5px 10px; }"
        "QListWidget::item:selected { background: %2; }")
                                 .arg(kColorBorder, kColorAccent));
    connect(m_suggest, &QListWidget::itemClicked, this, &NoteEditorDialog::applySuggestion);
    lay->addWidget(m_suggest);

    auto *btns = new QHBoxLayout;
    auto *ok = new QPushButton(QStringLiteral("保存"));
    ok->setObjectName(QStringLiteral("PrimaryBtn"));
    auto *cancel = new QPushButton(QStringLiteral("取消"));
    connect(ok, &QPushButton::clicked, this, &QDialog::accept);
    connect(cancel, &QPushButton::clicked, this, &QDialog::reject);
    btns->addStretch(1);
    btns->addWidget(ok);
    btns->addWidget(cancel);
    lay->addLayout(btns);

    m_editor->installEventFilter(this);
    connect(m_editor, &QPlainTextEdit::textChanged, this, &NoteEditorDialog::updateSuggestions);
    connect(m_editor, &QPlainTextEdit::cursorPositionChanged, this, &NoteEditorDialog::updateSuggestions);
}

QString NoteEditorDialog::text() const
{
    return m_editor->toPlainText().trimmed();
}

void NoteEditorDialog::hideSuggestions()
{
    m_suggest->setVisible(false);
    m_suggestions.clear();
}

void NoteEditorDialog::updateSuggestions()
{
    // 光标前的文本
    const QTextCursor c = m_editor->textCursor();
    const QString before = m_editor->toPlainText().left(c.position());
    const int lastHash = before.lastIndexOf(QLatin1Char('#'));
    if (lastHash < 0) {
        hideSuggestions();
        return;
    }
    QString between = before.mid(lastHash + 1);
    if (between.contains(QLatin1Char(' ')) || between.contains(QLatin1Char('#'))) {
        hideSuggestions();
        return;
    }
    const QString query = between.toLower();
    QStringList pool;
    if (query.isEmpty()) {
        pool = m_existingTags.mid(0, 5);
    } else {
        for (const QString &t : m_existingTags) {
            if (t.toLower().startsWith(query)) {
                pool << t;
                if (pool.size() >= 5)
                    break;
            }
        }
    }
    m_suggestions = pool;
    m_suggestionIndex = -1;
    if (pool.isEmpty()) {
        hideSuggestions();
        return;
    }
    m_suggest->clear();
    for (const QString &t : pool)
        new QListWidgetItem(QLatin1Char('#') + t, m_suggest);
    m_suggest->setVisible(true);
}

void NoteEditorDialog::applySuggestion(QListWidgetItem *item)
{
    const QString tag = item->text().mid(1);
    QTextCursor c = m_editor->textCursor();
    const QString text = m_editor->toPlainText();
    const int pos = c.position();
    const int lastHash = text.lastIndexOf(QLatin1Char('#'), pos - 1);
    if (lastHash < 0)
        return;
    const QString newText = text.left(lastHash) + QLatin1Char('#') + tag + QLatin1Char(' ') + text.mid(pos);
    m_editor->setPlainText(newText);
    QTextCursor nc = m_editor->textCursor();
    nc.setPosition(lastHash + 1 + tag.size() + 1);
    m_editor->setTextCursor(nc);
    hideSuggestions();
}

bool NoteEditorDialog::eventFilter(QObject *obj, QEvent *event)
{
    if (obj == m_editor && event->type() == QEvent::KeyPress) {
        auto *ke = static_cast<QKeyEvent *>(event);
        const int key = ke->key();
        const bool ctrl = ke->modifiers() & Qt::ControlModifier;
        const bool alt = ke->modifiers() & Qt::AltModifier;
        if (!m_suggestions.isEmpty()) {
            const int n = m_suggestions.size();
            if (key == Qt::Key_Down) {
                m_suggestionIndex = (m_suggestionIndex + 1) % n;
                m_suggest->setCurrentRow(m_suggestionIndex);
                return true;
            }
            if (key == Qt::Key_Up) {
                m_suggestionIndex = (m_suggestionIndex - 1 + n) % n;
                m_suggest->setCurrentRow(m_suggestionIndex);
                return true;
            }
            if ((key == Qt::Key_Return || key == Qt::Key_Enter || key == Qt::Key_Tab) &&
                m_suggestionIndex >= 0) {
                applySuggestion(m_suggest->item(m_suggestionIndex));
                return true;
            }
            if (key == Qt::Key_Escape) {
                hideSuggestions();
                return true;
            }
        }
        if ((key == Qt::Key_Return || key == Qt::Key_Enter) && ctrl) {
            accept();
            return true;
        }
        if (key == Qt::Key_S && alt) {
            accept();
            return true;
        }
        if (key == Qt::Key_Escape) {
            hideSuggestions();
            return true;
        }
    }
    return QDialog::eventFilter(obj, event);
}

// ------------------------------------------------------------------ //
// CommentsDialog
CommentsDialog::CommentsDialog(qint64 noteId, QWidget *parent)
    : QDialog(parent), m_noteId(noteId)
{
    setWindowTitle(QStringLiteral("评论 · 笔记 #%1").arg(noteId));
    setModal(true);
    resize(480, 420);

    auto *lay = new QVBoxLayout(this);
    lay->setSpacing(8);

    m_list = new QListWidget;
    m_list->setStyleSheet(QStringLiteral(
        "QListWidget { background: %1; border: 1px solid %2; border-radius: 6px; }")
                              .arg(kColorBgElev, kColorBorder));
    lay->addWidget(m_list, 1);

    auto *row = new QHBoxLayout;
    m_input = new QPlainTextEdit;
    m_input->setPlaceholderText(QStringLiteral("添加评论… 以 [[时间戳]] 关联其它笔记"));
    m_input->setMaximumHeight(70);
    row->addWidget(m_input, 1);
    auto *btn = new QPushButton(QStringLiteral("发表"));
    btn->setStyleSheet(QStringLiteral("QPushButton { background: %1; color: white; border: none;"
                                      " border-radius: 6px; padding: 6px 16px; }")
                           .arg(kColorAccent));
    connect(btn, &QPushButton::clicked, this, &QDialog::accept);
    row->addWidget(btn);
    lay->addLayout(row);
}

void CommentsDialog::setComments(const QList<Comment> &comments)
{
    m_list->clear();
    if (comments.isEmpty()) {
        m_list->addItem(QStringLiteral("（还没有评论）"));
        return;
    }
    for (const Comment &c : comments) {
        const QString mark = c.pending ? QStringLiteral("  [待同步]") : QString();
        auto *item = new QListWidgetItem(QStringLiteral("%1  %2%3")
                                             .arg(formatLocal(c.createdAt), c.content, mark));
        item->setToolTip(c.content);
        m_list->addItem(item);
    }
}

QString CommentsDialog::commentText() const
{
    return m_input->toPlainText().trimmed();
}

} // namespace awqtui
