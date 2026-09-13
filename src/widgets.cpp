// widgets.cpp
#include "widgets.h"

#include "config.h"
#include "mdrender.h"
#include "theme.h"

#include <QApplication>
#include <QClipboard>
#include <QCursor>
#include <QDateTime>
#include <QDesktopServices>
#include <QGraphicsDropShadowEffect>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QListWidgetItem>
#include <QMenu>
#include <QMouseEvent>
#include <QRegularExpression>
#include <QScreen>
#include <QShowEvent>
#include <QStyle>
#include <QTimer>
#include <QToolButton>
#include <QUrl>
#include <QVariantAnimation>
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

void showToast(const QString &text, QScreen *anchorScreen)
{
    QScreen *screen = anchorScreen;
    if (!screen)
        screen = QGuiApplication::screenAt(QCursor::pos());
    if (!screen)
        screen = QGuiApplication::primaryScreen();
    if (!screen)
        return;

    // 无边框置顶工具窗：WA_ShowWithoutActivating 保证不抢焦点，
    // Qt::Tool 不进任务栏/Alt+Tab，主窗口隐藏时气泡也照常可见
    auto *toast = new QWidget(nullptr, Qt::Tool | Qt::FramelessWindowHint
                                         | Qt::WindowStaysOnTopHint);
    toast->setAttribute(Qt::WA_ShowWithoutActivating);
    toast->setAttribute(Qt::WA_TranslucentBackground);
    auto *lay = new QVBoxLayout(toast);
    lay->setContentsMargins(0, 0, 0, 0);
    auto *label = new QLabel(text, toast);
    label->setStyleSheet(scaleQss(QStringLiteral(
        "QLabel { color: %1; background: %2; border: 1px solid %3;"
        " border-radius: 8px; padding: 9px 18px; font-size: 13px; }")
                            .arg(gTheme->fg, gTheme->bgElev, gTheme->border)));
    lay->addWidget(label);

    toast->adjustSize();
    const QRect avail = screen->availableGeometry();
    toast->move(avail.x() + (avail.width() - toast->width()) / 2,
                avail.y() + avail.height() - toast->height() - si(90));
    toast->show();

    // 时间轴：前 10% 淡入、中间停留、后 20% 淡出，结束时 DeleteWhenStopped 自毁
    auto *anim = new QVariantAnimation(toast);
    anim->setDuration(1800);
    anim->setStartValue(0.0);
    anim->setEndValue(1.0);
    QObject::connect(anim, &QVariantAnimation::valueChanged, toast, [toast](const QVariant &v) {
        const double t = v.toDouble();
        const double opacity = t < 0.1 ? t / 0.1 : t > 0.8 ? (1.0 - t) / 0.2 : 1.0;
        toast->setWindowOpacity(opacity);
    });
    anim->start(QAbstractAnimation::DeleteWhenStopped);
}

// ------------------------------------------------------------------ //
// StatusBadge
StatusBadge::StatusBadge(QWidget *parent) : QWidget(parent)
{
    auto *lay = new QHBoxLayout(this);
    lay->setContentsMargins(0, 0, 0, 0);
    lay->setSpacing(si(6));
    m_dot = new QLabel(QStringLiteral("●"));
    m_label = new QLabel(QStringLiteral("未知"));
    lay->addWidget(m_dot);
    lay->addWidget(m_label);
    setState(State::Unknown);
}

void StatusBadge::applyStyle()
{
    const QString c = m_color.isEmpty() ? kColorFgMuted : m_color;
    m_dot->setStyleSheet(scaleQss(QStringLiteral("color: %1; font-size: 12px;").arg(c)));
    m_label->setStyleSheet(scaleQss(QStringLiteral("color: %1; font-size: 12px;").arg(c)));
}

void StatusBadge::applyUiScale()
{
    applyStyle();
}

void StatusBadge::setState(State s, const QString &text)
{
    m_color = kColorFgMuted;
    QString label = text;
    switch (s) {
    case State::Connected:
        m_color = kColorOk;
        if (label.isEmpty())
            label = QStringLiteral("已连接");
        break;
    case State::Syncing:
        m_color = kColorWarn;
        if (label.isEmpty())
            label = QStringLiteral("同步中…");
        break;
    case State::Disconnected:
    case State::Error:
        m_color = kColorDanger;
        if (label.isEmpty())
            label = (s == State::Error) ? QStringLiteral("出错") : QStringLiteral("已断开");
        break;
    default:
        if (label.isEmpty())
            label = QStringLiteral("未知");
        break;
    }
    m_label->setText(label);
    applyStyle();
    setToolTip(label);
}

// ------------------------------------------------------------------ //
// NoteCard —— MoeMemos 风格卡片：头部（相对时间 + 置顶/状态图标 + ⋯ 菜单）+ Markdown 内容
NoteCard::NoteCard(const Note &note, bool pinned, QWidget *parent)
    : QFrame(parent), m_note(note), m_pinned(pinned)
{
    setObjectName(QStringLiteral("NoteCard"));
    // 玻璃卡片背景（半透明 + 顶部高光）+ 玻璃亮边；悬浮时背景向强调色靠拢
    const QString cardBg = glassBg(kColorBgElev);
    const QString cardBorder = glassBorder();
    const qreal d = (gTheme && gTheme->light) ? -1.0 : 1.0;
    const QString cardHover = glassEnabled() ? withAlpha(mix(kColorBgElev, kColorAccent, 0.10).toUtf8().constData(), 0.85)
                                              : shade(kColorBgElev, 0.05 * d);
    m_baseStyle = scaleQss(QStringLiteral(
        "QFrame#NoteCard { background: %1; border: 1px solid %2; border-radius: 12px; }"
        "QFrame#NoteCard:hover { background: %3; border-color: %4; }")
        .arg(cardBg, cardBorder, cardHover, kColorAccent));
    setStyleSheet(m_baseStyle);
    // 卡片投影（受全局阴影强度控制）
    makeDropShadow(this);

    auto *lay = new QVBoxLayout(this);
    lay->setContentsMargins(si(14), si(10), si(10), si(12));
    lay->setSpacing(si(6));

    // ---- 头部行：时间（左）+ 状态图标 + ⋯ 菜单（右） ----
    auto *header = new QHBoxLayout;
    header->setSpacing(si(6));

    auto *time = new QLabel(formatRelative(note.updatedAt.isEmpty() ? note.createdAt : note.updatedAt));
    time->setToolTip(QStringLiteral("创建 %1\n更新 %2")
                         .arg(formatLocal(note.createdAt), formatLocal(note.updatedAt)));
    time->setStyleSheet(scaleQss(QStringLiteral(
        "color: %1; font-size: 11px; background: transparent; border: none;")
        .arg(kColorFgMuted)));
    // 窄卡（主导航展开等）时允许时间标签被压缩，避免把右上角 ⋯ 按钮挤出可视范围
    time->setMinimumWidth(0);
    header->addWidget(time);

    if (m_pinned) {
        auto *pin = new QLabel(QStringLiteral("⚑"));
        pin->setToolTip(QStringLiteral("已置顶"));
        pin->setStyleSheet(scaleQss(QStringLiteral(
            "color: %1; font-size: 13px; background: transparent; border: none;")
            .arg(kColorWarn)));
        header->addWidget(pin);
    }
    if (note.conflict) {
        auto *warn = new QLabel(QStringLiteral("⚠"));
        warn->setToolTip(QStringLiteral("存在同步冲突"));
        warn->setStyleSheet(scaleQss(QStringLiteral(
            "color: %1; font-size: 12px; background: transparent; border: none;")
            .arg(kColorWarn)));
        header->addWidget(warn);
    }
    if (!note.pendingOp.isEmpty()) {
        const bool del = note.pendingOp == QLatin1String("delete");
        auto *pend = new QLabel(del ? QStringLiteral("🗑") : QStringLiteral("⏳"));
        pend->setToolTip(del ? QStringLiteral("待同步删除") : QStringLiteral("待同步"));
        pend->setStyleSheet(scaleQss(QStringLiteral(
            "color: %1; font-size: 12px; background: transparent; border: none;")
            .arg(kColorWarn)));
        header->addWidget(pend);
    }

    header->addStretch(1);

    // 用普通按钮 + 手动 exec() 弹菜单：QToolButton::setMenu 会自动画一个三角箭头与
    // "⋯" 重叠，且 InstantPopup 弹窗在 QGraphicsProxyWidget 内会卡鼠标抓取导致界面假死。
    auto *menuBtn = new QPushButton(QStringLiteral("⋯"));
    menuBtn->setCursor(Qt::PointingHandCursor);
    menuBtn->setFixedSize(si(38), si(26));
    menuBtn->setStyleSheet(scaleQss(QStringLiteral(
        "QPushButton { background: transparent; border: none; border-radius: 5px;"
        " color: %1; font-size: 16px; font-weight: 700; padding: 0; }"
        "QPushButton:hover { background: %2; color: %3; }")
        .arg(kColorFgMuted, kColorBgElev2, kColorFg)));
    connect(menuBtn, &QPushButton::clicked, this, [this, menuBtn] {
        // 菜单在栈上构建，exec() 关闭后才执行动作，避免列表重建时销毁打开中的菜单
        QMenu menu(menuBtn);
        QAction *actPin = menu.addAction(m_pinned ? QStringLiteral("取消置顶") : QStringLiteral("置顶"));
        QAction *actCopy = menu.addAction(QStringLiteral("复制内容"));
        QAction *actEdit = menu.addAction(QStringLiteral("编辑"));
        QAction *actCmt = menu.addAction(QStringLiteral("评论"));
        QAction *actDetails = menu.addAction(QStringLiteral("详细信息"));
        QAction *actConvert = menu.addAction(QStringLiteral("转为待办"));
        QAction *actDel = menu.addAction(QStringLiteral("删除"));
        actDel->setIcon(QApplication::style()->standardIcon(QStyle::SP_TrashIcon));
        QAction *chosen = menu.exec(menuBtn->mapToGlobal(QPoint(0, menuBtn->height())));
        if (chosen == actPin)
            emit togglePinnedRequested(m_note.id);
        else if (chosen == actCopy)
            QApplication::clipboard()->setText(m_note.content);
        else if (chosen == actEdit)
            emit editRequested(m_note.id);
        else if (chosen == actCmt)
            emit commentRequested(m_note.id);
        else if (chosen == actDetails)
            emit detailsRequested(m_note.id);
        else if (chosen == actConvert)
            emit convertToTodoRequested(m_note.id);
        else if (chosen == actDel)
            emit deleteRequested(m_note.id);
    });
    header->addWidget(menuBtn);

    lay->addLayout(header);

    // ---- 内容：完整 Markdown + #标签 高亮 + 可点击任务清单 ----
    const MarkdownRenderResult md = renderMarkdown(note.content);
    auto *content = new QLabel(md.html);
    content->setWordWrap(true);
    content->setTextFormat(Qt::RichText);
    content->setTextInteractionFlags(Qt::TextBrowserInteraction);
    content->setOpenExternalLinks(false); // 手动分发：任务切换 / 外链
    content->setStyleSheet(scaleQss(QStringLiteral(
        "color: %1; background: transparent; border: none; font-size: 14px;")
        .arg(kColorFg)));
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
    // #标签（层级 tag 的段级点击，mdrender 渲染为 awtag:// 链接）
    if (link.startsWith(QLatin1String("awtag://"))) {
        emit tagClicked(link.mid(8));
        return;
    }
    const QUrl url(link);
    if (url.isValid())
        QDesktopServices::openUrl(url);
}

// 在内容下方注入「被评论/被引用笔记」预览：灰色小字 + 圆角底衬，点击可跳转
void NoteCard::setParentReference(qint64 parentId, const QString &preview)
{
    if (parentId <= 0 || preview.isEmpty())
        return;
    m_parentId = parentId;
    m_parentRef = new QLabel(QStringLiteral("↩ %1").arg(preview));
    m_parentRef->setObjectName(QStringLiteral("ParentRef"));
    m_parentRef->setWordWrap(true);
    m_parentRef->setTextInteractionFlags(Qt::NoTextInteraction);
    m_parentRef->setCursor(Qt::PointingHandCursor);
    m_parentRef->setToolTip(QStringLiteral("跳转到被评论的笔记（#%1）\n%2").arg(parentId).arg(preview));
    m_parentRef->setStyleSheet(scaleQss(QStringLiteral(
        "QLabel#ParentRef { color: %1; font-size: 12px; background: %2;"
        " border-radius: 6px; padding: 5px 8px; }"
        "QLabel#ParentRef:hover { color: %3; }")
        .arg(kColorFgMuted, kColorBgElev2, kColorAccent)));
    m_parentRef->installEventFilter(this);
    // 卡片主布局为 QVBoxLayout：预览追加在内容之后
    static_cast<QVBoxLayout *>(layout())->addWidget(m_parentRef);
}

bool NoteCard::eventFilter(QObject *obj, QEvent *event)
{
    if (obj == m_parentRef && event->type() == QEvent::MouseButtonRelease) {
        auto *me = static_cast<QMouseEvent *>(event);
        if (me->button() == Qt::LeftButton && m_parentId > 0)
            emit parentReferenceClicked(m_parentId);
        return true; // 吞掉事件，避免冒泡
    }
    return QFrame::eventFilter(obj, event);
}

// 跳转定位时的视觉反馈：边框高亮闪烁，随后恢复基础样式
// 开启动画时为平滑的「强调色脉冲」；关闭时退化为静态高亮后延迟恢复
void NoteCard::flashHighlight()
{
    const QString bg = glassBg(kColorBgElev);
    const QString radius = QStringLiteral("12px");
    if (gFxAnimations) {
        auto *anim = new QVariantAnimation(this);
        anim->setStartValue(0.0);
        anim->setEndValue(1.0);
        anim->setDuration(700);
        anim->setEasingCurve(QEasingCurve::OutCubic);
        connect(anim, &QVariantAnimation::valueChanged, this, [this, bg, radius](const QVariant &v) {
            const qreal t = v.toReal();
            const QString bc = mix(kColorBorder, kColorAccent, t);
            setStyleSheet(scaleQss(QStringLiteral(
                "QFrame#NoteCard { background: %1; border: 2px solid %2; border-radius: %3; }")
                .arg(bg, bc, radius)));
        });
        connect(anim, &QVariantAnimation::finished, this, [this] {
            if (!m_baseStyle.isEmpty())
                setStyleSheet(m_baseStyle);
        });
        anim->start(QAbstractAnimation::DeleteWhenStopped);
        return;
    }
    setStyleSheet(scaleQss(QStringLiteral(
        "QFrame#NoteCard { background: %1; border: 2px solid %2; border-radius: %3; }")
        .arg(bg, kColorAccent, radius)));
    QTimer::singleShot(1200, this, [this] {
        if (!m_baseStyle.isEmpty())
            setStyleSheet(m_baseStyle);
    });
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
    lay->setSpacing(si(10));

    auto *hint = new QLabel(QStringLiteral("提示：# 输入标签，Ctrl+Enter 或 Alt+S 提交，Esc 取消"));
    hint->setStyleSheet(scaleQss(QStringLiteral("color: %1; font-size: 11px;").arg(kColorFgMuted)));
    lay->addWidget(hint);

    m_editor = new QPlainTextEdit;
    m_editor->setPlaceholderText(QStringLiteral("写点什么… 用 #标签 归类"));
    m_editor->setPlainText(initial);
    m_editor->setStyleSheet(scaleQss(QStringLiteral(
        "QPlainTextEdit { background: %1; border: 1px solid %2; border-radius: 6px;"
        " padding: 10px; font-size: 14px; }")
                                .arg(kColorBgElev, kColorBorder)));
    lay->addWidget(m_editor, 1);

    m_suggest = new QListWidget;
    m_suggest->setMaximumHeight(si(120));
    m_suggest->setVisible(false);
    m_suggest->setStyleSheet(scaleQss(QStringLiteral(
        "QListWidget { background: %1; border: 1px solid %2; border-radius: 6px; }"
        "QListWidget::item { padding: 5px 10px; }"
        "QListWidget::item:selected { background: %3; }")
                                 .arg(kColorBgElev2, kColorBorder, kColorAccent)));
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
    lay->setSpacing(si(8));

    m_list = new QListWidget;
    m_list->setStyleSheet(scaleQss(QStringLiteral(
        "QListWidget { background: %1; border: 1px solid %2; border-radius: 6px; }")
                              .arg(kColorBgElev, kColorBorder)));
    lay->addWidget(m_list, 1);

    auto *row = new QHBoxLayout;
    m_input = new QPlainTextEdit;
    m_input->setPlaceholderText(QStringLiteral("添加评论… 以 [[时间戳]] 关联其它笔记"));
    m_input->setMaximumHeight(si(70));
    row->addWidget(m_input, 1);
    auto *btn = new QPushButton(QStringLiteral("发表"));
    btn->setStyleSheet(scaleQss(QStringLiteral("QPushButton { background: %1; color: white; border: none;"
                                               " border-radius: 6px; padding: 6px 16px; }")
                           .arg(kColorAccent)));
    connect(btn, &QPushButton::clicked, this, &QDialog::accept);
    row->addWidget(btn);
    lay->addLayout(row);

    // 打开对话框时焦点直接落在输入框，避免用户额外点击
    m_input->setFocus();
}

void CommentsDialog::showEvent(QShowEvent *event)
{
    QDialog::showEvent(event);
    // show 时窗口可能重置焦点，再次确保输入框获得焦点
    m_input->setFocus(Qt::OtherFocusReason);
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

// ------------------------------------------------------------------ //
// 笔记详细信息对话框：元信息区（添加/更新/同步时间、来源设备、版本等）+ 历史版本区
NoteDetailsDialog::NoteDetailsDialog(const Note &note, QWidget *parent)
    : QDialog(parent), m_noteId(note.id)
{
    setWindowTitle(QStringLiteral("笔记详情 · #%1").arg(note.id));
    setModal(true);
    resize(640, 620);

    auto *lay = new QVBoxLayout(this);
    lay->setSpacing(si(8));

    // ---- 元信息区 ----
    auto *infoBox = new QFrame;
    infoBox->setObjectName(QStringLiteral("DetailsBox"));
    infoBox->setStyleSheet(scaleQss(QStringLiteral(
        "QFrame#DetailsBox { background: %1; border: 1px solid %2; border-radius: 8px; }")
        .arg(kColorBgElev, kColorBorder)));
    auto *grid = new QGridLayout(infoBox);
    grid->setContentsMargins(si(12), si(10), si(12), si(10));
    grid->setHorizontalSpacing(si(14));
    grid->setVerticalSpacing(si(6));
    grid->setColumnStretch(1, 1);

    auto addRow = [&grid](int row, const QString &label, const QString &value, const char *color) {
        auto *l = new QLabel(label);
        l->setStyleSheet(scaleQss(QStringLiteral(
            "color: %1; font-size: 12px; background: transparent; border: none;")
            .arg(kColorFgMuted)));
        auto *v = new QLabel(value);
        v->setWordWrap(true);
        v->setTextInteractionFlags(Qt::TextSelectableByMouse);
        v->setStyleSheet(scaleQss(QStringLiteral(
            "color: %1; font-size: 12px; background: transparent; border: none;")
            .arg(color)));
        grid->addWidget(l, row, 0, Qt::AlignTop);
        grid->addWidget(v, row, 1);
        return v;
    };

    static const QString kTimeFmt = QStringLiteral("yyyy-MM-dd HH:mm:ss");
    addRow(0, QStringLiteral("笔记 ID"), QStringLiteral("#%1").arg(note.id), kColorFg);
    addRow(1, QStringLiteral("添加时间"), formatLocal(note.createdAt, kTimeFmt), kColorFg);
    addRow(2, QStringLiteral("更新时间"), formatLocal(note.updatedAt, kTimeFmt), kColorFg);
    if (note.syncedAt.isEmpty())
        addRow(3, QStringLiteral("最后同步"), QStringLiteral("未同步"), kColorWarn);
    else
        addRow(3, QStringLiteral("最后同步"), formatLocal(note.syncedAt, kTimeFmt), kColorFg);

    // 来源设备：本机笔记显示「本机」，其余显示原始 device_id（可被 setDeviceName 回填）
    m_deviceId = note.deviceId;
    m_deviceValue = new QLabel;
    if (m_deviceId.isEmpty())
        m_deviceValue->setText(QStringLiteral("未知"));
    else if (m_deviceId == deviceId())
        m_deviceValue->setText(QStringLiteral("本机"));
    else
        m_deviceValue->setText(m_deviceId);
    if (!m_deviceId.isEmpty())
        m_deviceValue->setToolTip(QStringLiteral("device_id: %1").arg(m_deviceId));
    m_deviceValue->setWordWrap(true);
    m_deviceValue->setTextInteractionFlags(Qt::TextSelectableByMouse);
    m_deviceValue->setStyleSheet(scaleQss(QStringLiteral(
        "color: %1; font-size: 12px; background: transparent; border: none;")
        .arg(kColorFg)));
    auto *devLabel = new QLabel(QStringLiteral("来源设备"));
    devLabel->setStyleSheet(scaleQss(QStringLiteral(
        "color: %1; font-size: 12px; background: transparent; border: none;")
        .arg(kColorFgMuted)));
    grid->addWidget(devLabel, 4, 0, Qt::AlignTop);
    grid->addWidget(m_deviceValue, 4, 1);

    addRow(5, QStringLiteral("当前版本"), QStringLiteral("v%1").arg(note.version), kColorFg);
    // 标签：层级 tag 渲染为面包屑（每段独立可点，点击按「到该段为止的路径」筛选）
    {
        auto *tl = new QLabel(QStringLiteral("标签"));
        tl->setStyleSheet(scaleQss(QStringLiteral(
            "color: %1; font-size: 12px; background: transparent; border: none;")
            .arg(kColorFgMuted)));
        m_tagsValue = new QLabel;
        m_tagsValue->setWordWrap(true);
        m_tagsValue->setTextFormat(Qt::RichText);
        m_tagsValue->setTextInteractionFlags(Qt::TextBrowserInteraction);
        m_tagsValue->setOpenExternalLinks(false);
        m_tagsValue->setStyleSheet(scaleQss(QStringLiteral(
            "color: %1; font-size: 12px; background: transparent; border: none;")
            .arg(kColorFg)));
        if (note.tags.isEmpty()) {
            m_tagsValue->setText(QStringLiteral("无"));
        } else {
            QStringList parts;
            for (const QString &t : note.tags) {
                const QStringList segs = t.split(QLatin1Char('/'), Qt::SkipEmptyParts);
                QString acc;
                QStringList segLinks;
                for (const QString &seg : segs) {
                    acc = acc.isEmpty() ? seg : acc + QLatin1Char('/') + seg;
                    segLinks << QStringLiteral("<a href='awtag://%1' style='color:#7fb3ff;text-decoration:none;'>%2</a>")
                                    .arg(acc.toHtmlEscaped(), seg.toHtmlEscaped());
                }
                parts << segLinks.join(QStringLiteral("<span style='color:%1;'> / </span>").arg(kColorFgMuted));
            }
            m_tagsValue->setText(parts.join(QStringLiteral(",&nbsp; ")));
        }
        connect(m_tagsValue, &QLabel::linkActivated, this, &NoteDetailsDialog::tagClicked);
        grid->addWidget(tl, 6, 0, Qt::AlignTop);
        grid->addWidget(m_tagsValue, 6, 1);
    }

    QString status = QStringLiteral("正常");
    const char *statusColor = kColorOk;
    if (note.conflict) {
        status = QStringLiteral("存在同步冲突");
        statusColor = kColorDanger;
    } else if (note.pendingOp == QLatin1String("create")) {
        status = QStringLiteral("待同步（新建）");
        statusColor = kColorWarn;
    } else if (note.pendingOp == QLatin1String("update")) {
        status = QStringLiteral("待同步（修改）");
        statusColor = kColorWarn;
    } else if (note.pendingOp == QLatin1String("delete")) {
        status = QStringLiteral("待同步（删除）");
        statusColor = kColorWarn;
    }
    addRow(7, QStringLiteral("状态"), status, statusColor);
    addRow(8, QStringLiteral("内容长度"),
           QStringLiteral("%1 字符").arg(note.content.length()), kColorFg);

    lay->addWidget(infoBox);

    // ---- 历史版本区（自原「历史版本」对话框迁移）----
    auto *histLabel = new QLabel(QStringLiteral("历史版本"));
    histLabel->setStyleSheet(scaleQss(QStringLiteral(
        "color: %1; font-size: 13px; font-weight: 600; background: transparent; border: none;")
        .arg(kColorFg)));
    lay->addWidget(histLabel);

    auto *split = new QHBoxLayout;

    m_list = new QListWidget;
    m_list->setMaximumWidth(si(240));
    m_list->setStyleSheet(scaleQss(QStringLiteral(
        "QListWidget { background: %1; border: 1px solid %2; border-radius: 6px; }")
                              .arg(kColorBgElev, kColorBorder)));
    split->addWidget(m_list);

    m_preview = new QPlainTextEdit;
    m_preview->setReadOnly(true);
    m_preview->setPlaceholderText(QStringLiteral("（选择一个版本查看内容）"));
    m_preview->setStyleSheet(scaleQss(QStringLiteral(
        "QPlainTextEdit { background: %1; border: 1px solid %2; border-radius: 6px; }")
                                  .arg(kColorBgElev, kColorBorder)));
    split->addWidget(m_preview, 1);

    lay->addLayout(split, 1);

    auto *row = new QHBoxLayout;
    auto *btnCopy = new QPushButton(QStringLiteral("复制内容"));
    connect(btnCopy, &QPushButton::clicked, this, &NoteDetailsDialog::onCopyClicked);
    row->addWidget(btnCopy);

    auto *btnConvert = new QPushButton(QStringLiteral("转为待办"));
    connect(btnConvert, &QPushButton::clicked, this, [this] { emit convertRequested(m_noteId); });
    row->addWidget(btnConvert);

    m_btnRestore = new QPushButton(QStringLiteral("恢复此版本"));
    m_btnRestore->setEnabled(false);
    m_btnRestore->setStyleSheet(scaleQss(QStringLiteral(
        "QPushButton { background: %1; color: white; border: none;"
        " border-radius: 6px; padding: 6px 16px; }")
                                    .arg(kColorAccent)));
    connect(m_btnRestore, &QPushButton::clicked, this, &NoteDetailsDialog::onRestoreClicked);
    row->addWidget(m_btnRestore);

    auto *btnClose = new QPushButton(QStringLiteral("关闭"));
    connect(btnClose, &QPushButton::clicked, this, &QDialog::reject);
    row->addWidget(btnClose);
    row->addStretch(1);
    lay->addLayout(row);

    connect(m_list, &QListWidget::currentRowChanged, this, &NoteDetailsDialog::onCurrentRowChanged);
}

void NoteDetailsDialog::setHistory(const QList<NoteHistory> &items)
{
    m_items = items;
    m_list->clear();
    if (items.isEmpty()) {
        m_list->addItem(QStringLiteral("（暂无历史版本）"));
        m_preview->clear();
        m_btnRestore->setEnabled(false);
        return;
    }
    for (const NoteHistory &h : items) {
        const QString ts = formatLocal(h.snapshotAt.isEmpty() ? h.updatedAt : h.snapshotAt);
        auto *item = new QListWidgetItem(QStringLiteral("v%1  %2").arg(h.version).arg(ts));
        item->setToolTip(h.deviceId.isEmpty()
                             ? h.content
                             : QStringLiteral("设备 %1\n──────\n%2").arg(h.deviceId, h.content));
        m_list->addItem(item);
    }
    m_list->setCurrentRow(0);
}

void NoteDetailsDialog::setHistoryUnavailable(const QString &reason)
{
    m_items.clear();
    m_list->clear();
    m_list->addItem(reason);
    m_preview->clear();
    m_preview->setPlaceholderText(QStringLiteral("（暂无可查看的历史版本）"));
    m_btnRestore->setEnabled(false);
}

void NoteDetailsDialog::setDeviceName(const QString &name)
{
    if (!m_deviceValue || name.isEmpty())
        return;
    m_deviceValue->setText(name);
}

void NoteDetailsDialog::onCurrentRowChanged(int row)
{
    if (row < 0 || row >= m_items.size()) {
        m_preview->clear();
        m_btnRestore->setEnabled(false);
        return;
    }
    m_preview->setPlainText(m_items.at(row).content);
    m_btnRestore->setEnabled(true);
}

void NoteDetailsDialog::onCopyClicked()
{
    const int row = m_list->currentRow();
    if (row < 0 || row >= m_items.size())
        return;
    QApplication::clipboard()->setText(m_items.at(row).content);
}

void NoteDetailsDialog::onRestoreClicked()
{
    const int row = m_list->currentRow();
    if (row < 0 || row >= m_items.size())
        return;
    emit restoreRequested(m_noteId, m_items.at(row).content);
    accept();
}

} // namespace awqtui
