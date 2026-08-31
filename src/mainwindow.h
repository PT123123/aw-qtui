// mainwindow.h —— 主窗口：左侧导航 + 页面堆栈
#pragma once

#include <QMainWindow>
#include <QStackedWidget>
#include <QStringList>

class QEvent;
class QKeyEvent;
class QLabel;
class QPushButton;
class QWheelEvent;

namespace awqtui {

class ApiClient;
class MdnsDiscovery;
class GlobalHotkey;
class InboxPage;
class SyncPage;
class ActivityPage;
class TimelinePage;
class DayPage;
class StatsPage;
class TagStore;

class MainWindow : public QMainWindow
{
    Q_OBJECT
public:
    explicit MainWindow(const QString &serverUrl = QString(), QWidget *parent = nullptr);
    ~MainWindow() override;

    InboxPage *inboxPage() const { return m_inbox; }
    SyncPage *syncPage() const { return m_sync; }
    ActivityPage *activityPage() const { return m_activity; }
    TimelinePage *timelinePage() const { return m_timeline; }
    DayPage *dayPage() const { return m_day; }
    StatsPage *statsPage() const { return m_stats; }

    void switchPage(int index);

    // 当前页面缩放比（1.0 = 100%）
    qreal zoomScale() const { return m_zoom; }

protected:
    void keyPressEvent(QKeyEvent *event) override;
    bool nativeEvent(const QByteArray &eventType, void *message, qintptr *result) override;
    bool eventFilter(QObject *obj, QEvent *event) override;

private slots:
    void onGlobalHotkey(int id);

private:
    void buildUi();
    void updateStatus();
    // 读取配置并注册全部全局热键；返回未能注册的快捷键描述列表（空 = 全部成功）
    QStringList applyShortcuts();
    // 打开设置对话框：编辑期间暂停热键，保存/取消后重新注册
    void openSettings();
    // 全局热键唤醒：还原/置前窗口并激活
    void wakeUpAndShow();
    // 页面缩放：以 factor 倍率放大/缩小整体 UI（Ctrl+滚轮 / +/-）
    void zoomBy(qreal factor, bool underMouse);
    // 设置绝对缩放比并应用（0.3 ~ 3.0），持久化并显示右下角百分比提示
    void setZoom(qreal zoom, bool underMouse);
    // 把当前缩放比落到位：重生成全局 QSS + 放大基准字体，并通知页面重应用其缩放样式
    void applyUiScale();
    // 右下角短暂显示当前缩放百分比
    void showZoomBadge();
    // 事件目标是否属于可缩放的主窗口内容区
    bool isInsideZoomable(QObject *obj) const;
    // 焦点控件是否为文本输入类（此时无修饰 +/- 应交给输入，不做缩放）
    bool isTextEditingWidget(const QWidget *w) const;

    ApiClient *m_api;
    MdnsDiscovery *m_mdns;
    GlobalHotkey *m_hotkey;
    TagStore *m_tagStore;
    ActivityPage *m_activity;
    TimelinePage *m_timeline;
    InboxPage *m_inbox;
    SyncPage *m_sync;
    DayPage *m_day;
    StatsPage *m_stats;
    QStackedWidget *m_stack;
    // 左侧导航栏（缩放时按比例调整宽度）
    QWidget *m_nav;
    // 页面缩放：当前缩放比（1.0 = 100%）与右下角百分比提示
    qreal m_zoom;
    QLabel *m_zoomBadge;
    QPushButton *m_navActivity;
    QPushButton *m_navTimeline;
    QPushButton *m_navInbox;
    QPushButton *m_navSync;
    QPushButton *m_navDay;
    QPushButton *m_navStats;
    QLabel *m_deviceLabel;
};

} // namespace awqtui
