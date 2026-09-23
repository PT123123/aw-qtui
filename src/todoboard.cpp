// todoboard.cpp —— 任务页「平铺（看板）」视图实现
#include "todoboard.h"

#include "theme.h"

#include <QAbstractAnimation>
#include <QApplication>
#include <QCheckBox>
#include <QCursor>
#include <QDrag>
#include <QDragEnterEvent>
#include <QDragLeaveEvent>
#include <QDragMoveEvent>
#include <QDropEvent>
#include <QEnterEvent>
#include <QFrame>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QMimeData>
#include <QMouseEvent>
#include <QPainter>
#include <QScrollArea>
#include <QScrollBar>
#include <QStyle>
#include <QVBoxLayout>

#include <algorithm>

namespace awqtui {

namespace {

const char *kMimeName = "application/x-awqtui-taskids";

// 看板里没有清单归属的任务（收集箱）
const char *kInboxDot = "#767f8c";

QString priorityGlyph(int p)
{
    switch (p) {
    case TodoPriorityHigh: return QStringLiteral("▲");
    case TodoPriorityMedium: return QStringLiteral("◆");
    case TodoPriorityLow: return QStringLiteral("▼");
    default: return QString();
    }
}

QColor priorityColor(int p)
{
    switch (p) {
    case TodoPriorityHigh: return QColor(kColorDanger);
    case TodoPriorityMedium: return QColor(kColorAccent);
    case TodoPriorityLow: return QColor(kColorOk);
    default: return QColor(kColorFgMuted);
    }
}

QString dueLabel(const TodoTask &t)
{
    const QDate d = QDate::fromString(t.dueDate, Qt::ISODate);
    if (!d.isValid())
        return t.dueDate;
    const QDate today = QDate::currentDate();
    if (d == today) return QStringLiteral("今天");
    if (d == today.addDays(1)) return QStringLiteral("明天");
    if (d == today.addDays(-1)) return QStringLiteral("昨天");
    if (d.year() == today.year())
        return d.toString(QStringLiteral("M月d日"));
    return d.toString(QStringLiteral("yyyy年M月d日"));
}

// 小胶囊（标签 / 清单名）
QLabel *makePill(const QString &text, const QColor &c)
{
    auto *l = new QLabel(text);
    l->setAttribute(Qt::WA_TransparentForMouseEvents);
    l->setStyleSheet(
        QStringLiteral("color:%1;background:%2;border:1px solid %3;border-radius:%4;"
                       "padding:0px %5;font-size:%6;font-weight:500;")
            .arg(c.name(),
                 withAlpha(c.name().toUtf8().constData(), 0.12),
                 withAlpha(c.name().toUtf8().constData(), 0.30),
                 sp(6), sp(5), sp(10)));
    return l;
}

// 列内滚动条（细、无槽）
QString columnScrollQss()
{
    return QStringLiteral(
               "QScrollArea{background:transparent;border:none;}"
               "QScrollBar:vertical{background:transparent;width:%1;margin:0;}"
               "QScrollBar::handle:vertical{background:%2;border-radius:%3;min-height:%4;}"
               "QScrollBar::handle:vertical:hover{background:%5;}"
               "QScrollBar::add-line:vertical,QScrollBar::sub-line:vertical{height:0;}"
               "QScrollBar::add-page:vertical,QScrollBar::sub-page:vertical{background:transparent;}")
        .arg(sp(6), withAlpha(kColorBorder, 0.90), sp(3), sp(20), withAlpha(kColorFg, 0.28));
}

// 勾选框样式（与列表视图的 TodoTaskRow 保持一致：圆形）
//
// ⚠ 数值必须用 si()：sp() 返回的是 "12px" 这种带单位的串，拼进 `width:%1px` 就成了
// `width:12pxpx`（非法 → 被 Qt 丢弃），指示器会塌缩成 4x4 的小点。
// Qt QSS 的 width/height 是 content box，border 画在它外面，border-radius 按含边框的外框算，
// 所以「直径 16px 的圆」= content 12 + border 2x2，radius = 16/2 = 8。
QString checkQss()
{
    return QStringLiteral("QCheckBox::indicator{width:%1px;height:%2px;border-radius:%3px;"
                          "border:2px solid %4;background:transparent;}"
                          "QCheckBox::indicator:hover{border-color:%5;}"
                          "QCheckBox::indicator:checked{background:%5;border-color:%5;}")
        .arg(si(12)).arg(si(12)).arg(si(8)).arg(kColorBorder, kColorAccent);
}

// 自绘专用的「带透明度取色」。
//
// 坑：theme.h 的 withAlpha() 返回的是给 **样式表** 用的 "rgba(r,g,b,a)" 字符串。
//     QColor(QString) 并不认这种格式（Qt 6.8 实测得到 invalid 颜色），而
//     QPainter::setBrush(invalidColor) 会退化成「纯黑不透明」填充 —— 卡片直接变黑块。
//     QSS 里随便用 withAlpha()；凡是交给 QPainter 的，一律走这个函数。
QColor colorA(const char *hex, qreal a)
{
    const QColor c{QString::fromLatin1(hex)};
    return QColor(c.red(), c.green(), c.blue(),
                  qBound(0, qRound(qBound(0.0, a, 1.0) * 255.0), 255));
}

} // namespace

// ══════════════════════════════════════════════════════════
// 拖拽协议
// ══════════════════════════════════════════════════════════
QString todoBoardMimeType()
{
    return QString::fromLatin1(kMimeName);
}

QString encodeBoardDrag(qint64 fromListId, int cardHeight, const QList<qint64> &ids)
{
    QStringList parts;
    parts << QStringLiteral("from=%1").arg(fromListId);
    parts << QStringLiteral("h=%1").arg(cardHeight);
    QStringList idStr;
    for (qint64 id : ids)
        idStr << QString::number(id);
    parts << QStringLiteral("ids=%1").arg(idStr.join(QLatin1Char(',')));
    return parts.join(QLatin1Char(';'));
}

bool decodeBoardDrag(const QString &payload, qint64 *fromListId, int *cardHeight,
                     QList<qint64> *ids)
{
    if (payload.isEmpty())
        return false;
    bool gotFrom = false, gotIds = false;
    const auto parts = payload.split(QLatin1Char(';'), Qt::SkipEmptyParts);
    for (const QString &p : parts) {
        const int eq = p.indexOf(QLatin1Char('='));
        if (eq <= 0)
            continue;
        const QString key = p.left(eq);
        const QString val = p.mid(eq + 1);
        if (key == QLatin1String("from")) {
            bool ok = false;
            const qint64 v = val.toLongLong(&ok);
            if (!ok)
                return false;
            if (fromListId)
                *fromListId = v;
            gotFrom = true;
        } else if (key == QLatin1String("h")) {
            if (cardHeight)
                *cardHeight = val.toInt();
        } else if (key == QLatin1String("ids")) {
            for (const QString &s : val.split(QLatin1Char(','), Qt::SkipEmptyParts)) {
                bool ok = false;
                const qint64 v = s.toLongLong(&ok);
                if (ok && ids)
                    ids->append(v);
            }
            gotIds = true;
        }
    }
    return gotFrom && gotIds;
}

// ══════════════════════════════════════════════════════════
// 拖拽浮层渲染
// ══════════════════════════════════════════════════════════
QPixmap renderBoardDragPixmap(QWidget *card, int *padOut)
{
    const int pad = si(16);
    if (padOut)
        *padOut = pad;
    if (!card || card->width() <= 0 || card->height() <= 0)
        return QPixmap();

    const qreal dpr = card->devicePixelRatioF();
    const int w = card->width() + pad * 2;
    const int h = card->height() + pad * 2;
    QPixmap pm(qRound(w * dpr), qRound(h * dpr));
    pm.setDevicePixelRatio(dpr);
    pm.fill(Qt::transparent);

    QPainter p(&pm);
    p.setRenderHint(QPainter::Antialiasing, true);
    p.setRenderHint(QPainter::SmoothPixmapTransform, true);

    const QRectF body(pad, pad, card->width(), card->height());
    const qreal r = si(8);

    // 柔和投影：由外向内叠多层圆角矩形，越靠内叠加越厚 → 近似高斯；整体轻微下移
    const QColor shadowColor = (gTheme && gTheme->light) ? QColor(60, 64, 72) : QColor(0, 0, 0);
    p.setPen(Qt::NoPen);
    for (int i = pad; i >= 1; --i) {
        QColor c = shadowColor;
        c.setAlpha(16);
        p.setBrush(c);
        const QRectF ring = body.adjusted(-i, -i + i * 0.30, i, i);
        p.drawRoundedRect(ring, r + i, r + i);
    }

    // 卡片本体：直接抓当前外观，勾选框 / 标题 / 元信息全保留
    p.drawPixmap(QPoint(pad, pad), card->grab());

    // 2px accent 环 —— 浮层"抬起"的视觉锚点
    // 注意用花括号初始化：QPen pen(QColor(kColorAccent)) 会被解析成函数声明（most vexing parse）
    QPen pen{QColor(kColorAccent)};
    pen.setWidthF(qMax(1.0, qreal(si(2))));
    p.setPen(pen);
    p.setBrush(Qt::NoBrush);
    const qreal half = pen.widthF() / 2.0;
    p.drawRoundedRect(QRectF(pad - half, pad - half,
                             card->width() + half * 2, card->height() + half * 2),
                      r + half, r + half);
    p.end();
    return pm;
}

// ══════════════════════════════════════════════════════════
// TodoFadeButton
// ══════════════════════════════════════════════════════════
TodoFadeButton::TodoFadeButton(QWidget *parent)
    : QToolButton(parent)
{
    setFocusPolicy(Qt::NoFocus);
    setCursor(Qt::PointingHandCursor);
    setAutoRaise(true);
    setAttribute(Qt::WA_Hover);
    m_fx = new QGraphicsOpacityEffect(this);
    m_fx->setOpacity(0.0);
    setGraphicsEffect(m_fx);
    setAttribute(Qt::WA_TransparentForMouseEvents, true);
    setStyleSheet(
        QStringLiteral("QToolButton{color:%1;background:transparent;border:none;"
                       "border-radius:%2;font-size:%3;padding:0;}"
                       "QToolButton:hover{background:%4;color:%5;}")
            .arg(kColorFgMuted, sp(6), sp(13), withAlpha(kColorFg, 0.10), kColorFg));
}

void TodoFadeButton::fadeTo(qreal to, int ms)
{
    setAttribute(Qt::WA_TransparentForMouseEvents, to < 0.5);
    if (m_anim)
        m_anim->stop();
    if (!gFxAnimations || ms <= 0) {
        m_fx->setOpacity(to);
        return;
    }
    auto *a = new QPropertyAnimation(m_fx, "opacity", this);
    a->setDuration(ms);
    a->setStartValue(m_fx->opacity());
    a->setEndValue(to);
    a->setEasingCurve(QEasingCurve::OutCubic);
    m_anim = a;
    a->start(QAbstractAnimation::DeleteWhenStopped);
}

void TodoFadeButton::setEngaged(bool on)
{
    if (m_anim)
        m_anim->stop();
    m_fx->setOpacity(on ? 1.0 : 0.0);
    setAttribute(Qt::WA_TransparentForMouseEvents, !on);
}

// ══════════════════════════════════════════════════════════
// TodoBoardGap —— 落点占位框
// ══════════════════════════════════════════════════════════
TodoBoardGap::TodoBoardGap(QWidget *parent)
    : QWidget(parent)
{
    setObjectName(QStringLiteral("TodoBoardGap"));
    setAttribute(Qt::WA_TransparentForMouseEvents);
    setFixedHeight(0);
}

void TodoBoardGap::setGapHeight(int h)
{
    m_gapHeight = qMax(0, h);
    setFixedHeight(m_gapHeight);
    update();
}

void TodoBoardGap::paintEvent(QPaintEvent *)
{
    if (m_gapHeight <= 3)
        return;
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, true);
    const qreal w = qMax(1.0, qreal(si(2)));
    const QRectF r = QRectF(rect()).adjusted(w / 2, w / 2, -w / 2, -w / 2);
    p.setBrush(colorA(kColorAccent, 0.10));
    QPen pen{colorA(kColorAccent, 0.80)};
    pen.setWidthF(w);
    pen.setStyle(Qt::CustomDashLine);
    pen.setDashPattern({4.0, 3.0});
    p.setPen(pen);
    p.drawRoundedRect(r, si(8), si(8));
}

// ══════════════════════════════════════════════════════════
// TodoBoardCard
// ══════════════════════════════════════════════════════════
TodoBoardCard::TodoBoardCard(const TodoTask &task, QWidget *parent)
    : QWidget(parent), m_id(task.id)
{
    setObjectName(QStringLiteral("TodoBoardCard"));
    setAttribute(Qt::WA_Hover);
    setCursor(Qt::PointingHandCursor);
    setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);

    auto *lay = new QVBoxLayout(this);
    lay->setContentsMargins(si(10), si(9), si(10), si(9));
    lay->setSpacing(si(4));

    auto *top = new QHBoxLayout;
    top->setSpacing(si(8));

    m_chk = new QCheckBox;
    m_chk->setChecked(task.completed);
    m_chk->setCursor(Qt::PointingHandCursor);
    m_chk->setToolTip(task.completed ? QStringLiteral("标记为未完成")
                                     : QStringLiteral("标记为已完成"));
    m_chk->setStyleSheet(checkQss());
    connect(m_chk, &QCheckBox::clicked, this, [this](bool checked) {
        emit toggleRequested(m_id, checked);
    });
    top->addWidget(m_chk, 0, Qt::AlignTop);

    m_title = new QLabel(task.title);
    m_title->setWordWrap(true);
    m_title->setTextInteractionFlags(Qt::NoTextInteraction);
    // 标题事件穿透到卡片：悬停高亮与拖拽起手都靠卡片自身
    m_title->setAttribute(Qt::WA_TransparentForMouseEvents);
    m_title->setStyleSheet(
        QStringLiteral("font-size:%1;font-weight:600;background:transparent;color:%2;"
                       "line-height:135%;")
            .arg(sp(13), kColorFg));
    top->addWidget(m_title, 1);

    // 悬停浮现的「⋯」：常驻占位，靠透明度显隐，避免标题换行宽度跳动
    m_more = new TodoFadeButton(this);
    m_more->setText(glyph::Menu);
    m_more->setToolTip(QStringLiteral("任务菜单"));
    m_more->setFixedSize(si(20), si(20));
    connect(m_more, &QToolButton::clicked, this, [this] {
        emit menuRequested(m_id, m_more->mapToGlobal(QPoint(0, m_more->height())));
    });
    top->addWidget(m_more, 0, Qt::AlignTop);

    lay->addLayout(top);

    // 元信息行：优先级 / 截止 / 标签（只建需要的，省一层布局）
    QStringList tags = task.tags;
    const bool hasDue = task.hasDue();
    const bool hasPrio = task.priority > TodoPriorityNone;
    if (hasPrio || hasDue || !tags.isEmpty()) {
        m_meta = new QWidget(this);
        m_meta->setAttribute(Qt::WA_TransparentForMouseEvents);
        m_meta->setContentsMargins(si(24), 0, 0, 0);   // 与标题左缘对齐
        auto *ml = new QHBoxLayout(m_meta);
        ml->setContentsMargins(0, 0, 0, 0);
        ml->setSpacing(si(5));
        if (hasPrio) {
            auto *p = new QLabel(priorityGlyph(task.priority));
            p->setStyleSheet(QStringLiteral("color:%1;font-size:%2;font-weight:700;"
                                            "background:transparent;")
                                 .arg(priorityColor(task.priority).name(), sp(11)));
            ml->addWidget(p);
        }
        if (hasDue) {
            const QDate d = QDate::fromString(task.dueDate, Qt::ISODate);
            const bool overdue = d.isValid() && d < QDate::currentDate();
            const bool isToday = d.isValid() && d == QDate::currentDate();
            QString col = QString::fromLatin1(kColorFgMuted);
            QString bg = QStringLiteral("transparent");
            QString bd = QString::fromLatin1(kColorBorder);
            if (overdue) {
                col = QString::fromLatin1(kColorDanger);
                bg = withAlpha(kColorDanger, 0.10);
                bd = withAlpha(kColorDanger, 0.40);
            } else if (isToday) {
                col = QString::fromLatin1(kColorAccent);
                bg = withAlpha(kColorAccent, 0.10);
                bd = withAlpha(kColorAccent, 0.40);
            }
            auto *due = new QLabel(dueLabel(task));
            due->setStyleSheet(
                QStringLiteral("color:%1;font-size:%2;padding:0px %3;background:%4;"
                               "border:1px solid %5;border-radius:%6;font-weight:500;")
                    .arg(col, sp(10), sp(5), bg, bd, sp(6)));
            ml->addWidget(due);
        }
        int shown = 0;
        for (const QString &tag : tags) {
            if (shown++ >= 2)
                break;
            ml->addWidget(makePill(tag, QColor(kColorAccent)));
        }
        if (tags.size() > 2) {
            auto *more = new QLabel(QStringLiteral("+%1").arg(tags.size() - 2));
            more->setStyleSheet(QStringLiteral("color:%1;font-size:%2;background:transparent;")
                                    .arg(kColorMuted2, sp(10)));
            ml->addWidget(more);
        }
        ml->addStretch(1);
        lay->addWidget(m_meta);
    }
}

void TodoBoardCard::setHoverT(qreal v)
{
    m_hoverT = qBound(0.0, v, 1.0);
    update();
}

void TodoBoardCard::setRingT(qreal v)
{
    m_ringT = qBound(0.0, v, 1.0);
    update();
}

void TodoBoardCard::setDimmed(bool on)
{
    if (m_dimmed == on)
        return;
    m_dimmed = on;
    update();
}

void TodoBoardCard::playSettle()
{
    // 先直接点亮再淡出：对应视频里落位后卡片仍带一圈 accent 环
    setRingT(1.0);
    animTo("ringT", 0.0, 620);
}

void TodoBoardCard::animTo(const char *prop, qreal to, int ms)
{
    const bool hover = (qstrcmp(prop, "hoverT") == 0);
    QPointer<QPropertyAnimation> &slot = hover ? m_hoverAnim : m_ringAnim;
    if (slot)
        slot->stop();
    if (!gFxAnimations || ms <= 0) {
        if (hover) setHoverT(to); else setRingT(to);
        return;
    }
    auto *a = new QPropertyAnimation(this, prop, this);
    a->setDuration(ms);
    a->setEndValue(to);
    a->setEasingCurve(QEasingCurve::OutCubic);
    slot = a;
    a->start(QAbstractAnimation::DeleteWhenStopped);
}

void TodoBoardCard::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, true);
    p.setOpacity(m_dimmed ? 0.32 : 1.0);

    const qreal t = m_hoverT;
    const qreal rad = si(8);
    // 悬停上浮 1px：只挪绘制矩形，不改几何（否则列内布局会跟着抖）
    const QRectF r = QRectF(rect()).adjusted(0.5, 0.5 - t, -0.5, -0.5 - t);

    // 卡片底色用 kColorFg 叠亮，不用 kColorBgElev2 —— 各主题里 elev2 都比页面底色更暗，
    // 叠上去会让卡片显得比所在列「更深」，像挖了个洞而不是浮起来。
    // 走 kColorFg 半透明叠色保证任何主题下卡片都略亮于列底（亮色主题即略深于列底）。
    const QColor bg{colorA(kColorFg, 0.045 + 0.045 * t)};
    const QColor bd{t > 0.01 ? colorA(kColorAccent, 0.50) : colorA(kColorBorder, 0.75)};
    p.setPen(QPen(bd, 1.0));
    p.setBrush(bg);
    p.drawRoundedRect(r, rad, rad);

    if (m_ringT > 0.01) {
        QPen rp{colorA(kColorAccent, 0.90 * m_ringT)};
        rp.setWidthF(qMax(1.0, qreal(si(2))));
        p.setPen(rp);
        p.setBrush(Qt::NoBrush);
        const qreal half = rp.widthF() / 2.0;
        p.drawRoundedRect(QRectF(rect()).adjusted(half, half, -half, -half), rad, rad);
    }
}

void TodoBoardCard::mousePressEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton) {
        m_pressed = true;
        m_press = event->position().toPoint();
    }
    QWidget::mousePressEvent(event);
}

void TodoBoardCard::mouseMoveEvent(QMouseEvent *event)
{
    if (!m_pressed) {
        QWidget::mouseMoveEvent(event);
        return;
    }
    if ((event->position().toPoint() - m_press).manhattanLength()
        < QApplication::startDragDistance()) {
        QWidget::mouseMoveEvent(event);
        return;
    }
    m_pressed = false;

    // 先抓图（此时卡片还是普通外观），抓到之后再变暗
    int pad = 0;
    const QPixmap pm = renderBoardDragPixmap(this, &pad);
    setDimmed(true);

    QDrag drag(this);
    auto *mime = new QMimeData;
    mime->setData(todoBoardMimeType(),
                  encodeBoardDrag(m_listId, height(), {m_id}).toUtf8());
    drag.setMimeData(mime);
    if (!pm.isNull()) {
        drag.setPixmap(pm);
        drag.setHotSpot(QPoint(m_press.x() + pad, m_press.y() + pad));
    }

    // 拖拽期间禁止重建看板（列被删会让 QDrag 的落点悬垂）
    boardDragActive() = true;
    drag.exec(Qt::MoveAction);
    boardDragActive() = false;

    setDimmed(false);
    QWidget::mouseMoveEvent(event);
}

void TodoBoardCard::mouseReleaseEvent(QMouseEvent *event)
{
    if (m_pressed && event->button() == Qt::LeftButton) {
        m_pressed = false;
        if (rect().contains(event->position().toPoint())) {
            emit activated(m_id);
            event->accept();
            return;
        }
    }
    m_pressed = false;
    QWidget::mouseReleaseEvent(event);
}

void TodoBoardCard::enterEvent(QEnterEvent *event)
{
    setCursor(Qt::PointingHandCursor);
    animTo("hoverT", 1.0, gFxAnimations ? 140 : 0);
    if (m_more)
        m_more->fadeTo(1.0);
    QWidget::enterEvent(event);
}

void TodoBoardCard::leaveEvent(QEvent *event)
{
    animTo("hoverT", 0.0, gFxAnimations ? 160 : 0);
    if (m_more)
        m_more->fadeTo(0.0);
    QWidget::leaveEvent(event);
}

// ══════════════════════════════════════════════════════════
// TodoBoardColumn
// ══════════════════════════════════════════════════════════
TodoBoardColumn::TodoBoardColumn(qint64 listId, QWidget *parent)
    : QWidget(parent), m_listId(listId)
{
    setObjectName(QStringLiteral("TodoBoardColumn"));
    setAttribute(Qt::WA_StyledBackground, true);
    setAttribute(Qt::WA_Hover);
    setAcceptDrops(true);
    setMinimumWidth(si(206));
    // 上限：清单少时列被拉成整屏宽会很怪（卡片一行放不下几个字），到 340 就封顶
    setMaximumWidth(si(340));

    auto *lay = new QVBoxLayout(this);
    lay->setContentsMargins(si(10), si(10), si(10), si(8));
    lay->setSpacing(si(8));

    // ── 列头：色点 + 清单名 + 计数 +（悬停浮现）＋ / ⋯ ──
    auto *head = new QHBoxLayout;
    head->setSpacing(si(7));

    m_dot = new QLabel(this);
    m_dot->setFixedSize(si(8), si(8));
    m_dot->setAttribute(Qt::WA_TransparentForMouseEvents);
    head->addWidget(m_dot);

    m_name = new QLabel(this);
    m_name->setObjectName(QStringLiteral("TodoBColName"));
    m_name->setAttribute(Qt::WA_TransparentForMouseEvents);
    head->addWidget(m_name);

    m_count = new QLabel(this);
    m_count->setObjectName(QStringLiteral("TodoBColCount"));
    m_count->setAttribute(Qt::WA_TransparentForMouseEvents);
    head->addWidget(m_count);
    head->addStretch(1);

    m_addTop = new TodoFadeButton(this);
    m_addTop->setText(QStringLiteral("＋"));
    m_addTop->setToolTip(QStringLiteral("添加任务"));
    m_addTop->setFixedSize(si(20), si(20));
    connect(m_addTop, &QToolButton::clicked, this, &TodoBoardColumn::startQuickAdd);
    head->addWidget(m_addTop);

    m_menu = new TodoFadeButton(this);
    m_menu->setText(glyph::Menu);
    m_menu->setToolTip(QStringLiteral("清单菜单"));
    m_menu->setFixedSize(si(20), si(20));
    connect(m_menu, &QToolButton::clicked, this, [this] {
        emit listMenuRequested(m_listId, m_menu->mapToGlobal(QPoint(0, m_menu->height())));
    });
    head->addWidget(m_menu);

    lay->addLayout(head);

    // ── 卡片区（列内纵向滚动：列头与「添加任务」始终可见） ──
    m_scroll = new QScrollArea(this);
    m_scroll->setWidgetResizable(true);
    m_scroll->setFrameShape(QFrame::NoFrame);
    m_scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_scroll->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    m_scroll->setStyleSheet(columnScrollQss());
    m_scroll->viewport()->setAutoFillBackground(false);

    m_cards = new QWidget;
    m_cards->setObjectName(QStringLiteral("TodoBCards"));
    m_cards->setAutoFillBackground(false);
    m_cardsLay = new QVBoxLayout(m_cards);
    m_cardsLay->setContentsMargins(0, 0, 0, 0);
    m_cardsLay->setSpacing(si(8));
    m_cardsLay->addStretch(1);
    m_scroll->setWidget(m_cards);
    lay->addWidget(m_scroll, 1);

    // 拖放目标只认列本身：只要子控件 acceptDrops 为真就会截走 dragMove/drop，
    // 列再也收不到事件，表现为「拖过去毫无反应」。
    m_scroll->setAcceptDrops(false);
    m_scroll->viewport()->setAcceptDrops(false);
    m_cards->setAcceptDrops(false);

    m_gap = new TodoBoardGap(m_cards);
    m_gap->hide();

    // ── 底部「＋ 添加任务」/ 行内输入框 ──
    m_addTask = new QToolButton(this);
    m_addTask->setObjectName(QStringLiteral("TodoBAddTask"));
    m_addTask->setText(QStringLiteral("＋ 添加任务"));
    m_addTask->setCursor(Qt::PointingHandCursor);
    m_addTask->setFocusPolicy(Qt::NoFocus);
    m_addTask->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    m_addTask->setFixedHeight(si(28));
    m_addTask->setToolButtonStyle(Qt::ToolButtonTextOnly);
    connect(m_addTask, &QToolButton::clicked, this, &TodoBoardColumn::startQuickAdd);
    lay->addWidget(m_addTask);

    m_addEdit = new QLineEdit(this);
    m_addEdit->setObjectName(QStringLiteral("TodoBAddEdit"));
    m_addEdit->setPlaceholderText(QStringLiteral("任务标题，回车添加"));
    m_addEdit->setFixedHeight(si(28));
    m_addEdit->hide();
    m_addEdit->installEventFilter(this);
    connect(m_addEdit, &QLineEdit::returnPressed, this, [this] { endQuickAdd(true); });
    lay->addWidget(m_addEdit);

    applyStyle();
}

void TodoBoardColumn::applyStyle()
{
    const QString qss = QStringLiteral(
        "QWidget#TodoBoardColumn{background:%1;border:1px solid %2;border-radius:%3;}"
        "QWidget#TodoBoardColumn[colHover=\"true\"]{background:%4;border:1px solid %5;}"
        "QWidget#TodoBoardColumn[dropActive=\"true\"]{background:%6;border:1px solid %7;}"
        "QLabel#TodoBColName{color:%8;font-size:%9;font-weight:600;background:transparent;}"
        "QLabel#TodoBColCount{color:%10;font-size:%11;background:transparent;}"
        "QToolButton#TodoBAddTask{color:%10;background:transparent;border:none;"
        "border-radius:%12;font-size:%13;text-align:left;padding-left:%14;}"
        "QToolButton#TodoBAddTask:hover{background:%15;color:%16;}"
        "QLineEdit#TodoBAddEdit{background:%17;border:1px solid %18;border-radius:%12;"
        "padding:0 %19;font-size:%13;color:%16;}"
        "QLineEdit#TodoBAddEdit:focus{border:1px solid %20;}")
        .arg(withAlpha(kColorFg, 0.028),                       // 列底
             withAlpha(kColorBorder, 0.55),                    // 列描边
             sp(12),
             withAlpha(kColorFg, 0.055),                       // 悬停列底
             withAlpha(kColorBorder, 0.90),
             withAlpha(kColorAccent, 0.09),                    // 拖拽目标列底
             withAlpha(kColorAccent, 0.45),
             kColorFg, sp(13),
             kColorMuted2, sp(11),
             sp(8), sp(12.5), sp(8),
             withAlpha(kColorFg, 0.07), kColorFg,
             withAlpha(kColorBgElev2, 0.55), kColorBorder, sp(8),
             kColorAccent);
    setStyleSheet(qss);
}

void TodoBoardColumn::setContent(const QString &name, const QColor &color, bool inbox,
                                 const QList<TodoTask> &tasks, qint64 settleTaskId)
{
    m_color = color.isValid() ? color.name() : QString::fromLatin1(kInboxDot);

    // 色点
    QPixmap pm(si(8), si(8));
    pm.fill(Qt::transparent);
    {
        QPainter p(&pm);
        p.setRenderHint(QPainter::Antialiasing, true);
        p.setPen(Qt::NoPen);
        p.setBrush(QColor(m_color));
        p.drawRoundedRect(QRectF(pm.rect()), si(2.5), si(2.5));
    }
    m_dot->setPixmap(pm);

    m_name->setText(inbox ? QStringLiteral("收集箱") : name);
    m_count->setText(QString::number(tasks.size()));
    m_addEdit->setPlaceholderText(inbox ? QStringLiteral("新任务（收集箱）")
                                        : QStringLiteral("新任务（%1）").arg(name));

    // 重建卡片（列内顺序由上层排序后给出）
    const auto old = cardList();
    for (auto *c : old)
        c->deleteLater();
    for (auto *c : old)
        m_cardsLay->removeWidget(c);
    hideGap(false);

    int idx = 0;
    for (const TodoTask &t : tasks) {
        auto *card = new TodoBoardCard(t, m_cards);
        card->setListId(m_listId);
        connect(card, &TodoBoardCard::activated, this, &TodoBoardColumn::taskActivated);
        connect(card, &TodoBoardCard::toggleRequested, this,
                &TodoBoardColumn::taskToggleRequested);
        connect(card, &TodoBoardCard::menuRequested, this,
                &TodoBoardColumn::taskMenuRequested);
        m_cardsLay->insertWidget(idx++, card);
        card->show();
        if (t.id == settleTaskId)
            card->playSettle();
    }
}

QList<TodoBoardCard *> TodoBoardColumn::cardList() const
{
    QList<TodoBoardCard *> out;
    for (int i = 0; i < m_cardsLay->count(); ++i) {
        if (auto *c = qobject_cast<TodoBoardCard *>(m_cardsLay->itemAt(i)->widget()))
            out.append(c);
    }
    return out;
}

void TodoBoardColumn::setColHover(bool on)
{
    if (m_hovered == on)
        return;
    m_hovered = on;
    setProperty("colHover", on);
    style()->unpolish(this);
    style()->polish(this);
    if (m_addTop)
        m_addTop->fadeTo(on ? 1.0 : 0.0);
    if (m_menu)
        m_menu->fadeTo(on ? 1.0 : 0.0);
}

void TodoBoardColumn::setDropActive(bool on)
{
    if (m_dropActive == on)
        return;
    m_dropActive = on;
    setProperty("dropActive", on);
    style()->unpolish(this);
    style()->polish(this);
}

void TodoBoardColumn::enterEvent(QEnterEvent *event)
{
    setColHover(true);
    QWidget::enterEvent(event);
}

void TodoBoardColumn::leaveEvent(QEvent *event)
{
    setColHover(false);
    QWidget::leaveEvent(event);
}

// 计算插入位：取第一个「中线在光标下方」的卡片下标；都不满足则插到末尾
int TodoBoardColumn::dropIndexAt(int cardsY) const
{
    const auto cards = cardList();
    for (int i = 0; i < cards.size(); ++i) {
        const QRect g = cards.at(i)->geometry();
        if (cardsY < g.center().y())
            return i;
    }
    return cards.size();
}

void TodoBoardColumn::moveGapTo(int index, int cardH)
{
    // 上一轮收起的动画可能还在跑，先停掉（否则两个动画同时写 gapHeight）
    if (m_gapAnim)
        m_gapAnim->stop();

    const auto cards = cardList();
    index = qBound(0, index, cards.size());
    const int targetH = qMax(si(34), cardH);

    if (!m_gap->isVisible()) {
        m_gap->setGapHeight(targetH);
        m_cardsLay->insertWidget(index, m_gap);
        m_gap->show();
        m_gap->raise();
        m_gapIndex = index;
        return;
    }
    if (m_gapIndex != index) {
        m_cardsLay->removeWidget(m_gap);
        m_cardsLay->insertWidget(index, m_gap);
        m_gapIndex = index;
    }
    if (m_gap->gapHeight() != targetH) {
        auto *a = new QPropertyAnimation(m_gap, "gapHeight", m_gap);
        a->setDuration(gFxAnimations ? 150 : 0);
        a->setStartValue(m_gap->gapHeight());
        a->setEndValue(targetH);
        a->setEasingCurve(QEasingCurve::OutCubic);
        m_gapAnim = a;
        a->start(QAbstractAnimation::DeleteWhenStopped);
    }
}

void TodoBoardColumn::hideGap(bool animated)
{
    m_gapIndex = -1;
    if (!m_gap->isVisible())
        return;
    if (m_gapAnim)
        m_gapAnim->stop();
    if (!animated || !gFxAnimations) {
        m_cardsLay->removeWidget(m_gap);
        m_gap->setGapHeight(0);
        m_gap->hide();
        return;
    }
    auto *a = new QPropertyAnimation(m_gap, "gapHeight", m_gap);
    a->setDuration(140);
    a->setStartValue(m_gap->gapHeight());
    a->setEndValue(0);
    a->setEasingCurve(QEasingCurve::OutCubic);
    connect(a, &QPropertyAnimation::finished, m_gap, [this] {
        // 拖拽又开始了就别收（m_dropActive 为真说明是新的一轮）
        if (m_dropActive)
            return;
        m_cardsLay->removeWidget(m_gap);
        m_gap->hide();
    });
    m_gapAnim = a;
    a->start(QAbstractAnimation::DeleteWhenStopped);
}

bool TodoBoardColumn::eventFilter(QObject *watched, QEvent *event)
{
    if (watched == m_addEdit) {
        if (event->type() == QEvent::KeyPress) {
            auto *ke = static_cast<QKeyEvent *>(event);
            if (ke->key() == Qt::Key_Escape) {
                endQuickAdd(false);
                return true;
            }
        } else if (event->type() == QEvent::FocusOut) {
            endQuickAdd(true);
            return false;
        }
    }
    return QWidget::eventFilter(watched, event);
}

void TodoBoardColumn::startQuickAdd()
{
    m_addTask->hide();
    m_addEdit->clear();
    m_addEdit->show();
    m_addEdit->setFocus();
}

void TodoBoardColumn::endQuickAdd(bool commit)
{
    if (!m_addEdit->isVisible())
        return;
    const QString title = m_addEdit->text().trimmed();
    m_addEdit->clear();
    m_addEdit->hide();
    m_addTask->show();
    if (commit && !title.isEmpty())
        emit quickAdd(m_listId, title);
}

// ── 拖放 ─────────────────────────────────────────────────
void TodoBoardColumn::dragEnterEvent(QDragEnterEvent *event)
{
    qint64 from = -1;
    int h = 0;
    QList<qint64> ids;
    if (!decodeBoardDrag(QString::fromUtf8(event->mimeData()->data(todoBoardMimeType())),
                         &from, &h, &ids)
        || ids.isEmpty() || from == m_listId) {
        event->ignore();
        return;
    }
    event->setDropAction(Qt::MoveAction);
    event->accept();
    setDropActive(true);
}

void TodoBoardColumn::dragMoveEvent(QDragMoveEvent *event)
{
    qint64 from = -1;
    int h = 0;
    QList<qint64> ids;
    if (!decodeBoardDrag(QString::fromUtf8(event->mimeData()->data(todoBoardMimeType())),
                         &from, &h, &ids)
        || ids.isEmpty() || from == m_listId) {
        event->ignore();
        return;
    }
    event->setDropAction(Qt::MoveAction);
    event->accept();
    setDropActive(true);

    const QPoint inCards = m_cards->mapFrom(this, event->position().toPoint());
    moveGapTo(dropIndexAt(inCards.y()), h > 0 ? h : si(46));
}

void TodoBoardColumn::dragLeaveEvent(QDragLeaveEvent *event)
{
    setDropActive(false);
    hideGap(true);
    QWidget::dragLeaveEvent(event);
}

void TodoBoardColumn::dropEvent(QDropEvent *event)
{
    qint64 from = -1;
    int h = 0;
    QList<qint64> ids;
    const bool ok = decodeBoardDrag(
        QString::fromUtf8(event->mimeData()->data(todoBoardMimeType())), &from, &h, &ids);

    setDropActive(false);
    hideGap(false);

    if (!ok || ids.isEmpty() || from == m_listId) {
        event->ignore();
        return;
    }
    event->setDropAction(Qt::MoveAction);
    event->accept();
    for (qint64 id : ids)
        emit taskDropped(id, m_listId);
}

// ══════════════════════════════════════════════════════════
// TodoBoardView
// ══════════════════════════════════════════════════════════
TodoBoardView::TodoBoardView(QWidget *parent)
    : QScrollArea(parent)
{
    setObjectName(QStringLiteral("TodoBoardView"));
    setWidgetResizable(true);
    setFrameShape(QFrame::NoFrame);
    setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    viewport()->setAutoFillBackground(false);

    m_canvas = new QWidget;
    m_canvas->setObjectName(QStringLiteral("TodoBoardCanvas"));
    m_canvas->setAutoFillBackground(false);
    m_lay = new QHBoxLayout(m_canvas);
    m_lay->setContentsMargins(0, 0, 0, 0);
    setWidget(m_canvas);

    applyMetrics();
}

// 滚动条尺寸（sp()）与列间距（si()）都按当次的 gUiScale 定死，缩放变化要重算
void TodoBoardView::applyMetrics()
{
    m_lay->setSpacing(si(12));
    setStyleSheet(QStringLiteral(
                      "QScrollArea{background:transparent;border:none;}"
                      "QScrollBar:horizontal{background:transparent;height:%1;margin:0;}"
                      "QScrollBar::handle:horizontal{background:%2;border-radius:%3;min-width:%4;}"
                      "QScrollBar::handle:horizontal:hover{background:%5;}"
                      "QScrollBar::add-line:horizontal,QScrollBar::sub-line:horizontal{width:0;}"
                      "QScrollBar::add-page:horizontal,QScrollBar::sub-page:horizontal{background:transparent;}")
                      .arg(sp(8), withAlpha(kColorBorder, 0.90), sp(4), sp(24),
                           withAlpha(kColorFg, 0.28)));
}

void TodoBoardView::rebuildColumns(const QList<TodoList> &lists,
                                   const QHash<qint64, QString> &listColors)
{
    // 期望的列序列：直接沿用 m_lists —— 它的第 0 项就是虚拟收集箱（id=0，见
    // TodoApiStore::fetchLists），这里绝不能再前置一个 id=0，否则会出现两列重复的收集箱。
    QList<qint64> want;
    for (const auto &l : lists)
        want.append(l.id);

    bool same = (want.size() == m_cols.size());
    if (same) {
        for (int i = 0; i < want.size(); ++i) {
            if (m_cols.at(i)->listId() != want.at(i)) {
                same = false;
                break;
            }
        }
    }
    // 列（TodoBoardColumn）的几何与字号都在其构造函数里按当时的 gUiScale 定死（si()/sp()），
    // 所以比例一变就必须整板重建 —— 只判「清单集合没变」会让看板留在旧比例
    // （用户现场：页面各处都放大了，板里的清单/卡片却还是小的）
    const bool scaleChanged = (m_builtScale < 0.0) || !qFuzzyCompare(m_builtScale, gUiScale);
    if (same && !scaleChanged)
        return;
    if (scaleChanged)
        applyMetrics();
    m_builtScale = gUiScale;

    // 列集合变了：整板重建（清单增删不频繁，且拖拽期间已被 boardDragActive 挡住）
    for (auto *c : m_cols) {
        m_lay->removeWidget(c);
        c->deleteLater();
    }
    m_cols.clear();

    for (qint64 id : want) {
        auto *col = new TodoBoardColumn(id, m_canvas);
        connect(col, &TodoBoardColumn::taskActivated, this, &TodoBoardView::taskActivated);
        connect(col, &TodoBoardColumn::taskToggleRequested, this,
                &TodoBoardView::taskToggleRequested);
        connect(col, &TodoBoardColumn::taskMenuRequested, this,
                &TodoBoardView::taskMenuRequested);
        connect(col, &TodoBoardColumn::listMenuRequested, this,
                &TodoBoardView::listMenuRequested);
        connect(col, &TodoBoardColumn::taskDropped, this, &TodoBoardView::taskDropped);
        connect(col, &TodoBoardColumn::quickAdd, this, [this](qint64 listId, const QString &t) {
            // 由 TodoPage 统一走 m_source->createTask（见 TodoBoardView 的使用方）
            emit quickAddRequested(listId, t);
        });
        m_lay->addWidget(col, 1);
        m_cols.append(col);
        col->show();
    }

    // 列太少时给个最小宽度，保证横向滚动条在窗口很窄时也能出现
    const int n = m_cols.size();
    m_canvas->setMinimumWidth(n * si(206) + qMax(0, n - 1) * si(12));
}

void TodoBoardView::setData(const QList<TodoList> &lists, const QList<TodoTask> &tasks,
                            const QHash<qint64, QString> &listColors, qint64 settleTaskId,
                            const std::function<bool(const TodoTask &, const TodoTask &)> &less)
{
    rebuildColumns(lists, listColors);

    QHash<qint64, QList<TodoTask>> byList;
    for (const auto &t : tasks) {
        if (t.completed)
            continue;   // 看板只展示未完成任务（与列表视图默认一致）
        byList[t.listId].append(t);
    }

    QHash<qint64, QString> names;
    for (const auto &l : lists)
        names.insert(l.id, l.name);

    for (auto *col : m_cols) {
        const qint64 id = col->listId();
        QList<TodoTask> ts = byList.value(id);
        if (less) {
            std::sort(ts.begin(), ts.end(), less);
        }
        const QString color = (id == 0) ? QString::fromLatin1(kInboxDot)
                                        : listColors.value(id, QString::fromLatin1(kInboxDot));
        col->setContent(names.value(id), QColor(color), id == 0, ts, settleTaskId);
    }
}

} // namespace awqtui
