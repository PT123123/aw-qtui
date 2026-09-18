// todopage.cpp —— Todo 页实现（参照 TickTick / Super Productivity）
#include "todopage.h"
#include <QDebug>
#include "ui_todopage.h"

#include "appsettings.h"
#include "theme.h"
#include "mockdata.h"
#include "todostore.h"

#include <QAction>
#include <QCheckBox>
#include <QColor>
#include <QComboBox>
#include <QCursor>
#include <QDate>
#include <QDateEdit>
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
#include <QMessageBox>
#include <QMouseEvent>
#include <QPainter>
#include <QPixmap>
#include <QPlainTextEdit>
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
#include <QTimer>
#include <QToolButton>
#include <QVBoxLayout>

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

    auto *lay = new QHBoxLayout(this);
    lay->setContentsMargins(si(10), si(3), si(8), si(3));
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
        m_icon->setPixmap(glyphIcon(m_glyph, c, si(13)).pixmap(si(16), si(16)));
    }
}

void TodoNavItem::applyStyle()
{
    const QString bg = m_selected ? kColorNavSel : QStringLiteral("transparent");
    const QString fg = m_selected
        ? ((gTheme && gTheme->light) ? QString::fromLatin1(kColorAccent) : QStringLiteral("#ffffff"))
        : QString::fromLatin1(kColorFg);
    const QString countCol = m_selected
        ? ((gTheme && gTheme->light) ? QString::fromLatin1(kColorAccent) : QStringLiteral("#ffffff"))
        : kColorMuted2;
    setStyleSheet(
        QStringLiteral("QWidget#TodoNavItem{background:%1;border:none;border-radius:8px;}"
                       "QWidget#TodoNavItem:hover{background:%2;}"
                       "QWidget#TodoNavItem QLabel{background:transparent;border:none;}"
                       "QLabel#TodoNavName{color:%3;font-size:%4;font-weight:%5;}"
                       "QLabel#TodoNavCount{color:%6;font-size:%7;}")
            .arg(bg, kColorHover, fg, sp(13), m_selected ? QStringLiteral("600") : QStringLiteral("500"),
                 countCol, sp(11)));
    update();
}

void TodoNavItem::mousePressEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton)
        emit clicked();
    QWidget::mousePressEvent(event);
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

    auto *lay = new QHBoxLayout(this);
    lay->setContentsMargins(si(12), si(8), si(12), si(8));
    lay->setSpacing(si(10));

    // 圆形勾选框（完成框 / 多选框互斥显示在同一位置）
    const QString chkStyle =
        QStringLiteral("QCheckBox::indicator{width:%1px;height:%2px;border-radius:%3px;"
                       "border:2px solid %4;background:transparent;}"
                       "QCheckBox::indicator:hover{border-color:%5;}"
                       "QCheckBox::indicator:checked{background:%5;border-color:%5;}")
            .arg(sp(18)).arg(sp(18)).arg(sp(9)).arg(kColorBorder, kColorAccent);

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
        emit toggleRequested(m_taskId, checked);
    });
    lay->addWidget(m_chk);

    // 中间列：可换行标题 + 标题下标签胶囊行
    auto *mid = new QVBoxLayout;
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

    QStringList metaPills;
    if (!listName.isEmpty())
        metaPills << listName;
    for (const auto &tag : task.tags)
        metaPills << tag;
    if (!metaPills.isEmpty()) {
        auto *pillRow = new QHBoxLayout;
        pillRow->setSpacing(si(4));
        for (const QString &text : metaPills) {
            const QColor c = (text == listName && !dotColor.isEmpty())
                                 ? QColor(dotColor) : QColor(kColorAccent);
            pillRow->addWidget(makePill(text, c));
        }
        pillRow->addStretch(1);
        mid->addLayout(pillRow);
        m_hasMeta = true;
    }
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
        m_rightW = rw - si(6);   // 末尾多余的一层间隔不计入簇宽
    }

    // 首屏即应用底/hover 样式，保证鼠标移到任务项时有高亮
    setHighlighted(false);
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

// 行样式：多选选中 > 详情高亮 > 普通（含 hover）
// 左侧 accent 色条作为选中/高亮的视觉锚点，比整行描边更精致
void TodoTaskRow::applyRowStyle()
{
    m_rowStyled = true;
    // 行内文字/勾选框等子控件背景透明，避免与行高亮叠加出深色块
    const QString sub = QStringLiteral(
        "QWidget#TodoRow QLabel,QWidget#TodoRow QCheckBox{background:transparent;}");

    // 用左边框模拟 accent 色条（3px 宽、圆角左内边），其余边为极淡描边或透明
    QString base;
    if (m_selected) {
        // 多选选中：accent 色条 + 淡底 + 完整描边
        base = QStringLiteral(
            "QWidget#TodoRow{"
            "background:%1;"
            "border:1px solid %2;"
            "border-left:3px solid %3;"
            "border-radius:8px;"
            "}"
            "QWidget#TodoRow:hover{"
            "background:%4;"
            "border:1px solid %5;"
            "border-left:3px solid %3;"
            "}")
            .arg(withAlpha(kColorAccent, 0.16), withAlpha(kColorAccent, 0.55), kColorAccent,
                 withAlpha(kColorAccent, 0.24), withAlpha(kColorAccent, 0.70));
    } else if (m_highlighted) {
        // 详情高亮：accent 色条 + 更淡底 + 细描边
        base = QStringLiteral(
            "QWidget#TodoRow{"
            "background:%1;"
            "border:1px solid %2;"
            "border-left:3px solid %3;"
            "border-radius:8px;"
            "}"
            "QWidget#TodoRow:hover{"
            "background:%4;"
            "border:1px solid %5;"
            "border-left:3px solid %3;"
            "}")
            .arg(withAlpha(kColorAccent, 0.10), withAlpha(kColorAccent, 0.35), kColorAccent,
                 withAlpha(kColorAccent, 0.16), withAlpha(kColorAccent, 0.50));
    } else {
        // 普通态：透明底 + 透明边框，hover 时淡底 + 极淡描边
        base = QStringLiteral(
            "QWidget#TodoRow{"
            "background:transparent;"
            "border:1px solid transparent;"
            "border-left:3px solid transparent;"
            "border-radius:8px;"
            "}"
            "QWidget#TodoRow:hover{"
            "background:%1;"
            "border:1px solid %2;"
            "border-left:3px solid %3;"
            "}")
            .arg(withAlpha(kColorAccent, 0.07), withAlpha(kColorBorder, 0.70),
                 withAlpha(kColorAccent, 0.40));
    }
    setStyleSheet(sub + base);
}

void TodoTaskRow::mousePressEvent(QMouseEvent *event)
{
    if (m_multi) {
        // 多选模式：整行点击 = 切换选中（不打开详情）
        setSelected(!m_selected);
        emit selectionToggled(m_taskId, m_selected);
    } else {
        emit selected(m_taskId);
    }
    QWidget::mousePressEvent(event);
}

// ══════════════════════════════════════════════════════════
// TodoPage
// ══════════════════════════════════════════════════════════
TodoPage::TodoPage(TodoSource *source, QWidget *parent)
    : QWidget(parent), m_source(source)
{
    buildUi();
    connect(m_source, &TodoSource::dataChanged, this, &TodoPage::onDataChanged);
    m_source->load();
}

TodoPage::~TodoPage()
{
    delete ui;
}

void TodoPage::refresh()
{
    onDataChanged();
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
    ui->listLay->setContentsMargins(si(20), 0, si(16), 0);
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

    applyPageStyles();
    rebuildNavLists();
    updateNavCounts();
    setNavChecked();
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

void TodoPage::rebuildList()
{
    m_list->clear();
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
        QWidget *rw = makeRow(t);
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
                QWidget *rw = makeRow(t);
                m_list->setItemWidget(item, rw);
                if (m_animateNext)
                    fadeInWidget(rw, 180);
            }
        }
        m_completedBtn->setVisible(!m_multi);
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
}

QWidget *TodoPage::makeRow(const TodoTask &task)
{
    QString dot;
    QString listName;
    if (task.listId != 0 && m_listColors.contains(task.listId)) {
        dot = m_listColors.value(task.listId);
        // 聚合视图（全部/最近7天/已完成）：用清单名胶囊表达归属，右侧圆点省略
        if (m_view == ViewAll || m_view == ViewNext7 || m_view == ViewDone) {
            for (const auto &l : m_lists) {
                if (l.id == task.listId) {
                    listName = l.name;
                    break;
                }
            }
            dot.clear();
        }
    }
    auto *row = new TodoTaskRow(task, dot, listName);
    row->setMultiSelectMode(m_multi);
    row->setSelected(m_multi && m_selectedIds.contains(task.id));
    connect(row, &TodoTaskRow::selected, this, [this](qint64 id) {
        m_selectedTask = id;
        loadDetail(id);
        setRowHighlight(id);
    });
    connect(row, &TodoTaskRow::toggleRequested, this, &TodoPage::onToggleRequested);
    connect(row, &TodoTaskRow::selectionToggled, this, &TodoPage::onSelectionToggled);
    return row;
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
    m_source->setTaskCompleted(id, completed);
    if (id == m_selectedTask && !m_loadingDetail)
        m_dDone->setChecked(completed);
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
