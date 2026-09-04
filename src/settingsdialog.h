// settingsdialog.h —— 设置界面：Tab 分页（外观 / 边缘修复 / 快捷键 / 关于）+ 全局快捷键配置
#pragma once

#include <QDialog>
#include <QJsonObject>
#include <QKeySequence>
#include <QLineEdit>

#include "appsettings.h"

class QComboBox;
class QCheckBox;
class QFocusEvent;
class QKeyEvent;
class QLabel;

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

// 同步配置数据（供 SettingsDialog 同步 Tab 读写，经 ApiClient 持久化到 aw-server-rust）
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

// 设置对话框：Tab 分页 —— 外观（主题+界面效果）/ 边缘修复（实验开关）/ 同步 / 快捷键 / 关于（设备信息）
class SettingsDialog : public QDialog
{
    Q_OBJECT
public:
    explicit SettingsDialog(const ShortcutConfig &cfg, const QString &themeId = QString(),
                            const UiEffects &fx = UiEffects{}, QWidget *parent = nullptr);

    ShortcutConfig config() const;
    // 当前选择的主题 ID
    QString themeId() const;
    // 当前勾选的界面效果开关
    UiEffects uiEffects() const;
    // 同步设置（D1 / LAN / 冷备）
    SyncSettingsConfig syncSettings() const;
    void setSyncSettings(const SyncSettingsConfig &s);
    // 校验快捷键配置，返回错误信息（空串表示通过）
    static QString validate(const ShortcutConfig &c);

private:
    ShortcutEdit *m_add;
    ShortcutEdit *m_inbox;
    QComboBox *m_themeCombo;
    QLabel *m_themeDesc;
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
    // 同步设置 Tab
    QCheckBox *m_cbSyncInbox;     // 收件箱 → D1 云同步
    QCheckBox *m_cbSyncActivity;  // ActivityWatch → 局域网同步
    QCheckBox *m_cbSyncTodo;      // 任务 → D1 云同步
    bool m_updatingPreset = false;
};

} // namespace awqtui
