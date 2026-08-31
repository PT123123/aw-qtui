// settingsdialog.h —— 设置界面：全局快捷键配置
#pragma once

#include <QDialog>
#include <QKeySequence>
#include <QLineEdit>

#include "appsettings.h"

class QKeyEvent;
class QFocusEvent;

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

// 设置对话框：编辑“添加记录”与“唤醒并跳转收件箱”两个全局快捷键
class SettingsDialog : public QDialog
{
    Q_OBJECT
public:
    explicit SettingsDialog(const ShortcutConfig &cfg, QWidget *parent = nullptr);

    ShortcutConfig config() const;
    // 校验配置，返回错误信息（空串表示通过）
    static QString validate(const ShortcutConfig &c);

private:
    ShortcutEdit *m_add;
    ShortcutEdit *m_inbox;
};

} // namespace awqtui
