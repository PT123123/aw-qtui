// stopwatchpage.cpp —— 秒表页（手动计时，停止后写入 aw-stopwatch-android bucket）
#include "stopwatchpage.h"
#include "ui_stopwatchpage.h"

#include "apiclient.h"
#include "charts.h"
#include "config.h"
#include "theme.h"

#include <QDateTime>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMessageBox>
#include <QPushButton>
#include <QScrollBar>
#include <QTimer>
#include <QVBoxLayout>

namespace awqtui {

StopwatchPage::StopwatchPage(ApiClient *api, QWidget *parent)
    : QWidget(parent), m_api(api)
{
    buildUi();
    applyStyle();

    m_tick = new QTimer(this);
    m_tick->setInterval(100);
    connect(m_tick, &QTimer::timeout, this, &StopwatchPage::onTick);
}

StopwatchPage::~StopwatchPage()
{
    delete ui;
}

void StopwatchPage::buildUi()
{
    // 静态布局来自 Qt Designer（stopwatchpage.ui -> ui_stopwatchpage.h）；
    // 控件名直接使用主题 QSS 的 objectName 角色，静态字体样式烘焙在 .ui 中
    ui = new Ui::StopwatchPage;
    ui->setupUi(this);

    // 成员别名：业务逻辑沿用 m_* 指针
    m_timeLabel = ui->StopwatchTime;
    m_statusLabel = ui->statusLabel;
    m_labelEdit = ui->labelEdit;
    m_startBtn = ui->PrimaryBtn;
    m_secondaryBtn = ui->secondaryBtn;
    m_clearBtn = ui->clearBtn;
    m_historyList = ui->HistoryList;

    // 主题色相关样式（kColor* 随主题切换，无法烘焙进 .ui）
    m_statusLabel->setStyleSheet(QStringLiteral("color: %1; margin-bottom: 8px;").arg(kColorFgMuted));
    ui->hint->setStyleSheet(QStringLiteral("color: %1; font-size: 11px;").arg(kColorFgMuted));

    connect(m_startBtn, &QPushButton::clicked, this, [this] {
        if (m_state == State::Idle) onStart();
        else if (m_state == State::Running || m_state == State::Paused) onStopReset();
    });
    connect(m_secondaryBtn, &QPushButton::clicked, this, &StopwatchPage::onPauseResume);
    connect(m_clearBtn, &QPushButton::clicked, this, &StopwatchPage::onClearHistory);
    connect(m_historyList, &QListWidget::itemDoubleClicked, this, &StopwatchPage::onDeleteRecord);
}

void StopwatchPage::applyStyle()
{
    // 使用全局主题
}

void StopwatchPage::refresh()
{
    rebuildHistory();
}

void StopwatchPage::onStart()
{
    if (!m_api) return;

    m_state = State::Running;
    m_sessionStartMs = QDateTime::currentMSecsSinceEpoch();
    m_accumMs = 0;
    m_tick->start();

    m_startBtn->setText(QStringLiteral("停止"));
    m_secondaryBtn->setText(QStringLiteral("暂停"));
    m_secondaryBtn->setEnabled(true);
    m_statusLabel->setText(QStringLiteral("计时中…"));

    // 确保 bucket 存在
    ensureBucket();
}

void StopwatchPage::onPauseResume()
{
    if (m_state == State::Running) {
        // 暂停
        m_state = State::Paused;
        m_accumMs += QDateTime::currentMSecsSinceEpoch() - m_sessionStartMs;
        m_tick->stop();
        m_secondaryBtn->setText(QStringLiteral("继续"));
        m_startBtn->setEnabled(true);
        m_statusLabel->setText(QStringLiteral("已暂停"));
    } else if (m_state == State::Paused) {
        // 继续
        m_state = State::Running;
        m_sessionStartMs = QDateTime::currentMSecsSinceEpoch();
        m_tick->start();
        m_secondaryBtn->setText(QStringLiteral("暂停"));
        m_statusLabel->setText(QStringLiteral("计时中…"));
    }
}

void StopwatchPage::onStopReset()
{
    if (m_state == State::Idle) return;

    qint64 totalMs = m_accumMs;
    if (m_state == State::Running || m_state == State::Stopped) {
        totalMs += QDateTime::currentMSecsSinceEpoch() - m_sessionStartMs;
    }

    if (totalMs < 1000) {
        // 太短，直接丢弃
        m_state = State::Idle;
        m_tick->stop();
        m_timeLabel->setText(QStringLiteral("00:00.0"));
        m_statusLabel->setText(QStringLiteral("准备就绪"));
        m_startBtn->setText(QStringLiteral("开始"));
        m_secondaryBtn->setEnabled(false);
        return;
    }

    // 保存记录
    StopwatchRecord rec;
    rec.startMs = m_sessionStartMs;
    rec.endMs = QDateTime::currentMSecsSinceEpoch();
    rec.durationMs = totalMs;
    rec.label = m_labelEdit->text().trimmed();
    m_history.append(rec);

    // 写入 bucket
    if (m_api && m_bucketReady) {
        const double durationSec = totalMs / 1000.0;
        QDateTime ts = QDateTime::fromMSecsSinceEpoch(rec.startMs);
        QJsonObject data;
        data.insert(QStringLiteral("label"), rec.label.isEmpty() ? QStringLiteral("Stopwatch") : rec.label);
        data.insert(QStringLiteral("duration"), durationSec);
        m_api->heartbeat(QStringLiteral("aw-stopwatch-android"), data, durationSec, ts);
    }

    m_state = State::Idle;
    m_tick->stop();
    m_timeLabel->setText(QStringLiteral("00:00.0"));
    m_statusLabel->setText(QStringLiteral("已记录 %1").arg(formatDuration(totalMs / 1000)));
    m_startBtn->setText(QStringLiteral("开始"));
    m_secondaryBtn->setEnabled(false);

    m_labelEdit->clear();
    rebuildHistory();
}

void StopwatchPage::onClearHistory()
{
    if (m_history.isEmpty()) return;
    auto ret = QMessageBox::question(this, QStringLiteral("清空历史"),
                                     QStringLiteral("确定要清空所有历史记录吗？"),
                                     QMessageBox::Yes | QMessageBox::No);
    if (ret == QMessageBox::Yes) {
        m_history.clear();
        rebuildHistory();
    }
}

void StopwatchPage::onTick()
{
    updateDisplay();
}

void StopwatchPage::updateDisplay()
{
    qint64 totalMs = m_accumMs;
    if (m_state == State::Running) {
        totalMs += QDateTime::currentMSecsSinceEpoch() - m_sessionStartMs;
    }
    m_timeLabel->setText(formatMs(totalMs));
}

void StopwatchPage::onDeleteRecord()
{
    const int row = m_historyList->currentRow();
    if (row < 0 || row >= m_history.size()) return;
    m_history.removeAt(row);
    rebuildHistory();
}

void StopwatchPage::rebuildHistory()
{
    m_historyList->clear();
    for (int i = m_history.size() - 1; i >= 0; --i) {
        const auto &r = m_history[i];
        QDateTime start = QDateTime::fromMSecsSinceEpoch(r.startMs);
        QString text = QStringLiteral("%1 · %2 · %3")
                           .arg(start.toString(QStringLiteral("yyyy-MM-dd HH:mm")),
                                formatDuration(r.durationMs / 1000),
                                r.label.isEmpty() ? QStringLiteral("(无标签)") : r.label);
        m_historyList->addItem(text);
    }
}

void StopwatchPage::ensureBucket()
{
    if (!m_api) return;
    if (m_bucketReady) return;
    QNetworkReply *r = m_api->createBucket(QStringLiteral("aw-stopwatch-android"),
                                           QStringLiteral("aw-qtui"),
                                           QStringLiteral("stopwatch"));
    connect(r, &QNetworkReply::finished, this, &StopwatchPage::onBucketReady);
}

void StopwatchPage::onBucketReady()
{
    QNetworkReply *r = qobject_cast<QNetworkReply *>(sender());
    if (!r) return;
    m_bucketReady = true;
    r->deleteLater();
}

} // namespace awqtui
