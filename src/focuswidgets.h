// focuswidgets.h —— 专注模块 UI（计时 / 专注记录 / 专注记录详情 / 倒数纪念日 / 功能模块设置）
#pragma once

#include <QDate>
#include <QDialog>
#include <QList>
#include <QWidget>

#include "appsettings.h"
#include "focusmodels.h"

class QCheckBox;
class QComboBox;
class QDateEdit;
class QLabel;
class QLineEdit;
class QListWidget;
class QPushButton;
class QSpinBox;
class QTimeEdit;
class QTimer;
class QToolButton;

namespace awqtui {

class FocusSource;
class TodoSource;

// ── 计时页：番茄倒计时（15/20/30/60 + 自定义）+ 正计时 ──
// 番茄完成 / 正计时「完成并记录」→ 写一条 FocusSession 到 FocusStore
class FocusTimerPage : public QWidget
{
    Q_OBJECT
public:
    explicit FocusTimerPage(FocusSource *focus, TodoSource *todo, QWidget *parent = nullptr);
    void applyUiScale();
    void refresh(); // 重建任务下拉

private slots:
    void onMode(int kind);
    void onPreset();
    void onStartPause();
    void onSecondary();
    void onTick();
    void onTaskChanged(int idx);

private:
    void buildUi();
    void applyStyle();
    void setMode(int kind);
    void setPresetRowVisible(bool visible);
    void toIdle();
    void updateHint();
    qint64 elapsedMs() const; // 累计专注（不含暂停）
    QString timeText() const;
    void record(qint64 durationSec);

    FocusSource *m_focus = nullptr;
    TodoSource *m_todo = nullptr;

    QList<QToolButton *> m_modeBtns;
    QList<QToolButton *> m_presetBtns;
    QWidget *m_presetRow = nullptr;   // 番茄预设行容器（正计时时隐藏）
    int m_kind = FocusPomodoro;
    int m_minutes = 25;
    QSpinBox *m_customMin = nullptr;
    QLineEdit *m_eventEdit = nullptr;
    QComboBox *m_taskCombo = nullptr;
    QLabel *m_timeLabel = nullptr;
    QLabel *m_hintLabel = nullptr;
    QPushButton *m_startBtn = nullptr;
    QPushButton *m_secondaryBtn = nullptr;
    QTimer *m_tick = nullptr;

    bool m_running = false;
    bool m_paused = false;
    qint64 m_accumMs = 0;        // 已累计专注（不含当前运行段）
    qint64 m_segStartMs = 0;     // 当前运行段起点 epoch ms
    qint64 m_sessionStartMs = 0; // 本次会话原始起点 epoch ms（记录用）
    qint64 m_taskId = 0;
};

// ── 专注记录：今日番茄 / 今日专注 / 总番茄 / 总专注时长 ──
class FocusOverviewPage : public QWidget
{
    Q_OBJECT
public:
    explicit FocusOverviewPage(FocusSource *focus, QWidget *parent = nullptr);
    void applyUiScale();
    void refresh();

private:
    void buildUi();
    void applyStyle();
    FocusSource *m_focus = nullptr;
    QLabel *m_todayPomo = nullptr;
    QLabel *m_todayTime = nullptr;
    QLabel *m_totalPomo = nullptr;
    QLabel *m_totalTime = nullptr;
};

// ── 专注记录详情：时间线列表（日期+事件+起止），删除 / 补记 ──
class FocusDetailPage : public QWidget
{
    Q_OBJECT
public:
    explicit FocusDetailPage(FocusSource *focus, QWidget *parent = nullptr);
    void applyUiScale();
    void refresh();

private:
    void onAddManual();
    FocusSource *m_focus = nullptr;
    QPushButton *m_addBtn = nullptr;
    QLabel *m_countLabel = nullptr;
    QListWidget *m_list = nullptr;
};

// ── 倒数纪念日：emoji + 名称 + 剩余天数，增删 ──
class FocusMemorialPage : public QWidget
{
    Q_OBJECT
public:
    explicit FocusMemorialPage(FocusSource *focus, QWidget *parent = nullptr);
    void applyUiScale();
    void refresh();

private:
    void onAdd();
    FocusSource *m_focus = nullptr;
    QPushButton *m_addBtn = nullptr;
    QLabel *m_countLabel = nullptr;
    QListWidget *m_list = nullptr;
};

// ── 功能模块设置：控制侧边栏显示开关（对齐滴答清单「功能模块」） ──
class FocusSettingsDialog : public QDialog
{
    Q_OBJECT
public:
    explicit FocusSettingsDialog(QWidget *parent = nullptr);
    FocusModules modules() const;

private:
    QCheckBox *m_timer = nullptr;
    QCheckBox *m_overview = nullptr;
    QCheckBox *m_detail = nullptr;
    QCheckBox *m_week = nullptr;
    QCheckBox *m_heatmap = nullptr;
    QCheckBox *m_best = nullptr;
    QCheckBox *m_calendar = nullptr;
    QCheckBox *m_memorial = nullptr;
};

} // namespace awqtui
