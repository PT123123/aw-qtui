// todopage.cpp —— Todo 页实现（参照 TickTick / Super Productivity）
#include "todopage.h"

#include "theme.h"
#include "mockdata.h"
#include "todostore.h"
#include "appsettings.h"
#include "focusstore.h"
#include "focuswidgets.h"
#include "focuscharts.h"

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
#include <QRegularExpression>
#include <QSizePolicy>
#include <QStackedWidget>
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
    mid->addWidget(title);
    if (!task.tags.isEmpty()) {
        auto *tags = new QLabel(task.tags.join(QStringLiteral(" · ")));
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
}

void TodoTaskRow::setHighlighted(bool on)
{
    if (m_highlighted == on)
        return;
    m_highlighted = on;
    setStyleSheet(on ? QStringLiteral("QWidget#TodoRow{background:rgba(76,139,245,0.14);border-radius:6px;}"
                                      "QWidget#TodoRow:hover{background:rgba(76,139,245,0.20);}")
                     : QStringLiteral("QWidget#TodoRow{background:transparent;border-radius:6px;}"
                                      "QWidget#TodoRow:hover{background:%1;}").arg(kColorHover));
}

void TodoTaskRow::mousePressEvent(QMouseEvent *event)
{
    emit selected(m_taskId);
    QWidget::mousePressEvent(event);
}

// ══════════════════════════════════════════════════════════
// TodoPage
// ══════════════════════════════════════════════════════════
TodoPage::TodoPage(TodoSource *source, FocusSource *focus, QWidget *parent)
    : QWidget(parent), m_source(source), m_focus(focus), m_focusModules(loadFocusModules())
{
    buildUi();
    connect(m_source, &TodoSource::dataChanged, this, &TodoPage::onDataChanged);
    m_source->load();
}

void TodoPage::refresh()
{
    onDataChanged();
    refreshModulePages();
}

void TodoPage::applyUiScale()
{
    if (m_sidebar)
        m_sidebar->setFixedWidth(si(180));
    if (m_detailPanel)
        m_detailPanel->setFixedWidth(si(280));
    applyPageStyles();
    rebuildSidebar();
    rebuildList();
    scaleModulePages();
}

void TodoPage::buildUi()
{
    auto *root = new QHBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(0);

    // ── 左侧导航 ──
    m_sidebar = new QWidget;
    m_sidebar->setObjectName(QStringLiteral("TodoSidebar"));
    m_sidebar->setFixedWidth(si(180));
    auto *sl = new QVBoxLayout(m_sidebar);
    sl->setContentsMargins(si(8), si(14), si(8), si(12));
    sl->setSpacing(si(4));

    auto *brand = new QLabel(QStringLiteral("任务"));
    brand->setObjectName(QStringLiteral("TodoBrand"));
    sl->addWidget(brand);

    auto addViewBtn = [this, &sl](const QString &text, ViewKind k) {
        auto *b = new QToolButton;
        b->setText(text);
        b->setCheckable(true);
        b->setObjectName(QStringLiteral("TodoSideBtn"));
        b->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
        connect(b, &QToolButton::clicked, this, [this, k] { selectView(k); });
        sl->addWidget(b);
        m_viewBtns.append(b);
    };
    addViewBtn(QStringLiteral("📥 收集箱"), ViewInbox);
    addViewBtn(QStringLiteral("📅 今天"), ViewToday);
    addViewBtn(QStringLiteral("📆 最近 7 天"), ViewNext7);
    addViewBtn(QStringLiteral("☰ 全部"), ViewAll);

    auto *listTitle = new QLabel(QStringLiteral("清单"));
    listTitle->setObjectName(QStringLiteral("TodoSection"));
    sl->addWidget(listTitle);

    m_listsBox = new QWidget;
    m_listsLay = new QVBoxLayout(m_listsBox);
    m_listsLay->setContentsMargins(0, 0, 0, 0);
    m_listsLay->setSpacing(si(2));
    sl->addWidget(m_listsBox);

    // ── 专注模块（侧边栏开关由 Todo 设置 → 功能模块 控制）──
    m_moduleSection = new QLabel(QStringLiteral("专注"));
    m_moduleSection->setObjectName(QStringLiteral("TodoModuleSection"));
    sl->addWidget(m_moduleSection);

    m_moduleBox = new QWidget;
    m_moduleLay = new QVBoxLayout(m_moduleBox);
    m_moduleLay->setContentsMargins(0, 0, 0, 0);
    m_moduleLay->setSpacing(si(2));
    sl->addWidget(m_moduleBox);

    const QStringList moduleLabels = {
        QStringLiteral("🍅 计时"), QStringLiteral("📊 专注记录"), QStringLiteral("🕓 专注记录详情"),
        QStringLiteral("📈 专注时间线"), QStringLiteral("🔥 热力图"), QStringLiteral("⏰ 最佳专注时间"),
        QStringLiteral("📅 日历"), QStringLiteral("🎂 倒数纪念日")
    };
    for (int i = 0; i < moduleLabels.size(); ++i) {
        auto *b = new QToolButton;
        b->setText(moduleLabels[i]);
        b->setCheckable(true);
        b->setObjectName(QStringLiteral("TodoSideBtn"));
        b->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
        connect(b, &QToolButton::clicked, this, [this, i] { showModule(ModuleKind(i)); });
        m_moduleLay->addWidget(b);
        m_moduleBtns.append(b);
    }

    sl->addStretch(1);

    m_newListBtn = new QPushButton(QStringLiteral("＋ 新建清单"));
    m_newListBtn->setObjectName(QStringLiteral("TodoNewList"));
    m_newListBtn->setCursor(Qt::PointingHandCursor);
    connect(m_newListBtn, &QPushButton::clicked, this, &TodoPage::onNewList);
    sl->addWidget(m_newListBtn);

    m_settingsBtn = new QPushButton(QStringLiteral("⚙ 设置"));
    m_settingsBtn->setObjectName(QStringLiteral("TodoNewList"));
    m_settingsBtn->setCursor(Qt::PointingHandCursor);
    m_settingsBtn->setToolTip(QStringLiteral("专注模块 · 侧边栏显示开关"));
    connect(m_settingsBtn, &QPushButton::clicked, this, &TodoPage::openFocusSettings);
    sl->addWidget(m_settingsBtn);

    root->addWidget(m_sidebar);

    // ── 中间任务列表（浮动表面卡片：材质背景 + 投影 + 圆角）──
    m_surface = new QWidget;
    m_surface->setObjectName(QStringLiteral("TodoSurface"));
    auto *ll = new QVBoxLayout(m_surface);
    ll->setContentsMargins(si(16), si(16), si(16), si(14));
    ll->setSpacing(si(10));

    auto *head = new QHBoxLayout;
    m_viewTitle = new QLabel;
    m_viewTitle->setObjectName(QStringLiteral("TodoViewTitle"));
    m_viewCount = new QLabel;
    m_viewCount->setObjectName(QStringLiteral("TodoViewCount"));
    head->addWidget(m_viewTitle);
    head->addStretch(1);
    head->addWidget(m_viewCount);
    ll->addLayout(head);

    m_quickAdd = new QLineEdit;
    m_quickAdd->setObjectName(QStringLiteral("TodoQuickAdd"));
    m_quickAdd->setClearButtonEnabled(true);
    connect(m_quickAdd, &QLineEdit::returnPressed, this, &TodoPage::onQuickAdd);
    ll->addWidget(m_quickAdd);

    m_list = new QListWidget;
    m_list->setObjectName(QStringLiteral("TodoList"));
    m_list->setSelectionMode(QAbstractItemView::NoSelection);
    m_list->setFocusPolicy(Qt::NoFocus);
    ll->addWidget(m_list, 1);

    m_completedBtn = new QPushButton;
    m_completedBtn->setObjectName(QStringLiteral("TodoCompleted"));
    m_completedBtn->setCheckable(true);
    m_completedBtn->setCursor(Qt::PointingHandCursor);
    connect(m_completedBtn, &QPushButton::clicked, this, [this](bool on) {
        m_showCompleted = on;
        rebuildList();
    });
    ll->addWidget(m_completedBtn);

    m_progress = new QLabel;
    m_progress->setObjectName(QStringLiteral("TodoProgress"));
    ll->addWidget(m_progress);

    // 表面卡片四周留出空隙（顶部/右侧/底部），左侧紧贴侧栏
    auto *centerHost = new QWidget;
    auto *chLay = new QVBoxLayout(centerHost);
    chLay->setContentsMargins(0, si(12), si(12), si(12));
    chLay->setSpacing(0);
    chLay->addWidget(m_surface);

    // ── 右侧详情面板 ──
    m_detailPanel = new QWidget;
    m_detailPanel->setObjectName(QStringLiteral("TodoDetail"));
    m_detailPanel->setFixedWidth(si(280));
    auto *dl = new QVBoxLayout(m_detailPanel);
    dl->setContentsMargins(si(14), si(14), si(14), si(14));
    dl->setSpacing(si(8));

    m_detailEmpty = new QLabel(QStringLiteral("选择任务以查看 / 编辑"));
    m_detailEmpty->setObjectName(QStringLiteral("TodoDetailEmpty"));
    m_detailEmpty->setAlignment(Qt::AlignCenter);
    dl->addWidget(m_detailEmpty, 1);

    m_detailBody = new QWidget;
    auto *db = new QVBoxLayout(m_detailBody);
    db->setContentsMargins(0, 0, 0, 0);
    db->setSpacing(si(8));

    m_dTitle = new QLineEdit;
    m_dTitle->setObjectName(QStringLiteral("TodoTitleEdit"));
    connect(m_dTitle, &QLineEdit::editingFinished, this, &TodoPage::commitDetail);
    db->addWidget(m_dTitle);

    m_dDone = new QCheckBox(QStringLiteral("已完成"));
    m_dDone->setCursor(Qt::PointingHandCursor);
    connect(m_dDone, &QCheckBox::toggled, this, [this](bool on) {
        if (m_loadingDetail || m_selectedTask == 0)
            return;
        m_source->setTaskCompleted(m_selectedTask, on);
    });
    db->addWidget(m_dDone);

    auto *form = new QGridLayout;
    form->setHorizontalSpacing(si(8));
    form->setVerticalSpacing(si(6));
    auto addLabel = [](const QString &s) {
        auto *l = new QLabel(s);
        l->setObjectName(QStringLiteral("TodoFieldLabel"));
        return l;
    };

    form->addWidget(addLabel(QStringLiteral("清单")), 0, 0);
    m_dList = new QComboBox;
    connect(m_dList, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this](int) {
        if (!m_loadingDetail)
            commitDetail();
    });
    form->addWidget(m_dList, 0, 1);

    form->addWidget(addLabel(QStringLiteral("优先级")), 1, 0);
    m_dPriority = new QComboBox;
    m_dPriority->addItem(QStringLiteral("无优先级"), TodoPriorityNone);
    m_dPriority->addItem(QStringLiteral("低"), TodoPriorityLow);
    m_dPriority->addItem(QStringLiteral("中"), TodoPriorityMedium);
    m_dPriority->addItem(QStringLiteral("高"), TodoPriorityHigh);
    connect(m_dPriority, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this](int) {
        if (!m_loadingDetail)
            commitDetail();
    });
    form->addWidget(m_dPriority, 1, 1);

    form->addWidget(addLabel(QStringLiteral("截止")), 2, 0);
    auto *dueRow = new QHBoxLayout;
    dueRow->setSpacing(si(4));
    m_dHasDue = new QCheckBox;
    m_dHasDue->setToolTip(QStringLiteral("设置截止日期"));
    m_dHasDue->setCursor(Qt::PointingHandCursor);
    connect(m_dHasDue, &QCheckBox::toggled, this, [this](bool on) {
        m_dDue->setEnabled(on);
        if (!m_loadingDetail)
            commitDetail();
    });
    m_dDue = new QDateEdit;
    m_dDue->setCalendarPopup(true);
    m_dDue->setDisplayFormat(QStringLiteral("yyyy-MM-dd"));
    m_dDue->setDate(QDate::currentDate());
    m_dDue->setEnabled(false);
    connect(m_dDue, &QDateEdit::dateChanged, this, [this](const QDate &) {
        if (!m_loadingDetail)
            commitDetail();
    });
    dueRow->addWidget(m_dHasDue);
    dueRow->addWidget(m_dDue, 1);
    form->addLayout(dueRow, 2, 1);

    form->addWidget(addLabel(QStringLiteral("重复")), 3, 0);
    m_dRecur = new QComboBox;
    m_dRecur->addItem(QStringLiteral("不重复"), QString());
    m_dRecur->addItem(QStringLiteral("每天"), QStringLiteral("daily"));
    m_dRecur->addItem(QStringLiteral("每个工作日"), QStringLiteral("weekdays"));
    m_dRecur->addItem(QStringLiteral("每周"), QStringLiteral("weekly"));
    m_dRecur->addItem(QStringLiteral("每月"), QStringLiteral("monthly"));
    connect(m_dRecur, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this](int) {
        if (!m_loadingDetail)
            commitDetail();
    });
    form->addWidget(m_dRecur, 3, 1);

    form->addWidget(addLabel(QStringLiteral("标签")), 4, 0);
    m_dTags = new QLineEdit;
    m_dTags->setPlaceholderText(QStringLiteral("逗号分隔"));
    connect(m_dTags, &QLineEdit::editingFinished, this, &TodoPage::commitDetail);
    form->addWidget(m_dTags, 4, 1);

    db->addLayout(form);

    auto *notesLabel = addLabel(QStringLiteral("备注"));
    db->addWidget(notesLabel);
    m_dNotes = new QPlainTextEdit;
    m_dNotes->setPlaceholderText(QStringLiteral("添加备注…"));
    m_dNotes->setMinimumHeight(si(56));
    connect(m_dNotes, &QPlainTextEdit::textChanged, this, [this] { m_commitTimer->start(); });
    db->addWidget(m_dNotes);

    auto *subLabel = addLabel(QStringLiteral("子任务"));
    db->addWidget(subLabel);
    m_dSubs = new QListWidget;
    m_dSubs->setObjectName(QStringLiteral("TodoSubs"));
    m_dSubs->setSelectionMode(QAbstractItemView::NoSelection);
    m_dSubs->setFocusPolicy(Qt::NoFocus);
    m_dSubs->setFixedHeight(si(120));
    db->addWidget(m_dSubs);

    m_dSubAdd = new QLineEdit;
    m_dSubAdd->setPlaceholderText(QStringLiteral("添加子任务…"));
    connect(m_dSubAdd, &QLineEdit::returnPressed, this, &TodoPage::onSubtaskAdd);
    db->addWidget(m_dSubAdd);

    db->addStretch(1);

    m_dDelete = new QPushButton(QStringLiteral("删除任务"));
    m_dDelete->setObjectName(QStringLiteral("DangerBtn"));
    m_dDelete->setCursor(Qt::PointingHandCursor);
    connect(m_dDelete, &QPushButton::clicked, this, &TodoPage::onTaskDelete);
    db->addWidget(m_dDelete);

    dl->addWidget(m_detailBody, 1);
    m_detailBody->hide();

    // ── 主区堆栈：第 0 页 = 任务视图（列表 + 详情），1..8 = 专注模块 ──
    m_mainStack = new QStackedWidget;
    auto *taskView = new QWidget;
    auto *tvLay = new QHBoxLayout(taskView);
    tvLay->setContentsMargins(0, 0, 0, 0);
    tvLay->setSpacing(0);
    tvLay->addWidget(centerHost, 1);
    tvLay->addWidget(m_detailPanel);
    m_mainStack->addWidget(taskView); // 0

    m_timerPage = new FocusTimerPage(m_focus, m_source);
    m_overviewPage = new FocusOverviewPage(m_focus);
    m_detailPage = new FocusDetailPage(m_focus);
    m_weekPage = new FocusWeekPage(m_focus);
    m_heatmapPage = new FocusHeatmapPage(m_focus);
    m_bestPage = new FocusBestPage(m_focus);
    m_calendarPage = new FocusCalendarPage(m_focus, m_source);
    m_memorialPage = new FocusMemorialPage(m_focus);
    m_mainStack->addWidget(m_timerPage);    // 1
    m_mainStack->addWidget(m_overviewPage); // 2
    m_mainStack->addWidget(m_detailPage);   // 3
    m_mainStack->addWidget(m_weekPage);     // 4
    m_mainStack->addWidget(m_heatmapPage);  // 5
    m_mainStack->addWidget(m_bestPage);     // 6
    m_mainStack->addWidget(m_calendarPage); // 7
    m_mainStack->addWidget(m_memorialPage); // 8

    root->addWidget(m_mainStack, 1);

    m_commitTimer = new QTimer(this);
    m_commitTimer->setSingleShot(true);
    m_commitTimer->setInterval(250);
    connect(m_commitTimer, &QTimer::timeout, this, &TodoPage::commitDetail);

    applyPageStyles();
    rebuildSidebar();
}

void TodoPage::applyPageStyles()
{
    if (m_sidebar)
        m_sidebar->setStyleSheet(
            QStringLiteral("QWidget#TodoSidebar{background:%1;border-right:1px solid %2;}")
                .arg(glassBg(kColorBgElev), glassBorder()));
    // 中间任务列表表面：玻璃渐变背景 + 圆角 + 玻璃亮边；阴影随强度增删
    if (m_surface) {
        m_surface->setStyleSheet(
            QStringLiteral("QWidget#TodoSurface{background:%1;border:1px solid %2;border-radius:12px;}")
                .arg(glassBg(kColorBgElev), glassBorder()));
        clearDropShadow(m_surface, m_surfaceShadow);
        m_surfaceShadow = makeDropShadow(m_surface);
    }
    if (auto *brand = m_sidebar ? m_sidebar->findChild<QLabel *>(QStringLiteral("TodoBrand")) : nullptr)
        brand->setStyleSheet(QStringLiteral("font-size:%1;font-weight:700;color:%2;padding:%3 %4;")
                                 .arg(sp(18), kColorFg, sp(4), sp(8)));
    if (auto *sec = m_sidebar ? m_sidebar->findChild<QLabel *>(QStringLiteral("TodoSection")) : nullptr)
        sec->setStyleSheet(QStringLiteral("color:%1;font-size:%2;font-weight:700;padding:%3 %4 2px;")
                               .arg(kColorMuted2, sp(10), sp(6), sp(8)));

    const QString viewStyle =
        QStringLiteral("QToolButton#TodoSideBtn{text-align:left;padding:%1 %2;border:none;"
                       "border-radius:6px;color:%3;background:transparent;font-size:%4;}"
                       "QToolButton#TodoSideBtn:hover{background:%5;color:%6;}"
                       "QToolButton#TodoSideBtn:checked{background:rgba(76,139,245,0.16);color:white;}")
            .arg(sp(8), sp(10), kColorFgMuted, sp(13), kColorBgElev2, kColorFg);
    for (auto *b : m_viewBtns)
        b->setStyleSheet(viewStyle);
    for (auto *b : m_moduleBtns)
        b->setStyleSheet(viewStyle);

    if (m_moduleSection)
        m_moduleSection->setStyleSheet(
            QStringLiteral("color:%1;font-size:%2;font-weight:700;padding:%3 %4 2px;")
                .arg(kColorMuted2, sp(10), sp(6), sp(8)));
    if (m_settingsBtn)
        m_settingsBtn->setStyleSheet(
            QStringLiteral("QPushButton#TodoNewList{text-align:left;padding:%1 %2;border:none;"
                           "border-radius:6px;color:%3;background:transparent;font-size:%4;}"
                           "QPushButton#TodoNewList:hover{background:%5;color:%6;}")
                .arg(sp(8), sp(10), kColorFgMuted, sp(13), kColorBgElev2, kColorFg));

    if (m_newListBtn)
        m_newListBtn->setStyleSheet(
            QStringLiteral("QPushButton#TodoNewList{text-align:left;padding:%1 %2;border:none;"
                           "border-radius:6px;color:%3;background:transparent;font-size:%4;}"
                           "QPushButton#TodoNewList:hover{background:%5;color:%6;}")
                .arg(sp(8), sp(10), kColorAccent, sp(13), kColorBgElev2, kColorFg));

    if (m_viewTitle)
        m_viewTitle->setStyleSheet(QStringLiteral("font-size:%1;font-weight:700;color:%2;").arg(sp(20), kColorFg));
    if (m_viewCount)
        m_viewCount->setStyleSheet(QStringLiteral("color:%1;font-size:%2;").arg(kColorFgMuted, sp(12)));
    if (m_quickAdd)
        m_quickAdd->setStyleSheet(
            QStringLiteral("QLineEdit#TodoQuickAdd{font-size:%1;padding:%2 %3;border:1px dashed %4;"
                           "border-radius:8px;background:%5;}")
                .arg(sp(14), sp(8), sp(10), kColorBorder, kColorBgElev));
    if (m_list)
        m_list->setStyleSheet(
            QStringLiteral("QListWidget#TodoList{background:transparent;border:none;outline:none;}"
                           "QListWidget#TodoList::item{border:none;padding:2px;}"));
    if (m_completedBtn)
        m_completedBtn->setStyleSheet(
            QStringLiteral("QPushButton#TodoCompleted{text-align:left;padding:%1 %2;border:none;"
                           "border-radius:6px;color:%3;background:transparent;font-size:%2;}")
                .arg(sp(6), sp(8), kColorFgMuted));
    if (m_progress)
        m_progress->setStyleSheet(QStringLiteral("color:%1;font-size:%2;padding-left:%3;").arg(kColorFgMuted, sp(11), sp(8)));

    if (m_detailPanel)
        m_detailPanel->setStyleSheet(
            QStringLiteral("QWidget#TodoDetail{background:%1;border-left:1px solid %2;}")
                .arg(glassBg(kColorBgElev), glassBorder()));
    if (m_detailEmpty)
        m_detailEmpty->setStyleSheet(QStringLiteral("color:%1;font-size:%2;").arg(kColorFgMuted, sp(12)));
    if (m_dTitle)
        m_dTitle->setStyleSheet(
            QStringLiteral("QLineEdit#TodoTitleEdit{font-size:%1;font-weight:700;border:1px solid transparent;"
                           "background:transparent;padding:4px 2px;border-radius:6px;}"
                           "QLineEdit#TodoTitleEdit:focus{border:1px solid %2;background:%3;}")
                .arg(sp(16), kColorAccent, kColorBg));
    if (m_dSubs)
        m_dSubs->setStyleSheet(
            QStringLiteral("QListWidget#TodoSubs{background:transparent;border:1px solid %1;border-radius:6px;}"
                           "QListWidget#TodoSubs::item{border:none;padding:2px;}")
                .arg(kColorBorder));
    for (QLabel *l : m_detailBody ? m_detailBody->findChildren<QLabel *>(QStringLiteral("TodoFieldLabel")) : QList<QLabel *>())
        l->setStyleSheet(QStringLiteral("color:%1;font-size:%2;").arg(kColorFgMuted, sp(11)));
}

void TodoPage::rebuildSidebar()
{
    // 清空清单按钮区（重建，跟随 lists 变化与缩放）
    if (m_listsLay) {
        while (auto *item = m_listsLay->takeAt(0)) {
            if (auto *w = item->widget())
                w->deleteLater();
            delete item;
        }
    }
    m_listBtns.clear();
    for (const auto &l : m_lists) {
        auto *b = new QToolButton;
        b->setText(l.name);
        b->setProperty("listId", l.id);
        b->setCheckable(true);
        b->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
        b->setObjectName(QStringLiteral("TodoListBtn"));
        b->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
        b->setToolTip(QStringLiteral("右键可重命名 / 删除"));
        QPixmap pm(si(12), si(12));
        pm.fill(QColor(m_listColors.value(l.id)));
        b->setIcon(QIcon(pm));
        b->setIconSize(QSize(si(14), si(14)));
        b->setStyleSheet(
            QStringLiteral("QToolButton#TodoListBtn{text-align:left;padding:%1 %2;border:none;"
                           "border-radius:6px;color:%3;background:transparent;font-size:%4;}"
                           "QToolButton#TodoListBtn:hover{background:%5;color:%6;}"
                           "QToolButton#TodoListBtn:checked{background:rgba(76,139,245,0.16);color:white;"
                           "border-left:3px solid %7;padding-left:%8;}")
                .arg(sp(7), sp(10), kColorFgMuted, sp(13), kColorBgElev2, kColorFg,
                     m_listColors.value(l.id), sp(9)));
        connect(b, &QToolButton::clicked, this, [this, l] { selectView(ViewList, l.id); });

        b->setContextMenuPolicy(Qt::CustomContextMenu);
        connect(b, &QToolButton::customContextMenuRequested, this, [this, b, l](const QPoint &pos) {
            QMenu menu;
            QAction *ren = menu.addAction(QStringLiteral("重命名清单…"));
            QAction *del = menu.addAction(QStringLiteral("删除清单"));
            QAction *act = menu.exec(b->mapToGlobal(pos));
            if (act == ren)
                onRenameList(l.id);
            else if (act == del)
                onDeleteList(l.id);
        });

        m_listsLay->addWidget(b);
        m_listBtns.append(b);
    }
    m_listsBox->setVisible(!m_lists.isEmpty());
    applyModuleVis();
    setViewButtonsChecked();
}

// 按功能模块开关控制「专注」分组与各模块按钮的侧边栏显隐
void TodoPage::applyModuleVis()
{
    if (m_moduleBox)
        m_moduleBox->setVisible(m_focusModules.timer || m_focusModules.overview
                                || m_focusModules.detail || m_focusModules.week
                                || m_focusModules.heatmap || m_focusModules.best
                                || m_focusModules.calendar || m_focusModules.memorial);
    if (m_moduleSection)
        m_moduleSection->setVisible(m_moduleBox ? m_moduleBox->isVisible() : false);
    const bool flags[] = {
        m_focusModules.timer, m_focusModules.overview, m_focusModules.detail,
        m_focusModules.week, m_focusModules.heatmap, m_focusModules.best,
        m_focusModules.calendar, m_focusModules.memorial
    };
    for (int i = 0; i < m_moduleBtns.size(); ++i)
        m_moduleBtns[i]->setVisible(i < 8 && flags[i]);
    // 当前展示的模块被关闭：退回任务视图
    if (m_moduleActive && (int(m_module) < 0 || int(m_module) >= 8 || !flags[int(m_module)]))
        selectView(ViewInbox);
}

void TodoPage::setViewButtonsChecked()
{
    for (auto *b : m_viewBtns)
        b->setChecked(false);
    for (auto *b : m_listBtns)
        b->setChecked(false);
    for (auto *b : m_moduleBtns)
        b->setChecked(false);

    if (m_moduleActive) {
        if (int(m_module) >= 0 && int(m_module) < m_moduleBtns.size())
            m_moduleBtns[int(m_module)]->setChecked(true);
        return;
    }

    switch (m_view) {
    case ViewInbox: if (m_viewBtns.size() > 0) m_viewBtns[0]->setChecked(true); break;
    case ViewToday: if (m_viewBtns.size() > 1) m_viewBtns[1]->setChecked(true); break;
    case ViewNext7: if (m_viewBtns.size() > 2) m_viewBtns[2]->setChecked(true); break;
    case ViewAll:   if (m_viewBtns.size() > 3) m_viewBtns[3]->setChecked(true); break;
    case ViewList:
        for (auto *b : m_listBtns) {
            if (b->property("listId").toLongLong() == m_viewList) {
                b->setChecked(true);
                break;
            }
        }
        break;
    }
}

// 切到专注模块：主区堆栈切页 + 侧边栏高亮
void TodoPage::showModule(ModuleKind kind)
{
    m_moduleActive = true;
    m_module = kind;
    if (m_mainStack) {
        const int idx = 1 + int(kind);
        if (idx < m_mainStack->count())
            m_mainStack->setCurrentIndex(idx);
    }
    setViewButtonsChecked();
}

void TodoPage::openFocusSettings()
{
    FocusSettingsDialog dlg(this);
    if (dlg.exec() != QDialog::Accepted)
        return;
    m_focusModules = dlg.modules();
    saveFocusModules(m_focusModules);
    applyModuleVis();
}

void TodoPage::scaleModulePages()
{
    if (m_timerPage) m_timerPage->applyUiScale();
    if (m_overviewPage) m_overviewPage->applyUiScale();
    if (m_detailPage) m_detailPage->applyUiScale();
    if (m_weekPage) m_weekPage->applyUiScale();
    if (m_heatmapPage) m_heatmapPage->applyUiScale();
    if (m_bestPage) m_bestPage->applyUiScale();
    if (m_calendarPage) m_calendarPage->applyUiScale();
    if (m_memorialPage) m_memorialPage->applyUiScale();
}

void TodoPage::refreshModulePages()
{
    if (m_timerPage) m_timerPage->refresh();
    if (m_overviewPage) m_overviewPage->refresh();
    if (m_detailPage) m_detailPage->refresh();
    if (m_weekPage) m_weekPage->refresh();
    if (m_heatmapPage) m_heatmapPage->refresh();
    if (m_bestPage) m_bestPage->refresh();
    if (m_calendarPage) m_calendarPage->refresh();
    if (m_memorialPage) m_memorialPage->refresh();
}

void TodoPage::selectView(ViewKind kind, qint64 listId)
{
    if (m_view == kind && m_viewList == listId && !m_moduleActive) {
        setViewButtonsChecked();
        return;
    }
    m_view = kind;
    m_viewList = listId;
    m_moduleActive = false;
    m_showCompleted = false;
    m_animateNext = true; // 视图切换：给本次重建的任务行加入场淡入
    if (m_mainStack && m_mainStack->count() > 0)
        m_mainStack->setCurrentIndex(0);
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

bool TodoPage::taskLessThan(const TodoTask &a, const TodoTask &b)
{
    if (a.priority != b.priority)
        return a.priority > b.priority;
    const bool ad = a.hasDue(), bd = b.hasDue();
    if (ad != bd)
        return ad;
    if (ad && bd) {
        const QDate da = QDate::fromString(a.dueDate, Qt::ISODate);
        const QDate db = QDate::fromString(b.dueDate, Qt::ISODate);
        if (da.isValid() && db.isValid() && da != db)
            return da < db;
    }
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
    std::sort(open.begin(), open.end(), TodoPage::taskLessThan);
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

    rebuildSidebar();
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

    m_detailEmpty->hide();
    m_detailBody->show();

    m_dTitle->setText(t->title);
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
    t.title = m_dTitle->text().trimmed();
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
