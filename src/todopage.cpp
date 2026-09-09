// todopage.cpp —— Todo 页实现（参照 TickTick / Super Productivity）
#include "todopage.h"
#include "ui_todopage.h"

#include "appsettings.h"
#include "theme.h"
#include "mockdata.h"
#include "todostore.h"

#include <QAction>
#include <QCheckBox>
#include <QColor>
#include <QComboBox>
#include <QDate>
#include <QDateEdit>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QIcon>
#include <QInputDialog>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMenu>
#include <QMessageBox>
#include <QMouseEvent>
#include <QPixmap>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QEasingCurve>
#include <QPropertyAnimation>
#include <QRegularExpression>
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

} // namespace

// ══════════════════════════════════════════════════════════
// TodoTaskRow —— 单行任务
// ══════════════════════════════════════════════════════════
TodoTaskRow::TodoTaskRow(const TodoTask &task, const QString &dotColor, QWidget *parent)
    : QWidget(parent), m_taskId(task.id)
{
    setAttribute(Qt::WA_Hover);
    setCursor(Qt::PointingHandCursor);
    setObjectName(QStringLiteral("TodoRow"));
    setMinimumHeight(si(40));

    auto *lay = new QHBoxLayout(this);
    lay->setContentsMargins(si(12), si(8), si(12), si(8));
    lay->setSpacing(si(8));

    auto *chk = new QCheckBox;
    chk->setChecked(task.completed);
    chk->setCursor(Qt::PointingHandCursor);
    chk->setToolTip(task.completed ? QStringLiteral("标记为未完成") : QStringLiteral("标记为已完成"));
    connect(chk, &QCheckBox::clicked, this, [this](bool checked) {
        emit toggleRequested(m_taskId, checked);
    });
    lay->addWidget(chk);

    // 中间列：标题 + 标签
    auto *mid = new QVBoxLayout;
    mid->setSpacing(si(2));
    auto *title = new QLabel(task.title);
    title->setWordWrap(false);
    title->setTextInteractionFlags(Qt::NoTextInteraction);
    title->setStyleSheet(
        QStringLiteral("font-size:%1; font-weight:600; %2")
            .arg(sp(14),
                 task.completed ? QStringLiteral("color:%1; text-decoration:line-through;").arg(kColorMuted2)
                                : QStringLiteral("color:%1;").arg(kColorFg)));
    title->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    // 关键：让标题/标签区域鼠标事件穿透到行自身，确保 :hover 高亮可靠触发
    title->setAttribute(Qt::WA_TransparentForMouseEvents);
    mid->addWidget(title);
    if (!task.tags.isEmpty()) {
        auto *tags = new QLabel(task.tags.join(QStringLiteral(" · ")));
        tags->setAttribute(Qt::WA_TransparentForMouseEvents);
        tags->setStyleSheet(
            QStringLiteral("color:%1; font-size:%2;").arg(kColorFgMuted, sp(11)));
        mid->addWidget(tags);
    }
    lay->addLayout(mid, 1);

    if (task.priority > TodoPriorityNone) {
        auto *p = new QLabel(priorityGlyph(task.priority));
        p->setToolTip(task.priority == TodoPriorityHigh ? QStringLiteral("高优先级")
                       : task.priority == TodoPriorityMedium ? QStringLiteral("中优先级")
                                                             : QStringLiteral("低优先级"));
        p->setStyleSheet(QStringLiteral("color:%1; font-size:%2; font-weight:700;")
                             .arg(priorityColor(task.priority).name(), sp(12)));
        lay->addWidget(p);
    }
    if (task.hasDue()) {
        const QDate d = QDate::fromString(task.dueDate, Qt::ISODate);
        const bool overdue = d.isValid() && !task.completed && d < QDate::currentDate();
        const QString col = overdue ? QString::fromLatin1(kColorDanger) : QString::fromLatin1(kColorFgMuted);
        auto *due = new QLabel(dueLabel(task));
        due->setStyleSheet(
            QStringLiteral("color:%1; font-size:%2; padding:1px %3; border:1px solid %4;"
                           " border-radius:%5;")
                .arg(col, sp(11), sp(6),
                     overdue ? QString::fromLatin1(kColorDanger) : QString::fromLatin1(kColorBorder), sp(8)));
        lay->addWidget(due);
    }
    if (!dotColor.isEmpty()) {
        QPixmap pm(si(10), si(10));
        pm.fill(QColor(dotColor));
        auto *dot = new QLabel;
        dot->setPixmap(pm);
        lay->addWidget(dot);
    }
    // 首屏即应用底/hover 样式，保证鼠标移到任务项时有高亮
    setHighlighted(false);
}

void TodoTaskRow::setHighlighted(bool on)
{
    if (m_rowStyled && m_highlighted == on)
        return;
    m_rowStyled = true;
    m_highlighted = on;
    // 行内文字/勾选框等子控件背景透明，避免与行高亮叠加出深色块
    const QString sub = QStringLiteral(
        "QWidget#TodoRow QLabel,QWidget#TodoRow QCheckBox{background:transparent;}");
    const QString base = on
        ? QStringLiteral("QWidget#TodoRow{background:rgba(76,139,245,0.14);border:1px solid rgba(76,139,245,0.55);border-radius:6px;}"
                         "QWidget#TodoRow:hover{background:rgba(76,139,245,0.22);border:1px solid rgba(76,139,245,0.8);}")
        : QStringLiteral("QWidget#TodoRow{background:transparent;border:1px solid transparent;border-radius:6px;}"
                         "QWidget#TodoRow:hover{background:rgba(255,255,255,0.09);border:1px solid rgba(148,163,184,0.6);}");
    setStyleSheet(sub + base);
}

void TodoTaskRow::mousePressEvent(QMouseEvent *event)
{
    emit selected(m_taskId);
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

void TodoPage::applyUiScale()
{
    ui->TodoTopBar->setFixedHeight(si(46));
    ui->topBarLay->setContentsMargins(si(16), 0, si(16), 0);
    ui->topBarLay->setSpacing(si(2));
    if (m_detailPanel) {
        m_detailW = si(280);
        // 展开状态下跟随缩放刷新宽度
        if (m_detailPanel->isVisible()) {
            m_detailPanel->setMaximumWidth(m_detailW);
            m_detailPanel->setMinimumWidth(m_detailW);
        }
    }
    
    applyPageStyles();
    rebuildListsMenu();
    rebuildList();
}

void TodoPage::buildUi()
{
    // 静态布局来自 Qt Designer（todopage.ui -> ui_todopage.h），
    // .ui 中的边距/间距为基准值，si() 缩放几何在此重设
    ui = new Ui::TodoPage;
    ui->setupUi(this);

    // ── 运行时缩放几何（随 UI 缩放变化，无法烘焙进 .ui） ──
    ui->TodoTopBar->setFixedHeight(si(46));
    ui->topBarLay->setContentsMargins(si(16), 0, si(16), 0);
    ui->topBarLay->setSpacing(si(2));
    ui->TodoDetail->setFixedWidth(si(280));
    ui->centerLay->setContentsMargins(si(12), si(12), si(12), si(12));
    ui->surfaceLay->setContentsMargins(si(16), si(16), si(16), si(14));
    ui->surfaceLay->setSpacing(si(10));
    ui->detailLay->setContentsMargins(si(14), si(14), si(14), si(14));
    ui->detailLay->setSpacing(si(8));
    ui->bodyLay->setSpacing(si(8));
    ui->form->setHorizontalSpacing(si(8));
    ui->form->setVerticalSpacing(si(6));
    ui->dueRow->setSpacing(si(4));
    ui->dNotes->setMinimumHeight(si(56));
    ui->TodoSubs->setFixedHeight(si(120));

    // ── 成员别名：业务逻辑沿用 m_* 指针，静态布局归属 .ui 文件 ──
    m_surface = ui->TodoSurface;
    m_viewTitle = ui->TodoViewTitle;
    m_viewCount = ui->TodoViewCount;
    m_quickAdd = ui->TodoQuickAdd;
    m_list = ui->TodoList;
    m_completedBtn = ui->completedBtn;
    m_progress = ui->TodoProgress;
    m_detailPanel = ui->TodoDetail;
    m_detailEmpty = ui->TodoDetailEmpty;
    m_detailBody = ui->detailBody;
    // 标题大字隐藏，界面更贴近背景（视图信息由顶部切换栏明确给出）
    m_viewTitle->setVisible(false);
    // 详情面板默认收起，点击任务时才滑出
    m_detailPanel->setMaximumWidth(0);
    m_detailPanel->setMinimumWidth(0);
    m_detailPanel->hide();
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

    // ── 顶部切换栏视图按钮（objectName 供 applyPageStyles 匹配） ──
    m_viewBtns = {ui->btnInbox, ui->btnToday, ui->btnNext7, ui->btnAll};
    for (auto *b : m_viewBtns)
        b->setObjectName(QStringLiteral("TodoSideBtn"));
    
    connect(ui->btnInbox, &QToolButton::clicked, this, [this] { selectView(ViewInbox); });
    connect(ui->btnToday, &QToolButton::clicked, this, [this] { selectView(ViewToday); });
    connect(ui->btnNext7, &QToolButton::clicked, this, [this] { selectView(ViewNext7); });
    connect(ui->btnAll, &QToolButton::clicked, this, [this] { selectView(ViewAll); });

    // 清单下拉：按钮弹出菜单（菜单内容随 lists 变化在 rebuildListsMenu 重建）
    m_listsBtn = ui->btnLists;
    m_listsBtn->setObjectName(QStringLiteral("TodoSideBtn"));
    m_listsMenu = new QMenu(m_listsBtn);
    m_listsBtn->setMenu(m_listsMenu);
    m_listsBtn->setPopupMode(QToolButton::InstantPopup);
    connect(m_listsMenu, &QMenu::aboutToShow, this, &TodoPage::rebuildListsMenu);

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
    rebuildListsMenu();
}

void TodoPage::applyPageStyles()
{
    // 顶部切换栏：完全透明、无边界，与主内容区融为一体
    if (auto *topBar = ui ? ui->TodoTopBar : nullptr)
        topBar->setStyleSheet(QStringLiteral("QWidget#TodoTopBar{background:transparent;}"));

    // 视图/清单切换按钮：下划线 Tab 风格，选中项 accent 文字 + 底部强调线
    const QString viewStyle =
        QStringLiteral("QToolButton#TodoSideBtn{padding:%1 %2;border:none;border-radius:%3px;color:%4;"
                       "background:transparent;font-size:%5;font-weight:500;}"
                       "QToolButton#TodoSideBtn:hover{background:rgba(255,255,255,0.06);color:%7;}"
                       "QToolButton#TodoSideBtn:checked{color:%8;font-weight:700;border-bottom:2px solid %8;}")
            .arg(sp(7), sp(14), sp(6), kColorFgMuted, sp(13), sp(1), kColorFg, kColorAccent);
    for (auto *b : m_viewBtns)
        b->setStyleSheet(viewStyle);
    if (m_listsBtn)
        m_listsBtn->setStyleSheet(viewStyle);

    // 中间任务列表表面：完全透明、无边框，直接落在主背景上（与其他页一致）
    if (m_surface) {
        m_surface->setStyleSheet(QStringLiteral("QWidget#TodoSurface{background:transparent;}"));
        clearDropShadow(m_surface, m_surfaceShadow);
        m_surfaceShadow = nullptr;
    }

    if (m_viewTitle)
        m_viewTitle->setStyleSheet(QStringLiteral("font-size:%1;font-weight:700;color:%2;").arg(sp(20), kColorFg));
    if (m_viewCount)
        m_viewCount->setStyleSheet(QStringLiteral("color:%1;font-size:%2;").arg(kColorFgMuted, sp(12)));
    if (m_quickAdd)
        m_quickAdd->setStyleSheet(
            QStringLiteral("QLineEdit#TodoQuickAdd{font-size:%1;padding:%2 %3;border:1px dashed %4;"
                           "border-radius:8px;background:transparent;}")
                .arg(sp(14), sp(8), sp(10), kColorBorder));
    if (m_list)
        m_list->setStyleSheet(
            QStringLiteral("QListWidget#TodoList{background:transparent;border:none;outline:none;}"
                           "QListWidget#TodoList::item{border:none;padding:2px;}"
                           "QListWidget#TodoList::item:hover,QListWidget#TodoList::item:selected{background:transparent;}"));
    if (m_completedBtn)
        m_completedBtn->setStyleSheet(
            QStringLiteral("QPushButton#TodoCompleted{text-align:left;padding:%1 %2;border:none;"
                           "border-radius:6px;color:%3;background:transparent;font-size:%2;}")
                .arg(sp(6), sp(8), kColorFgMuted));
    if (m_progress)
        m_progress->setStyleSheet(QStringLiteral("color:%1;font-size:%2;padding-left:%3;").arg(kColorFgMuted, sp(11), sp(8)));

    if (m_detailPanel)
        // 详情面板：透明背景 + 极淡左分隔线，与主内容区融为一体
        m_detailPanel->setStyleSheet(
            QStringLiteral("QWidget#TodoDetail{background:transparent;border-left:1px solid %1;}")
                .arg(glassBorder()));
    if (m_detailEmpty)
        m_detailEmpty->setStyleSheet(QStringLiteral("color:%1;font-size:%2;").arg(kColorFgMuted, sp(12)));
    // 详情面板内所有输入控件：透明底 + 细边框，去掉灰色块
    if (m_detailBody)
        m_detailBody->setStyleSheet(
            QStringLiteral("QWidget#detailBody QLineEdit,QWidget#detailBody QPlainTextEdit,"
                           "QWidget#detailBody QComboBox,QWidget#detailBody QDateEdit{"
                           "background:transparent;border:1px solid %1;border-radius:6px;}"
                           "QWidget#detailBody QComboBox::drop-down{border:none;width:20px;}"
                           "QWidget#detailBody QDateEdit{padding:4px 6px;}")
                .arg(kColorBorder));
    if (auto *head = ui->detailHeadTitle)
        head->setStyleSheet(
            QStringLiteral("color:%1;font-size:%2;font-weight:700;").arg(kColorFg, sp(13)));
    if (auto *closeBtn = ui->detailClose)
        closeBtn->setStyleSheet(
            QStringLiteral("QPushButton#detailClose{border:none;border-radius:6px;color:%1;"
                           "background:transparent;font-size:%2;padding:2px 6px;}"
                           "QPushButton#detailClose:hover{background:%3;color:%4;}")
                .arg(kColorFgMuted, sp(14), kColorBgElev2, kColorFg));
    if (m_dTitle)
        m_dTitle->setStyleSheet(
            QStringLiteral("QPlainTextEdit#TodoTitleEdit{font-size:%1;font-weight:700;border:1px solid transparent;"
                           "background:transparent;padding:4px 2px;border-radius:6px;}"
                           "QPlainTextEdit#TodoTitleEdit:focus{border:1px solid %2;background:transparent;}")
                .arg(sp(16), kColorAccent));
    if (m_dSubs)
        m_dSubs->setStyleSheet(
            QStringLiteral("QListWidget#TodoSubs{background:transparent;border:1px solid %1;border-radius:6px;}"
                           "QListWidget#TodoSubs::item{border:none;padding:2px;}")
                .arg(kColorBorder));
    for (QLabel *l : m_detailBody ? m_detailBody->findChildren<QLabel *>(QStringLiteral("TodoFieldLabel")) : QList<QLabel *>())
        l->setStyleSheet(QStringLiteral("color:%1;font-size:%2;").arg(kColorFgMuted, sp(11)));
}

void TodoPage::rebuildListsMenu()
{
    // 清单下拉菜单（重建，跟随 lists 变化与缩放；aboutToShow 时也会刷新）
    if (!m_listsMenu)
        return;
    m_listsMenu->clear();
    for (const auto &l : m_lists) {
        QPixmap pm(si(12), si(12));
        pm.fill(QColor(m_listColors.value(l.id)));
        QAction *a = m_listsMenu->addAction(pm, l.name);
        a->setCheckable(true);
        a->setChecked(m_view == ViewList && m_viewList == l.id);
        connect(a, &QAction::triggered, this, [this, l] { selectView(ViewList, l.id); });
    }
    if (!m_lists.isEmpty())
        m_listsMenu->addSeparator();
    m_listsMenu->addAction(QStringLiteral("＋ 新建清单…"), this, &TodoPage::onNewList);
    if (m_view == ViewList) {
        m_listsMenu->addAction(QStringLiteral("重命名清单…"), this, [this] { onRenameList(m_viewList); });
        m_listsMenu->addAction(QStringLiteral("删除清单"), this, [this] { onDeleteList(m_viewList); });
    }
}

void TodoPage::setViewButtonsChecked()
{
    for (auto *b : m_viewBtns)
        b->setChecked(false);

    switch (m_view) {
    case ViewInbox: if (m_viewBtns.size() > 0) m_viewBtns[0]->setChecked(true); break;
    case ViewToday: if (m_viewBtns.size() > 1) m_viewBtns[1]->setChecked(true); break;
    case ViewNext7: if (m_viewBtns.size() > 2) m_viewBtns[2]->setChecked(true); break;
    case ViewAll:   if (m_viewBtns.size() > 3) m_viewBtns[3]->setChecked(true); break;
    case ViewList:
        break;
    }
    // 清单下拉按钮：选中清单时点亮并显示清单名
    if (m_listsBtn) {
        m_listsBtn->setChecked(m_view == ViewList);
        QString name = QStringLiteral("清单");
        if (m_view == ViewList) {
            for (const auto &l : m_lists) {
                if (l.id == m_viewList) {
                    name = l.name;
                    break;
                }
            }
        }
        m_listsBtn->setText(name);
    }
    
}

// 顶部切换栏视图按钮仅展示文字（胶囊样式），无需字形图标

void TodoPage::selectView(ViewKind kind, qint64 listId)
{
    if (m_view == kind && m_viewList == listId) {
        setViewButtonsChecked();
        return;
    }
    m_view = kind;
    m_viewList = listId;
    m_showCompleted = false;
    m_animateNext = true;
    clearDetail();
    setViewButtonsChecked();
    rebuildList();
}

QString TodoPage::viewTitle() const
{
    switch (m_view) {
    case ViewInbox: return QStringLiteral("收集箱");
    case ViewToday: return QStringLiteral("今天");
    case ViewNext7: return QStringLiteral("最近 7 天");
    case ViewAll: return QStringLiteral("全部");
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
    m_viewTitle->setText(viewTitle());
    m_quickAdd->setPlaceholderText(QStringLiteral("添加任务到「%1」…").arg(viewTitle()));

    for (const auto &t : all) {
        if (t.completed)
            break; // all 已按「未完成在前」排好
        auto *item = new QListWidgetItem(m_list);
        item->setSizeHint(QSize(0, si(42)));
        m_list->addItem(item);
        QWidget *rw = makeRow(t);
        m_list->setItemWidget(item, rw);
        if (m_animateNext)
            fadeInWidget(rw, 180);
    }

    if (anyDone) {
        if (m_showCompleted) {
            for (const auto &t : done) {
                auto *item = new QListWidgetItem(m_list);
                item->setSizeHint(QSize(0, si(42)));
                m_list->addItem(item);
                QWidget *rw = makeRow(t);
                m_list->setItemWidget(item, rw);
                if (m_animateNext)
                    fadeInWidget(rw, 180);
            }
        }
        m_completedBtn->setVisible(true);
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

    m_viewCount->setText(QStringLiteral("%1 项待办").arg(openCount));
    int total = m_tasks.size();
    int doneTotal = 0;
    for (const auto &t : m_tasks)
        if (t.completed)
            ++doneTotal;
    m_progress->setText(QStringLiteral("已完成 %1 / %2").arg(doneTotal).arg(total));

    m_animateNext = false; // 入场动画仅在视图切换/初次构建时触发一次
    setRowHighlight(m_selectedTask);
}

QWidget *TodoPage::makeRow(const TodoTask &task)
{
    QString dot;
    if (task.listId != 0 && m_listColors.contains(task.listId))
        dot = m_listColors.value(task.listId);
    auto *row = new TodoTaskRow(task, dot);
    connect(row, &TodoTaskRow::selected, this, [this](qint64 id) {
        m_selectedTask = id;
        loadDetail(id);
        setRowHighlight(id);
    });
    connect(row, &TodoTaskRow::toggleRequested, this, &TodoPage::onToggleRequested);
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

// ── 数据变化 ───────────────────────────────────────────────
void TodoPage::onDataChanged()
{
    m_lists = m_source->lists();
    m_tasks = m_source->tasks();
    m_listColors.clear();
    for (const auto &l : m_lists)
        m_listColors.insert(l.id, l.color.isEmpty() ? colorForString(l.name).name() : l.color);

    rebuildListsMenu();
    reloadListCombo();
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

    slideDetail(true);
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
    slideDetail(false);
    if (m_detailBody)
        m_detailBody->hide();
    if (m_detailEmpty)
        m_detailEmpty->show();
}

// 详情面板滑入/收起：动画 maximumWidth 0 ↔ m_detailW，收起后隐藏，结束锁住最小宽
void TodoPage::slideDetail(bool open)
{
    if (!m_detailPanel)
        return;
    m_detailPanel->setMinimumWidth(0);
    if (open && !m_detailPanel->isVisible()) {
        m_detailPanel->setMaximumWidth(0);
        m_detailPanel->show();
    }
    auto *a = new QPropertyAnimation(m_detailPanel, "maximumWidth", this);
    a->setDuration(180);
    a->setStartValue(m_detailPanel->maximumWidth());
    a->setEndValue(open ? m_detailW : 0);
    a->setEasingCurve(QEasingCurve::OutCubic);
    connect(a, &QPropertyAnimation::finished, this, [this, open]() {
        if (!open)
            m_detailPanel->hide();
        m_detailPanel->setMinimumWidth(open ? m_detailW : 0);
    });
    a->start(QAbstractAnimation::DeleteWhenStopped);
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
