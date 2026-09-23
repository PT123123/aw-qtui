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
#include <QLineEdit>
#include <QListWidgetItem>
#include <QMenu>
#include <QMouseEvent>
#include <QPointer>
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

// 当前活跃的「可撤销」气泡（同一时刻只留一个）
static QPointer<QWidget> g_actionToast;

void showActionToast(const QString &text, const QString &actionText,
                     std::function<void()> onAction, int ms, QScreen *anchorScreen,
                     std::function<void()> onExpire)
{
    // 新气泡顶掉旧的：撤销是针对「刚才那一下」的，堆一屏气泡既乱又没必要
    if (g_actionToast)
        g_actionToast->close();

    QScreen *screen = anchorScreen;
    if (!screen)
        screen = QGuiApplication::screenAt(QCursor::pos());
    if (!screen)
        screen = QGuiApplication::primaryScreen();
    if (!screen)
        return;

    // WindowDoesNotAcceptFocus：点「撤销」时不会把主窗口顶成非激活态（标题栏不会变灰）
    auto *toast = new QWidget(nullptr, Qt::Tool | Qt::FramelessWindowHint
                                         | Qt::WindowStaysOnTopHint
                                         | Qt::WindowDoesNotAcceptFocus);
    toast->setAttribute(Qt::WA_ShowWithoutActivating);
    toast->setAttribute(Qt::WA_TranslucentBackground);
    toast->setAttribute(Qt::WA_DeleteOnClose);
    g_actionToast = toast;

    auto *outer = new QHBoxLayout(toast);
    outer->setContentsMargins(0, 0, 0, 0);

    auto *card = new QFrame(toast);
    card->setObjectName(QStringLiteral("ToastCard"));
    card->setStyleSheet(scaleQss(QStringLiteral(
        "#ToastCard { background: %1; border: 1px solid %2; border-radius: 10px; }")
        .arg(gTheme->bgElev, gTheme->border)));
    outer->addWidget(card);

    auto *lay = new QHBoxLayout(card);
    lay->setContentsMargins(si(16), si(9), si(8), si(9));
    lay->setSpacing(si(12));

    auto *label = new QLabel(text, card);
    label->setStyleSheet(scaleQss(QStringLiteral(
        "QLabel { color: %1; background: transparent; font-size: 13px; }").arg(gTheme->fg)));
    lay->addWidget(label);

    auto *btn = new QPushButton(actionText, card);
    btn->setCursor(Qt::PointingHandCursor);
    btn->setStyleSheet(scaleQss(QStringLiteral(
        "QPushButton { color: %1; background: transparent; border: none; border-radius: 6px;"
        " padding: 4px 10px; font-size: 13px; font-weight: 600; }"
        "QPushButton:hover { background: %2; }")
        .arg(kColorAccent)
        .arg(withAlpha(kColorAccent, 0.18))));
    lay->addWidget(btn);

    toast->adjustSize();
    const QRect avail = screen->availableGeometry();
    toast->move(avail.x() + (avail.width() - toast->width()) / 2,
                avail.y() + avail.height() - toast->height() - si(90));
    toast->show();
    toast->raise();

    auto fadeOut = [toast, onExpire] {
        if (toast->property("fading").toBool())
            return;
        toast->setProperty("fading", true);
        // 倒计时真正走完：交给调用方「到期提交」（悬停冻结期间不会走到这里）
        if (onExpire)
            onExpire();
        auto *anim = new QVariantAnimation(toast);
        anim->setDuration(200);
        anim->setStartValue(1.0);
        anim->setEndValue(0.0);
        QObject::connect(anim, &QVariantAnimation::valueChanged, toast, [toast](const QVariant &v) {
            toast->setWindowOpacity(v.toDouble());
        });
        QObject::connect(anim, &QVariantAnimation::finished, toast, [toast] { toast->close(); });
        anim->start(QAbstractAnimation::DeleteWhenStopped);
    };

    auto *life = new QTimer(toast);
    life->setSingleShot(true);
    life->setInterval(ms);
    QObject::connect(life, &QTimer::timeout, toast, fadeOut);
    life->start();

    // 悬停冻结：气泡的子控件会截走 Enter/Leave，所以直接问「光标是否还在气泡矩形内」
    auto *guard = new QTimer(toast);
    guard->setInterval(120);
    QObject::connect(guard, &QTimer::timeout, toast, [toast, life] {
        if (toast->property("fading").toBool())
            return;
        const bool inside = toast->rect().contains(toast->mapFromGlobal(QCursor::pos()));
        if (inside) {
            life->stop();
        } else if (!life->isActive()) {
            life->start();
        }
    });
    guard->start();

    QObject::connect(btn, &QPushButton::clicked, toast, [toast, onAction] {
        if (toast->property("fading").toBool())
            return;
        toast->setProperty("fading", true);   // 先封口，避免 onAction 触发重建时再次进来
        if (onAction)
            onAction();
        toast->close();
    });
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
// 虚拟化池实现
CardPool::~CardPool()
{
    // 池内卡片的 parent 已被置空（刻意脱离 wrap 管辖，才能跨 clear() 复用），
    // 它们不在任何 QObject 父子链上，不会自动回收 —— 必须在这里显式销毁。
    for (NoteCard *card : m_pool)
        delete card;
    m_pool.clear();
}

NoteCard *CardPool::acquire(const Note &note, bool pinned)
{
    syncScale();
    NoteCard *card;
    if (!m_pool.isEmpty()) {
        card = m_pool.takeLast(); // 弹出最近入池的
    } else {
        // 池空时创建新卡（parent=null 表示由外部负责生命周期）
        card = new NoteCard(note, pinned, nullptr);
        // 只给新建的卡挂一次信号：池里取出的旧卡连接还在，重挂会让一次点击触发多遍槽
        if (m_wiring)
            m_wiring(card);
    }
    card->setNote(note, pinned); // 重绑定内容
    return card;
}

// 池里的卡是按某个 gUiScale 构造的（几何 / scaleQss 内联样式都在构造函数里定死），
// 比例一变，池内容整体作废 —— 否则缩放后复用旧卡，卡片会一直停在旧比例（实测 bug）。
void CardPool::syncScale()
{
    if (m_pool.isEmpty() && m_scaleKey < 0.0) {
        m_scaleKey = gUiScale;
        return;
    }
    if (m_scaleKey >= 0.0 && qFuzzyCompare(m_scaleKey, gUiScale))
        return;
    clear();
    m_scaleKey = gUiScale;
}

void CardPool::release(NoteCard *card)
{
    if (!card)
        return;
    // 重置 parent 以便外部 deleteLater 或重新设置 parent
    card->setParent(nullptr);
    syncScale();
    // 跨比例的卡不复用：几何/样式已经定死在旧比例上，重新挂回列表只会得到一个
    // 「内容更新了但尺寸还是老样子」的卡片
    if (!qFuzzyCompare(card->builtScale(), gUiScale)) {
        card->deleteLater();
        return;
    }
    if (m_pool.size() >= m_maxSize) {
        // 池满：销毁最老的
        NoteCard *oldest = m_pool.takeFirst();
        oldest->deleteLater();
    }
    m_pool.append(card);
}

void CardPool::releaseAll(const QMap<int, NoteCard *> &active)
{
    // 统一走 release()：此前这里是复制粘贴的第二份实现，改漏一处就会让池里混进旧比例的卡
    for (NoteCard *card : active)
        release(card);
}

void CardPool::discardAll()
{
    m_pool.clear();
}

void CardPool::clear()
{
    for (NoteCard *card : m_pool)
        card->deleteLater();
    m_pool.clear();
}

// ------------------------------------------------------------------ //
// CardDelegate：虚拟化列表代理，把模型数据映射为池化的 NoteCard
CardDelegate::CardDelegate(CardPool *pool, QObject *parent)
    : QAbstractItemDelegate(parent), m_pool(pool)
{}

void CardDelegate::setBufferSize(int rows)
{
    m_bufferRows = rows;
}

void CardDelegate::paint(QPainter *painter, const QStyleOptionViewItem &option,
                         const QModelIndex &index) const
{
    // 绘制完全由 setNote() 绑定的 NoteCard widget 自己负责（WA_PaintOnScreen 背景透明）
    // QAbstractItemDelegate::paint 是空操作，所有内容都通过 updateEditorData -> 卡片自己重绘
    // 注意：本 paint() 实现为占位符，真实卡片绘制由 NoteCard 构造函数/makeCard 生成，
    //       代理主要负责 sizeHint 布局计算。True virtualization 通过 widget 池 + setItemWidget 实现。
    Q_UNUSED(painter);
    Q_UNUSED(option);
    Q_UNUSED(index);
}

QSize CardDelegate::sizeHint(const QStyleOptionViewItem &option, const QModelIndex &index) const
{
    // 卡片高度由 wrap 布局决定，宽度跟列表一致
    // 这里返回一个估计值；实际高度由 updateEditorGeometry 校准
    Q_UNUSED(option);
    Q_UNUSED(index);
    return QSize(option.rect.width(), si(90));
}

// ------------------------------------------------------------------ //
// NoteCard —— MoeMemos 风格卡片：头部（相对时间 + 置顶/状态图标 + ⋯ 菜单）+ Markdown 内容
NoteCard::NoteCard(const Note &note, bool pinned, QWidget *parent)
    : QFrame(parent), m_note(note), m_pinned(pinned)
{
    setObjectName(QStringLiteral("NoteCard"));
    // 记下构造时的缩放比：本类所有尺寸/内联样式都在构造函数里按 gUiScale 算死
    // （si() / scaleQss），setNote() 复用时不重算，故池子必须跨比例丢弃（见 CardPool::syncScale）
    m_builtScale = gUiScale;
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
    // 阴影改为 hover-only（event() 中按需创建/销毁），节省 GPU 资源

    // 根布局：勾选框列（仅多选模式显示）| 内容列。勾选框设了 WA_TransparentForMouseEvents，
    // 点它和点卡片空白处走同一条 mousePressEvent，判定逻辑只有一处。
    auto *lay = new QHBoxLayout(this);
    lay->setContentsMargins(si(14), si(10), si(10), si(12));
    lay->setSpacing(si(10));

    m_check = new QLabel(this);
    m_check->setObjectName(QStringLiteral("NoteCheck"));
    m_check->setFixedSize(si(18), si(18));
    m_check->setAlignment(Qt::AlignCenter);
    m_check->setAttribute(Qt::WA_TransparentForMouseEvents);
    m_check->hide();
    lay->addWidget(m_check, 0, Qt::AlignTop);

    m_col = new QVBoxLayout;
    m_col->setContentsMargins(0, 0, 0, 0);
    m_col->setSpacing(si(6));
    lay->addLayout(m_col, 1);

    // ---- 头部行：时间（左）+ 状态图标 + ⋯ 菜单（右） ----
    auto *header = new QHBoxLayout;
    header->setSpacing(si(6));

    m_timeLabel = new QLabel(formatRelative(note.updatedAt.isEmpty() ? note.createdAt : note.updatedAt));
    m_timeLabel->setToolTip(QStringLiteral("创建 %1\n更新 %2")
                                .arg(formatLocal(note.createdAt), formatLocal(note.updatedAt)));
    m_timeLabel->setStyleSheet(scaleQss(QStringLiteral(
        "color: %1; font-size: 11px; background: transparent; border: none;")
        .arg(kColorFgMuted)));
    // 窄卡（主导航展开等）时允许时间标签被压缩，避免把右上角 ⋯ 按钮挤出可视范围
    m_timeLabel->setMinimumWidth(0);
    header->addWidget(m_timeLabel);

    if (m_pinned) {
        m_pinLabel = new QLabel(QStringLiteral("⚑"));
        m_pinLabel->setToolTip(QStringLiteral("已置顶"));
        m_pinLabel->setStyleSheet(scaleQss(QStringLiteral(
            "color: %1; font-size: 13px; background: transparent; border: none;")
            .arg(kColorWarn)));
        header->addWidget(m_pinLabel);
    }
    if (note.conflict) {
        m_conflictLabel = new QLabel(QStringLiteral("⚠"));
        m_conflictLabel->setToolTip(QStringLiteral("存在同步冲突"));
        m_conflictLabel->setStyleSheet(scaleQss(QStringLiteral(
            "color: %1; font-size: 12px; background: transparent; border: none;")
            .arg(kColorWarn)));
        header->addWidget(m_conflictLabel);
    }
    if (!note.pendingOp.isEmpty()) {
        const bool del = note.pendingOp == QLatin1String("delete");
        m_pendingLabel = new QLabel(del ? QStringLiteral("🗑") : QStringLiteral("⏳"));
        m_pendingLabel->setToolTip(del ? QStringLiteral("待同步删除") : QStringLiteral("待同步"));
        m_pendingLabel->setStyleSheet(scaleQss(QStringLiteral(
            "color: %1; font-size: 12px; background: transparent; border: none;")
            .arg(kColorWarn)));
        header->addWidget(m_pendingLabel);
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
    m_menuBtn = menuBtn;

    m_col->addLayout(header);

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
    m_col->addWidget(content);
    m_content = content;
}

// 虚拟化池回收时重新绑定：保留完整 widget tree，只替换内容字段
void NoteCard::setNote(const Note &note, bool pinned)
{
    m_note = note;
    m_pinned = pinned;

    // 更新时间标签
    if (m_timeLabel) {
        m_timeLabel->setText(formatRelative(note.updatedAt.isEmpty() ? note.createdAt : note.updatedAt));
        m_timeLabel->setToolTip(QStringLiteral("创建 %1\n更新 %2")
                                    .arg(formatLocal(note.createdAt), formatLocal(note.updatedAt)));
    }

    // 置顶图标
    if (m_pinLabel) {
        m_pinLabel->setVisible(pinned);
        if (pinned)
            m_pinLabel->setToolTip(QStringLiteral("已置顶"));
    }

    // 冲突标记
    if (m_conflictLabel)
        m_conflictLabel->setVisible(note.conflict);

    // pending 操作标记
    if (m_pendingLabel) {
        if (!note.pendingOp.isEmpty()) {
            const bool del = note.pendingOp == QLatin1String("delete");
            m_pendingLabel->setText(del ? QStringLiteral("🗑") : QStringLiteral("⏳"));
            m_pendingLabel->setToolTip(del ? QStringLiteral("待同步删除") : QStringLiteral("待同步"));
            m_pendingLabel->setVisible(true);
        } else {
            m_pendingLabel->setVisible(false);
        }
    }

    // 正文 Markdown（最贵的一步）
    if (m_content) {
        const MarkdownRenderResult md = renderMarkdown(note.content);
        m_content->setText(md.html);
    }

    // 引用预览重置（setParentReference 会自行处理 m_parentRef 替换）
    if (m_parentRef) {
        m_parentRef->deleteLater();
        m_parentRef = nullptr;
        m_parentId = 0;
    }

    // 多选状态保持，只刷新勾选框样式（不会改变勾选状态）
    if (m_check)
        refreshCheckStyle();
}

// 多选模式：勾选框出现在左侧，⋯ 菜单让位，正文退出文本交互
// （TextBrowserInteraction 会把鼠标事件吃在 QLabel 里，点击就到不了卡片的 mousePressEvent）。
void NoteCard::setSelectionMode(bool on)
{
    if (m_selectMode == on)
        return;
    m_selectMode = on;
    if (m_check)
        m_check->setVisible(on);
    if (m_menuBtn)
        m_menuBtn->setVisible(!on);
    if (m_content) {
        m_content->setTextInteractionFlags(on ? Qt::NoTextInteraction
                                              : Qt::TextBrowserInteraction);
        m_content->setCursor(on ? Qt::PointingHandCursor : Qt::ArrowCursor);
    }
    if (!on)
        setChecked(false);
    else
        refreshCheckStyle();
}

void NoteCard::setChecked(bool on)
{
    if (m_checked == on)
        return;
    m_checked = on;
    refreshCheckStyle();
}

void NoteCard::refreshCheckStyle()
{
    if (!m_check)
        return;
    if (m_checked) {
        m_check->setText(QStringLiteral("✓"));
        m_check->setStyleSheet(scaleQss(QStringLiteral(
            "QLabel { background: %1; border: 2px solid %1; border-radius: 9px;"
            " color: #ffffff; font-size: 11px; font-weight: 700; }")
            .arg(kColorAccent)));
    } else {
        m_check->setText(QString());
        m_check->setStyleSheet(scaleQss(QStringLiteral(
            "QLabel { background: transparent; border: 2px solid %1; border-radius: 9px; }")
            .arg(withAlpha(kColorFgMuted, 0.55))));
    }
    // 选中整卡描边强调（QSS 不认 :checked，直接覆盖基础样式；取消时还原）。
    // 这里不能按 m_selectMode 判断「要不要还原」：退出多选时 setSelectionMode 已经把
    // m_selectMode 置 false 了，再调 setChecked(false) 就会跳过还原、卡片留着选中描边。
    if (!m_baseStyle.isEmpty()) {
        if (m_checked)
            setStyleSheet(scaleQss(QStringLiteral(
                "QFrame#NoteCard { background: %1; border: 1px solid %2; border-radius: 12px; }")
                .arg(glassBg(kColorBgElev), kColorAccent)));
        else
            setStyleSheet(m_baseStyle);
    }
}

void NoteCard::mousePressEvent(QMouseEvent *event)
{
    // 多选模式下点整卡 = 切换勾选（Ctrl/Shift 的语义交给宿主：加选 / 范围选）
    if (m_selectMode && event->button() == Qt::LeftButton) {
        emit selectionClicked(m_note.id, event->modifiers());
        event->accept();
        return;
    }
    QFrame::mousePressEvent(event);
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
    // 预览追加在内容列之后
    if (m_col)
        m_col->addWidget(m_parentRef);
}

bool NoteCard::eventFilter(QObject *obj, QEvent *event)
{
    if (obj == m_parentRef && event->type() == QEvent::MouseButtonRelease) {
        auto *me = static_cast<QMouseEvent *>(event);
        if (me->button() == Qt::LeftButton) {
            // 多选模式：引用预览同样是「卡片的一部分」，点它切换勾选而不是跳转
            if (m_selectMode)
                emit selectionClicked(m_note.id, me->modifiers());
            else if (m_parentId > 0)
                emit parentReferenceClicked(m_parentId);
        }
        return true; // 吞掉事件，避免冒泡
    }
    return QFrame::eventFilter(obj, event);
}

bool NoteCard::event(QEvent *event)
{
    switch (event->type()) {
    case QEvent::Enter:
        // 悬浮时按需创建投影阴影（关闭全局阴影时 makeDropShadow 返回 nullptr）
        if (!m_shadow)
            m_shadow = makeDropShadow(this);
        break;
    case QEvent::Leave:
        // 离开时销毁阴影（setGraphicsEffect(nullptr) 会删除旧 effect）
        if (m_shadow) {
            delete m_shadow;
            m_shadow = nullptr;
        }
        break;
    default:
        break;
    }
    return QFrame::event(event);
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
    // 模态由调用方决定：exec() 自带应用模态，快速记录走 show()（非模态独立窗口）
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
// TagPickerDialog —— 批量加/去标签（笔记页多选后调用）
TagPickerDialog::TagPickerDialog(const QStringList &known, const QStringList &noteTags,
                                 int noteCount, QWidget *parent)
    : QDialog(parent), m_known(known), m_noteTags(noteTags), m_noteCount(noteCount)
{
    setWindowTitle(QStringLiteral("批量标签"));
    setModal(true);
    resize(420, 480);

    auto *lay = new QVBoxLayout(this);
    lay->setSpacing(si(8));

    m_title = new QLabel(this);
    m_title->setStyleSheet(scaleQss(QStringLiteral("QLabel { color: %1; font-size: 13px; }")
                                        .arg(kColorFgMuted)));
    lay->addWidget(m_title);

    // 模式切换：添加 / 移除
    auto *tabs = new QHBoxLayout;
    tabs->setSpacing(si(6));
    const QString tabQss = scaleQss(QStringLiteral(
        "QPushButton { background: transparent; border: 1px solid %1; border-radius: 8px;"
        " color: %2; padding: 5px 14px; font-size: 12px; }"
        "QPushButton:checked { background: %3; border-color: %3; color: #ffffff; font-weight: 600; }")
        .arg(kColorBorder, kColorFgMuted, kColorAccent));
    m_tabAdd = new QPushButton(QStringLiteral("添加标签"), this);
    m_tabRemove = new QPushButton(QStringLiteral("移除标签"), this);
    for (auto *b : { m_tabAdd, m_tabRemove }) {
        b->setCheckable(true);
        b->setCursor(Qt::PointingHandCursor);
        b->setStyleSheet(tabQss);
        tabs->addWidget(b);
    }
    tabs->addStretch(1);
    lay->addLayout(tabs);

    m_newTag = new QLineEdit(this);
    m_newTag->setPlaceholderText(QStringLiteral("新标签名（可留空，从下方选择）…"));
    lay->addWidget(m_newTag);

    m_search = new QLineEdit(this);
    m_search->setPlaceholderText(QStringLiteral("搜索标签…"));
    lay->addWidget(m_search);

    m_list = new QListWidget(this);
    m_list->setStyleSheet(scaleQss(QStringLiteral(
        "QListWidget { background: %1; border: 1px solid %2; border-radius: 8px; outline: none; }"
        "QListWidget::item { padding: 6px 10px; color: %3; }"
        "QListWidget::item:selected { background: %4; color: %5; }")
        .arg(kColorBgElev, kColorBorder, kColorFg,
             withAlpha(kColorAccent, 0.18), kColorAccent)));
    lay->addWidget(m_list, 1);

    auto *btns = new QHBoxLayout;
    btns->addStretch(1);
    auto *cancel = new QPushButton(QStringLiteral("取消"), this);
    cancel->setStyleSheet(scaleQss(QStringLiteral(
        "QPushButton { background: transparent; border: 1px solid %1; border-radius: 8px;"
        " color: %2; padding: 6px 16px; font-size: 12px; }"
        "QPushButton:hover { border-color: %3; color: %3; }")
        .arg(kColorBorder, kColorFgMuted, kColorAccent)));
    auto *ok = new QPushButton(QStringLiteral("确定"), this);
    ok->setStyleSheet(scaleQss(QStringLiteral(
        "QPushButton { background: %1; color: #ffffff; border: none; border-radius: 8px;"
        " padding: 6px 18px; font-size: 12px; font-weight: 600; }")
        .arg(kColorAccent)));
    connect(cancel, &QPushButton::clicked, this, &QDialog::reject);
    connect(ok, &QPushButton::clicked, this, &TagPickerDialog::acceptCurrent);
    btns->addWidget(cancel);
    btns->addWidget(ok);
    lay->addLayout(btns);

    connect(m_tabAdd, &QPushButton::clicked, this, [this] { setMode(Mode::Add); });
    connect(m_tabRemove, &QPushButton::clicked, this, [this] { setMode(Mode::Remove); });
    connect(m_search, &QLineEdit::textChanged, this, [this](const QString &) { refreshList(); });
    connect(m_list, &QListWidget::itemDoubleClicked, this,
            [this](QListWidgetItem *) { acceptCurrent(); });
    connect(m_newTag, &QLineEdit::returnPressed, this, &TagPickerDialog::acceptCurrent);

    setMode(Mode::Add);
}

void TagPickerDialog::setMode(Mode m)
{
    m_mode = m;
    const bool add = (m == Mode::Add);
    m_tabAdd->setChecked(add);
    m_tabRemove->setChecked(!add);
    m_newTag->setVisible(add);
    m_title->setText(add ? QStringLiteral("给选中的 %1 条笔记添加同一个标签").arg(m_noteCount)
                         : QStringLiteral("从选中的 %1 条笔记移除某个标签").arg(m_noteCount));
    refreshList();
    (add ? static_cast<QWidget *>(m_newTag) : static_cast<QWidget *>(m_search))->setFocus();
}

void TagPickerDialog::refreshList()
{
    if (!m_list)
        return;
    const bool add = (m_mode == Mode::Add);
    const QStringList &src = add ? m_known : m_noteTags;
    const QString filter = m_search->text().trimmed();
    m_list->clear();
    for (const QString &t : src) {
        if (!filter.isEmpty() && !t.contains(filter, Qt::CaseInsensitive))
            continue;
        m_list->addItem(t);
    }
    if (m_list->count() == 0) {
        // 占位项不可选（NoItemFlags），只作提示，tag() 不会把它当成标签
        auto *ph = new QListWidgetItem(add
                                           ? QStringLiteral("（没有匹配的标签，可直接在上方输入新标签）")
                                           : QStringLiteral("（选中的笔记没有匹配的标签）"));
        ph->setFlags(Qt::NoItemFlags);
        m_list->addItem(ph);
    }
}

void TagPickerDialog::acceptCurrent()
{
    // 空标签直接忽略（不关窗），避免误点确定后弹出「无操作」的空结果
    if (tag().isEmpty())
        return;
    accept();
}

QString TagPickerDialog::tag() const
{
    QString t;
    if (m_mode == Mode::Add)
        t = m_newTag->text().trimmed();
    if (t.isEmpty()) {
        QListWidgetItem *item = m_list->currentItem();
        if (item && (item->flags() & Qt::ItemIsEnabled))
            t = item->text().trimmed();
    }
    if (t.startsWith(QLatin1Char('#')))
        t = t.mid(1).trimmed();
    return t;
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
