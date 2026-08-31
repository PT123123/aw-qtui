// timingdialog.cpp
#include "timingdialog.h"

#include <QDateTime>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QLabel>
#include <QMessageBox>
#include <QPushButton>
#include <QSpinBox>
#include <QTabWidget>
#include <QTimer>
#include <QVBoxLayout>

#include "charts.h" // formatDuration

namespace awqtui {

static QString formatMs(qint64 ms)
{
    return formatDuration(ms / 1000);
}

static QStringList askTags(TagStore *store, QWidget *parent, const QString &title,
                           const QStringList &def = {})
{
    // 从最近组合中选择或输入新标签
    QStringList choices = store ? store->allCombinations() : QStringList();
    if (choices.size() > 12)
        choices = choices.mid(0, 12);
    choices.prepend(QStringLiteral("(手动输入…)"));
    bool ok = false;
    const QString pick = QInputDialog::getItem(parent, title, QStringLiteral("标签组合："), choices,
                                               0, false, &ok);
    if (!ok)
        return {};
    if (pick == QStringLiteral("(手动输入…)")) {
        const QString text = QInputDialog::getText(parent, title, QStringLiteral("标签（逗号分隔）："));
        if (text.trimmed().isEmpty())
            return {};
        QStringList out;
        for (const auto &t : text.split(QLatin1Char(','), Qt::SkipEmptyParts)) {
            const QString s = t.trimmed();
            if (!s.isEmpty())
                out << s;
        }
        return out;
    }
    QStringList out;
    for (const auto &t : pick.split(QLatin1Char(','), Qt::SkipEmptyParts)) {
        const QString s = t.trimmed();
        if (!s.isEmpty())
            out << s;
    }
    return out;
}

TimingDialog::TimingDialog(TagStore *store, QWidget *parent)
    : QDialog(parent), m_store(store)
{
    setWindowTitle(QStringLiteral("计时工具"));
    resize(460, 360);

    auto *tabs = new QTabWidget(this);
    auto *root = new QVBoxLayout(this);
    root->addWidget(tabs);

    // ── 秒表 ──
    auto *sw = new QWidget;
    auto *swl = new QVBoxLayout(sw);
    m_swDisplay = new QLabel(QStringLiteral("00:00:00"));
    m_swDisplay->setAlignment(Qt::AlignCenter);
    m_swDisplay->setStyleSheet(QStringLiteral("font-size:42px;font-weight:700;color:#e6e6e6;"));
    swl->addWidget(m_swDisplay);
    auto *swBtns = new QHBoxLayout;
    m_swStart = new QPushButton(QStringLiteral("Start"));
    m_swStart->setObjectName(QStringLiteral("PrimaryBtn"));
    m_swPause = new QPushButton(QStringLiteral("Pause"));
    m_swPause->setEnabled(false);
    m_swStop = new QPushButton(QStringLiteral("Stop & 保存"));
    m_swStop->setObjectName(QStringLiteral("DangerBtn"));
    m_swStop->setEnabled(false);
    swBtns->addWidget(m_swStart);
    swBtns->addWidget(m_swPause);
    swBtns->addWidget(m_swStop);
    swl->addLayout(swBtns);
    auto *swHint = new QLabel(
        QStringLiteral("开始前先选标签；停止后生成一条时间段标签。暂停再继续会累计时长。"));
    swHint->setWordWrap(true);
    swHint->setStyleSheet(QStringLiteral("color:#9aa4b0;font-size:11px;"));
    swl->addWidget(swHint);
    tabs->addTab(sw, QStringLiteral("秒表"));

    // ── 计时器 ──
    auto *timer = new QWidget;
    auto *tl = new QVBoxLayout(timer);
    auto *trow = new QHBoxLayout;
    trow->addWidget(new QLabel(QStringLiteral("分钟")));
    m_timerMin = new QSpinBox;
    m_timerMin->setRange(1, 600);
    m_timerMin->setValue(25);
    m_timerDisplay = new QLabel(QStringLiteral("00:00"));
    m_timerDisplay->setStyleSheet(QStringLiteral("font-size:32px;font-weight:700;color:#e6e6e6;"));
    trow->addWidget(m_timerMin);
    trow->addWidget(m_timerDisplay);
    trow->addStretch(1);
    tl->addLayout(trow);
    m_timerStart = new QPushButton(QStringLiteral("Start"));
    m_timerStart->setObjectName(QStringLiteral("PrimaryBtn"));
    tl->addWidget(m_timerStart);
    tabs->addTab(timer, QStringLiteral("计时器"));

    // ── 番茄钟 ──
    auto *pomo = new QWidget;
    auto *pl = new QVBoxLayout(pomo);
    m_pomoDisplay = new QLabel(QStringLiteral("25:00"));
    m_pomoDisplay->setAlignment(Qt::AlignCenter);
    m_pomoDisplay->setStyleSheet(QStringLiteral("font-size:42px;font-weight:700;color:#e6e6e6;"));
    pl->addWidget(m_pomoDisplay);
    m_pomoStart = new QPushButton(QStringLiteral("开始 25 分钟工作"));
    m_pomoStart->setObjectName(QStringLiteral("PrimaryBtn"));
    pl->addWidget(m_pomoStart);
    auto *pomoHint = new QLabel(QStringLiteral("工作 25 分钟 → 选标签 → 休息 5 分钟（Break）。"));
    pomoHint->setWordWrap(true);
    pomoHint->setStyleSheet(QStringLiteral("color:#9aa4b0;font-size:11px;"));
    pl->addWidget(pomoHint);
    tabs->addTab(pomo, QStringLiteral("番茄钟"));

    // 定时器
    m_swTimer = new QTimer(this);
    m_swTimer->setInterval(1000);
    m_timerTick = new QTimer(this);
    m_timerTick->setInterval(1000);
    m_pomoTick = new QTimer(this);
    m_pomoTick->setInterval(1000);

    connect(m_swStart, &QPushButton::clicked, this, &TimingDialog::onSwStart);
    connect(m_swPause, &QPushButton::clicked, this, &TimingDialog::onSwPause);
    connect(m_swStop, &QPushButton::clicked, this, &TimingDialog::onSwStop);
    connect(m_swTimer, &QTimer::timeout, this, &TimingDialog::onSwTick);
    connect(m_timerStart, &QPushButton::clicked, this, &TimingDialog::onTimerStart);
    connect(m_timerTick, &QTimer::timeout, this, &TimingDialog::onTimerTick);
    connect(m_pomoStart, &QPushButton::clicked, this, &TimingDialog::onPomoStart);
    connect(m_pomoTick, &QTimer::timeout, this, &TimingDialog::onPomoTick);

    auto *close = new QPushButton(QStringLiteral("关闭"));
    root->addWidget(close, 0, Qt::AlignRight);
    connect(close, &QPushButton::clicked, this, &QDialog::accept);
}

TimingDialog::~TimingDialog() = default;

// ── 秒表 ────────────────────────────────────────────────────
void TimingDialog::onSwStart()
{
    const QStringList tags = askTags(m_store, this, QStringLiteral("开始秒表"));
    if (tags.isEmpty())
        return;
    m_swTags = tags;
    m_swBillable = m_store ? m_store->newTagsBillableByDefault() : false;
    m_swStartMs = QDateTime::currentMSecsSinceEpoch();
    m_swResumeMs = m_swStartMs;
    m_swAccum = 0;
    m_swRunning = true;
    m_swTimer->start();
    m_swStart->setEnabled(false);
    m_swPause->setEnabled(true);
    m_swStop->setEnabled(true);
    m_swPause->setText(QStringLiteral("Pause"));
    onSwTick();
}

void TimingDialog::onSwPause()
{
    if (m_swRunning) {
        m_swAccum += QDateTime::currentMSecsSinceEpoch() - m_swResumeMs;
        m_swRunning = false;
        m_swTimer->stop();
        m_swPause->setText(QStringLiteral("Resume"));
    } else {
        m_swResumeMs = QDateTime::currentMSecsSinceEpoch();
        m_swRunning = true;
        m_swTimer->start();
        m_swPause->setText(QStringLiteral("Pause"));
    }
}

void TimingDialog::onSwStop()
{
    if (!m_store || m_swTags.isEmpty())
        return;
    qint64 end = QDateTime::currentMSecsSinceEpoch();
    if (m_swRunning)
        m_swAccum += end - m_swResumeMs;
    const qint64 totalMs = m_swAccum;
    if (totalMs < 1000) {
        m_swRunning = false;
        m_swTimer->stop();
        m_swStart->setEnabled(true);
        m_swPause->setEnabled(false);
        m_swStop->setEnabled(false);
        m_swDisplay->setText(QStringLiteral("00:00:00"));
        return;
    }
    m_store->addSegment(m_swStartMs, m_swStartMs + totalMs, m_swTags, QString(), m_swBillable);
    m_swRunning = false;
    m_swTimer->stop();
    m_swStart->setEnabled(true);
    m_swPause->setEnabled(false);
    m_swStop->setEnabled(false);
    m_swDisplay->setText(QStringLiteral("00:00:00"));
    QMessageBox::information(this, QStringLiteral("秒表"),
                             QStringLiteral("已保存 %1（%2）").arg(m_swTags.join(QStringLiteral(", ")),
                                                                  formatMs(totalMs)));
    emit tagsChanged();
}

void TimingDialog::onSwTick()
{
    qint64 total = m_swAccum;
    if (m_swRunning)
        total += QDateTime::currentMSecsSinceEpoch() - m_swResumeMs;
    m_swDisplay->setText(formatMs(total));
}

// ── 计时器 ──────────────────────────────────────────────────
void TimingDialog::onTimerStart()
{
    const QStringList tags = askTags(m_store, this, QStringLiteral("开始计时器"));
    if (tags.isEmpty())
        return;
    m_timerTags = tags;
    m_timerEndMs = QDateTime::currentMSecsSinceEpoch() + qint64(m_timerMin->value()) * 60000;
    m_timerRunning = true;
    m_timerStart->setEnabled(false);
    m_timerTick->start();
    onTimerTick();
}

void TimingDialog::onTimerTick()
{
    if (!m_timerRunning)
        return;
    const qint64 remain = m_timerEndMs - QDateTime::currentMSecsSinceEpoch();
    if (remain <= 0) {
        m_timerRunning = false;
        m_timerTick->stop();
        m_timerStart->setEnabled(true);
        m_timerDisplay->setText(QStringLiteral("00:00"));
        if (m_store) {
            const qint64 start = m_timerEndMs - qint64(m_timerMin->value()) * 60000;
            m_store->addSegment(start, m_timerEndMs, m_timerTags, QString(), false);
            emit tagsChanged();
        }
        QMessageBox::information(this, QStringLiteral("计时器"),
                                 QStringLiteral("时间到！已保存标签。"));
        return;
    }
    m_timerDisplay->setText(QStringLiteral("%1:%2")
                                .arg(remain / 60000, 2, 10, QLatin1Char('0'))
                                .arg((remain % 60000) / 1000, 2, 10, QLatin1Char('0')));
}

// ── 番茄钟 ──────────────────────────────────────────────────
void TimingDialog::onPomoStart()
{
    const QStringList tags = askTags(m_store, this, QStringLiteral("开始工作段"));
    if (tags.isEmpty())
        return;
    m_pomoTags = tags;
    m_pomoWork = true;
    m_pomoEndMs = QDateTime::currentMSecsSinceEpoch() + 25 * 60000;
    m_pomoRunning = true;
    m_pomoStart->setEnabled(false);
    m_pomoTick->start();
    onPomoTick();
}

void TimingDialog::onPomoBreakConfirm()
{
    if (m_pomoWork) {
        // 工作结束：保存工作段标签
        if (m_store)
            m_store->addSegment(m_pomoEndMs - 25 * 60000, m_pomoEndMs, m_pomoTags, QString(),
                                false);
        QMessageBox box(this);
        box.setWindowTitle(QStringLiteral("番茄钟"));
        box.setText(QStringLiteral("工作完成！休息 5 分钟？"));
        auto *ok = box.addButton(QStringLiteral("Ok（休息并打 Break 标签）"), QMessageBox::AcceptRole);
        auto *skip = box.addButton(QStringLiteral("Skip break（再工作 25 分钟）"), QMessageBox::AcceptRole);
        auto *notag = box.addButton(QStringLiteral("Do not tag（休息不打标签）"), QMessageBox::AcceptRole);
        box.addButton(QMessageBox::Cancel);
        box.exec();
        QAbstractButton *clicked = box.clickedButton();
        if (clicked == skip || clicked == box.button(QMessageBox::Cancel)) {
            // 再工作（不保存 break）
            m_pomoWork = true;
            m_pomoEndMs = QDateTime::currentMSecsSinceEpoch() + 25 * 60000;
            m_pomoTick->start();
            onPomoTick();
            return;
        }
        if (clicked == ok && m_store)
            m_store->addSegment(m_pomoEndMs, QDateTime::currentMSecsSinceEpoch() + 5 * 60000,
                                {QStringLiteral("Break")}, QString(), false);
        m_pomoWork = false;
        m_pomoEndMs = QDateTime::currentMSecsSinceEpoch() + 5 * 60000;
        m_pomoTick->start();
        onPomoTick();
        emit tagsChanged();
    } else {
        // 休息结束
        m_pomoWork = true;
        m_pomoRunning = false;
        m_pomoTick->stop();
        m_pomoStart->setEnabled(true);
        m_pomoDisplay->setText(QStringLiteral("25:00"));
        QMessageBox::information(this, QStringLiteral("番茄钟"),
                                 QStringLiteral("休息结束，可以开始下一轮。"));
    }
}

void TimingDialog::onPomoTick()
{
    if (!m_pomoRunning)
        return;
    const qint64 remain = m_pomoEndMs - QDateTime::currentMSecsSinceEpoch();
    if (remain <= 0) {
        m_pomoRunning = false;
        m_pomoTick->stop();
        onPomoBreakConfirm();
        return;
    }
    m_pomoDisplay->setText(QStringLiteral("%1:%2")
                               .arg(remain / 60000, 2, 10, QLatin1Char('0'))
                               .arg((remain % 60000) / 1000, 2, 10, QLatin1Char('0')));
}

} // namespace awqtui
