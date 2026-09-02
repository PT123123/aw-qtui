// focuswidgets.cpp —— 专注模块 UI 实现（计时 / 专注记录 / 详情 / 纪念日 / 设置）
#include "focuswidgets.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDateEdit>
#include <QDialogButtonBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QListWidgetItem>
#include <QMessageBox>
#include <QPushButton>
#include <QScrollArea>
#include <QSpinBox>
#include <QTimeEdit>
#include <QTimer>
#include <QToolButton>
#include <QVBoxLayout>

#include <algorithm>

#include "focusstore.h"
#include "theme.h"
#include "todomodels.h"
#include "todostore.h"

namespace awqtui {

// ==================================================================== //
// 计时页
// ==================================================================== //
FocusTimerPage::FocusTimerPage(FocusSource *focus, TodoSource *todo, QWidget *parent)
    : QWidget(parent), m_focus(focus), m_todo(todo)
{
    m_tick = new QTimer(this);
    m_tick->setInterval(250);
    connect(m_tick, &QTimer::timeout, this, &FocusTimerPage::onTick);
    buildUi();
    setMode(FocusPomodoro);
    refresh();
    connect(m_focus, &FocusSource::dataChanged, this, [this] { refresh(); });
}

void FocusTimerPage::buildUi()
{
    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(si(20), si(16), si(20), si(16));
    root->setSpacing(si(12));

    auto *title = new QLabel(QStringLiteral("计时"));
    title->setStyleSheet(QStringLiteral("font-size: %1; font-weight: 700; color: %2;")
                             .arg(sp(16), QString::fromLatin1(kColorFg)));
    root->addWidget(title);

    // 模式切换：番茄计时 / 正计时
    auto *modeRow = new QHBoxLayout;
    modeRow->setSpacing(si(8));
    const auto mkMode = [this, modeRow](const char *txt) {
        auto *b = new QToolButton;
        b->setText(QString::fromLatin1(txt));
        b->setCheckable(true);
        b->setCursor(Qt::PointingHandCursor);
        b->setObjectName(QStringLiteral("FocusModeBtn"));
        m_modeBtns.append(b);
        modeRow->addWidget(b);
        return b;
    };
    QToolButton *pomo = mkMode("🍅 番茄计时");
    QToolButton *sw = mkMode("⏱ 正计时");
    pomo->setChecked(true);
    connect(pomo, &QToolButton::clicked, this, [this] { onMode(FocusPomodoro); });
    connect(sw, &QToolButton::clicked, this, [this] { onMode(FocusStopwatch); });
    root->addLayout(modeRow);

    // 番茄预设
    auto *presetBox = new QWidget;
    m_presetRow = presetBox;
    auto *presetRow = new QHBoxLayout(presetBox);
    presetRow->setContentsMargins(0, 0, 0, 0);
    presetRow->setSpacing(si(8));
    auto *presetLabel = new QLabel(QStringLiteral("番茄时长"));
    presetLabel->setStyleSheet(QStringLiteral("color: %1; font-size: %2;")
                                   .arg(QString::fromLatin1(kColorFgMuted), sp(12)));
    presetRow->addWidget(presetLabel);
    const int presets[] = {15, 20, 30, 60};
    for (const int min : presets) {
        auto *b = new QToolButton;
        b->setText(QStringLiteral("%1 分钟").arg(min));
        b->setCheckable(true);
        b->setCursor(Qt::PointingHandCursor);
        b->setObjectName(QStringLiteral("FocusPresetBtn"));
        b->setProperty("minutes", min);
        if (min == 25)
            b->setChecked(false);
        m_presetBtns.append(b);
        presetRow->addWidget(b);
        connect(b, &QToolButton::clicked, this, &FocusTimerPage::onPreset);
    }
    m_customMin = new QSpinBox;
    m_customMin->setRange(1, 240);
    m_customMin->setValue(25);
    m_customMin->setSuffix(QStringLiteral(" 分钟"));
    m_customMin->setFixedWidth(si(110));
    m_customMin->setToolTip(QStringLiteral("自定义番茄时长"));
    presetRow->addWidget(m_customMin);
    presetRow->addStretch(1);
    root->addWidget(presetBox);

    // 大表盘
    m_timeLabel = new QLabel(QStringLiteral("25:00"));
    m_timeLabel->setAlignment(Qt::AlignCenter);
    m_timeLabel->setStyleSheet(QStringLiteral(
        "font-size: %1; font-weight: 700; color: %2; letter-spacing: 2px;")
        .arg(sp(56), QString::fromLatin1(kColorAccent)));
    root->addWidget(m_timeLabel);

    m_hintLabel = new QLabel;
    m_hintLabel->setAlignment(Qt::AlignCenter);
    m_hintLabel->setStyleSheet(QStringLiteral("color: %1; font-size: %2;")
                                   .arg(QString::fromLatin1(kColorFgMuted), sp(12)));
    root->addWidget(m_hintLabel);

    // 事件 / 任务
    auto *evRow = new QHBoxLayout;
    evRow->setSpacing(si(8));
    m_eventEdit = new QLineEdit;
    m_eventEdit->setPlaceholderText(QStringLiteral("正在专注什么？"));
    m_eventEdit->setClearButtonEnabled(true);
    evRow->addWidget(m_eventEdit, 1);
    m_taskCombo = new QComboBox;
    m_taskCombo->setMinimumWidth(si(180));
    m_taskCombo->setToolTip(QStringLiteral("关联任务（可选）"));
    evRow->addWidget(m_taskCombo);
    root->addLayout(evRow);

    // 控制按钮
    auto *btnRow = new QHBoxLayout;
    btnRow->setSpacing(si(10));
    btnRow->addStretch(1);
    m_secondaryBtn = new QPushButton;
    m_secondaryBtn->setObjectName(QStringLiteral("DangerBtn"));
    m_secondaryBtn->setCursor(Qt::PointingHandCursor);
    btnRow->addWidget(m_secondaryBtn);
    m_startBtn = new QPushButton;
    m_startBtn->setObjectName(QStringLiteral("PrimaryBtn"));
    m_startBtn->setCursor(Qt::PointingHandCursor);
    m_startBtn->setMinimumWidth(si(120));
    btnRow->addWidget(m_startBtn);
    btnRow->addStretch(1);
    root->addLayout(btnRow);

    root->addStretch(1);

    connect(m_startBtn, &QPushButton::clicked, this, &FocusTimerPage::onStartPause);
    connect(m_secondaryBtn, &QPushButton::clicked, this, &FocusTimerPage::onSecondary);
    connect(m_taskCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &FocusTimerPage::onTaskChanged);
    connect(m_customMin, QOverload<int>::of(&QSpinBox::valueChanged), this, [this](int v) {
        for (auto *b : m_presetBtns)
            b->setChecked(false);
        m_minutes = v;
        if (m_kind == FocusPomodoro && !m_running && !m_paused)
            m_timeLabel->setText(timeText());
    });

    applyStyle();
}

void FocusTimerPage::applyStyle()
{
    setStyleSheet(QStringLiteral(R"(
        QToolButton#FocusModeBtn {
            border: 1px solid %1; border-radius: 8px; padding: 7px 16px;
            color: %2; font-size: 13px; background: %3;
        }
        QToolButton#FocusModeBtn:hover { background: %4; }
        QToolButton#FocusModeBtn:checked {
            background: %5; color: white; border-color: %6; font-weight: 600;
        }
        QToolButton#FocusPresetBtn {
            border: 1px solid %1; border-radius: 8px; padding: 6px 12px;
            color: %2; font-size: 12px; background: %3;
        }
        QToolButton#FocusPresetBtn:hover { background: %4; }
        QToolButton#FocusPresetBtn:checked {
            background: %5; color: white; border-color: %6; font-weight: 600;
        }
    )")
        .arg(QString::fromLatin1(kColorBorder), QString::fromLatin1(kColorFg),
             QString::fromLatin1(kColorBgElev), kColorHover,
             QString::fromLatin1(kColorAccent), QString::fromLatin1(kColorAccent)));
}

void FocusTimerPage::applyUiScale()
{
    applyStyle();
}

// 需要保存的预设行成员：在头文件里没有声明，这里用成员变量 m_presetRow
// 已在 buildUi 里赋值为局部变量，保持向下兼容
void FocusTimerPage::refresh()
{
    // 重建任务下拉（不丢当前选择）
    if (!m_todo || !m_taskCombo)
        return;
    const qint64 cur = m_taskId;
    const QString curText = m_taskCombo->currentText();
    m_taskCombo->blockSignals(true);
    m_taskCombo->clear();
    m_taskCombo->addItem(QStringLiteral("（不关联任务）"), QVariant::fromValue<qint64>(0));
    const auto tasks = m_todo->tasks();
    for (const auto &t : tasks) {
        if (t.completed)
            continue;
        m_taskCombo->addItem(t.title, QVariant::fromValue<qint64>(t.id));
    }
    int idx = m_taskCombo->findData(QVariant::fromValue<qint64>(cur));
    if (idx < 0)
        idx = m_taskCombo->findText(curText);
    if (idx < 0)
        idx = 0;
    m_taskCombo->setCurrentIndex(idx);
    m_taskCombo->blockSignals(false);
    onTaskChanged(idx);
}

void FocusTimerPage::onTaskChanged(int idx)
{
    const qint64 id = m_taskCombo->itemData(idx).toLongLong();
    m_taskId = id;
    if (id > 0 && idx >= 0) {
        const auto tasks = m_todo ? m_todo->tasks() : QList<TodoTask>{};
        for (const auto &t : tasks) {
            if (t.id == id) {
                m_eventEdit->setText(t.title);
                break;
            }
        }
    }
}

void FocusTimerPage::setMode(int kind)
{
    m_kind = kind;
    for (int i = 0; i < m_modeBtns.size(); ++i)
        m_modeBtns[i]->setChecked(i == (kind == FocusStopwatch ? 1 : 0));
    const bool pomo = (kind == FocusPomodoro);
    setPresetRowVisible(pomo);
    m_secondaryBtn->setText(pomo ? QStringLiteral("重置") : QStringLiteral("完成并记录"));
    toIdle();
}

void FocusTimerPage::setPresetRowVisible(bool visible)
{
    if (m_presetRow)
        m_presetRow->setVisible(visible);
}

void FocusTimerPage::onMode(int kind)
{
    if (m_running || m_paused) {
        // 切换模式前提示未保存的计时
        const auto ans = QMessageBox::question(
            this, QStringLiteral("切换计时模式"),
            QStringLiteral("当前计时尚未记录，确定切换并丢弃吗？"),
            QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
        if (ans != QMessageBox::Yes)
            return;
    }
    setMode(kind);
}

void FocusTimerPage::onPreset()
{
    auto *b = qobject_cast<QToolButton *>(sender());
    if (!b)
        return;
    for (auto *o : m_presetBtns)
        o->setChecked(o == b);
    m_minutes = b->property("minutes").toInt();
    m_customMin->blockSignals(true);
    m_customMin->setValue(m_minutes);
    m_customMin->blockSignals(false);
    if (m_kind == FocusPomodoro && !m_running && !m_paused)
        m_timeLabel->setText(timeText());
}

void FocusTimerPage::toIdle()
{
    m_running = false;
    m_paused = false;
    m_accumMs = 0;
    m_segStartMs = 0;
    m_sessionStartMs = 0;
    m_tick->stop();
    m_startBtn->setText(QStringLiteral("开始"));
    m_secondaryBtn->setEnabled(false);
    m_timeLabel->setText(timeText());
    updateHint();
}

void FocusTimerPage::onStartPause()
{
    if (!m_running) {
        // 开始 / 继续
        const qint64 now = QDateTime::currentMSecsSinceEpoch();
        if (!m_paused)
            m_sessionStartMs = now;
        m_segStartMs = now;
        m_running = true;
        m_paused = false;
        m_tick->start();
        m_startBtn->setText(QStringLiteral("暂停"));
        m_secondaryBtn->setEnabled(m_kind == FocusStopwatch);
    } else if (!m_paused) {
        // 暂停
        m_accumMs += QDateTime::currentMSecsSinceEpoch() - m_segStartMs;
        m_paused = true;
        m_tick->stop();
        m_startBtn->setText(QStringLiteral("继续"));
        m_secondaryBtn->setEnabled(true);
    } else {
        // 继续
        m_segStartMs = QDateTime::currentMSecsSinceEpoch();
        m_paused = false;
        m_tick->start();
        m_startBtn->setText(QStringLiteral("暂停"));
    }
    updateHint();
}

void FocusTimerPage::onSecondary()
{
    if (m_kind == FocusPomodoro) {
        toIdle();
        return;
    }
    // 正计时：完成并记录
    const qint64 el = elapsedMs();
    if (el >= 1000)
        record(el / 1000);
    toIdle();
}

void FocusTimerPage::onTick()
{
    if (m_kind == FocusPomodoro) {
        const qint64 remaining = qint64(m_minutes) * 60000 - elapsedMs();
        m_timeLabel->setText(timeText());
        if (remaining <= 0) {
            record(qint64(m_minutes) * 60);
            toIdle();
            m_hintLabel->setText(QStringLiteral("🍅 番茄完成！已记录一条专注"));
        }
    } else {
        m_timeLabel->setText(timeText());
    }
}

qint64 FocusTimerPage::elapsedMs() const
{
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    return m_accumMs + ((m_running && !m_paused) ? (now - m_segStartMs) : 0);
}

QString FocusTimerPage::timeText() const
{
    if (m_kind == FocusPomodoro) {
        const qint64 rem = qMax<qint64>(0, qint64(m_minutes) * 60000 - elapsedMs());
        const qint64 tot = qint64(m_minutes) * 60;
        const qint64 m = rem / 60000, s = (rem / 1000) % 60;
        Q_UNUSED(tot);
        return QStringLiteral("%1:%2").arg(m, 2, 10, QLatin1Char('0'))
            .arg(s, 2, 10, QLatin1Char('0'));
    }
    const qint64 el = elapsedMs();
    const qint64 h = el / 3600000, m = (el / 60000) % 60, s = (el / 1000) % 60;
    if (h > 0)
        return QStringLiteral("%1:%2:%3").arg(h).arg(m, 2, 10, QLatin1Char('0'))
            .arg(s, 2, 10, QLatin1Char('0'));
    return QStringLiteral("%1:%2").arg(m, 2, 10, QLatin1Char('0')).arg(s, 2, 10, QLatin1Char('0'));
}

void FocusTimerPage::updateHint()
{
    QString base;
    if (m_kind == FocusPomodoro)
        base = QStringLiteral("番茄 %1 分钟 · 倒计时").arg(m_minutes);
    else
        base = QStringLiteral("正计时 · 从 0 起数");
    QString st;
    if (m_running && !m_paused)
        st = QStringLiteral("· 进行中");
    else if (m_paused)
        st = QStringLiteral("· 已暂停");
    else
        st = QStringLiteral("· 未开始");
    m_hintLabel->setText(base + st);
}

void FocusTimerPage::record(qint64 durationSec)
{
    if (!m_focus || durationSec <= 0)
        return;
    const qint64 end = QDateTime::currentMSecsSinceEpoch();
    const qint64 start = m_sessionStartMs > 0 ? m_sessionStartMs : end - durationSec * 1000;
    m_focus->addSession(m_kind, start, end, durationSec, m_eventEdit->text().trimmed(), m_taskId);
}

// ==================================================================== //
// 专注记录（概览）
// ==================================================================== //
FocusOverviewPage::FocusOverviewPage(FocusSource *focus, QWidget *parent)
    : QWidget(parent), m_focus(focus)
{
    buildUi();
    connect(m_focus, &FocusSource::dataChanged, this, &FocusOverviewPage::refresh);
    refresh();
}

void FocusOverviewPage::buildUi()
{
    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(si(20), si(16), si(20), si(16));
    root->setSpacing(si(12));

    auto *title = new QLabel(QStringLiteral("专注记录"));
    title->setStyleSheet(QStringLiteral("font-size: %1; font-weight: 700; color: %2;")
                             .arg(sp(16), QString::fromLatin1(kColorFg)));
    root->addWidget(title);

    auto *cards = new QHBoxLayout;
    cards->setSpacing(si(10));
    const auto mkCard = [this, cards](QLabel **out) {
        auto *box = new QWidget;
        box->setObjectName(QStringLiteral("StatCard"));
        auto *l = new QVBoxLayout(box);
        l->setContentsMargins(si(14), si(12), si(14), si(12));
        l->setSpacing(si(4));
        auto *v = new QLabel(QStringLiteral("0"));
        v->setAlignment(Qt::AlignCenter);
        v->setStyleSheet(QStringLiteral("font-size: %1; font-weight: 700; color: %2;")
                             .arg(sp(24), QString::fromLatin1(kColorAccent)));
        l->addWidget(v);
        l->addStretch(1);
        *out = v;
        cards->addWidget(box, 1);
        return box;
    };
    auto *c1 = mkCard(&m_todayPomo);
    auto *c2 = mkCard(&m_todayTime);
    auto *c3 = mkCard(&m_totalPomo);
    auto *c4 = mkCard(&m_totalTime);
    // 卡片标题
    const QStringList labels = {
        QStringLiteral("今日番茄"), QStringLiteral("今日专注"),
        QStringLiteral("总番茄"), QStringLiteral("总专注时长")
    };
    const QList<QWidget *> boxes = {c1, c2, c3, c4};
    for (int i = 0; i < boxes.size(); ++i) {
        auto *lab = new QLabel(labels[i]);
        lab->setAlignment(Qt::AlignCenter);
        lab->setStyleSheet(QStringLiteral("color: %1; font-size: %2;")
                               .arg(QString::fromLatin1(kColorFgMuted), sp(12)));
        auto *bl = qobject_cast<QVBoxLayout *>(boxes[i]->layout());
        bl->addWidget(lab);
    }
    root->addLayout(cards);
    root->addStretch(1);
    applyStyle();
}

void FocusOverviewPage::applyStyle()
{
    setStyleSheet(QStringLiteral("QWidget#StatCard { background: %1; border: 1px solid %2; "
                                 "border-radius: 12px; }")
                      .arg(glassBg(kColorBgElev), glassBorder()));
}

void FocusOverviewPage::applyUiScale()
{
    setStyleSheet(QString());
    applyStyle();
}

void FocusOverviewPage::refresh()
{
    if (!m_focus)
        return;
    const auto sessions = m_focus->sessions();
    const QDate today = QDate::currentDate();
    int todayPomo = 0, totalPomo = 0;
    qint64 totalSec = 0;
    for (const auto &s : sessions) {
        totalSec += s.durationSec;
        if (s.kind == FocusPomodoro) {
            ++totalPomo;
            if (sessionDate(s) == today)
                ++todayPomo;
        }
    }
    m_todayPomo->setText(QString::number(todayPomo));
    m_todayTime->setText(fmtDuration(dayFocusSec(sessions, today)));
    m_totalPomo->setText(QString::number(totalPomo));
    m_totalTime->setText(fmtDuration(totalSec));
}

// ==================================================================== //
// 专注记录详情
// ==================================================================== //
FocusDetailPage::FocusDetailPage(FocusSource *focus, QWidget *parent)
    : QWidget(parent), m_focus(focus)
{
    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(si(20), si(16), si(20), si(16));
    root->setSpacing(si(10));

    auto *head = new QHBoxLayout;
    auto *title = new QLabel(QStringLiteral("专注记录详情"));
    title->setStyleSheet(QStringLiteral("font-size: %1; font-weight: 700; color: %2;")
                             .arg(sp(16), QString::fromLatin1(kColorFg)));
    head->addWidget(title);
    m_countLabel = new QLabel;
    m_countLabel->setStyleSheet(QStringLiteral("color: %1; font-size: %2;")
                                    .arg(QString::fromLatin1(kColorFgMuted), sp(12)));
    head->addWidget(m_countLabel);
    head->addStretch(1);
    m_addBtn = new QPushButton(QStringLiteral("＋ 补记"));
    m_addBtn->setCursor(Qt::PointingHandCursor);
    head->addWidget(m_addBtn);
    root->addLayout(head);

    m_list = new QListWidget;
    m_list->setSelectionMode(QAbstractItemView::NoSelection);
    m_list->setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
    root->addWidget(m_list, 1);

    connect(m_addBtn, &QPushButton::clicked, this, &FocusDetailPage::onAddManual);
    connect(m_focus, &FocusSource::dataChanged, this, &FocusDetailPage::refresh);
    refresh();
}

void FocusDetailPage::applyUiScale()
{
    setStyleSheet(QString());
    setStyleSheet(QStringLiteral("QListWidget::item { padding: 8px 10px; }"));
}

void FocusDetailPage::refresh()
{
    if (!m_focus)
        return;
    m_list->clear();
    auto sessions = m_focus->sessions();
    std::sort(sessions.begin(), sessions.end(),
              [](const FocusSession &a, const FocusSession &b) { return a.startMs > b.startMs; });
    m_countLabel->setText(QStringLiteral("共 %1 条").arg(sessions.size()));
    for (const auto &s : sessions) {
        const QDate d = sessionDate(s);
        const QString range = QStringLiteral("%1 - %2")
                                  .arg(fmtClock(s.startMs), fmtClock(s.endMs));
        const QString kindName = s.kind == FocusStopwatch
                                     ? QStringLiteral("正计时") : QStringLiteral("番茄");
        const QString title = QStringLiteral("%1 · %2 · %3")
                                  .arg(d.toString(QStringLiteral("yyyy-MM-dd")), range, fmtDuration(s.durationSec));
        auto *row = new QWidget;
        auto *hl = new QHBoxLayout(row);
        hl->setContentsMargins(si(4), si(2), si(4), si(2));
        hl->setSpacing(si(8));
        auto *tag = new QLabel(kindName);
        tag->setStyleSheet(QStringLiteral("background: %1; color: %2; border-radius: 4px; "
                                          "padding: 2px 8px; font-size: %3;")
                               .arg(QString::fromLatin1(kColorTagBg),
                                    QString::fromLatin1(kColorTagFg), sp(11)));
        tag->setAlignment(Qt::AlignCenter);
        hl->addWidget(tag);
        auto *main = new QLabel;
        main->setText(QStringLiteral("<b>%1</b><br/><span style='color:%2;'>%3</span>")
                          .arg(title, QString::fromLatin1(kColorFgMuted),
                               s.eventName.isEmpty() ? QStringLiteral("（未命名）")
                                                     : s.eventName.toHtmlEscaped()));
        main->setStyleSheet(QStringLiteral("font-size: %1; color: %2;")
                                .arg(sp(12.5), QString::fromLatin1(kColorFg)));
        hl->addWidget(main, 1);
        auto *del = new QToolButton;
        del->setText(QStringLiteral("✕"));
        del->setCursor(Qt::PointingHandCursor);
        del->setStyleSheet(QStringLiteral("color: %1; border: none; font-size: %2;")
                               .arg(QString::fromLatin1(kColorDanger), sp(14)));
        del->setToolTip(QStringLiteral("删除该条记录"));
        hl->addWidget(del);
        connect(del, &QToolButton::clicked, this, [this, id = s.id] {
            m_focus->deleteSession(id);
        });
        auto *item = new QListWidgetItem;
        item->setSizeHint(row->sizeHint());
        m_list->addItem(item);
        m_list->setItemWidget(item, row);
    }
}

void FocusDetailPage::onAddManual()
{
    if (!m_focus)
        return;
    auto *dlg = new QDialog(this);
    dlg->setWindowTitle(QStringLiteral("补记专注记录"));
    auto *lay = new QVBoxLayout(dlg);
    auto *dateEdit = new QDateEdit(QDate::currentDate());
    dateEdit->setCalendarPopup(true);
    auto *startEdit = new QTimeEdit(QTime(9, 0));
    auto *endEdit = new QTimeEdit(QTime(9, 25));
    auto *evEdit = new QLineEdit;
    evEdit->setPlaceholderText(QStringLiteral("事件 / 任务名（可选）"));
    auto *box = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    box->button(QDialogButtonBox::Ok)->setText(QStringLiteral("保存"));
    box->button(QDialogButtonBox::Cancel)->setText(QStringLiteral("取消"));
    auto addRow = [lay](const QString &label, QWidget *w) {
        auto *r = new QHBoxLayout;
        auto *l = new QLabel(label);
        l->setMinimumWidth(si(70));
        r->addWidget(l);
        r->addWidget(w, 1);
        lay->addLayout(r);
    };
    addRow(QStringLiteral("日期"), dateEdit);
    addRow(QStringLiteral("开始"), startEdit);
    addRow(QStringLiteral("结束"), endEdit);
    addRow(QStringLiteral("事件"), evEdit);
    lay->addWidget(box);
    connect(box, &QDialogButtonBox::accepted, dlg, &QDialog::accept);
    connect(box, &QDialogButtonBox::rejected, dlg, &QDialog::reject);
    if (dlg->exec() != QDialog::Accepted)
        return;
    const QDateTime st(dateEdit->date(), startEdit->time());
    const QDateTime en(dateEdit->date(), endEdit->time());
    if (en <= st)
        return;
    m_focus->addSession(FocusStopwatch, st.toMSecsSinceEpoch(), en.toMSecsSinceEpoch(),
                        st.secsTo(en), evEdit->text().trimmed(), 0);
}

// ==================================================================== //
// 倒数纪念日
// ==================================================================== //
FocusMemorialPage::FocusMemorialPage(FocusSource *focus, QWidget *parent)
    : QWidget(parent), m_focus(focus)
{
    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(si(20), si(16), si(20), si(16));
    root->setSpacing(si(10));

    auto *head = new QHBoxLayout;
    auto *title = new QLabel(QStringLiteral("倒数纪念日"));
    title->setStyleSheet(QStringLiteral("font-size: %1; font-weight: 700; color: %2;")
                             .arg(sp(16), QString::fromLatin1(kColorFg)));
    head->addWidget(title);
    m_countLabel = new QLabel;
    m_countLabel->setStyleSheet(QStringLiteral("color: %1; font-size: %2;")
                                    .arg(QString::fromLatin1(kColorFgMuted), sp(12)));
    head->addWidget(m_countLabel);
    head->addStretch(1);
    m_addBtn = new QPushButton(QStringLiteral("＋ 新增"));
    m_addBtn->setCursor(Qt::PointingHandCursor);
    head->addWidget(m_addBtn);
    root->addLayout(head);

    m_list = new QListWidget;
    m_list->setSelectionMode(QAbstractItemView::NoSelection);
    root->addWidget(m_list, 1);

    connect(m_addBtn, &QPushButton::clicked, this, &FocusMemorialPage::onAdd);
    connect(m_focus, &FocusSource::dataChanged, this, &FocusMemorialPage::refresh);
    refresh();
}

void FocusMemorialPage::applyUiScale()
{
    setStyleSheet(QString());
}

void FocusMemorialPage::refresh()
{
    if (!m_focus)
        return;
    m_list->clear();
    auto mems = m_focus->memorials();
    const QDate today = QDate::currentDate();
    // 按剩余天数升序
    std::sort(mems.begin(), mems.end(), [today](const MemorialDay &a, const MemorialDay &b) {
        return QDate::fromString(a.dateIso, Qt::ISODate).toJulianDay()
               < QDate::fromString(b.dateIso, Qt::ISODate).toJulianDay();
    });
    m_countLabel->setText(QStringLiteral("%1 个纪念日").arg(mems.size()));
    for (const auto &m : mems) {
        const QDate d = QDate::fromString(m.dateIso, Qt::ISODate);
        const qint64 days = d.isValid() ? today.daysTo(d) : 0;
        QString remain;
        if (!d.isValid())
            remain = QStringLiteral("日期无效");
        else if (days == 0)
            remain = QStringLiteral("就是今天 🎉");
        else if (days > 0)
            remain = QStringLiteral("还剩 %1 天").arg(days);
        else
            remain = QStringLiteral("已过 %1 天").arg(-days);

        auto *row = new QWidget;
        auto *hl = new QHBoxLayout(row);
        hl->setContentsMargins(si(4), si(4), si(4), si(4));
        hl->setSpacing(si(10));
        auto *emo = new QLabel(m.emoji.isEmpty() ? QStringLiteral("🎉") : m.emoji);
        emo->setStyleSheet(QStringLiteral("font-size: %1;").arg(sp(22)));
        hl->addWidget(emo);
        auto *main = new QLabel;
        const QString dateStr = d.isValid() ? d.toString(QStringLiteral("yyyy-MM-dd"))
                                            : m.dateIso;
        main->setText(QStringLiteral("<b>%1</b><br/><span style='color:%2;'>%3 · %4</span>")
                          .arg(m.name.toHtmlEscaped(), QString::fromLatin1(kColorFgMuted),
                               remain, dateStr));
        main->setStyleSheet(QStringLiteral("font-size: %1; color: %2;")
                                .arg(sp(13), QString::fromLatin1(kColorFg)));
        hl->addWidget(main, 1);
        auto *del = new QToolButton;
        del->setText(QStringLiteral("✕"));
        del->setCursor(Qt::PointingHandCursor);
        del->setStyleSheet(QStringLiteral("color: %1; border: none; font-size: %2;")
                               .arg(QString::fromLatin1(kColorDanger), sp(14)));
        del->setToolTip(QStringLiteral("删除"));
        hl->addWidget(del);
        connect(del, &QToolButton::clicked, this, [this, id = m.id] {
            m_focus->deleteMemorial(id);
        });
        auto *item = new QListWidgetItem;
        item->setSizeHint(row->sizeHint());
        m_list->addItem(item);
        m_list->setItemWidget(item, row);
    }
}

void FocusMemorialPage::onAdd()
{
    if (!m_focus)
        return;
    auto *dlg = new QDialog(this);
    dlg->setWindowTitle(QStringLiteral("新增纪念日"));
    auto *lay = new QVBoxLayout(dlg);

    auto *emojiEdit = new QLineEdit(QStringLiteral("🎉"));
    emojiEdit->setMaxLength(8);
    emojiEdit->setToolTip(QStringLiteral("展示图标（可选，可输入任意 emoji / 文字）"));
    const QStringList quick = {QStringLiteral("🎉"), QStringLiteral("🎂"), QStringLiteral("🎁"),
                               QStringLiteral("🧧"), QStringLiteral("💍"), QStringLiteral("🎓"),
                               QStringLiteral("🏠"), QStringLiteral("✈️"), QStringLiteral("💰"),
                               QStringLiteral("❤️")};
    auto *emojiRow = new QHBoxLayout;
    for (const auto &e : quick) {
        auto *b = new QToolButton;
        b->setText(e);
        b->setCursor(Qt::PointingHandCursor);
        b->setStyleSheet(QStringLiteral("font-size: %1; padding: 4px; border: none;")
                             .arg(sp(16)));
        connect(b, &QToolButton::clicked, this, [emojiEdit, e] { emojiEdit->setText(e); });
        emojiRow->addWidget(b);
    }
    emojiRow->addStretch(1);

    auto *nameEdit = new QLineEdit;
    nameEdit->setPlaceholderText(QStringLiteral("纪念日名称，如「老妈生日」"));
    auto *dateEdit = new QDateEdit(QDate::currentDate().addMonths(1));
    dateEdit->setCalendarPopup(true);

    auto *box = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    box->button(QDialogButtonBox::Ok)->setText(QStringLiteral("保存"));
    box->button(QDialogButtonBox::Cancel)->setText(QStringLiteral("取消"));
    auto addRow = [lay](const QString &label, QWidget *w) {
        auto *r = new QHBoxLayout;
        auto *l = new QLabel(label);
        l->setMinimumWidth(si(70));
        r->addWidget(l);
        r->addWidget(w, 1);
        lay->addLayout(r);
    };
    lay->addWidget(emojiEdit);
    lay->addLayout(emojiRow);
    addRow(QStringLiteral("名称"), nameEdit);
    addRow(QStringLiteral("日期"), dateEdit);
    lay->addWidget(box);
    connect(box, &QDialogButtonBox::accepted, dlg, &QDialog::accept);
    connect(box, &QDialogButtonBox::rejected, dlg, &QDialog::reject);
    if (dlg->exec() != QDialog::Accepted)
        return;
    if (nameEdit->text().trimmed().isEmpty())
        return;
    m_focus->addMemorial(nameEdit->text().trimmed(), emojiEdit->text().trimmed(),
                         dateEdit->date().toString(Qt::ISODate));
}

// ==================================================================== //
// 功能模块设置
// ==================================================================== //
FocusSettingsDialog::FocusSettingsDialog(QWidget *parent) : QDialog(parent)
{
    setWindowTitle(QStringLiteral("专注模块 · 侧边栏显示开关"));
    setModal(true);
    auto *root = new QVBoxLayout(this);
    root->setSpacing(si(10));

    auto *tip = new QLabel(QStringLiteral("控制以下功能模块是否显示在 Todo 侧边栏（关闭仅隐藏，不删除数据）"));
    tip->setWordWrap(true);
    tip->setStyleSheet(QStringLiteral("color: %1; font-size: %2;")
                           .arg(QString::fromLatin1(kColorFgMuted), sp(12)));
    root->addWidget(tip);

    const FocusModules cur = loadFocusModules();
    const auto mk = [this, root](const QString &text, bool on) {
        auto *c = new QCheckBox(text);
        c->setChecked(on);
        root->addWidget(c);
        return c;
    };
    m_timer = mk(QStringLiteral("🍅 计时（番茄 / 正计时）"), cur.timer);
    m_overview = mk(QStringLiteral("📊 专注记录（今日番茄 / 总番茄 / 总专注时长）"), cur.overview);
    m_detail = mk(QStringLiteral("🕓 专注记录详情（时间线）"), cur.detail);
    m_week = mk(QStringLiteral("📈 专注时间线（周热力格）"), cur.week);
    m_heatmap = mk(QStringLiteral("🔥 热力图（月度 + 年度）"), cur.heatmap);
    m_best = mk(QStringLiteral("⏰ 最佳专注时间（24h 柱状）"), cur.best);
    m_calendar = mk(QStringLiteral("📅 日历"), cur.calendar);
    m_memorial = mk(QStringLiteral("🎂 倒数纪念日"), cur.memorial);

    auto *box = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    box->button(QDialogButtonBox::Ok)->setText(QStringLiteral("保存"));
    box->button(QDialogButtonBox::Cancel)->setText(QStringLiteral("取消"));
    root->addWidget(box);
    connect(box, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(box, &QDialogButtonBox::rejected, this, &QDialog::reject);
}

FocusModules FocusSettingsDialog::modules() const
{
    FocusModules m;
    m.timer = m_timer->isChecked();
    m.overview = m_overview->isChecked();
    m.detail = m_detail->isChecked();
    m.week = m_week->isChecked();
    m.heatmap = m_heatmap->isChecked();
    m.best = m_best->isChecked();
    m.calendar = m_calendar->isChecked();
    m.memorial = m_memorial->isChecked();
    return m;
}

} // namespace awqtui
