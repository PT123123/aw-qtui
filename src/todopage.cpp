// todopage.cpp —— Todo 页实现（参照 TickTick / Super Productivity）
#include "todopage.h"
#include <QDebug>
#include "ui_todopage.h"

#include "appsettings.h"
#include "apiclient.h"
#include "config.h"
#include "theme.h"
#include "mockdata.h"
#include "todoboard.h"
#include "todostore.h"
#include "widgets.h"

#include <QAction>
#include <QApplication>
#include <QCheckBox>
#include <QClipboard>
#include <QColor>
#include <QComboBox>
#include <QContextMenuEvent>
#include <QCursor>
#include <QDate>
#include <QDateEdit>
#include <QEnterEvent>
#include <QFont>
#include <QFontMetrics>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QIcon>
#include <QInputDialog>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMenu>
#include <QVector>
#include <QMessageBox>
#include <QMouseEvent>
#include <QPainter>
#include <QPen>
#include <QPixmap>
#include <QPlainTextEdit>
#include <QPointer>
#include <QPushButton>
#include <QEasingCurve>
#include <QPropertyAnimation>
#include <QRect>
#include <QRegularExpression>
#include <QSet>
#include <QSignalBlocker>
#include <QSizePolicy>
#include <QStackedWidget>
#include <QAbstractTextDocumentLayout>
#include <QTextDocument>
#include <QTextLayout>
#include <QTimer>
#include <QToolButton>
#include <QVBoxLayout>
#include <QtMath>

#include <algorithm>

namespace awqtui {

namespace {

// ── 展示辅助（行与详情共用） ──────────────────────────────
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
    if (!t.hasDue())
        return QString();
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

// 新建清单配色（避免与已有清单重复用同色）
QString pickListColor(int index, const QList<TodoList> &existing)
{
    static const QList<QString> kPalette = {
        QStringLiteral("#4c8bf5"), QStringLiteral("#3fb950"), QStringLiteral("#d29922"),
        QStringLiteral("#e5534b"), QStringLiteral("#a371f7"), QStringLiteral("#39c5cf"),
        QStringLiteral("#f778ba"), QStringLiteral("#e3b341"),
    };
    for (int i = 0; i < kPalette.size() * 2; ++i) {
        const QString c = kPalette.at((index + i) % kPalette.size());
        bool used = false;
        for (const auto &l : existing)
            if (l.color == c) { used = true; break; }
        if (!used)
            return c;
    }
    return kPalette.at(index % kPalette.size());
}

// 彩色胶囊标签（任务标签 / 清单名共用）：主题色文字 + 淡色底 + 同色描边
QLabel *makePill(const QString &text, const QColor &c)
{
    auto *l = new QLabel(text);
    l->setAttribute(Qt::WA_TransparentForMouseEvents);
    l->setStyleSheet(
        QStringLiteral("color:%1;background:%2;border:1px solid %3;border-radius:%4;"
                       "padding:1px %5;font-size:%6;font-weight:500;")
            .arg(c.name(),
                 withAlpha(c.name().toUtf8().constData(), 0.12),
                 withAlpha(c.name().toUtf8().constData(), 0.30),
                 sp(8), sp(6), sp(11)));
    return l;
}

// 优先级中文名（详细信息用；行内用符号，见 priorityGlyph）
QString priorityName(int p)
{
    switch (p) {
    case TodoPriorityHigh: return QStringLiteral("高");
    case TodoPriorityMedium: return QStringLiteral("中");
    case TodoPriorityLow: return QStringLiteral("低");
    default: return QStringLiteral("无");
    }
}

// 设备端类型 → 中文（对齐服务端 DeviceKind 的 as_str：windows/linux/macos/android/unknown）
QString deviceKindLabel(const QString &kind)
{
    if (kind == QLatin1String("windows")) return QStringLiteral("Windows 电脑");
    if (kind == QLatin1String("linux")) return QStringLiteral("Linux 电脑");
    if (kind == QLatin1String("macos")) return QStringLiteral("macOS 电脑");
    if (kind == QLatin1String("android")) return QStringLiteral("安卓手机");
    return QString();
}

} // namespace

// ══════════════════════════════════════════════════════════
// TodoNavItem —— 左侧列表导航项（图标/圆点 + 名称 + 计数）
// ══════════════════════════════════════════════════════════
TodoNavItem::TodoNavItem(QWidget *parent)
    : QWidget(parent)
{
    setAttribute(Qt::WA_Hover);
    setCursor(Qt::PointingHandCursor);
    setObjectName(QStringLiteral("TodoNavItem"));
    setMinimumHeight(si(34));
    // 底色 / 边框 / 左侧色条全部由 QSS 画（含常态的透明边框占位，hover 时不产生位移）
    setAttribute(Qt::WA_StyledBackground, true);

    auto *lay = new QHBoxLayout(this);
    // 左 6 + border-left 3 + border-right 1 = 内容左起 10；右 7 + 1 = 8，与加边框前一致
    lay->setContentsMargins(si(6), si(3), si(7), si(3));
    lay->setSpacing(si(8));

    m_icon = new QLabel;
    m_icon->setObjectName(QStringLiteral("TodoNavIcon"));
    m_icon->setFixedSize(si(16), si(16));
    m_icon->setAlignment(Qt::AlignCenter);
    m_icon->setAttribute(Qt::WA_TransparentForMouseEvents);
    lay->addWidget(m_icon);

    m_name = new QLabel;
    m_name->setObjectName(QStringLiteral("TodoNavName"));
    m_name->setAttribute(Qt::WA_TransparentForMouseEvents);
    lay->addWidget(m_name, 1);

    m_count = new QLabel;
    m_count->setObjectName(QStringLiteral("TodoNavCount"));
    m_count->setAttribute(Qt::WA_TransparentForMouseEvents);
    m_count->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    lay->addWidget(m_count);

    // 图标 / 名称 / 计数三个装饰性子控件一律让鼠标事件穿透到本项自身。
    // 否则鼠标压到它们上面时本项会收到 Leave —— hover 强调当场闪掉。
    for (QWidget *w : findChildren<QWidget *>())
        w->setAttribute(Qt::WA_TransparentForMouseEvents);

    applyStyle();
}

void TodoNavItem::setText(const QString &name)
{
    m_name->setText(name);
}

void TodoNavItem::setGlyph(const QString &glyph)
{
    m_dot = false;
    m_glyph = glyph;
    renderIcon();
}

void TodoNavItem::setColorDot(const QColor &c)
{
    m_dot = true;
    m_dotColor = c;
    renderIcon();
}

void TodoNavItem::setCount(int n)
{
    if (n <= 0) {
        m_count->clear();
        m_count->setVisible(false);
    } else {
        m_count->setText(QString::number(n));
        m_count->setVisible(true);
    }
}

void TodoNavItem::setSelected(bool on)
{
    if (m_selected == on)
        return;
    m_selected = on;
    applyStyle();
    renderIcon();
}

void TodoNavItem::renderIcon()
{
    if (!m_icon)
        return;
    if (m_dot) {
        QPixmap pm(si(10), si(10));
        pm.fill(Qt::transparent);
        QPainter p(&pm);
        p.setRenderHint(QPainter::Antialiasing);
        p.setBrush(m_dotColor.isValid() ? m_dotColor : QColor(kColorFgMuted));
        p.setPen(Qt::NoPen);
        p.drawEllipse(pm.rect());
        p.end();
        m_icon->setPixmap(pm);
    } else {
        QColor c(kColorFgMuted);
        if (m_selected)
            c = (gTheme && gTheme->light) ? QColor(kColorAccent) : QColor(Qt::white);
        else if (m_hovered)
            c = QColor(kColorFg);   // hover 时图标提亮，强化「当前指向哪一项」
        m_icon->setPixmap(glyphIcon(m_glyph, c, si(13)).pixmap(si(16), si(16)));
    }
}

// 导航项样式：选中 > hover > 常态，三档都用 accent 派生色。
//
// 不用 QSS 的 :hover 伪态，原因有二：
//   1) 伪态同样覆盖选中态底色 —— 鼠标划过当前选中项时，选中视觉会当场消失；
//   2) 清单项随数据变化被 qDeleteAll 重建，新实例收不到 Enter，鼠标不动就一直没有 hover。
// 统一改成显式 m_hovered 驱动，与 TodoTaskRow 保持同一套写法。
void TodoNavItem::applyStyle()
{
    QString bg, bd, lf, fg, countCol;
    const bool light = (gTheme && gTheme->light);

    if (m_selected) {
        // 选中：accent 淡底 + 左侧 accent 色条 + 完整描边，hover 时整体加深一格
        bg = withAlpha(kColorAccent, m_hovered ? 0.26 : 0.17);
        bd = withAlpha(kColorAccent, m_hovered ? 0.60 : 0.40);
        lf = QString::fromLatin1(kColorAccent);
        fg = light ? QString::fromLatin1(kColorAccent) : QStringLiteral("#ffffff");
        countCol = fg;
    } else if (m_hovered) {
        // hover：更淡的 accent 底 + 左侧 accent 色条
        bg = withAlpha(kColorAccent, 0.11);
        bd = withAlpha(kColorAccent, 0.28);
        lf = withAlpha(kColorAccent, 0.85);
        fg = QString::fromLatin1(kColorFg);
        countCol = kColorMuted2;
    } else {
        // 常态：全透明（边框宽度恒定，切到 hover 时不产生任何位移）
        bg = bd = lf = QStringLiteral("transparent");
        fg = QString::fromLatin1(kColorFg);
        countCol = kColorMuted2;
    }

    setStyleSheet(
        QStringLiteral("QWidget#TodoNavItem{background:%1;border:1px solid %2;"
                       "border-left:3px solid %3;border-radius:8px;}"
                       "QWidget#TodoNavItem QLabel{background:transparent;border:none;}"
                       "QLabel#TodoNavName{color:%4;font-size:%5;font-weight:%6;}"
                       "QLabel#TodoNavCount{color:%7;font-size:%8;}")
            .arg(bg, bd, lf, fg, sp(13), m_selected ? QStringLiteral("600") : QStringLiteral("500"),
                 countCol, sp(11)));
    update();
}

void TodoNavItem::setHovered(bool on)
{
    if (m_hovered == on)
        return;
    m_hovered = on;
    applyStyle();
    renderIcon();
}

void TodoNavItem::refreshHoverFromCursor()
{
    // 不依赖 Enter/Leave 配对：直接问光标此刻落在哪。
    setHovered(rect().contains(mapFromGlobal(QCursor::pos())));
}

void TodoNavItem::enterEvent(QEnterEvent *event)
{
    setHovered(true);
    QWidget::enterEvent(event);
}

void TodoNavItem::leaveEvent(QEvent *event)
{
    // 鼠标可能只是移到了项内的图标 / 名称上，此时不应取消强调
    refreshHoverFromCursor();
    QWidget::leaveEvent(event);
}

void TodoNavItem::showEvent(QShowEvent *event)
{
    // 清单项每次数据变化都被重建，新实例收不到 Enter。
    // 显示时按光标实际位置补一次判定，鼠标没动也不会掉 hover。
    QWidget::showEvent(event);
    refreshHoverFromCursor();
}

void TodoNavItem::mousePressEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton)
        emit clicked();
    QWidget::mousePressEvent(event);
}

// 清空一个「平铺控件」布局：只删它直接持有的控件，不碰嵌套布局
// （胶囊行 / 右侧信息簇都是这种浅布局，用它就够）。
static void clearLayoutWidgets(QLayout *l)
{
    if (!l)
        return;
    while (QLayoutItem *it = l->takeAt(0)) {
        delete it->widget();
        delete it;
    }
}

// 把子布局从父布局里摘掉（摘完它的控件已清空，不再参与布局）。
// 用于「本行没有标签 / 右侧没有内容」时不留空布局 —— 空布局虽然尺寸是 0，
// 但仍会被父 QBoxLayout 算一份 spacing，白顶高行高 / 把 ⋯ 按钮往左挤。
static void dropSubLayout(QLayout *parent, QLayout *child)
{
    if (!parent || !child)
        return;
    if (QLayoutItem *it = parent->takeAt(parent->indexOf(child)))
        delete it;
}

// ══════════════════════════════════════════════════════════
// TodoTaskRow —— 单行任务（仿 TickTick：圆形勾选框 + 可换行标题 + 标签胶囊 + 右侧信息簇）
// ══════════════════════════════════════════════════════════
TodoTaskRow::TodoTaskRow(const TodoTask &task, const QString &dotColor,
                         const QString &listName, QWidget *parent)
    : QWidget(parent), m_taskId(task.id)
{
    setAttribute(Qt::WA_Hover);
    setCursor(Qt::PointingHandCursor);
    setObjectName(QStringLiteral("TodoRow"));
    setMinimumHeight(si(40));
    // 行底 / 描边 / 左侧色条全部由 QSS 画
    setAttribute(Qt::WA_StyledBackground, true);

    auto *lay = new QHBoxLayout(this);
    m_lay = lay;
    lay->setContentsMargins(si(12), si(8), si(12), si(8));
    lay->setSpacing(si(10));

    // 圆形勾选框（完成框 / 多选框互斥显示在同一位置）
    //
    // ⚠ 这里必须用 si() 而不是 sp()：sp() 返回的是 "14px" 这种**带单位**的串，
    // 拼进 `width:%1px` 会变成 `width:14pxpx` —— 非法值被 Qt 直接丢弃，
    // 指示器塌缩成 4x4 的小点（实测：box=4x4 全填充），用户看到的就是「勾选框变成一个小点」。
    //
    // 尺寸换算（Qt QSS 的 box model，已实测）：
    //   width/height 是 **content box**，border 画在它外面；
    //   border-radius 按**含边框的外框**算。
    //   所以「直径 18px 的正圆」= content 14 + border 2x2，radius = 18/2 = 9。
    const QString chkStyle =
        QStringLiteral("QCheckBox::indicator{width:%1px;height:%2px;border-radius:%3px;"
                       "border:2px solid %4;background:transparent;}"
                       "QCheckBox::indicator:hover{border-color:%5;}"
                       "QCheckBox::indicator:checked{background:%5;border-color:%5;}")
            .arg(si(14)).arg(si(14)).arg(si(9)).arg(kColorBorder, kColorAccent);

    m_selChk = new QCheckBox;
    m_selChk->setCursor(Qt::PointingHandCursor);
    m_selChk->setToolTip(QStringLiteral("选中该任务"));
    m_selChk->setStyleSheet(chkStyle);
    m_selChk->hide();
    connect(m_selChk, &QCheckBox::clicked, this, [this](bool checked) {
        m_selected = checked;
        applyRowStyle();
        emit selectionToggled(m_taskId, checked);
    });
    lay->addWidget(m_selChk);

    m_chk = new QCheckBox;
    m_chk->setChecked(task.completed);
    m_chk->setCursor(Qt::PointingHandCursor);
    m_chk->setToolTip(task.completed ? QStringLiteral("标记为未完成") : QStringLiteral("标记为已完成"));
    m_chk->setStyleSheet(chkStyle);
    connect(m_chk, &QCheckBox::clicked, this, [this](bool checked) {
        // 打勾（完成）先播一段划线动画，动画跑完才写回数据 —— 数据一落库本行就被重建掉了。
        // 取消完成 / 多选模式直接提交，不做动画。
        if (checked && !m_multi) {
            playCompleteAnimation();
            return;
        }
        emit toggleRequested(m_taskId, checked);
    });
    lay->addWidget(m_chk);

    // 中间列：可换行标题 + 标题下标签胶囊行
    auto *mid = new QVBoxLayout;
    m_midLay = mid;
    mid->setSpacing(si(3));

    m_title = new QLabel(task.title);
    m_title->setWordWrap(true);
    m_title->setTextInteractionFlags(Qt::NoTextInteraction);
    // 关键：让标题/胶囊区域鼠标事件穿透到行自身，确保 :hover 高亮可靠触发
    m_title->setAttribute(Qt::WA_TransparentForMouseEvents);
    m_title->setStyleSheet(
        QStringLiteral("font-size:%1;font-weight:600;background:transparent;%2")
            .arg(sp(14),
                 task.completed ? QStringLiteral("color:%1;text-decoration:line-through;").arg(kColorMuted2)
                                : QStringLiteral("color:%1;").arg(kColorFg)));
    mid->addWidget(m_title);

    // 胶囊行：只在有标签/清单名时才建（无则整个摘掉，不留空布局白顶行高）
    rebuildPills(task, dotColor, listName);
    lay->addLayout(mid, 1);

    // 右侧信息簇：优先级字形 + 截止徽章 + 清单圆点（宽度 m_rightW 供 heightForWidth 折算标题空间）
    auto *right = new QHBoxLayout;
    right->setSpacing(si(6));
    int rw = 0;

    if (task.priority > TodoPriorityNone) {
        auto *p = new QLabel(priorityGlyph(task.priority));
        p->setToolTip(task.priority == TodoPriorityHigh ? QStringLiteral("高优先级")
                       : task.priority == TodoPriorityMedium ? QStringLiteral("中优先级")
                                                             : QStringLiteral("低优先级"));
        p->setStyleSheet(QStringLiteral("color:%1;font-size:%2;font-weight:700;background:transparent;")
                             .arg(priorityColor(task.priority).name(), sp(12)));
        p->setFixedWidth(si(16));
        right->addWidget(p);
        rw += si(16) + si(6);
    }
    if (task.hasDue()) {
        const QDate d = QDate::fromString(task.dueDate, Qt::ISODate);
        const bool overdue = d.isValid() && !task.completed && d < QDate::currentDate();
        const bool isToday = d.isValid() && !task.completed && d == QDate::currentDate();
        QString col, bg, border;
        if (overdue) {
            col = QString::fromLatin1(kColorDanger);
            bg = withAlpha(kColorDanger, 0.10);
            border = withAlpha(kColorDanger, 0.40);
        } else if (isToday) {
            col = QString::fromLatin1(kColorAccent);
            bg = withAlpha(kColorAccent, 0.10);
            border = withAlpha(kColorAccent, 0.40);
        } else {
            col = QString::fromLatin1(kColorFgMuted);
            bg = QStringLiteral("transparent");
            border = QString::fromLatin1(kColorBorder);
        }
        auto *due = new QLabel(dueLabel(task));
        due->setStyleSheet(
            QStringLiteral("color:%1;font-size:%2;padding:1px %3;background:%4;"
                           "border:1px solid %5;border-radius:%6;font-weight:500;")
                .arg(col, sp(11), sp(6), bg, border, sp(6)));
        right->addWidget(due);
        QFont f = font();
        f.setPixelSize(si(11));
        rw += QFontMetrics(f).horizontalAdvance(dueLabel(task)) + si(12) + si(2) + si(6);
    }
    if (!dotColor.isEmpty()) {
        QPixmap pm(si(10), si(10));
        pm.fill(QColor(dotColor));
        auto *dot = new QLabel;
        dot->setPixmap(pm);
        dot->setFixedSize(si(10), si(10));
        right->addWidget(dot);
        rw += si(10) + si(6);
    }
    if (rw > 0) {
        lay->addLayout(right);
        m_rightLay = right;      // 池复用（setTask）直接操作这个指针，不再按布局下标找
        m_rightW = rw - si(6);   // 末尾多余的一层间隔不计入簇宽
    } else {
        delete right;            // 没有内容就不入布局（空布局仍会白占一份 spacing）
    }

    // 悬停浮现的「⋯」任务菜单：常驻占位、只切透明度。
    // 若改成 setVisible，鼠标移上去标题可用宽度会突变 → 换行位置跳动。
    m_more = new TodoFadeButton(this);
    m_more->setText(glyph::Menu);
    m_more->setToolTip(QStringLiteral("任务菜单"));
    m_more->setFixedSize(si(22), si(22));
    connect(m_more, &QToolButton::clicked, this, [this] {
        emit menuRequested(m_taskId, m_more->mapToGlobal(QPoint(0, m_more->height())));
    });
    lay->addWidget(m_more, 0, Qt::AlignVCenter);
    m_rightW += si(10) + si(22);   // 行内间距 + 按钮宽度（始终计入标题可用宽度）

    // 装饰性子控件（标题、标签胶囊、优先级、截止日期、清单圆点）一律让鼠标事件穿透到行自身。
    // 否则鼠标压到它们上面时行会收到 Leave —— hover 高亮当场闪掉。
    for (QWidget *w : findChildren<QWidget *>())
        w->setAttribute(Qt::WA_TransparentForMouseEvents);
    // 真正需要交互的两个（勾选框 / ⋯ 菜单）恢复接收事件，并由事件过滤器把 hover
    // 状态同步给整行 —— 这样鼠标停在勾选框上时，整行照样是强调态。
    for (QWidget *w : {static_cast<QWidget *>(m_chk), static_cast<QWidget *>(m_selChk),
                       static_cast<QWidget *>(m_more)}) {
        w->setAttribute(Qt::WA_TransparentForMouseEvents, false);
        w->installEventFilter(this);
    }

    // 首屏即应用底/hover 样式，保证鼠标移到任务项时有高亮
    setHighlighted(false);
}

void TodoTaskRow::setTask(const TodoTask &task, const QString &dotColor, const QString &listName)
{
    m_taskId = task.id;
    m_title->setText(task.title);
    m_title->setStyleSheet(
        QStringLiteral("font-size:%1;font-weight:600;background:transparent;%2")
            .arg(sp(14),
                 task.completed ? QStringLiteral("color:%1;text-decoration:line-through;").arg(kColorMuted2)
                                : QStringLiteral("color:%1;").arg(kColorFg)));
    m_chk->setChecked(task.completed);
    m_chk->setToolTip(task.completed ? QStringLiteral("标记为未完成") : QStringLiteral("标记为已完成"));
    // 重绑标签/清单胶囊行与右侧信息簇（按成员指针重建，不按布局下标取容器）
    rebuildPills(task, dotColor, listName);
    rebuildRightCluster(task, dotColor);
}

// ── 胶囊行 / 右侧信息簇：构造函数与池复用共用同一份实现，避免两处逻辑漂移 ──
void TodoTaskRow::rebuildPills(const TodoTask &task, const QString &dotColor, const QString &listName)
{
    QStringList metaPills;
    if (!listName.isEmpty())
        metaPills << listName;
    for (const auto &tag : task.tags)
        metaPills << tag;

    if (metaPills.isEmpty()) {
        if (m_pillLay) {
            clearLayoutWidgets(m_pillLay);
            dropSubLayout(m_midLay, m_pillLay);
            m_pillLay = nullptr;
        }
        m_hasMeta = false;
        return;
    }

    if (!m_pillLay) {
        m_pillLay = new QHBoxLayout;
        m_pillLay->setSpacing(si(4));
        // 追加在标题之后：m_title 永远是 mid 的第 0 项，这里只动胶囊行自己
        m_midLay->addLayout(m_pillLay);
    } else {
        clearLayoutWidgets(m_pillLay);
    }
    for (const QString &text : metaPills) {
        const QColor c = (text == listName && !dotColor.isEmpty())
                             ? QColor(dotColor) : QColor(kColorAccent);
        m_pillLay->addWidget(makePill(text, c));
    }
    m_pillLay->addStretch(1);
    m_hasMeta = true;
}

void TodoTaskRow::rebuildRightCluster(const TodoTask &task, const QString &dotColor)
{
    if (!m_lay)
        return;
    if (m_rightLay)
        clearLayoutWidgets(m_rightLay);

    // 只有确实有内容时才把簇插回布局，位置取 ⋯ 按钮之前（与构造函数里的顺序一致）
    const auto cluster = [this]() -> QHBoxLayout * {
        if (!m_rightLay) {
            m_rightLay = new QHBoxLayout;
            m_rightLay->setSpacing(si(6));
            const int at = m_more ? m_lay->indexOf(m_more) : -1;
            if (at >= 0)
                m_lay->insertLayout(at, m_rightLay);
            else
                m_lay->addLayout(m_rightLay);
        }
        return m_rightLay;
    };

    int rw = 0;
    if (task.priority > TodoPriorityNone) {
        auto *p = new QLabel(priorityGlyph(task.priority));
        p->setToolTip(task.priority == TodoPriorityHigh ? QStringLiteral("高优先级")
                               : task.priority == TodoPriorityMedium ? QStringLiteral("中优先级")
                                                                     : QStringLiteral("低优先级"));
        p->setStyleSheet(QStringLiteral("color:%1;font-size:%2;font-weight:700;background:transparent;")
                             .arg(priorityColor(task.priority).name(), sp(12)));
        p->setFixedWidth(si(16));
        cluster()->addWidget(p);
        rw += si(16) + si(6);
    }
    if (task.hasDue()) {
        const QDate d = QDate::fromString(task.dueDate, Qt::ISODate);
        const bool overdue = d.isValid() && !task.completed && d < QDate::currentDate();
        const bool isToday = d.isValid() && !task.completed && d == QDate::currentDate();
        QString col, bg, border;
        if (overdue) {
            col = QString::fromLatin1(kColorDanger);
            bg = withAlpha(kColorDanger, 0.10);
            border = withAlpha(kColorDanger, 0.40);
        } else if (isToday) {
            col = QString::fromLatin1(kColorAccent);
            bg = withAlpha(kColorAccent, 0.10);
            border = withAlpha(kColorAccent, 0.40);
        } else {
            col = QString::fromLatin1(kColorFgMuted);
            bg = QStringLiteral("transparent");
            border = QString::fromLatin1(kColorBorder);
        }
        auto *due = new QLabel(dueLabel(task));
        due->setStyleSheet(
            QStringLiteral("color:%1;font-size:%2;padding:1px %3;background:%4;"
                           "border:1px solid %5;border-radius:%6;font-weight:500;")
                .arg(col, sp(11), sp(6), bg, border, sp(6)));
        cluster()->addWidget(due);
        QFont f = font();
        f.setPixelSize(si(11));
        rw += QFontMetrics(f).horizontalAdvance(dueLabel(task)) + si(12) + si(2) + si(6);
    }
    if (!dotColor.isEmpty()) {
        QPixmap pm(si(10), si(10));
        pm.fill(QColor(dotColor));
        auto *dot = new QLabel;
        dot->setPixmap(pm);
        dot->setFixedSize(si(10), si(10));
        cluster()->addWidget(dot);
        rw += si(10) + si(6);
    }

    // m_rightW 与构造函数同口径：扣掉末尾多计的一层 spacing，再加上
    // 「行内间距 + ⋯ 按钮宽度」（⋯ 常驻，始终占掉标题可用宽度）。
    // 旧实现只在 rw > 0 时赋值：池复用到「无右侧内容」的任务时会留着上一轮的大值，标题被过度压缩。
    if (rw > 0) {
        m_rightW = (rw - si(6)) + si(10) + si(22);
    } else {
        if (m_rightLay) {
            dropSubLayout(m_lay, m_rightLay);
            m_rightLay = nullptr;
        }
        m_rightW = si(10) + si(22);
    }
}

// 长标题换行：按可用宽度（行宽 - 勾选框 - 边距 - 右侧簇）折算换行后的标题高度
int TodoTaskRow::heightForWidth(int w) const
{
    const int titleW = w - si(24) - si(18) - si(10) - m_rightW;
    int h;
    if (m_title && titleW > 40) {
        QFont f = m_title->font();
        f.setPixelSize(si(14));
        f.setWeight(QFont::DemiBold);
        const QRect r = QFontMetrics(f).boundingRect(QRect(0, 0, titleW, 4096),
                                                     Qt::TextWordWrap, m_title->text());
        h = r.height() + si(2);
    } else {
        h = si(20);
    }
    if (m_hasMeta)
        h += si(20) + si(3);
    return qMax(si(40), h + si(16));   // 上下边距各 si(8)
}

void TodoTaskRow::setHighlighted(bool on)
{
    if (m_rowStyled && m_highlighted == on)
        return;
    m_highlighted = on;
    applyRowStyle();
}

void TodoTaskRow::setMultiSelectMode(bool on)
{
    if (m_multi == on)
        return;
    m_multi = on;
    if (m_selChk)
        m_selChk->setVisible(on);
    if (m_chk)
        m_chk->setVisible(!on);
    if (!on && m_selected) {
        m_selected = false;
        if (m_selChk) {
            QSignalBlocker block(m_selChk);
            m_selChk->setChecked(false);
        }
    }
    applyRowStyle();
}

void TodoTaskRow::setSelected(bool on)
{
    if (m_selected == on) {
        if (m_selChk && m_selChk->isChecked() != on) {
            QSignalBlocker block(m_selChk);
            m_selChk->setChecked(on);
        }
        return;
    }
    m_selected = on;
    if (m_selChk) {
        QSignalBlocker block(m_selChk);
        m_selChk->setChecked(on);
    }
    applyRowStyle();
}

// 行样式：多选选中 > 详情高亮 > 普通；每一档都带 hover 强化。
// 左侧 accent 色条作为选中/高亮的视觉锚点，比整行描边更精致。
//
// hover 用显式状态 m_hovered，而不是 QSS 的 :hover 伪态 —— 实测 :hover 本身可用，
// 但行内子控件（标题 / 胶囊 / 勾选框）会截走鼠标，行随之收到 Leave，伪态中途失效。
// 所以统一按「光标是否落在行矩形内」判定，鼠标停在哪都不会掉高亮。
void TodoTaskRow::applyRowStyle()
{
    m_rowStyled = true;
    QString bg, bd, lf;
    if (m_selected) {
        // 多选选中：accent 色条 + 淡底 + 完整描边
        bg = withAlpha(kColorAccent, m_hovered ? 0.24 : 0.16);
        bd = withAlpha(kColorAccent, m_hovered ? 0.70 : 0.55);
        lf = QString::fromLatin1(kColorAccent);
    } else if (m_highlighted) {
        // 详情高亮：accent 色条 + 更淡底 + 细描边
        bg = withAlpha(kColorAccent, m_hovered ? 0.16 : 0.10);
        bd = withAlpha(kColorAccent, m_hovered ? 0.50 : 0.35);
        lf = QString::fromLatin1(kColorAccent);
    } else if (m_hovered) {
        // 普通态 hover：淡 accent 底 + accent 描边 + 左侧 accent 色条（强调）
        bg = withAlpha(kColorAccent, 0.11);
        bd = withAlpha(kColorAccent, 0.28);
        lf = withAlpha(kColorAccent, 0.85);
    } else {
        // 普通态：全透明
        bg = bd = lf = QStringLiteral("transparent");
    }
    // 行内文字 / 勾选框等子控件背景透明，避免与行高亮叠加出深色块
    setStyleSheet(QStringLiteral(
        "QWidget#TodoRow QLabel,QWidget#TodoRow QCheckBox{background:transparent;}"
        "QWidget#TodoRow{background:%1;border:1px solid %2;border-left:3px solid %3;"
        "border-radius:8px;}")
        .arg(bg, bd, lf));
}

void TodoTaskRow::setHovered(bool on)
{
    if (m_hovered == on)
        return;
    m_hovered = on;
    applyRowStyle();
    if (m_more)
        m_more->fadeTo(on ? 1.0 : 0.0);
}

void TodoTaskRow::refreshHoverFromCursor()
{
    // 不依赖 Enter/Leave 的配对：直接问光标此刻落在哪。
    // 鼠标从行移到行内的勾选框上时行会收到 Leave，但整行理应保持强调。
    setHovered(rect().contains(mapFromGlobal(QCursor::pos())));
}

bool TodoTaskRow::eventFilter(QObject *watched, QEvent *event)
{
    // 勾选框与 ⋯ 按钮要接收点击，没法设鼠标穿透，于是把它们的 Enter/Leave
    // 转成「整行是否仍处于 hover」的重新判定。
    if (event->type() == QEvent::Enter || event->type() == QEvent::Leave)
        refreshHoverFromCursor();
    // 在勾选框 / ⋯ 上右键，也弹本行的任务菜单（否则会被这几个子控件的默认上下文菜单吃掉）
    if (event->type() == QEvent::ContextMenu) {
        emit menuRequested(m_taskId, static_cast<QContextMenuEvent *>(event)->globalPos());
        return true;
    }
    return QWidget::eventFilter(watched, event);
}

void TodoTaskRow::enterEvent(QEnterEvent *event)
{
    setHovered(true);
    QWidget::enterEvent(event);
}

void TodoTaskRow::leaveEvent(QEvent *event)
{
    // 鼠标可能只是移到了行内的姊妹控件上，此时不应取消强调
    refreshHoverFromCursor();
    QWidget::leaveEvent(event);
}

void TodoTaskRow::mousePressEvent(QMouseEvent *event)
{
    // 右键：既不该「打开详情」，也不该让 QListWidget 顺带改动选中项（那会让详情栏
    // 切来切去、整行高亮闪一下再复原）。整个吃掉，菜单交给 contextMenuEvent。
    if (event->button() == Qt::RightButton) {
        event->accept();
        return;
    }
    if (event->button() != Qt::LeftButton) {
        QWidget::mousePressEvent(event);
        return;
    }
    if (m_multi) {
        // 多选模式：整行点击 = 切换选中（不打开详情）
        setSelected(!m_selected);
        emit selectionToggled(m_taskId, m_selected);
    } else {
        emit selected(m_taskId);
    }
    QWidget::mousePressEvent(event);
}

void TodoTaskRow::contextMenuEvent(QContextMenuEvent *event)
{
    // 右键落在行任意位置（含勾选框 / ⋯ 之上，见 eventFilter）都弹同一个任务菜单
    emit menuRequested(m_taskId, event->globalPos());
    event->accept();
}

// 完成动画：勾选框立刻填充，标题上有一道删除线从左往右扫过，扫完再把完成状态写回数据层。
// 顺序不能反 —— 一旦提交，列表就会重建、本行当场销毁，动画根本来不及播。
void TodoTaskRow::playCompleteAnimation()
{
    if (m_completing)
        return;
    m_completing = true;
    if (m_chk) {
        m_chk->setEnabled(false);   // 动画期间不接第二次点击
        QSignalBlocker block(m_chk);
        m_chk->setChecked(true);    // 立即变成实心圆（QSS :checked 那档）
    }

    auto *anim = new QVariantAnimation(this);
    anim->setDuration(260);
    anim->setStartValue(0.0);
    anim->setEndValue(1.0);
    anim->setEasingCurve(QEasingCurve::OutCubic);
    connect(anim, &QVariantAnimation::valueChanged, this, [this](const QVariant &v) {
        m_strike = v.toDouble();
        update();
    });
    connect(anim, &QVariantAnimation::finished, this, [this] {
        m_strike = 1.0;
        update();
        // 延到下一轮事件循环再提交：数据变更会同步重建列表并销毁本行，
        // 直接在动画自己的 finished 回调里走这条路，会踩到正在收尾的动画对象。
        QTimer::singleShot(0, this, [this] { emit toggleRequested(m_taskId, true); });
    });
    anim->start(QAbstractAnimation::DeleteWhenStopped);
}

void TodoTaskRow::paintEvent(QPaintEvent *event)
{
    QWidget::paintEvent(event);   // 行底 / hover 底色仍由 QSS（WA_StyledBackground）负责
    if (m_strike > 0.0) {
        QPainter p(this);
        paintStrike(p);
    }
}

// 标题删除线：QSS 的 text-decoration 不可动画，只能按标题的换行排版自绘。
// m_strike 是总进度 0..1，按行依次消费 —— 上一行画满了才轮到下一行。
void TodoTaskRow::paintStrike(QPainter &p)
{
    if (!m_title || m_title->text().isEmpty())
        return;
    const QRect tr = m_title->geometry();
    if (tr.width() <= 4 || tr.height() <= 0)
        return;

    QFont f = m_title->font();
    f.setPixelSize(si(14));
    f.setWeight(QFont::DemiBold);

    QTextLayout layout(m_title->text(), f);
    QTextOption opt;
    opt.setWrapMode(QTextOption::WrapAtWordBoundaryOrAnywhere);
    layout.setTextOption(opt);
    layout.beginLayout();
    while (true) {
        QTextLine line = layout.createLine();
        if (!line.isValid())
            break;
        line.setLineWidth(tr.width());
    }
    layout.endLayout();

    const int n = layout.lineCount();
    if (n <= 0)
        return;
    int total = 0;
    for (int i = 0; i < n; ++i)
        total += qCeil(layout.lineAt(i).naturalTextWidth());
    if (total <= 0)
        return;
    int remain = qRound(total * qBound(0.0, m_strike, 1.0));

    const QFontMetrics fm(f);
    QPen pen{QColor(kColorMuted2)};   // 花括号：圆括号会被当成函数声明（most vexing parse）
    pen.setWidthF(qMax(1.0, qreal(si(1))));
    pen.setCapStyle(Qt::RoundCap);

    p.save();
    p.setPen(pen);
    for (int i = 0; i < n && remain > 0; ++i) {
        const QTextLine line = layout.lineAt(i);
        const int w = qCeil(line.naturalTextWidth());
        const int drawW = qMin(w, remain);
        remain -= w;
        // 用字体自己的删除线位置，比 height()/2 更贴合字形中线
        const qreal y = tr.top() + line.y() + line.ascent() - fm.strikeOutPos();
        const qreal x = tr.left() + line.x();
        p.drawLine(QPointF(x, y), QPointF(x + drawW, y));
    }
    p.restore();
}

// ══════════════════════════════════════════════════════════
// TaskDetailsDialog —— 任务详细信息（口径对齐笔记详情）
// ══════════════════════════════════════════════════════════
TaskDetailsDialog::TaskDetailsDialog(const TodoTask &task, const QString &listName,
                                     bool showRecurrence, QWidget *parent)
    : QDialog(parent), m_deviceId(task.deviceId)
{
    setWindowTitle(QStringLiteral("任务详情 · #%1").arg(task.id));
    // 非模态：打开详情时仍能在任务列表里翻别的任务
    resize(600, 620);

    auto *lay = new QVBoxLayout(this);
    lay->setSpacing(si(8));

    auto *infoBox = new QFrame;
    infoBox->setObjectName(QStringLiteral("TaskDetailsBox"));
    infoBox->setStyleSheet(scaleQss(QStringLiteral(
        "QFrame#TaskDetailsBox { background: %1; border: 1px solid %2; border-radius: 8px; }")
        .arg(kColorBgElev, kColorBorder)));
    auto *grid = new QGridLayout(infoBox);
    grid->setContentsMargins(si(12), si(10), si(12), si(10));
    grid->setHorizontalSpacing(si(14));
    grid->setVerticalSpacing(si(6));
    grid->setColumnStretch(1, 1);

    int row = 0;
    auto addRow = [&grid, &row](const QString &label, const QString &value, const char *color) {
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
        ++row;
        return v;
    };

    static const QString kTimeFmt = QStringLiteral("yyyy-MM-dd HH:mm:ss");

    addRow(QStringLiteral("任务 ID"), QStringLiteral("#%1").arg(task.id), kColorFg);
    addRow(QStringLiteral("标题"), task.title.isEmpty() ? QStringLiteral("（空）") : task.title,
           kColorFg);

    const char *statusColor = task.completed ? kColorOk : kColorFgMuted;
    QString status = task.completed ? QStringLiteral("已完成") : QStringLiteral("进行中");
    if (task.conflict) {
        status = QStringLiteral("存在同步冲突");
        statusColor = kColorDanger;
    }
    addRow(QStringLiteral("状态"), status, statusColor);

    addRow(QStringLiteral("所属清单"),
           listName.isEmpty() ? QStringLiteral("收集箱") : listName, kColorFg);
    addRow(QStringLiteral("优先级"), priorityName(task.priority),
           task.priority == TodoPriorityHigh   ? kColorDanger
           : task.priority == TodoPriorityMedium ? kColorAccent
           : task.priority == TodoPriorityLow  ? kColorOk
                                               : kColorFgMuted);
    if (task.hasDue()) {
        const QString rel = dueLabel(task);
        addRow(QStringLiteral("截止日期"),
               rel.isEmpty() ? task.dueDate : QStringLiteral("%1（%2）").arg(task.dueDate, rel),
               kColorFg);
    } else {
        addRow(QStringLiteral("截止日期"), QStringLiteral("无"), kColorFgMuted);
    }
    if (showRecurrence)
        addRow(QStringLiteral("重复"), recurrenceLabel(task.recurrence), kColorFg);
    addRow(QStringLiteral("标签"),
           task.tags.isEmpty() ? QStringLiteral("无") : task.tags.join(QStringLiteral("、")),
           kColorFg);
    if (task.subtasks.isEmpty()) {
        addRow(QStringLiteral("子任务"), QStringLiteral("无"), kColorFgMuted);
    } else {
        const int done = task.subtasks.size() - task.openSubtaskCount();
        QString subText = QStringLiteral("共 %1 项，已完成 %2 项").arg(task.subtasks.size()).arg(done);
        auto *subs = addRow(QStringLiteral("子任务"), subText, kColorFg);
        QStringList lines;
        for (const auto &s : task.subtasks)
            lines << QStringLiteral("%1 %2").arg(s.completed ? QStringLiteral("✓") : QStringLiteral("○"), s.title);
        subs->setToolTip(lines.join(QLatin1Char('\n')));
    }

    addRow(QStringLiteral("添加时间"), formatLocal(task.createdAt, kTimeFmt), kColorFg);
    addRow(QStringLiteral("更新时间"), formatLocal(task.updatedAt, kTimeFmt), kColorFg);
    if (task.completed) {
        addRow(QStringLiteral("完成时间"),
               task.completedAt.isEmpty() ? QStringLiteral("未知")
                                          : formatLocal(task.completedAt, kTimeFmt),
               kColorFg);
    }
    if (task.syncedAt.isEmpty())
        addRow(QStringLiteral("最后同步"), QStringLiteral("未同步"), kColorWarn);
    else
        addRow(QStringLiteral("最后同步"), formatLocal(task.syncedAt, kTimeFmt), kColorFg);

    // 来源设备：本机显示「本机」，其余先显示原始 device_id（异步解析后回填设备名）
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
    {
        auto *l = new QLabel(QStringLiteral("来源设备"));
        l->setStyleSheet(scaleQss(QStringLiteral(
            "color: %1; font-size: 12px; background: transparent; border: none;")
            .arg(kColorFgMuted)));
        grid->addWidget(l, row, 0, Qt::AlignTop);
        grid->addWidget(m_deviceValue, row, 1);
        ++row;
    }

    addRow(QStringLiteral("当前版本"), QStringLiteral("v%1").arg(task.version), kColorFg);
    addRow(QStringLiteral("备注长度"),
           task.notes.isEmpty() ? QStringLiteral("无备注")
                                : QStringLiteral("%1 字符").arg(task.notes.length()),
           kColorFg);

    lay->addWidget(infoBox);

    // ---- 备注原文 ----
    auto *notesLabel = new QLabel(QStringLiteral("备注"));
    notesLabel->setStyleSheet(scaleQss(QStringLiteral(
        "color: %1; font-size: 13px; font-weight: 600; background: transparent; border: none;")
        .arg(kColorFg)));
    lay->addWidget(notesLabel);

    auto *notes = new QPlainTextEdit;
    notes->setReadOnly(true);
    notes->setPlaceholderText(QStringLiteral("（无备注）"));
    notes->setPlainText(task.notes);
    notes->setStyleSheet(scaleQss(QStringLiteral(
        "QPlainTextEdit { background: %1; border: 1px solid %2; border-radius: 6px; }")
                              .arg(kColorBgElev, kColorBorder)));
    lay->addWidget(notes, 1);

    auto *btnRow = new QHBoxLayout;
    auto *btnCopy = new QPushButton(QStringLiteral("复制标题与备注"));
    connect(btnCopy, &QPushButton::clicked, this, [this, task] {
        const QString text = task.notes.isEmpty()
                                 ? task.title
                                 : QStringLiteral("%1\n\n%2").arg(task.title, task.notes);
        QApplication::clipboard()->setText(text);
    });
    btnRow->addWidget(btnCopy);
    btnRow->addStretch(1);
    auto *btnClose = new QPushButton(QStringLiteral("关闭"));
    connect(btnClose, &QPushButton::clicked, this, &QDialog::reject);
    btnRow->addWidget(btnClose);
    lay->addLayout(btnRow);
}

void TaskDetailsDialog::setDeviceName(const QString &name)
{
    if (!m_deviceValue || name.isEmpty())
        return;
    m_deviceValue->setText(name);
}

// ══════════════════════════════════════════════════════════
// TodoPage
// ══════════════════════════════════════════════════════════
TodoPage::TodoPage(TodoSource *source, QWidget *parent)
    : QWidget(parent), m_source(source)
{
    m_cardPool = new TodoPool(this, 3);
    buildUi();
    connect(m_source, &TodoSource::dataChanged, this, &TodoPage::onDataChanged);
    m_source->load();
}

TodoPage::~TodoPage()
{
    delete m_cardPool;
    delete ui;
}

void TodoPage::refresh()
{
    // 真重拉：这里以前只是 onDataChanged() 重绘内存快照，一个请求都不发 ——
    // 手机端的任务即便已同步进本机 todo.db，界面也要等用户在本机写一次才更新。
    // 重绘交给 store 回包后的 dataChanged；renderSignature 会让无变化的轮次不重建列表。
    flushPendingEdits();  // debounce 里的标题/备注先落库，别被一轮重拉盖掉
    m_source->load();
}

// 退出前冲刷：标题/备注是 250ms debounce 提交（见 buildUi 里 m_commitTimer 的接线），
// 直接 quit 会丢掉停手不到 250ms 的那次输入。单实例让位与正常退出都先走这里。
void TodoPage::flushPendingEdits()
{
    if (m_commitTimer && m_commitTimer->isActive())
        commitDetail();   // commitDetail 自身会 stop 定时器，并做 m_selectedTask/加载态防护
}

// 统一几何：页面栅格（8pt）+ 卡片内边距 + 三栏内边距 + 详情栏固定宽度。
// buildUi() 与 applyUiScale() 都走这里，避免两处数值漂移。
void TodoPage::applyMetrics()
{
    // 页面：左右 20、下 20；顶距由卡片内边距提供
    ui->rootLayout->setContentsMargins(si(20), 0, si(20), si(20));
    ui->rootLayout->setSpacing(si(12));

    // 整页唯一一张卡片：左导航 + 任务列表 + 详情三栏，两处 1px 内缩分隔线
    ui->surfaceLay->setContentsMargins(0, si(20), 0, si(18));
    ui->surfaceLay->setSpacing(0);
    ui->navSep->setFixedWidth(qMax(1, si(1)));
    ui->colSep->setFixedWidth(qMax(1, si(1)));
    // 平铺视图下详情栏收起、板占满整宽 → 右侧不再需要给分隔线留内缩
    ui->listLay->setContentsMargins(si(20), 0, m_boardMode ? si(20) : si(16), 0);
    ui->listLay->setSpacing(si(12));
    ui->detailLay->setContentsMargins(si(16), 0, si(20), 0);
    ui->detailLay->setSpacing(si(12));
    ui->bodyLay->setSpacing(si(10));
    ui->form->setHorizontalSpacing(si(8));
    ui->form->setVerticalSpacing(si(8));
    ui->form->setColumnMinimumWidth(0, si(56));   // 标签列固定，控件列吃掉余量
    ui->form->setColumnStretch(1, 1);
    ui->dueRow->setSpacing(si(8));
    ui->footLay->setSpacing(si(10));
    ui->subsLay->setContentsMargins(si(6), si(6), si(6), si(6));
    ui->dNotes->setMinimumHeight(si(52));
    ui->subsBox->setMinimumHeight(si(128));
    ui->TodoQuickAdd->setFixedHeight(si(36));
    if (m_bulkBar && ui->bulkLay) {
        ui->bulkLay->setContentsMargins(si(10), si(6), si(10), si(6));
        ui->bulkLay->setSpacing(si(4));
    }
    if (m_detailPanel) {
        // 详情栏常驻：固定宽度（不再滑入 / 收起）
        m_detailW = si(340);
        m_detailPanel->setFixedWidth(m_detailW);
    }
}

void TodoPage::applyUiScale()
{
    applyMetrics();

    applyPageStyles();
    rebuildNavLists();
    updateNavCounts();
    setNavChecked();
    rebuildList();
}

void TodoPage::buildUi()
{
    // 静态布局来自 Qt Designer（todopage.ui -> ui_todopage.h），
    // .ui 中的边距/间距为基准值，si() 缩放几何在此重设
    ui = new Ui::TodoPage;
    ui->setupUi(this);

    // 纯 QWidget 想画 QSS 的底色 / 边框，必须打开 WA_StyledBackground
    for (QWidget *w : {ui->TodoSurface, ui->footRow, ui->subsBox})
        w->setAttribute(Qt::WA_StyledBackground, true);

    // ── 成员别名：业务逻辑沿用 m_* 指针，静态布局归属 .ui 文件 ──
    m_surface = ui->TodoSurface;
    m_viewTitle = ui->TodoViewTitle;
    m_viewCount = ui->TodoViewCount;
    m_quickAdd = ui->TodoQuickAdd;
    m_list = ui->TodoList;
    m_completedBtn = ui->completedBtn;
    m_completedBtn->setObjectName(QStringLiteral("TodoCompleted"));   // 与 applyPageStyles 的选择器对齐
    m_progress = ui->TodoProgress;
    m_detailPanel = ui->TodoDetail;
    m_detailEmpty = ui->TodoDetailEmpty;
    m_detailBody = ui->detailBody;
    // 卡片标题：与「计数 / 排序」同一行，给卡片一个身份（原为隐藏）
    m_viewTitle->setVisible(true);
    m_dTitle = ui->TodoTitleEdit;
    // 标题框高度随内容自适应（多行完全展开），且内容变化时提交
    if (m_dTitle) {
        auto *layout = m_dTitle->document()->documentLayout();
        const auto updateH = [this, layout] {
            const int docH = int(layout->documentSize().height());
            m_dTitle->setFixedHeight(qMax(si(32), docH + si(14)));
            m_dTitle->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
            m_dTitle->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        };
        connect(m_dTitle->document(), &QTextDocument::contentsChanged, this, updateH);
        updateH();
    }
    m_dDone = ui->dDone;
    m_dList = ui->dList;
    m_dPriority = ui->dPriority;
    m_dHasDue = ui->dHasDue;
    m_dDue = ui->dDue;
    m_dRecur = ui->dRecur;
    m_dTags = ui->dTags;
    m_dNotes = ui->dNotes;
    m_dSubs = ui->TodoSubs;
    m_dSubAdd = ui->dSubAdd;
    m_dDelete = ui->dDelete;

    // ── 运行时几何（随 UI 缩放变化，无法烘焙进 .ui） ──
    // 必须放在别名赋值之后：applyMetrics 会读 m_detailPanel / m_bulkBar，
    // 提前调用时它们是未初始化的野指针（曾导致启动即段错误）。
    applyMetrics();

    // ── 左侧列表导航（智能清单 + 自定义清单 + 已完成） ──
    buildNav();

    // ── 视图模式（列表 / 平铺）分段开关，插在「排序」左侧 ──
    m_boardMode = loadTodoBoardMode();
    buildViewToggle();

    // 字段标签 objectName（applyPageStyles 按 TodoFieldLabel findChildren 匹配）
    for (QLabel *l : {ui->lblList, ui->lblPriority, ui->lblDue, ui->lblRecur,
                      ui->lblTags, ui->notesLabel, ui->subLabel})
        l->setObjectName(QStringLiteral("TodoFieldLabel"));

    // ── 排序模式（对齐 Android ⋮ 排序子菜单；选择持久化到 awqtui.ini） ──
    m_sortBox = ui->sortBox;
    m_sortBox->setItemData(0, static_cast<int>(SortMode::Default));
    m_sortBox->setItemData(1, static_cast<int>(SortMode::NewestFirst));
    m_sortBox->setItemData(2, static_cast<int>(SortMode::Reverse));
    m_sortBox->setItemData(3, static_cast<int>(SortMode::ByPriority));
    m_sortBox->setItemData(4, static_cast<int>(SortMode::ByDue));
    m_sort = static_cast<SortMode>(loadTodoSortMode());
    // 恢复持久化选择（放在 connect 之前，避免触发保存与重建）
    const int sortIdx = m_sortBox->findData(static_cast<int>(m_sort));
    if (sortIdx >= 0)
        m_sortBox->setCurrentIndex(sortIdx);
    connect(m_sortBox, &QComboBox::currentIndexChanged, this, [this](int idx) {
        const int mode = m_sortBox->itemData(idx).toInt();
        m_sort = static_cast<SortMode>(mode);
        saveTodoSortMode(mode);
        rebuildList();
    });

    // ── 详情下拉条目的 userData ──
    m_dPriority->setItemData(0, TodoPriorityNone);
    m_dPriority->setItemData(1, TodoPriorityLow);
    m_dPriority->setItemData(2, TodoPriorityMedium);
    m_dPriority->setItemData(3, TodoPriorityHigh);
    m_dRecur->setItemData(0, QString());
    m_dRecur->setItemData(1, QStringLiteral("daily"));
    m_dRecur->setItemData(2, QStringLiteral("weekdays"));
    m_dRecur->setItemData(3, QStringLiteral("weekly"));
    m_dRecur->setItemData(4, QStringLiteral("monthly"));
    m_dDue->setDate(QDate::currentDate());
    // API 数据源：服务端不支持重复规则（对齐 Android supportsRecurrence=false 隐藏 UI）
    if (!m_source->supportsRecurrence()) {
        ui->lblRecur->hide();
        m_dRecur->hide();
    }

    // ── 行控件跟随视口宽度重排（退出全屏/还原窗口时避免行右侧控件被顶出可视区） ──
    new ItemWidgetRelayoutFilter(m_list, 60, m_list);
    new ItemWidgetRelayoutFilter(m_dSubs, 60, m_dSubs);

    // ── 多选（批量操作）控件 ──
    buildBulkBar();

    // ── 平铺（看板）视图 ──
    buildBoardView();

    // ── 信号连接 ──
    connect(m_quickAdd, &QLineEdit::returnPressed, this, &TodoPage::onQuickAdd);
    connect(m_completedBtn, &QPushButton::clicked, this, [this](bool on) {
        m_showCompleted = on;
        rebuildList();
    });
    connect(m_dTitle, &QPlainTextEdit::textChanged, this, [this] { m_commitTimer->start(); });
    connect(m_dDone, &QCheckBox::toggled, this, [this](bool on) {
        if (m_loadingDetail || m_selectedTask == 0)
            return;
        m_source->setTaskCompleted(m_selectedTask, on);
    });
    connect(m_dList, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this](int) {
        if (!m_loadingDetail)
            commitDetail();
    });
    connect(m_dPriority, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this](int) {
        if (!m_loadingDetail)
            commitDetail();
    });
    connect(m_dHasDue, &QCheckBox::toggled, this, [this](bool on) {
        m_dDue->setEnabled(on);
        if (!m_loadingDetail)
            commitDetail();
    });
    connect(m_dDue, &QDateEdit::dateChanged, this, [this](const QDate &) {
        if (!m_loadingDetail)
            commitDetail();
    });
    connect(m_dRecur, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this](int) {
        if (!m_loadingDetail)
            commitDetail();
    });
    connect(m_dTags, &QLineEdit::editingFinished, this, &TodoPage::commitDetail);
    connect(m_dNotes, &QPlainTextEdit::textChanged, this, [this] { m_commitTimer->start(); });
    connect(m_dSubAdd, &QLineEdit::returnPressed, this, &TodoPage::onSubtaskAdd);
    connect(m_dDelete, &QPushButton::clicked, this, &TodoPage::onTaskDelete);

    m_commitTimer = new QTimer(this);
    m_commitTimer->setSingleShot(true);
    m_commitTimer->setInterval(250);
    connect(m_commitTimer, &QTimer::timeout, this, &TodoPage::commitDetail);

    m_detailBody->hide();

    // 详情页顶部的「收起」按钮：收起详情面板（回到仅任务列表）
    if (auto *closeBtn = ui->detailClose)
        connect(closeBtn, &QPushButton::clicked, this, [this] {
            if (m_selectedTask)
                clearDetail();
        });

    // 详情页顶部的「信息」：弹任务详细信息（同步元信息、来源设备）
    if (auto *infoBtn = ui->detailInfo)
        connect(infoBtn, &QPushButton::clicked, this, [this] {
            if (m_selectedTask)
                showTaskDetails(m_selectedTask);
        });

    applyPageStyles();
    rebuildNavLists();
    updateNavCounts();
    setNavChecked();

    // 落到持久化的视图模式（列表 / 平铺）
    setBoardMode(m_boardMode);
}

void TodoPage::applyPageStyles()
{
    // 左侧列表导航：与主内容区融为一体，无额外底色
    if (auto *sep = ui->navSep)
        sep->setStyleSheet(
            QStringLiteral("QFrame#navSep{background:%1;border:none;}").arg(withAlpha(kColorBorder, 0.50)));
    if (m_navListLabel)
        m_navListLabel->setStyleSheet(
            QStringLiteral("color:%1;font-size:%2;font-weight:600;padding:%3 %4;")
                .arg(kColorMuted2, sp(11), sp(6), sp(10)));
    if (m_newListBtn)
        m_newListBtn->setStyleSheet(
            QStringLiteral("QToolButton#TodoNewList{border:none;border-radius:8px;color:%1;"
                           "font-size:%2;padding:%3 %4;background:transparent;}"
                           "QToolButton#TodoNewList:hover{background:%5;color:%6;}")
                .arg(kColorFgMuted, sp(12), sp(6), sp(10),
                     withAlpha(kColorFg, 0.06), kColorFg));

    // ── 整页唯一一张卡片：精致玻璃质感 + 双层描边轮廓 + 顶部微高光 ──
    if (m_surface) {
        // 外层：微亮描边（玻璃边缘反光感），内层：实色描边强化轮廓
        // 用 QSS 的 border 做主描边，顶部用 inset 阴影模拟高光
        const QString topHi = gTheme && gTheme->light
            ? QStringLiteral("rgba(255,255,255,0.06)")
            : QStringLiteral("rgba(255,255,255,0.04)");
        m_surface->setStyleSheet(
            QStringLiteral("QWidget#TodoSurface{"
                           "background:%1;"
                           "border:1px solid %2;"
                           "border-radius:12px;"
                           "border-top-color:%3;"
                           "}")
                .arg(glassBg(kColorBgElev),
                     glassBorder(),
                     topHi));
        if (shadowsEnabled())
            m_surfaceShadow = makeDropShadow(m_surface, 2);
        else
            clearDropShadow(m_surface, m_surfaceShadow);
    }
    // 两栏之间：1px 内缩分隔线（不贴卡片上下边缘）
    if (auto *sep = ui->colSep)
        sep->setStyleSheet(
            QStringLiteral("QFrame#colSep{background:%1;border:none;}").arg(withAlpha(kColorBorder, 0.50)));

    if (m_viewTitle)
        m_viewTitle->setStyleSheet(QStringLiteral("font-size:%1;font-weight:600;color:%2;").arg(sp(15), kColorFg));
    if (m_viewCount)
        m_viewCount->setStyleSheet(QStringLiteral("color:%1;font-size:%2;").arg(kColorFgMuted, sp(12)));

    // 排序下拉：幽灵按钮风格，与视图切换按钮一致
    if (m_sortBox)
        m_sortBox->setStyleSheet(
            QStringLiteral("QComboBox#sortBox{background:transparent;border:1px solid transparent;"
                           "border-radius:6px;color:%1;padding:0 %2;font-size:%3;min-height:%4;}"
                           "QComboBox#sortBox:hover{background:%5;border-color:%6;color:%7;}"
                           "QComboBox#sortBox::drop-down{border:none;width:%2;}"
                           "QComboBox#sortBox QAbstractItemView{"
                           "background:%8;border:1px solid %9;border-radius:8px;padding:4px;"
                           "selection-background-color:%10;selection-color:white;}")
                .arg(kColorFgMuted, sp(8), sp(12), sp(28),
                     withAlpha(kColorFg, 0.06), withAlpha(kColorBorder, 0.7), kColorFg,
                     kColorBgElev2, kColorBorder, kColorAccent));

    // 快速添加输入框：聚焦时描边加深 + 背景变实
    if (m_quickAdd)
        m_quickAdd->setStyleSheet(
            QStringLiteral("QLineEdit#TodoQuickAdd{font-size:%1;padding:0 %2;border:1px solid %3;"
                           "border-radius:8px;background:%4;color:%5;}"
                           "QLineEdit#TodoQuickAdd:hover{border-color:%6;}"
                           "QLineEdit#TodoQuickAdd:focus{border-color:%7;"
                           "background:%8;}")
                .arg(sp(13), sp(12), kColorBorder, withAlpha(kColorBgElev2, 0.55), kColorFg,
                     withAlpha(kColorAccent, 0.6),
                     kColorAccent,
                     withAlpha(kColorBgElev2, 0.75)));
    if (m_list)
        m_list->setStyleSheet(
            QStringLiteral("QListWidget#TodoList{background:transparent;border:none;outline:none;}"
                           "QListWidget#TodoList::item{border:none;padding:%1;}"
                           "QListWidget#TodoList::item:hover,QListWidget#TodoList::item:selected{background:transparent;}")
                .arg(sp(3)));
    if (m_completedBtn)
        m_completedBtn->setStyleSheet(
            QStringLiteral("QPushButton#TodoCompleted{text-align:left;padding:%1 %2;border:1px solid transparent;"
                           "border-radius:6px;color:%3;background:transparent;font-size:%4;}"
                           "QPushButton#TodoCompleted:hover{background:%5;color:%6;border-color:%7;}")
                .arg(sp(6), sp(10), kColorFgMuted, sp(12), withAlpha(kColorAccent, 0.08), kColorFg,
                     withAlpha(kColorAccent, 0.25)));
    if (m_progress)
        m_progress->setStyleSheet(QStringLiteral("color:%1;font-size:%2;").arg(kColorFgMuted, sp(12)));
    // 底部状态行：上方 1px 极淡分隔
    if (auto *foot = ui->footRow)
        foot->setStyleSheet(
            QStringLiteral("QWidget#footRow{background:transparent;border:none;border-top:1px solid %1;}")
                .arg(withAlpha(kColorBorder, 0.55)));

    // 详情分栏：透明底
    if (m_detailPanel)
        m_detailPanel->setStyleSheet(QStringLiteral("QWidget#TodoDetail{background:transparent;border:none;}"));
    if (m_detailEmpty)
        m_detailEmpty->setStyleSheet(
            QStringLiteral("color:%1;font-size:%2;padding:%3;").arg(kColorFgMuted, sp(12), sp(16)));

    // 详情内输入控件：统一高度 + 聚焦外发光
    if (m_detailBody)
        m_detailBody->setStyleSheet(
            QStringLiteral("QWidget#detailBody QLineEdit,QWidget#detailBody QPlainTextEdit,"
                           "QWidget#detailBody QComboBox,QWidget#detailBody QDateEdit{"
                           "background:%1;border:1px solid %2;border-radius:8px;color:%3;font-size:%4;"
                           "selection-background-color:%5;selection-color:white;}"
                           "QWidget#detailBody QLineEdit,QWidget#detailBody QComboBox,QWidget#detailBody QDateEdit{"
                           "min-height:%6;padding:0 %7;}"
                           "QWidget#detailBody QPlainTextEdit{padding:%8 %7;}"
                           "QWidget#detailBody QComboBox::drop-down{border:none;width:20px;}"
                           "QWidget#detailBody QComboBox QAbstractItemView{"
                           "background:%9;border:1px solid %2;border-radius:8px;padding:4px;"
                           "selection-background-color:%5;selection-color:white;}"
                           "QWidget#detailBody QLineEdit:hover,QWidget#detailBody QPlainTextEdit:hover,"
                           "QWidget#detailBody QComboBox:hover,QWidget#detailBody QDateEdit:hover{border-color:%10;}"
                           "QWidget#detailBody QLineEdit:focus,QWidget#detailBody QPlainTextEdit:focus,"
                           "QWidget#detailBody QComboBox:focus,QWidget#detailBody QDateEdit:focus{"
                           "border-color:%5;background:%11;}")
                .arg(withAlpha(kColorBgElev2, 0.5), kColorBorder, kColorFg, sp(13), kColorAccent,
                     sp(28), sp(8), sp(6),
                     kColorBgElev2,
                     withAlpha(kColorAccent, 0.55),
                     withAlpha(kColorBgElev2, 0.75)));
    if (auto *head = ui->detailHeadTitle)
        head->setStyleSheet(
            QStringLiteral("color:%1;font-size:%2;font-weight:600;").arg(kColorFg, sp(13)));
    if (auto *closeBtn = ui->detailClose)
        closeBtn->setStyleSheet(
            QStringLiteral("QPushButton#detailClose{border:none;border-radius:8px;color:%1;"
                           "background:transparent;font-size:%2;padding:2px 8px;min-width:%3;min-height:%3;}"
                           "QPushButton#detailClose:hover{background:%4;color:%5;}")
                .arg(kColorFgMuted, sp(14), sp(24),
                     withAlpha(kColorFg, 0.08), kColorFg));
    if (auto *infoBtn = ui->detailInfo)
        infoBtn->setStyleSheet(
            QStringLiteral("QPushButton#detailInfo{border:none;border-radius:8px;color:%1;"
                           "background:transparent;font-size:%2;padding:2px 8px;}"
                           "QPushButton#detailInfo:hover{background:%3;color:%4;}")
                .arg(kColorFgMuted, sp(12), withAlpha(kColorFg, 0.08), kColorFg));
    if (m_dTitle)
        m_dTitle->setStyleSheet(
            QStringLiteral("QPlainTextEdit#TodoTitleEdit{font-size:%1;font-weight:600;border:1px solid transparent;"
                           "background:transparent;padding:6px 8px;border-radius:8px;}"
                           "QPlainTextEdit#TodoTitleEdit:hover{border:1px solid %2;}"
                           "QPlainTextEdit#TodoTitleEdit:focus{border:1px solid %3;background:%4;}")
                .arg(sp(16), withAlpha(kColorBorder, 0.6), kColorAccent, withAlpha(kColorBgElev2, 0.4)));

    // 子任务容器：淡底 + 描边，内部更紧凑
    if (auto *box = ui->subsBox)
        box->setStyleSheet(
            QStringLiteral("QWidget#subsBox{background:%1;border:1px solid %2;border-radius:8px;}")
                .arg(withAlpha(kColorBgElev2, 0.3), kColorBorder));
    if (m_dSubs)
        m_dSubs->setStyleSheet(
            QStringLiteral("QListWidget#TodoSubs{background:transparent;border:none;outline:none;}"
                           "QListWidget#TodoSubs::item{border:none;padding:%1;}"
                           "QListWidget#TodoSubs::item:hover,QListWidget#TodoSubs::item:selected{background:transparent;}")
                .arg(sp(1)));
    if (m_dSubAdd)
        m_dSubAdd->setStyleSheet(
            QStringLiteral("QLineEdit#dSubAdd{background:transparent;border:none;border-top:1px solid %1;"
                           "padding:%2 %3;font-size:%4;color:%5;}"
                           "QLineEdit#dSubAdd:focus{border-top-color:%6;background:%7;}")
                .arg(withAlpha(kColorBorder, 0.55), sp(6), sp(4), sp(12), kColorFg,
                     kColorAccent, withAlpha(kColorAccent, 0.04)));

    // 删除按钮：危险色描边 + 悬浮填色
    if (m_dDelete)
        m_dDelete->setStyleSheet(
            QStringLiteral("QPushButton#dDelete{border:1px solid %1;border-radius:8px;color:%2;"
                           "background:transparent;font-size:%3;min-height:%4;font-weight:500;}"
                           "QPushButton#dDelete:hover{background:%5;border-color:%2;color:%6;}")
                .arg(withAlpha(kColorDanger, 0.45), kColorDanger, sp(12), sp(30),
                     kColorDanger, QStringLiteral("#ffffff")));

    for (QLabel *l : m_detailBody ? m_detailBody->findChildren<QLabel *>(QStringLiteral("TodoFieldLabel")) : QList<QLabel *>())
        l->setStyleSheet(QStringLiteral("color:%1;font-size:%2;font-weight:500;").arg(kColorFgMuted, sp(12)));

    // 顶栏「多选」入口：胶囊描边按钮，选中态填充 accent 渐变
    if (m_multiBtn)
        m_multiBtn->setStyleSheet(
            QStringLiteral("QToolButton#btnMulti{padding:%1 %2;border:1px solid %3;border-radius:8px;"
                           "color:%4;background:transparent;font-size:%5;font-weight:500;}"
                           "QToolButton#btnMulti:hover{border-color:%6;color:%6;background:%7;}"
                           "QToolButton#btnMulti:checked{background:%8;border-color:%8;"
                           "color:#ffffff;font-weight:600;}")
                .arg(sp(6), sp(12), kColorBorder, kColorFgMuted, sp(12),
                     kColorAccent, withAlpha(kColorAccent, 0.08),
                     kColorAccent));

    // 批量操作条：accent 淡底 + 顶部强调线 + 精致按钮态
    if (m_bulkBar)
        m_bulkBar->setStyleSheet(
            QStringLiteral("QWidget#TodoBulkBar{background:%1;border:1px solid %2;border-radius:%3px;"
                           "border-top:2px solid %4;}"
                           "QWidget#TodoBulkBar QToolButton{color:%5;border:none;background:transparent;"
                           "font-size:%6;padding:%7 %8;border-radius:6px;font-weight:500;}"
                           "QWidget#TodoBulkBar QToolButton:hover{background:%9;color:%10;}"
                           "QWidget#TodoBulkBar QToolButton:pressed{background:%11;}"
                           "QWidget#TodoBulkBar QToolButton:disabled{color:%12;}"
                           "QWidget#TodoBulkBar QLabel{background:transparent;color:%10;"
                           "font-size:%6;font-weight:600;}")
                .arg(withAlpha(kColorAccent, 0.09), withAlpha(kColorAccent, 0.30), sp(8),
                     kColorAccent,
                     kColorFgMuted, sp(12), sp(4), sp(8),
                     withAlpha(kColorAccent, 0.16), kColorFg,
                     withAlpha(kColorAccent, 0.24),
                     kColorMuted2));
}

void TodoPage::buildBulkBar()
{
    // 顶栏「多选」入口（checkable：再点一次退出多选）
    m_multiBtn = ui->btnMulti;
    m_multiBtn->setCheckable(true);
    connect(m_multiBtn, &QToolButton::toggled, this, &TodoPage::onMultiToggled);

    m_bulkBar = ui->TodoBulkBar;
    m_bulkBar->hide();   // 仅多选模式下显示
    ui->bulkLay->setContentsMargins(si(10), si(6), si(10), si(6));
    ui->bulkLay->setSpacing(si(4));

    m_bulkCount = ui->bulkCount;
    m_bulkSelectAll = ui->bulkSelectAll;
    m_bulkDone = ui->bulkDone;
    m_bulkUndone = ui->bulkUndone;
    m_bulkMoveBtn = ui->bulkMove;
    m_bulkPriorityBtn = ui->bulkPriority;
    m_bulkDueBtn = ui->bulkDue;
    m_bulkDeleteBtn = ui->bulkDelete;

    connect(m_bulkSelectAll, &QToolButton::clicked, this, &TodoPage::onBulkSelectAll);
    connect(m_bulkDone, &QToolButton::clicked, this, [this] { onBulkSetCompleted(true); });
    connect(m_bulkUndone, &QToolButton::clicked, this, [this] { onBulkSetCompleted(false); });
    connect(m_bulkDeleteBtn, &QToolButton::clicked, this, &TodoPage::onBulkDelete);

    // 移动到清单：菜单每次弹出时按当前清单重建
    m_bulkMoveBtn->setPopupMode(QToolButton::InstantPopup);
    m_bulkMoveMenu = new QMenu(m_bulkMoveBtn);
    m_bulkMoveBtn->setMenu(m_bulkMoveMenu);
    connect(m_bulkMoveMenu, &QMenu::aboutToShow, this, [this] {
        m_bulkMoveMenu->clear();
        m_bulkMoveMenu->addAction(QStringLiteral("收集箱"), this, [this] { onBulkMove(0); });
        for (const auto &l : m_lists)
            m_bulkMoveMenu->addAction(l.name, this, [this, l] { onBulkMove(l.id); });
    });

    // 优先级
    m_bulkPriorityBtn->setPopupMode(QToolButton::InstantPopup);
    m_bulkPriorityMenu = new QMenu(m_bulkPriorityBtn);
    m_bulkPriorityBtn->setMenu(m_bulkPriorityMenu);
    connect(m_bulkPriorityMenu, &QMenu::aboutToShow, this, [this] {
        m_bulkPriorityMenu->clear();
        m_bulkPriorityMenu->addAction(QStringLiteral("无优先级"), this,
                                      [this] { onBulkPriority(TodoPriorityNone); });
        m_bulkPriorityMenu->addAction(QStringLiteral("低"), this,
                                      [this] { onBulkPriority(TodoPriorityLow); });
        m_bulkPriorityMenu->addAction(QStringLiteral("中"), this,
                                      [this] { onBulkPriority(TodoPriorityMedium); });
        m_bulkPriorityMenu->addAction(QStringLiteral("高"), this,
                                      [this] { onBulkPriority(TodoPriorityHigh); });
    });

    // 截止日期（相对日期快捷项；清除项仅在本地源下生效，见 TodoSource 注释）
    m_bulkDueBtn->setPopupMode(QToolButton::InstantPopup);
    m_bulkDueMenu = new QMenu(m_bulkDueBtn);
    m_bulkDueBtn->setMenu(m_bulkDueMenu);
    connect(m_bulkDueMenu, &QMenu::aboutToShow, this, [this] {
        m_bulkDueMenu->clear();
        const QDate today = QDate::currentDate();
        m_bulkDueMenu->addAction(QStringLiteral("今天"), this,
                                 [this, today] { onBulkDue(today.toString(Qt::ISODate)); });
        m_bulkDueMenu->addAction(QStringLiteral("明天"), this,
                                 [this, today] { onBulkDue(today.addDays(1).toString(Qt::ISODate)); });
        m_bulkDueMenu->addAction(QStringLiteral("后天"), this,
                                 [this, today] { onBulkDue(today.addDays(2).toString(Qt::ISODate)); });
        m_bulkDueMenu->addAction(QStringLiteral("一周后"), this,
                                 [this, today] { onBulkDue(today.addDays(7).toString(Qt::ISODate)); });
        m_bulkDueMenu->addSeparator();
        QAction *clearDue = m_bulkDueMenu->addAction(QStringLiteral("清除截止日期"), this,
                                                    [this] { onBulkDue(QString()); });
        if (!m_source->supportsDueClear()) {
            // 服务端 due_date 为 Option 字段，省略即保留原值，清空无法生效
            clearDue->setEnabled(false);
            clearDue->setToolTip(QStringLiteral("服务端不支持清空截止日期"));
        }
    });

    updateBulkBar();
}

// ══════════════════════════════════════════════════════════
// 平铺（看板）视图
// ══════════════════════════════════════════════════════════
// 列表头部的「列表 / 平铺」分段开关（插在排序控件左侧）
void TodoPage::buildViewToggle()
{
    m_viewSeg = new QWidget(this);
    m_viewSeg->setObjectName(QStringLiteral("TodoViewSeg"));
    m_viewSeg->setAttribute(Qt::WA_StyledBackground, true);

    auto *lay = new QHBoxLayout(m_viewSeg);
    lay->setContentsMargins(si(2), si(2), si(2), si(2));
    lay->setSpacing(0);

    const auto makeBtn = [this, lay](const QString &text, const QString &tip) {
        auto *b = new QToolButton(m_viewSeg);
        b->setObjectName(QStringLiteral("TodoSegBtn"));
        b->setText(text);
        b->setToolTip(tip);
        b->setCheckable(true);
        b->setFocusPolicy(Qt::NoFocus);
        b->setCursor(Qt::PointingHandCursor);
        b->setToolButtonStyle(Qt::ToolButtonTextOnly);
        b->setFixedHeight(si(24));
        lay->addWidget(b);
        return b;
    };
    m_segList = makeBtn(QStringLiteral("列表"), QStringLiteral("列表视图"));
    m_segBoard = makeBtn(QStringLiteral("平铺"),
                         QStringLiteral("平铺视图：清单横排成列，任务卡片可直接拖到别的清单"));

    // 用 clicked 而不是 toggled：避免 setBoardMode 内回写 checked 时递归
    connect(m_segList, &QToolButton::clicked, this, [this] { setBoardMode(false); });
    connect(m_segBoard, &QToolButton::clicked, this, [this] { setBoardMode(true); });

    m_viewSeg->setStyleSheet(
        QStringLiteral(
            "QWidget#TodoViewSeg{background:%1;border:1px solid %2;border-radius:%3;}"
            "QToolButton#TodoSegBtn{color:%4;background:transparent;border:none;"
            "border-radius:%5;font-size:%6;padding:0 %7;}"
            "QToolButton#TodoSegBtn:hover{color:%8;}"
            "QToolButton#TodoSegBtn:checked{background:%9;color:%10;font-weight:600;}")
            .arg(withAlpha(kColorFg, 0.05), withAlpha(kColorBorder, 0.70), sp(8),
                 kColorFgMuted, sp(6), sp(12), sp(10), kColorFg,
                 withAlpha(kColorAccent, 0.22), kColorAccent));

    const int idx = ui->head->indexOf(ui->sortBox);
    ui->head->insertWidget(idx >= 0 ? idx : ui->head->count(), m_viewSeg);
}

void TodoPage::buildBoardView()
{
    m_board = new TodoBoardView(this);
    // 插到原 TodoList 的位置（listLay: 0=head 1=快速添加 2=TodoList 3=批量条 4=底部行）
    ui->listLay->insertWidget(3, m_board);
    ui->listLay->setStretchFactor(m_board, 1);
    m_board->hide();

    connect(m_board, &TodoBoardView::taskActivated, this, [this](qint64 id) {
        // 平铺视图默认收起详情栏，点卡片才临时展开
        m_selectedTask = id;
        m_detailWanted = true;
        updateDetailVisibility();
        loadDetail(id);
        setRowHighlight(id);
    });
    connect(m_board, &TodoBoardView::taskToggleRequested, this, &TodoPage::onToggleRequested);
    connect(m_board, &TodoBoardView::taskMenuRequested, this, &TodoPage::onBoardTaskMenu);
    connect(m_board, &TodoBoardView::listMenuRequested, this, &TodoPage::onBoardListMenu);
    connect(m_board, &TodoBoardView::quickAddRequested, this,
            [this](qint64 listId, const QString &title) {
                m_source->createTask(title, listId, QString());
            });
    connect(m_board, &TodoBoardView::taskDropped, this, [this](qint64 id, qint64 listId) {
        m_settleTask = id;   // 重建后该卡片播「accent 环淡出」
        m_source->moveTasks({id}, listId);
    });
}

void TodoPage::setBoardMode(bool on)
{
    const bool changed = (m_boardMode != on);
    m_boardMode = on;
    if (changed)
        saveTodoBoardMode(on);

    if (m_segList) {
        QSignalBlocker block(m_segList);
        m_segList->setChecked(!on);
    }
    if (m_segBoard) {
        QSignalBlocker block(m_segBoard);
        m_segBoard->setChecked(on);
    }

    // 看板不支持多选批量操作：切过去先退出多选
    if (on && m_multiBtn && m_multiBtn->isChecked())
        m_multiBtn->setChecked(false);

    m_detailWanted = !on;   // 平铺视图：详情栏默认收起
    applyMetrics();         // 板模式下左右边距对称
    updateDetailVisibility();

    const bool listMode = !on;
    m_quickAdd->setVisible(listMode);
    m_list->setVisible(listMode);
    if (ui->footRow)
        ui->footRow->setVisible(listMode);
    if (m_bulkBar)
        m_bulkBar->setVisible(listMode && m_multi);
    if (m_completedBtn)
        m_completedBtn->setVisible(false);   // 仅列表视图用，rebuildList 会按需重现
    if (m_multiBtn)
        m_multiBtn->setVisible(listMode);
    if (m_board)
        m_board->setVisible(on);

    m_animateNext = true;
    if (on)
        rebuildBoard();
    else
        rebuildList();
}

void TodoPage::rebuildBoard()
{
    if (!m_board)
        return;
    if (!m_boardMode) {
        m_board->hide();
        return;
    }
    m_board->setVisible(true);

    m_board->setData(m_lists, m_tasks, m_listColors, m_settleTask,
                     [this](const TodoTask &a, const TodoTask &b) {
                         return taskLessThan(a, b, m_sort);
                     });
    m_settleTask = 0;

    int open = 0;
    for (const auto &t : m_tasks)
        if (!t.completed)
            ++open;
    m_viewTitle->setText(QStringLiteral("全部清单"));
    m_viewCount->setText(QStringLiteral("%1 个清单 · %2 项待办")
                             .arg(m_lists.size() + 1)
                             .arg(open));
}

// 详情栏可见性：列表视图常驻；平铺视图默认收起，点卡片临时展开、点「收起」关回去
void TodoPage::updateDetailVisibility()
{
    const bool show = m_boardMode ? m_detailWanted : true;
    if (m_detailPanel)
        m_detailPanel->setVisible(show);
    if (ui->colSep)
        ui->colSep->setVisible(show);
}

void TodoPage::onBoardTaskMenu(qint64 id, const QPoint &globalPos)
{
    onTaskRowMenu(id, globalPos);
}

void TodoPage::onBoardListMenu(qint64 listId, const QPoint &globalPos)
{
    if (listId == 0)
        return;   // 收集箱是内置清单，不可重命名 / 删除
    QMenu menu(this);
    menu.addAction(QStringLiteral("重命名清单…"), this, [this, listId] { onRenameList(listId); });
    menu.addAction(QStringLiteral("删除清单"), this, [this, listId] { onDeleteList(listId); });
    menu.exec(globalPos);
}

// 列表行 ⋯ 与看板卡片 ⋯ 共用同一份菜单
void TodoPage::onTaskRowMenu(qint64 id, const QPoint &globalPos)
{
    QMenu menu(this);
    QMenu *moveTo = menu.addMenu(QStringLiteral("移到清单"));
    moveTo->addAction(QStringLiteral("收集箱"), this, [this, id] {
        m_settleTask = id;
        m_source->moveTasks({id}, 0);
    });
    for (const auto &l : m_lists) {
        moveTo->addAction(l.name, this, [this, id, listId = l.id] {
            m_settleTask = id;
            m_source->moveTasks({id}, listId);
        });
    }
    menu.addSeparator();
    menu.addAction(QStringLiteral("打开详情"), this, [this, id] {
        m_selectedTask = id;
        m_detailWanted = true;
        updateDetailVisibility();
        loadDetail(id);
        setRowHighlight(id);
    });
    menu.addAction(QStringLiteral("详细信息"), this, [this, id] { showTaskDetails(id); });
    menu.addAction(QStringLiteral("删除任务"), this, [this, id] {
        QMessageBox box(this);
        box.setWindowTitle(QStringLiteral("删除任务"));
        box.setText(QStringLiteral("确定删除该任务？"));
        box.setStandardButtons(QMessageBox::Yes | QMessageBox::Cancel);
        if (box.exec() == QMessageBox::Yes)
            m_source->deleteTask(id);
    });
    menu.exec(globalPos);
}

void TodoPage::showTaskDetails(qint64 id)
{
    const TodoTask *found = nullptr;
    for (const auto &t : m_tasks) {
        if (t.id == id) {
            found = &t;
            break;
        }
    }
    if (!found)
        return;

    QString listName;
    for (const auto &l : m_lists) {
        if (l.id == found->listId) {
            listName = l.name;
            break;
        }
    }

    auto *dlg = new TaskDetailsDialog(*found, listName, m_source->supportsRecurrence(), this);
    dlg->setAttribute(Qt::WA_DeleteOnClose);
    dlg->show();

    // 来源设备名解析（best-effort）：device_id → 已配对设备名 + 端类型；失败则保留原始 id
    if (!m_api || found->deviceId.isEmpty())
        return;
    const QString wanted = found->deviceId;
    QPointer<TaskDetailsDialog> guard(dlg);
    QNetworkReply *r = m_api->getSyncDevices();
    connect(r, &QNetworkReply::finished, this, [r, guard, wanted] {
        r->deleteLater();
        QJsonDocument doc;
        QString err;
        if (!ApiClient::parseReply(r, &doc, &err) || !guard)
            return;
        const auto arr = doc.isArray() ? doc.array() : QJsonArray();
        for (const auto &v : arr) {
            if (!v.isObject())
                continue;
            const SyncDevice d = SyncDevice::fromJson(v.toObject());
            if (d.id != wanted)
                continue;
            QString name = d.alias.isEmpty() ? d.name : d.alias;
            const QString kind = deviceKindLabel(d.deviceKind);
            if (!kind.isEmpty())
                name = name.isEmpty() ? kind : QStringLiteral("%1 · %2").arg(name, kind);
            if (d.isSelf)
                name += QStringLiteral("（本机）");
            if (!name.isEmpty())
                guard->setDeviceName(name);
            break;
        }
    });
}

// ══════════════════════════════════════════════════════════
// 左侧列表导航（仿 TickTick：智能清单 + 自定义清单 + 已完成）
// ══════════════════════════════════════════════════════════
TodoNavItem *TodoPage::makeNavItem(const QString &name)
{
    auto *it = new TodoNavItem(m_listNav ? m_listNav : this);
    it->setText(name);
    return it;
}

void TodoPage::buildNav()
{
    m_listNav = ui->listNav;
    auto *lay = ui->navLay;

    m_navInbox = makeNavItem(QStringLiteral("收集箱"));
    m_navInbox->setGlyph(glyph::Inbox);
    connect(m_navInbox, &TodoNavItem::clicked, this, [this] { selectView(ViewInbox); });
    lay->addWidget(m_navInbox);

    m_navToday = makeNavItem(QStringLiteral("今天"));
    m_navToday->setGlyph(glyph::Calendar);
    connect(m_navToday, &TodoNavItem::clicked, this, [this] { selectView(ViewToday); });
    lay->addWidget(m_navToday);

    m_navNext7 = makeNavItem(QStringLiteral("最近 7 天"));
    m_navNext7->setGlyph(glyph::Recent);
    connect(m_navNext7, &TodoNavItem::clicked, this, [this] { selectView(ViewNext7); });
    lay->addWidget(m_navNext7);

    m_navAll = makeNavItem(QStringLiteral("全部"));
    m_navAll->setGlyph(glyph::ViewAll);
    connect(m_navAll, &TodoNavItem::clicked, this, [this] { selectView(ViewAll); });
    lay->addWidget(m_navAll);

    // 智能清单与自定义清单之间的分组间距
    lay->addSpacing(si(8));

    // 「清单」分组小标题
    m_navListLabel = new QLabel(QStringLiteral("清单"), m_listNav);
    m_navListLabel->setObjectName(QStringLiteral("TodoNavLabel"));
    lay->addWidget(m_navListLabel);

    // 自定义清单项容器（rebuildNavLists 随 lists 变化重建）
    m_navListsBox = new QWidget(m_listNav);
    m_navListsLay = new QVBoxLayout(m_navListsBox);
    m_navListsLay->setContentsMargins(0, 0, 0, 0);
    m_navListsLay->setSpacing(2);
    lay->addWidget(m_navListsBox);

    // ＋ 新建清单
    m_newListBtn = new QToolButton(m_listNav);
    m_newListBtn->setObjectName(QStringLiteral("TodoNewList"));
    m_newListBtn->setText(QStringLiteral("＋ 新建清单"));
    m_newListBtn->setCursor(Qt::PointingHandCursor);
    m_newListBtn->setToolTip(QStringLiteral("新建一个自定义清单"));
    connect(m_newListBtn, &QToolButton::clicked, this, &TodoPage::onNewList);
    lay->addWidget(m_newListBtn);

    lay->addStretch(1);

    // 已完成（常驻底部）
    m_navDone = makeNavItem(QStringLiteral("已完成"));
    m_navDone->setGlyph(glyph::Checkbox);
    connect(m_navDone, &TodoNavItem::clicked, this, [this] { selectView(ViewDone); });
    lay->addWidget(m_navDone);

    rebuildNavLists();
    updateNavCounts();
    setNavChecked();
}

void TodoPage::rebuildNavLists()
{
    if (!m_navListsLay)
        return;
    qDeleteAll(m_navListItems);
    m_navListItems.clear();

    for (const auto &l : m_lists) {
        auto *it = makeNavItem(l.name);
        it->setColorDot(QColor(m_listColors.value(l.id)));
        m_navListsLay->addWidget(it);
        m_navListItems.append(it);
        connect(it, &TodoNavItem::clicked, this, [this, id = l.id] { selectView(ViewList, id); });

        // 右键：重命名 / 删除清单
        it->setContextMenuPolicy(Qt::CustomContextMenu);
        connect(it, &QWidget::customContextMenuRequested, this, [this, id = l.id](const QPoint &) {
            QMenu menu(this);
            menu.addAction(QStringLiteral("重命名清单…"), this, [this, id] { onRenameList(id); });
            menu.addAction(QStringLiteral("删除清单"), this, [this, id] { onDeleteList(id); });
            menu.exec(QCursor::pos());
        });
    }
    if (m_navListsBox)
        m_navListsBox->setVisible(!m_lists.isEmpty());
}

void TodoPage::updateNavCounts()
{
    const QDate today = QDate::currentDate();
    int inbox = 0, todayN = 0, next7 = 0, allN = 0, doneN = 0;
    QHash<qint64, int> listCounts;
    for (const auto &t : m_tasks) {
        if (t.completed) {
            ++doneN;
            continue;
        }
        ++allN;
        if (t.listId == 0)
            ++inbox;
        else
            ++listCounts[t.listId];
        if (t.hasDue()) {
            const QDate d = QDate::fromString(t.dueDate, Qt::ISODate);
            if (d.isValid() && d <= today)
                ++todayN;
            if (d.isValid() && d <= today.addDays(6))
                ++next7;
        }
    }
    if (m_navInbox) m_navInbox->setCount(inbox);
    if (m_navToday) m_navToday->setCount(todayN);
    if (m_navNext7) m_navNext7->setCount(next7);
    if (m_navAll) m_navAll->setCount(allN);
    if (m_navDone) m_navDone->setCount(doneN);
    // 清单项顺序与 m_lists 一致（rebuildNavLists 按序追加）
    for (int i = 0; i < m_lists.size() && i < m_navListItems.size(); ++i)
        m_navListItems[i]->setCount(listCounts.value(m_lists[i].id, 0));
}

void TodoPage::setNavChecked()
{
    if (m_navInbox) m_navInbox->setSelected(m_view == ViewInbox);
    if (m_navToday) m_navToday->setSelected(m_view == ViewToday);
    if (m_navNext7) m_navNext7->setSelected(m_view == ViewNext7);
    if (m_navAll) m_navAll->setSelected(m_view == ViewAll);
    if (m_navDone) m_navDone->setSelected(m_view == ViewDone);
    for (int i = 0; i < m_lists.size() && i < m_navListItems.size(); ++i)
        m_navListItems[i]->setSelected(m_view == ViewList && m_viewList == m_lists[i].id);
}

void TodoPage::selectView(ViewKind kind, qint64 listId)
{
    if (m_view == kind && m_viewList == listId) {
        setNavChecked();
        return;
    }
    m_view = kind;
    m_viewList = listId;
    m_showCompleted = false;
    m_animateNext = true;
    clearDetail();
    setNavChecked();
    rebuildList();
}

QString TodoPage::viewTitle() const
{
    switch (m_view) {
    case ViewInbox: return QStringLiteral("收集箱");
    case ViewToday: return QStringLiteral("今天");
    case ViewNext7: return QStringLiteral("最近 7 天");
    case ViewAll: return QStringLiteral("全部");
    case ViewDone: return QStringLiteral("已完成");
    case ViewList:
        for (const auto &l : m_lists)
            if (l.id == m_viewList)
                return l.name;
        return QStringLiteral("清单");
    }
    return QString();
}

bool TodoPage::taskLessThan(const TodoTask &a, const TodoTask &b, SortMode mode)
{
    const bool ad = a.hasDue(), bd = b.hasDue();
    const QDate da = ad ? QDate::fromString(a.dueDate, Qt::ISODate) : QDate();
    const QDate db = bd ? QDate::fromString(b.dueDate, Qt::ISODate) : QDate();
    switch (mode) {
    case SortMode::NewestFirst:
        // 最近添加：createdAt 降序（ISO 字符串同格式字典序即时间序），id 决胜
        if (a.createdAt != b.createdAt)
            return a.createdAt > b.createdAt;
        return a.id > b.id;
    case SortMode::ByDue:
        // 按截止日期：有期限优先、日期升序，其余按默认
        if (ad != bd)
            return ad;
        if (ad && bd && da.isValid() && db.isValid() && da != db)
            return da < db;
        break;
    case SortMode::ByPriority:
        // 按优先级：高在前，同优先级按默认
        if (a.priority != b.priority)
            return a.priority > b.priority;
        break;
    case SortMode::Reverse:
        // 倒序：默认排序的完全反转
        return taskLessThan(b, a, SortMode::Default);
    case SortMode::Default:
        break;
    }
    // 默认：优先级降序 → 有期限在前 → due 升序 → sortOrder
    if (a.priority != b.priority)
        return a.priority > b.priority;
    if (ad != bd)
        return ad;
    if (ad && bd && da.isValid() && db.isValid() && da != db)
        return da < db;
    return a.sortOrder < b.sortOrder;
}

QList<TodoTask> TodoPage::visibleTasks() const
{
    QList<TodoTask> open, done;
    const QDate today = QDate::currentDate();
    for (const auto &t : m_tasks) {
        bool inView = false;
        switch (m_view) {
        case ViewInbox: inView = (t.listId == 0); break;
        case ViewToday:
            inView = t.hasDue()
                     && QDate::fromString(t.dueDate, Qt::ISODate).isValid()
                     && QDate::fromString(t.dueDate, Qt::ISODate) <= today;
            break;
        case ViewNext7:
            inView = t.hasDue()
                     && QDate::fromString(t.dueDate, Qt::ISODate).isValid()
                     && QDate::fromString(t.dueDate, Qt::ISODate) <= today.addDays(6);
            break;
        case ViewAll: inView = true; break;
        case ViewDone: inView = t.completed; break;
        case ViewList: inView = (t.listId == m_viewList); break;
        }
        if (!inView)
            continue;
        if (t.completed)
            done.append(t);
        else
            open.append(t);
    }
    std::sort(open.begin(), open.end(),
              [this](const TodoTask &a, const TodoTask &b) {
                  return taskLessThan(a, b, m_sort);
              });
    std::sort(done.begin(), done.end(), [](const TodoTask &a, const TodoTask &b) {
        return a.completedAt > b.completedAt;
    });
    return open + done;
}

// ── widget 池实现 ─────────────────────────────────────────────────────────────
TodoTaskRow *TodoPage::TodoPool::acquire(const TodoTask &task, const QString &dotColor,
                                         const QString &listName, bool multi, bool selected)
{
    TodoTaskRow *row;
    bool reused = true;
    if (!m_pool.isEmpty()) {
        row = m_pool.takeLast();
    } else {
        // 新建的行：构造函数已按 task/dotColor/listName 填好内容（与 setTask 共用
        // rebuildPills / rebuildRightCluster），不必再 setTask 一遍。
        reused = false;
        row = new TodoTaskRow(task, dotColor, listName);
        // 信号连接只在新创建时挂载；池化回收时连接已存在，直接复用
        // 注意：lambda 捕获列表里不能直接写成员 m_page（必须是被捕获函数作用域内的变量），
        //       用初始化捕获拷一份指针；行的寿命可能长于 TodoPool，故不抓 this。
        QObject::connect(row, &TodoTaskRow::selected, m_page, [page = m_page](qint64 id) {
            page->m_selectedTask = id;
            page->loadDetail(id);
            page->setRowHighlight(id);
        });
        QObject::connect(row, &TodoTaskRow::toggleRequested, m_page, &TodoPage::onToggleRequested);
        QObject::connect(row, &TodoTaskRow::selectionToggled, m_page, &TodoPage::onSelectionToggled);
        QObject::connect(row, &TodoTaskRow::menuRequested, m_page, &TodoPage::onTaskRowMenu);
    }
    if (reused)
        row->setTask(task, dotColor, listName);   // 池里的行还带着上一任任务的内容
    row->setMultiSelectMode(multi);
    row->setSelected(selected);
    return row;
}

// 入池前的必备动作：这些行是从 QListWidget::clear() 里出来的，而任务页的行**本身就是
// item widget**，clear() 会对它挂一个 deleteLater（Qt 走的是异步销毁，clear() 返回时行还活着）。
// 若不撤掉，行被重新 setItemWidget 挂回列表后，事件循环一转就被 DeferredDelete 删掉，
// 列表里会留下悬空的 item widget（离屏探针实测：复用后 alive 3 → 0）。
// 收件箱那侧不需要这一步，因为卡片是 wrap 的子控件、clear() 之后才摘 parent，被 deleteLater
// 的是 wrap 而不是卡片。这里必须显式撤；撤完再决定是入池还是自己 deleteLater 淘汰。
static void unArmPendingDelete(QWidget *w)
{
    QCoreApplication::removePostedEvents(w, QEvent::DeferredDelete);
}

void TodoPage::TodoPool::release(TodoTaskRow *row)
{
    if (!row)
        return;
    unArmPendingDelete(row);
    if (m_pool.size() >= m_maxSize) {
        row->deleteLater();
    } else {
        m_pool.append(row);
    }
}

void TodoPage::TodoPool::releaseAll(const QMap<int, TodoTaskRow *> &active)
{
    for (auto *row : active) {
        if (!row)
            continue;
        unArmPendingDelete(row);
        if (m_pool.size() >= m_maxSize)
            row->deleteLater();
        else
            m_pool.append(row);
    }
}

void TodoPage::TodoPool::discardAll()
{
    m_pool.clear();
}

void TodoPage::TodoPool::clear()
{
    qDeleteAll(m_pool);
    m_pool.clear();
}

void TodoPage::rebuildList()
{
    // 收集当前列表中的行，复用到池中（避免 clear() 逐个 delete 造成 O(n²) 重建）
    QMap<int, TodoTaskRow *> currentRows;
    for (int i = 0; i < m_list->count(); ++i) {
        if (auto *row = qobject_cast<TodoTaskRow *>(m_list->itemWidget(m_list->item(i))))
            currentRows[i] = row;
    }
    m_list->clear();
    if (m_cardPool)
        m_cardPool->releaseAll(currentRows);

    const auto all = visibleTasks();
    int openCount = 0;
    QList<TodoTask> done;
    for (const auto &t : all) {
        if (t.completed)
            done.append(t);
        else
            ++openCount;
    }

    const bool anyDone = !done.isEmpty();
    const bool isDoneView = (m_view == ViewDone);
    m_viewTitle->setText(viewTitle());
    m_quickAdd->setPlaceholderText(QStringLiteral("添加任务到「%1」…").arg(viewTitle()));

    for (const auto &t : all) {
        if (t.completed && !isDoneView)
            break; // all 已按「未完成在前」排好
        auto *item = new QListWidgetItem(m_list);
        item->setSizeHint(QSize(0, si(62)));
        m_list->addItem(item);
        QString dot;
        QString listName;
        if (t.listId != 0 && m_listColors.contains(t.listId)) {
            dot = m_listColors.value(t.listId);
            if (m_view == ViewAll || m_view == ViewNext7 || m_view == ViewDone) {
                for (const auto &l : m_lists) {
                    if (l.id == t.listId) {
                        listName = l.name;
                        break;
                    }
                }
                dot.clear();
            }
        }
        TodoTaskRow *rw = m_cardPool->acquire(t, dot, listName, m_multi,
                                               m_multi && m_selectedIds.contains(t.id));
        m_list->setItemWidget(item, rw);
        if (m_animateNext)
            fadeInWidget(rw, 180);
    }

    if (isDoneView) {
        // 已完成视图：全部渲染，不再显示「显示已完成」折叠按钮
        m_completedBtn->setVisible(false);
    } else if (anyDone) {
        if (m_showCompleted) {
            for (const auto &t : done) {
                auto *item = new QListWidgetItem(m_list);
                item->setSizeHint(QSize(0, si(62)));
                m_list->addItem(item);
                QString dot;
                QString listName;
                if (t.listId != 0 && m_listColors.contains(t.listId)) {
                    dot = m_listColors.value(t.listId);
                    if (m_view == ViewAll || m_view == ViewNext7 || m_view == ViewDone) {
                        for (const auto &l : m_lists) {
                            if (l.id == t.listId) {
                                listName = l.name;
                                break;
                            }
                        }
                        dot.clear();
                    }
                }
                TodoTaskRow *rw = m_cardPool->acquire(t, dot, listName, m_multi,
                                                       m_multi && m_selectedIds.contains(t.id));
                m_list->setItemWidget(item, rw);
                if (m_animateNext)
                    fadeInWidget(rw, 180);
            }
        }
        m_completedBtn->setVisible(!m_multi && !m_boardMode);
        m_completedBtn->setChecked(m_showCompleted);
        m_completedBtn->setText(m_showCompleted
                                    ? QStringLiteral("隐藏已完成 (%1)").arg(done.size())
                                    : QStringLiteral("显示已完成 (%1)").arg(done.size()));
    } else {
        m_completedBtn->setVisible(false);
    }

    if (openCount == 0 && done.isEmpty()) {
        auto *item = new QListWidgetItem(m_list);
        item->setSizeHint(QSize(0, si(110)));
        m_list->addItem(item);
        auto *l = new QLabel(QStringLiteral("暂无任务\n在上方输入框回车即可添加"));
        l->setAlignment(Qt::AlignCenter);
        l->setStyleSheet(QStringLiteral("color:%1;padding:24px;").arg(kColorFgMuted));
        m_list->setItemWidget(item, l);
    }

    m_viewCount->setText(isDoneView
                             ? QStringLiteral("%1 项已完成").arg(done.size())
                             : QStringLiteral("%1 项待办").arg(openCount));
    int total = m_tasks.size();
    int doneTotal = 0;
    for (const auto &t : m_tasks)
        if (t.completed)
            ++doneTotal;
    m_progress->setText(QStringLiteral("已完成 %1 / %2").arg(doneTotal).arg(total));

    m_animateNext = false; // 入场动画仅在视图切换/初次构建时触发一次
    setRowHighlight(m_selectedTask);
    if (m_multi)
        updateBulkBar();

    // 平铺视图：列表控件是隐藏的，同一份数据改走看板渲染
    if (m_boardMode)
        rebuildBoard();
}

void TodoPage::setRowHighlight(qint64 id)
{
    for (int i = 0; i < m_list->count(); ++i) {
        auto *w = m_list->itemWidget(m_list->item(i));
        auto *row = qobject_cast<TodoTaskRow *>(w);
        if (row)
            row->setHighlighted(row->taskId() == id);
    }
}

// ══════════════════════════════════════════════════════════
// 多选 / 批量操作
// ══════════════════════════════════════════════════════════
QList<qint64> TodoPage::selectedTaskIds() const
{
    QList<qint64> ids;
    ids.reserve(m_selectedIds.size());
    for (qint64 id : m_selectedIds)
        ids.append(id);
    return ids;
}

// 当前列表里实际渲染出来的任务（未完成全部 + 已完成中已展开的部分）
QList<qint64> TodoPage::rowTaskIds() const
{
    QList<qint64> ids;
    const auto all = visibleTasks();
    for (const auto &t : all) {
        if (t.completed && !m_showCompleted)
            break;   // all 已按「未完成在前」排好
        ids.append(t.id);
    }
    return ids;
}

void TodoPage::setMultiSelect(bool on)
{
    if (m_multi == on)
        return;
    m_multi = on;
    m_selectedIds.clear();
    if (m_multiBtn && m_multiBtn->isChecked() != on) {
        QSignalBlocker block(m_multiBtn);
        m_multiBtn->setChecked(on);
    }
    if (m_bulkBar)
        m_bulkBar->setVisible(on);
    // 多选模式下隐藏快速添加框，避免与批量条混在一起
    if (m_quickAdd)
        m_quickAdd->setVisible(!on);
    rebuildList();
    updateBulkBar();
}

void TodoPage::onMultiToggled(bool on)
{
    setMultiSelect(on);
}

void TodoPage::onSelectionToggled(qint64 id, bool selected)
{
    if (selected)
        m_selectedIds.insert(id);
    else
        m_selectedIds.remove(id);
    syncRowSelection();
    updateBulkBar();
}

void TodoPage::syncRowSelection()
{
    for (int i = 0; i < m_list->count(); ++i) {
        auto *row = qobject_cast<TodoTaskRow *>(m_list->itemWidget(m_list->item(i)));
        if (row)
            row->setSelected(m_multi && m_selectedIds.contains(row->taskId()));
    }
}

void TodoPage::updateBulkBar()
{
    const int n = m_selectedIds.size();
    if (m_bulkCount)
        m_bulkCount->setText(QStringLiteral("已选 %1 项").arg(n));

    // 全选按钮文案：当前列表项已全部选中时显示「取消全选」
    const QList<qint64> ids = rowTaskIds();
    bool allSelected = !ids.isEmpty();
    for (qint64 id : ids) {
        if (!m_selectedIds.contains(id)) {
            allSelected = false;
            break;
        }
    }
    if (m_bulkSelectAll)
        m_bulkSelectAll->setText(allSelected ? QStringLiteral("取消全选") : QStringLiteral("全选"));

    const bool has = n > 0;
    for (auto *b : {m_bulkDone, m_bulkUndone, m_bulkMoveBtn, m_bulkPriorityBtn, m_bulkDueBtn, m_bulkDeleteBtn})
        if (b)
            b->setEnabled(has);
}

// 丢弃已被删除任务的选择残留（外部同步可能删任务）
void TodoPage::pruneSelection()
{
    if (m_selectedIds.isEmpty())
        return;
    QSet<qint64> alive;
    for (const auto &t : m_tasks)
        alive.insert(t.id);
    QSet<qint64> keep;
    for (qint64 id : m_selectedIds)
        if (alive.contains(id))
            keep.insert(id);
    m_selectedIds = keep;
}

void TodoPage::onBulkSelectAll()
{
    const QList<qint64> ids = rowTaskIds();
    bool allSelected = !ids.isEmpty();
    for (qint64 id : ids) {
        if (!m_selectedIds.contains(id)) {
            allSelected = false;
            break;
        }
    }
    m_selectedIds.clear();
    if (!allSelected)
        for (qint64 id : ids)
            m_selectedIds.insert(id);
    syncRowSelection();
    updateBulkBar();
}

void TodoPage::onBulkSetCompleted(bool completed)
{
    const QList<qint64> ids = selectedTaskIds();
    if (ids.isEmpty())
        return;
    m_source->setTasksCompleted(ids, completed);
    updateBulkBar();
}

void TodoPage::onBulkDelete()
{
    const QList<qint64> ids = selectedTaskIds();
    if (ids.isEmpty())
        return;
    QMessageBox box(this);
    box.setWindowTitle(QStringLiteral("删除任务"));
    box.setText(QStringLiteral("确定删除选中的 %1 个任务？").arg(ids.size()));
    box.setStandardButtons(QMessageBox::Yes | QMessageBox::Cancel);
    if (box.exec() != QMessageBox::Yes)
        return;
    m_selectedIds.clear();
    updateBulkBar();
    m_source->deleteTasks(ids);
}

void TodoPage::onBulkMove(qint64 listId)
{
    const QList<qint64> ids = selectedTaskIds();
    if (ids.isEmpty())
        return;
    m_source->moveTasks(ids, listId);
}

void TodoPage::onBulkPriority(int priority)
{
    const QList<qint64> ids = selectedTaskIds();
    if (ids.isEmpty())
        return;
    m_source->setTasksPriority(ids, priority);
}

void TodoPage::onBulkDue(const QString &dueDate)
{
    const QList<qint64> ids = selectedTaskIds();
    if (ids.isEmpty())
        return;
    m_source->setTasksDueDate(ids, dueDate);
}

// 列表渲染内容签名：视图/排序/多选等渲染开关 + 清单 + 任务全量字段。
// 用 toJson 序列化保证「凡参与渲染的字段都在签名里」，避免漏字段导致界面停在旧数据。
QString TodoPage::renderSignature() const
{
    QJsonObject root;
    root.insert(QStringLiteral("view"), int(m_view));
    root.insert(QStringLiteral("viewList"), double(m_viewList));
    root.insert(QStringLiteral("showCompleted"), m_showCompleted);
    root.insert(QStringLiteral("multi"), m_multi);
    root.insert(QStringLiteral("sort"), int(m_sort));
    QJsonArray lists;
    for (const auto &l : m_lists)
        lists.append(l.toJson());
    QJsonArray tasks;
    for (const auto &t : m_tasks)
        tasks.append(t.toJson());
    root.insert(QStringLiteral("lists"), lists);
    root.insert(QStringLiteral("tasks"), tasks);
    return QString::fromUtf8(QJsonDocument(root).toJson(QJsonDocument::Compact));
}

// ── 数据变化 ───────────────────────────────────────────────
void TodoPage::onDataChanged()
{
    m_lists = m_source->lists();
    m_tasks = m_source->tasks();
    m_listColors.clear();
    for (const auto &l : m_lists)
        m_listColors.insert(l.id, l.color.isEmpty() ? colorForString(l.name).name() : l.color);

    // 拖拽进行中不重建：重建会把卡片 widget delete 掉，而拖拽源卡片此刻正阻塞在
    // drag.exec() 上，删它就是删掉栈上的 this。清掉签名并挂一个轮询，等拖拽结束后补做。
    if (boardDragActive()) {
        m_renderSig.clear();
        if (!m_dragRefreshPending) {
            m_dragRefreshPending = true;
            m_dragRefreshTries = 0;
        }
        if (m_dragRefreshTries++ < 40) {
            QTimer::singleShot(150, this, [this] {
                m_dragRefreshPending = false;
                onDataChanged();
            });
        }
        return;
    }
    m_dragRefreshPending = false;

    // 内容没变就不重建：同步轮询每次落地都会广播 dataChanged，全量重建会让列表
    // 闪一下并把滚动位置拉回顶部，而用户看到的内容其实完全一样。
    const QString sig = renderSignature();
    if (sig == m_renderSig) {
        m_animateNext = false;
        return;
    }
    m_renderSig = sig;

    rebuildNavLists();
    updateNavCounts();
    setNavChecked();
    reloadListCombo();
    pruneSelection();   // 任务可能已被别处删除，先丢掉失效的选择残留
    rebuildList();

    bool exists = false;
    for (const auto &t : m_tasks)
        if (t.id == m_selectedTask) { exists = true; break; }
    if (exists)
        reloadSubtaskList();
    else
        clearDetail();
}

void TodoPage::reloadListCombo()
{
    const qint64 cur = m_dList->currentData().toLongLong();
    m_loadingDetail = true;
    m_dList->clear();
    m_dList->addItem(QStringLiteral("收集箱"), 0);
    for (const auto &l : m_lists)
        m_dList->addItem(l.name, l.id);
    int idx = m_dList->findData(cur);
    if (idx < 0)
        idx = 0;
    m_dList->setCurrentIndex(idx);
    m_loadingDetail = false;
}

void TodoPage::reloadSubtaskList()
{
    if (m_selectedTask == 0 || !m_dSubs)
        return;
    const TodoTask *t = nullptr;
    for (const auto &x : m_tasks)
        if (x.id == m_selectedTask) { t = &x; break; }
    if (!t)
        return;
    m_dSubs->clear();
    for (const auto &s : t->subtasks) {
        auto *item = new QListWidgetItem(m_dSubs);
        item->setSizeHint(QSize(0, si(28)));
        m_dSubs->addItem(item);
        m_dSubs->setItemWidget(item, makeSubtaskRow(s));
    }
}

// ── 快速添加 / 清单管理 ────────────────────────────────────
void TodoPage::onQuickAdd()
{
    const QString title = m_quickAdd->text().trimmed();
    if (title.isEmpty())
        return;
    qint64 listId = 0;
    QString due;
    if (m_view == ViewList)
        listId = m_viewList;
    else if (m_view == ViewToday)
        due = QDate::currentDate().toString(Qt::ISODate);
    m_source->createTask(title, listId, due);
    m_quickAdd->clear();
}

void TodoPage::onNewList()
{
    bool ok = false;
    const QString name = QInputDialog::getText(this, QStringLiteral("新建清单"),
                                               QStringLiteral("清单名称："),
                                               QLineEdit::Normal, QString(), &ok)
                             .trimmed();
    if (!ok || name.isEmpty())
        return;
    m_source->createList(name, pickListColor(int(m_lists.size()), m_lists));
}

void TodoPage::onRenameList(qint64 listId)
{
    QString old;
    for (const auto &l : m_lists)
        if (l.id == listId) { old = l.name; break; }
    bool ok = false;
    const QString name = QInputDialog::getText(this, QStringLiteral("重命名清单"),
                                               QStringLiteral("新名称："),
                                               QLineEdit::Normal, old, &ok)
                             .trimmed();
    if (ok && !name.isEmpty() && name != old)
        m_source->renameList(listId, name);
}

void TodoPage::onDeleteList(qint64 listId)
{
    QMessageBox box(this);
    box.setWindowTitle(QStringLiteral("删除清单"));
    box.setText(QStringLiteral("删除清单后，其中任务将移入收集箱。确定删除？"));
    box.setStandardButtons(QMessageBox::Yes | QMessageBox::Cancel);
    if (box.exec() == QMessageBox::Yes)
        m_source->deleteList(listId);
}

// ── 任务完成 / 删除 ────────────────────────────────────────
void TodoPage::onToggleRequested(qint64 id, bool completed)
{
    QString title;
    for (const auto &t : m_tasks)
        if (t.id == id) { title = t.title; break; }

    m_source->setTaskCompleted(id, completed);
    if (id == m_selectedTask && !m_loadingDetail)
        m_dDone->setChecked(completed);

    if (completed)
        showUndoToast(id, title);
}

// 完成任务后的「撤销」气泡：3s 内点按钮即可回退（气泡悬停时倒计时暂停）
void TodoPage::showUndoToast(qint64 id, const QString &title)
{
    QString t = title.simplified();
    if (t.size() > 24)
        t = t.left(23) + QStringLiteral("…");
    const QString text = t.isEmpty() ? QStringLiteral("已完成 1 项任务")
                                     : QStringLiteral("已完成「%1」").arg(t);

    // 气泡是独立顶层窗口，可能比本页活得久 —— 回调里用 QPointer 兜底
    QPointer<TodoPage> self(this);
    showActionToast(text, QStringLiteral("撤销"), [self, id] {
        if (!self)
            return;
        for (const auto &task : self->m_tasks) {
            if (task.id != id)
                continue;
            // 3 秒内该任务可能已被删除或同步覆盖，只在它仍是「已完成」时回退
            if (task.completed)
                self->m_source->setTaskCompleted(id, false);
            return;
        }
    }, 3000);
}

void TodoPage::onTaskDelete()
{
    if (m_selectedTask == 0)
        return;
    QMessageBox box(this);
    box.setWindowTitle(QStringLiteral("删除任务"));
    box.setText(QStringLiteral("确定删除该任务？"));
    box.setStandardButtons(QMessageBox::Yes | QMessageBox::Cancel);
    if (box.exec() == QMessageBox::Yes) {
        m_source->deleteTask(m_selectedTask);
        clearDetail();
    }
}

// ── 详情面板 ───────────────────────────────────────────────
void TodoPage::loadDetail(qint64 id)
{
    const TodoTask *t = nullptr;
    for (const auto &x : m_tasks)
        if (x.id == id) { t = &x; break; }
    if (!t) {
        clearDetail();
        return;
    }
    m_selectedTask = id;
    m_loadingDetail = true;

    m_detailEmpty->hide();
    m_detailBody->show();

    m_dTitle->setPlainText(t->title);
    m_dDone->setChecked(t->completed);

    int li = m_dList->findData(t->listId);
    if (li < 0)
        li = 0;
    m_dList->setCurrentIndex(li);

    int pi = m_dPriority->findData(t->priority);
    if (pi < 0)
        pi = 0;
    m_dPriority->setCurrentIndex(pi);

    if (t->hasDue()) {
        QDate d = QDate::fromString(t->dueDate, Qt::ISODate);
        if (!d.isValid())
            d = QDate::currentDate();
        m_dDue->setDate(d);
        m_dHasDue->setChecked(true);
    } else {
        m_dDue->setDate(QDate::currentDate());
        m_dHasDue->setChecked(false);
    }
    m_dDue->setEnabled(t->hasDue());

    int ri = m_dRecur->findData(t->recurrence);
    if (ri < 0)
        ri = 0;
    m_dRecur->setCurrentIndex(ri);

    m_dTags->setText(t->tags.join(QStringLiteral(", ")));
    m_dNotes->setPlainText(t->notes);

    m_loadingDetail = false;
    reloadSubtaskList();
}

void TodoPage::clearDetail()
{
    m_commitTimer->stop();
    m_selectedTask = 0;
    if (m_detailBody)
        m_detailBody->hide();
    if (m_detailEmpty)
        m_detailEmpty->show();
    // 平铺视图下「收起」= 把详情栏整个关掉，让板回到全宽
    if (m_boardMode) {
        m_detailWanted = false;
        updateDetailVisibility();
    }
}

void TodoPage::commitDetail()
{
    m_commitTimer->stop();
    if (m_loadingDetail || m_selectedTask == 0)
        return;
    const TodoTask *cur = nullptr;
    for (const auto &x : m_tasks)
        if (x.id == m_selectedTask) { cur = &x; break; }
    if (!cur)
        return;

    TodoTask t = *cur;
    t.title = m_dTitle->toPlainText().trimmed();
    t.listId = m_dList->currentData().toLongLong();
    t.priority = m_dPriority->currentData().toInt();
    t.dueDate = m_dHasDue->isChecked() ? m_dDue->date().toString(Qt::ISODate) : QString();
    t.recurrence = m_dRecur->currentData().toString();

    QStringList tags;
    const QStringList parts = m_dTags->text().split(QRegularExpression(QStringLiteral("[,，]")), Qt::SkipEmptyParts);
    for (QString p : parts) {
        p = p.trimmed();
        if (!p.isEmpty() && !tags.contains(p))
            tags << p;
    }
    t.tags = tags;
    t.notes = m_dNotes->toPlainText();
    m_source->updateTask(t);
}

// ── 子任务 ─────────────────────────────────────────────────
QWidget *TodoPage::makeSubtaskRow(const TodoSubtask &s)
{
    auto *w = new QWidget;
    auto *lay = new QHBoxLayout(w);
    lay->setContentsMargins(si(4), si(2), si(4), si(2));
    lay->setSpacing(si(6));

    auto *chk = new QCheckBox;
    chk->setChecked(s.completed);
    chk->setCursor(Qt::PointingHandCursor);
    connect(chk, &QCheckBox::toggled, this, [this, s](bool) {
        if (m_selectedTask == 0)
            return;
        m_source->toggleSubtask(m_selectedTask, s.id);
    });
    lay->addWidget(chk);

    auto *lab = new QLabel(s.title);
    lab->setWordWrap(false);
    lab->setStyleSheet(
        QStringLiteral("font-size:%1;%2").arg(sp(13),
            s.completed ? QStringLiteral("color:%1;text-decoration:line-through;").arg(kColorMuted2)
                        : QStringLiteral("color:%1;").arg(kColorFg)));
    lay->addWidget(lab, 1);

    auto *del = new QToolButton;
    del->setText(QStringLiteral("✕"));
    del->setCursor(Qt::PointingHandCursor);
    del->setAutoRaise(true);
    del->setToolTip(QStringLiteral("删除子任务"));
    del->setStyleSheet(
        QStringLiteral("QToolButton{color:%1;border:none;background:transparent;font-size:%2;}"
                       "QToolButton:hover{color:%3;}")
            .arg(kColorFgMuted, sp(12), kColorDanger));
    connect(del, &QToolButton::clicked, this, [this, s] {
        if (m_selectedTask == 0)
            return;
        m_source->removeSubtask(m_selectedTask, s.id);
    });
    lay->addWidget(del);
    return w;
}

void TodoPage::onSubtaskAdd()
{
    if (m_selectedTask == 0)
        return;
    const QString t = m_dSubAdd->text().trimmed();
    if (t.isEmpty())
        return;
    m_source->addSubtask(m_selectedTask, t);
    m_dSubAdd->clear();
}

} // namespace awqtui
