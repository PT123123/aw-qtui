// settingsdialog.h —— 设置界面：全局快捷键配置
#pragma once

#include <QDialog>
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

// 设置对话框：主题选择 + 界面效果开关（阴影/材质/动画）+ 两个全局快捷键
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
    bool m_updatingPreset = false;
};

} // namespace awqtui
