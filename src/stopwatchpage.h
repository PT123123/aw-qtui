// stopwatchpage.h —— 秒表页（手动计时，停止后写入 aw-stopwatch-android bucket）
#pragma once

#include <QWidget>
#include <QList>

class QLabel;
class QLineEdit;
class QListWidget;
class QPushButton;
class QTimer;

namespace awqtui {

class ApiClient;

struct StopwatchRecord {
    qint64 startMs = 0;
    qint64 endMs = 0;
    qint64 durationMs = 0;
    QString label;
};

class StopwatchPage : public QWidget
{
    Q_OBJECT
public:
    explicit StopwatchPage(ApiClient *api, QWidget *parent = nullptr);
    ~StopwatchPage() override;

    void refresh();

private slots:
    void onStart();
    void onPauseResume();
    void onStopReset();
    void onClearHistory();
    void onTick();
    void onDeleteRecord();
    void onBucketReady();

private:
    void buildUi();
    void applyStyle();
    void rebuildHistory();
    void updateDisplay();
    void ensureBucket();

    ApiClient *m_api = nullptr;

    // 显示
    QLabel *m_timeLabel = nullptr;
    QLabel *m_statusLabel = nullptr;
    QLineEdit *m_labelEdit = nullptr;

    // 按钮
    QPushButton *m_startBtn = nullptr;
    QPushButton *m_secondaryBtn = nullptr;  // 暂停/继续 或 重置
    QPushButton *m_clearBtn = nullptr;

    // 计时状态
    enum class State { Idle, Running, Paused, Stopped };
    State m_state = State::Idle;
    qint64 m_sessionStartMs = 0;   // 本次开始 epoch ms
    qint64 m_accumMs = 0;          // 已累计 ms（不含当前运行段）
    QTimer *m_tick = nullptr;

    // 历史
    QList<StopwatchRecord> m_history;
    QListWidget *m_historyList = nullptr;

    // bucket
    bool m_bucketReady = false;
};

} // namespace awqtui
