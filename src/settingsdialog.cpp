// settingsdialog.cpp —— 设置界面实现
#include "settingsdialog.h"

#include "theme.h"

#include <QDialogButtonBox>
#include <QFocusEvent>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QMessageBox>
#include <QPushButton>
#include <QVBoxLayout>

namespace awqtui {

// ------------------------------------------------------------------ //
// ShortcutEdit
// ------------------------------------------------------------------ //

ShortcutEdit::ShortcutEdit(QWidget *parent) : QLineEdit(parent)
{
    setReadOnly(true);
    setPlaceholderText(QStringLiteral("点击后按组合键，Esc 清除"));
    setClearButtonEnabled(false);
    setFixedWidth(240);
    setStyleSheet(QStringLiteral(
        "QLineEdit { background: %1; border: 1px solid %2; border-radius: 6px;"
        " padding: 6px 10px; selection-background-color: %3; }"
        "QLineEdit:focus { border-color: %3; }")
                      .arg(kColorBgElev, kColorBorder, kColorAccent));
}

void ShortcutEdit::setSequence(const QKeySequence &s)
{
    m_seq = s;
    refreshText();
    emit sequenceChanged(m_seq);
}

void ShortcutEdit::refreshText()
{
    if (m_seq.isEmpty())
        setText(QString());
    else
        setText(m_seq.toString(QKeySequence::NativeText));
}

void ShortcutEdit::keyPressEvent(QKeyEvent *event)
{
    const int key = event->key();
    // 忽略纯修饰键 / 未知键
    if (key == Qt::Key_unknown || key == Qt::Key_Control || key == Qt::Key_Shift ||
        key == Qt::Key_Alt || key == Qt::Key_Meta) {
        event->accept();
        return;
    }
    // Esc 清除
    if (key == Qt::Key_Escape) {
        setSequence(QKeySequence());
        event->accept();
        return;
    }
    // 记录有效修饰键 + 主键（QKeyCombination 编码，含 Shift 状态）
    const Qt::KeyboardModifiers mods =
        event->modifiers() &
        (Qt::ShiftModifier | Qt::ControlModifier | Qt::AltModifier | Qt::MetaModifier);
    setSequence(QKeySequence(QKeyCombination(mods, static_cast<Qt::Key>(key))));
    event->accept();
}

void ShortcutEdit::focusInEvent(QFocusEvent *event)
{
    QLineEdit::focusInEvent(event);
    setStyleSheet(QStringLiteral(
        "QLineEdit { background: %1; border: 1px solid %2; border-radius: 6px;"
        " padding: 6px 10px; selection-background-color: %3; }")
                      .arg(kColorBgElev, kColorAccent, kColorAccent));
}

void ShortcutEdit::focusOutEvent(QFocusEvent *event)
{
    QLineEdit::focusOutEvent(event);
    setStyleSheet(QStringLiteral(
        "QLineEdit { background: %1; border: 1px solid %2; border-radius: 6px;"
        " padding: 6px 10px; selection-background-color: %3; }"
        "QLineEdit:focus { border-color: %3; }")
                      .arg(kColorBgElev, kColorBorder, kColorAccent));
}

// ------------------------------------------------------------------ //
// SettingsDialog
// ------------------------------------------------------------------ //

SettingsDialog::SettingsDialog(const ShortcutConfig &cfg, QWidget *parent) : QDialog(parent)
{
    setWindowTitle(QStringLiteral("设置"));
    setMinimumWidth(460);

    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(24, 20, 24, 16);
    root->setSpacing(12);

    auto *title = new QLabel(QStringLiteral("全局快捷键"));
    title->setStyleSheet(QStringLiteral("font-size: 16px; font-weight: 700; color: white;"));
    root->addWidget(title);

    auto *hint = new QLabel(QStringLiteral(
        "全局快捷键在其它应用处于前台、本窗口最小化时也能触发。建议使用 Alt 或 Ctrl 组合键。"));
    hint->setWordWrap(true);
    hint->setStyleSheet(QStringLiteral("color: %1; font-size: 12px;").arg(kColorFgMuted));
    root->addWidget(hint);

    auto *form = new QFormLayout;
    form->setContentsMargins(0, 8, 0, 0);
    form->setHorizontalSpacing(16);
    form->setVerticalSpacing(12);

    m_add = new ShortcutEdit;
    m_add->setSequence(cfg.addNote);
    auto *addLabel = new QLabel(QStringLiteral("添加记录"));
    addLabel->setStyleSheet(QStringLiteral("color: %1;").arg(kColorFg));
    form->addRow(addLabel, m_add);

    m_inbox = new ShortcutEdit;
    m_inbox->setSequence(cfg.showInbox);
    auto *inboxLabel = new QLabel(QStringLiteral("唤醒并跳转收件箱"));
    inboxLabel->setStyleSheet(QStringLiteral("color: %1;").arg(kColorFg));
    form->addRow(inboxLabel, m_inbox);

    root->addLayout(form);

    auto *footHint = new QLabel(QStringLiteral(
        "快捷键留空（按 Esc）表示不启用该全局热键。保存后立即生效，冲突（被其它程序占用）会提示。"));
    footHint->setWordWrap(true);
    footHint->setStyleSheet(QStringLiteral("color: %1; font-size: 11px;").arg(kColorFgMuted));
    root->addWidget(footHint);

    auto *btns = new QDialogButtonBox(QDialogButtonBox::Save | QDialogButtonBox::Cancel);
    btns->button(QDialogButtonBox::Save)->setText(QStringLiteral("保存"));
    btns->button(QDialogButtonBox::Save)->setObjectName(QStringLiteral("PrimaryBtn"));
    btns->button(QDialogButtonBox::Cancel)->setText(QStringLiteral("取消"));
    btns->button(QDialogButtonBox::Cancel)->setStyleSheet(
        QStringLiteral("QPushButton { background: transparent; border: 1px solid %1; }")
            .arg(kColorBorder));
    connect(btns, &QDialogButtonBox::accepted, this, [this] {
        const QString err = validate(config());
        if (!err.isEmpty()) {
            QMessageBox::warning(this, QStringLiteral("快捷键无效"), err);
            return;
        }
        accept();
    });
    connect(btns, &QDialogButtonBox::rejected, this, &QDialog::reject);
    root->addWidget(btns);
}

ShortcutConfig SettingsDialog::config() const
{
    ShortcutConfig c;
    c.addNote = m_add->sequence();
    c.showInbox = m_inbox->sequence();
    return c;
}

QString SettingsDialog::validate(const ShortcutConfig &c)
{
    constexpr int kModMask = Qt::ShiftModifier | Qt::ControlModifier | Qt::AltModifier | Qt::MetaModifier;
    const QString addName = QStringLiteral("「添加记录」");
    const QString inboxName = QStringLiteral("「唤醒并跳转收件箱」");

    for (const auto &p : {qMakePair(c.addNote, addName), qMakePair(c.showInbox, inboxName)}) {
        if (p.first.isEmpty())
            continue; // 留空 = 不启用
        if (!(p.first[0].keyboardModifiers() & kModMask))
            return QStringLiteral("%1快捷键需要包含至少一个修饰键（Ctrl / Alt / Shift / Win），"
                                  "避免全局劫持普通按键。")
                .arg(p.second);
    }
    if (!c.addNote.isEmpty() && !c.showInbox.isEmpty() && c.addNote == c.showInbox)
        return QStringLiteral("两个快捷键不能设置为相同的组合。");
    return QString();
}

} // namespace awqtui
