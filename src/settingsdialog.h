// settingsdialog.h —— 设置界面：Tab 分页（外观 / 边缘修复 / 同步 / 快捷键 / 关于）+ 全局快捷键配置
// 核心实现为可复用组件 SettingsWidget：既被模态对话框（SettingsDialog）使用，
// 也被设置页「通用设置」Tab 内嵌（见 MainWindow::buildSettingsEditor）。
#pragma once

#include <QDialog>
#include <QJsonObject>
#include <QKeySequence>
#include <QLineEdit>
#include <QWidget>

#include "appsettings.h"

class QCheckBox;
class QComboBox;
class QFocusEvent;
class QKeyEvent;
class QLabel;
class QSlider;
class QTabWidget;
class QTimer;

namespace awqtui {

// 快捷键录入框：捕获组合键并显示原生文本；Esc 清空
class ShortcutEdit : public QLineEdit
{
    Q_OBJECT
public:
    explicit ShortcutEdit(QWidget *parent = nullptr);

    QKeySequence sequence() const { return m_seq; }
    void setSequence(const QKeySequence &s);

signals:
    void sequenceChanged(const QKeySequence &s);

protected:
    void keyPressEvent(QKeyEvent *event) override;
    void focusInEvent(QFocusEvent *event) override;
    void focusOutEvent(QFocusEvent *event) override;

private:
    void refreshText();
    QKeySequence m_seq;
};

// 同步配置数据（供 SettingsWidget 同步 Tab 读写，经 ApiClient 持久化到 aw-server-rust）
struct SyncSettingsConfig {
    bool syncInbox = true;    // 收件箱 → D1 云同步
    bool syncActivity = true; // ActivityWatch → 局域网同步
    bool syncTodo = true;     // 任务 → D1 云同步
    static SyncSettingsConfig fromJson(const QJsonObject &o) {
        SyncSettingsConfig c;
        c.syncInbox = o.value(QLatin1String("sync_inbox")).toBool(true);
        c.syncActivity = o.value(QLatin1String("sync_activity")).toBool(true);
        c.syncTodo = o.value(QLatin1String("sync_todo")).toBool(true);
        return c;
    }
    QJsonObject toJson() const {
        QJsonObject o;
        o.insert(QStringLiteral("sync_inbox"), syncInbox);
        o.insert(QStringLiteral("sync_activity"), syncActivity);
        o.insert(QStringLiteral("sync_todo"), syncTodo);
        return o;
    }
};

// 设置编辑组件（无窗口/无底部按钮，供内嵌或对话框复用）：
// 构建 外观 / 边缘修复 / 同步 / 快捷键 / 关于 五个 Tab，仅在各控件中保存值，
// 调用方通过 getter 读取并持久化。
class SettingsWidget : public QWidget
{
    Q_OBJECT
public:
    explicit SettingsWidget(const ShortcutConfig &cfg, const QString &themeId = QString(),
                            const UiEffects &fx = UiEffects{},
                            const QString &appIconId = QString(), double zoom = 1.0,
                            QWidget *parent = nullptr);

    QTabWidget *tabs() const { return m_tabs; }

    ShortcutConfig config() const;
    // 当前选择的主题 ID
    QString themeId() const;
    // 当前选择的程序图标 ID（见 theme.h kAppIconVariants）
    QString appIconId() const;
    // 当前勾选的界面效果开关
    UiEffects uiEffects() const;
    // 当前选择的界面缩放比（0.3~3.0）
    double zoom() const;
    // 由外部同步界面缩放比（如缩放对齐吸附后），仅更新滑块与数值，不触发 zoomChanged
    void setZoomValue(double z);
    // 同步设置（D1 / LAN / 冷备）
    SyncSettingsConfig syncSettings() const;
    void setSyncSettings(const SyncSettingsConfig &s);
    // 校验快捷键配置，返回错误信息（空串表示通过）
    static QString validate(const ShortcutConfig &c);

signals:
    // 界面缩放比发生变化（拖动滑块停止后短暂防抖发出，供内嵌设置做实时预览）
    void zoomChanged(double zoom);

private:
    QTabWidget *m_tabs = nullptr;
    ShortcutEdit *m_add;
    ShortcutEdit *m_inbox;
    QComboBox *m_themeCombo;
    QLabel *m_themeDesc;
    QComboBox *m_iconCombo;       // 程序图标（对齐 aw-android 可选启动图标）
    QComboBox *m_presetCombo;     // 效果预设
    QComboBox *m_shadowCombo;     // 阴影强度
    QComboBox *m_glassCombo;      // 玻璃强度
    QCheckBox *m_cbAnimations;    // 动画
    QCheckBox *m_cbDwm;           // DWM 系统背景（实验性）
    // 边缘/阴影修复开关（A/B 实验）：默认勾选 = 采用修复后的观感
    QCheckBox *m_cbFixEdge;       // 低对比描边
    QCheckBox *m_cbFixGlass;      // 玻璃防叠影
    QCheckBox *m_cbFixZoom;       // 缩放对齐
    QCheckBox *m_cbFixShadow;     // 投影随主题
    // 界面缩放比（外观 Tab）：滑块 30~300（百分比）+ 数值标签
    QSlider *m_zoomSlider;
    QLabel *m_zoomValue;
    QTimer *m_zoomDebounce;       // 滑块防抖：停止拖动才发出 zoomChanged
    // 同步设置 Tab
    QCheckBox *m_cbSyncInbox;     // 收件箱 → D1 云同步
    QCheckBox *m_cbSyncActivity;  // ActivityWatch → 局域网同步
    QCheckBox *m_cbSyncTodo;      // 任务 → D1 云同步
    bool m_updatingPreset = false;
};

// 设置对话框：包一层 SettingsWidget + 保存/取消按钮（托盘 / 收件箱入口使用）
class SettingsDialog : public QDialog
{
    Q_OBJECT
public:
    explicit SettingsDialog(const ShortcutConfig &cfg, const QString &themeId = QString(),
                            const UiEffects &fx = UiEffects{},
                            const QString &appIconId = QString(), double zoom = 1.0,
                            QWidget *parent = nullptr);

    SettingsWidget *widget() const { return m_widget; }

    ShortcutConfig config() const { return m_widget->config(); }
    QString themeId() const { return m_widget->themeId(); }
    QString appIconId() const { return m_widget->appIconId(); }
    UiEffects uiEffects() const { return m_widget->uiEffects(); }
    double zoom() const { return m_widget->zoom(); }
    SyncSettingsConfig syncSettings() const { return m_widget->syncSettings(); }
    void setSyncSettings(const SyncSettingsConfig &s) { m_widget->setSyncSettings(s); }
    // 校验快捷键配置，返回错误信息（空串表示通过）
    static QString validate(const ShortcutConfig &c) { return SettingsWidget::validate(c); }

private:
    SettingsWidget *m_widget = nullptr;
};

} // namespace awqtui