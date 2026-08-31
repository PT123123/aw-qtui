// timingdialog.h —— 计时工具：秒表 / 计时器 / 番茄钟
#pragma once

#include <QDialog>

#include "tagstore.h"

class QLabel;
class QPushButton;
class QSpinBox;
class QTabWidget;
class QTimer;

namespace awqtui {

class TimingDialog : public QDialog
{
    Q_OBJECT
public:
    explicit TimingDialog(TagStore *store, QWidget *parent = nullptr);
    ~TimingDialog() override;

signals:
    void tagsChanged();

private slots:
    // stopwatch
    void onSwStart();
    void onSwPause();
    void onSwStop();
    void onSwTick();
    // timer
    void onTimerStart();
    void onTimerTick();
    // pomodoro
    void onPomoStart();
    void onPomoBreakConfirm();
    void onPomoTick();

private:
    TagStore *m_store = nullptr;

    // 秒表
    QPushButton *m_swStart = nullptr;
    QPushButton *m_swPause = nullptr;
    QPushButton *m_swStop = nullptr;
    QLabel *m_swDisplay = nullptr;
    QTimer *m_swTimer = nullptr;
    qint64 m_swStartMs = 0;
    qint64 m_swAccum = 0;
    qint64 m_swResumeMs = 0;
    bool m_swRunning = false;
    QStringList m_swTags;
    bool m_swBillable = false;

    // 计时器
    QSpinBox *m_timerMin = nullptr;
    QPushButton *m_timerStart = nullptr;
    QLabel *m_timerDisplay = nullptr;
    QTimer *m_timerTick = nullptr;
    qint64 m_timerEndMs = 0;
    bool m_timerRunning = false;
    QStringList m_timerTags;
    QString m_timerNotes;

    // 番茄钟
    QPushButton *m_pomoStart = nullptr;
    QLabel *m_pomoDisplay = nullptr;
    QTimer *m_pomoTick = nullptr;
    qint64 m_pomoEndMs = 0;
    bool m_pomoRunning = false;
    bool m_pomoWork = true;
    QStringList m_pomoTags;
};

} // namespace awqtui
