// stopwatchpage.cpp —— 秒表页（手动计时，停止后写入 aw-stopwatch-android bucket）
#include "stopwatchpage.h"

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

StopwatchPage::~StopwatchPage() = default;

void StopwatchPage::buildUi()
{
    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(24, 24, 24, 24);
    root->setSpacing(16);

    // 标题
    auto *title = new QLabel(QStringLiteral("秒表"));
    title->setObjectName(QStringLiteral("PageTitle"));
    root->addWidget(title);

    // 时间显示
    m_timeLabel = new QLabel(QStringLiteral("00:00.0"));
    m_timeLabel->setObjectName(QStringLiteral("StopwatchTime"));
    m_timeLabel->setAlignment(Qt::AlignCenter);
    m_timeLabel->setStyleSheet(QStringLiteral("font-size: 56px; font-weight: 200; font-family: 'Consolas', 'Courier New', monospace;"));
    root->addWidget(m_timeLabel);

    m_statusLabel = new QLabel(QStringLiteral("准备就绪"));
    m_statusLabel->setAlignment(Qt::AlignCenter);
    m_statusLabel->setStyleSheet(QStringLiteral("color: %1; margin-bottom: 8px;").arg(kColorFgMuted));
    root->addWidget(m_statusLabel);

    // 标签输入
    auto *labelRow = new QHBoxLayout;
    labelRow->addWidget(new QLabel(QStringLiteral("标签")));
    m_labelEdit = new QLineEdit;
    m_labelEdit->setPlaceholderText(QStringLiteral("可选：事件描述"));
    labelRow->addWidget(m_labelEdit, 1);
    root->addLayout(labelRow);

    // 按钮行
    auto *btnRow = new QHBoxLayout;
    m_startBtn = new QPushButton(QStringLiteral("开始"));
    m_startBtn->setObjectName(QStringLiteral("PrimaryBtn"));
    connect(m_startBtn, &QPushButton::clicked, this, [this] {
        if (m_state == State::Idle) onStart();
        else if (m_state == State::Running || m_state == State::Paused) onStopReset();
    });
    m_secondaryBtn = new QPushButton(QStringLiteral("暂停"));
    m_secondaryBtn->setEnabled(false);
    connect(m_secondaryBtn, &QPushButton::clicked, this, &StopwatchPage::onPauseResume);
    m_clearBtn = new QPushButton(QStringLiteral("清空历史"));
    connect(m_clearBtn, &QPushButton::clicked, this, &StopwatchPage::onClearHistory);
    btnRow->addWidget(m_startBtn);
    btnRow->addWidget(m_secondaryBtn);
    btnRow->addStretch(1);
    btnRow->addWidget(m_clearBtn);
    root->addLayout(btnRow);

    // 历史记录
    auto *histTitle = new QLabel(QStringLiteral("历史记录"));
    histTitle->setObjectName(QStringLiteral("SectionTitle"));
    root->addWidget(histTitle);

    m_historyList = new QListWidget;
    m_historyList->setObjectName(QStringLiteral("HistoryList"));
    m_historyList->setSelectionMode(QAbstractItemView::SingleSelection);
    connect(m_historyList, &QListWidget::itemDoubleClicked, this, &StopwatchPage::onDeleteRecord);
    root->addWidget(m_historyList, 1);

    auto *hint = new QLabel(QStringLiteral("双击历史条目可删除"));
    hint->setStyleSheet(QStringLiteral("color: %1; font-size: 11px;").arg(kColorFgMuted));
    root->addWidget(hint);

    root->addStretch(0);
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
