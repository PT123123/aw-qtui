// settingsdialog.cpp —— 设置界面实现
#include "settingsdialog.h"

#include "theme.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QFocusEvent>
#include <QFormLayout>
#include <QFrame>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QMessageBox>
#include <QPushButton>
#include <QVBoxLayout>

#include <iterator>

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

SettingsDialog::SettingsDialog(const ShortcutConfig &cfg, const QString &themeId,
                               const UiEffects &fx, QWidget *parent)
    : QDialog(parent)
{
    setWindowTitle(QStringLiteral("设置"));
    setMinimumWidth(480);

    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(24, 20, 24, 16);
    root->setSpacing(12);

    // ---- 外观：主题 ----
    auto *appearanceTitle = new QLabel(QStringLiteral("外观"));
    appearanceTitle->setStyleSheet(QStringLiteral("font-size: 16px; font-weight: 700; color: %1;").arg(kColorFg));
    root->addWidget(appearanceTitle);

    auto *themeRow = new QHBoxLayout;
    themeRow->setSpacing(12);
    auto *themeLabel = new QLabel(QStringLiteral("主题"));
    themeLabel->setStyleSheet(QStringLiteral("color: %1;").arg(kColorFg));
    m_themeCombo = new QComboBox;
    m_themeCombo->setMinimumWidth(260);
    m_themeCombo->setIconSize(QSize(24, 24));
    int cur = 0;
    for (int i = 0; i < (int)std::size(kThemes); ++i) {
        const Theme &t = kThemes[i];
        m_themeCombo->addItem(makeEmojiIcon(QString::fromUtf8(t.emoji)),
                              QStringLiteral("%1  %2").arg(QString::fromUtf8(t.emoji),
                                                          QString::fromUtf8(t.name)));
        if (QLatin1String(t.id) == themeId)
            cur = i;
    }
    m_themeCombo->setCurrentIndex(cur);
    themeRow->addWidget(themeLabel);
    themeRow->addWidget(m_themeCombo, 1);
    root->addLayout(themeRow);

    m_themeDesc = new QLabel;
    m_themeDesc->setWordWrap(true);
    m_themeDesc->setStyleSheet(QStringLiteral("color: %1; font-size: 11px;").arg(kColorFgMuted));
    root->addWidget(m_themeDesc);

    auto updateThemeDesc = [this] {
        const int i = m_themeCombo->currentIndex();
        if (i >= 0 && i < (int)std::size(kThemes))
            m_themeDesc->setText(QString::fromUtf8(kThemes[i].desc));
    };
    connect(m_themeCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
            [updateThemeDesc](int) { updateThemeDesc(); });
    updateThemeDesc();

    // ---- 界面效果：预设 + 阴影/玻璃强度 + 动画 + DWM ----
    auto *fxTitle = new QLabel(QStringLiteral("界面效果"));
    fxTitle->setStyleSheet(QStringLiteral("font-size: 14px; font-weight: 700; color: %1;").arg(kColorFg));
    root->addWidget(fxTitle);

    auto *fxHint = new QLabel(QStringLiteral(
        "玻璃为半透明高光面板，阴影为投影强度；主要作用于收件箱卡片、悬浮按钮与任务页面。"
        "选择预设可一键配置，手动调整单项后自动变为「自定义」。"));
    fxHint->setWordWrap(true);
    fxHint->setStyleSheet(QStringLiteral("color: %1; font-size: 11px;").arg(kColorFgMuted));
    root->addWidget(fxHint);

    const QString comboStyle = QStringLiteral(
        "QComboBox{background:%1;color:%2;border:1px solid %3;border-radius:6px;padding:4px 8px;min-height:20px;}"
        "QComboBox:hover{border-color:%4;}"
        "QComboBox QAbstractItemView{background:%1;color:%2;border:1px solid %3;selection-background-color:%4;}")
        .arg(kColorBgElev, kColorFg, kColorBorder, kColorAccent);

    // 预设行
    auto *presetRow = new QHBoxLayout;
    presetRow->setSpacing(12);
    auto *presetLabel = new QLabel(QStringLiteral("预设"));
    presetLabel->setStyleSheet(QStringLiteral("color: %1;").arg(kColorFg));
    presetLabel->setFixedWidth(56);
    m_presetCombo = new QComboBox;
    m_presetCombo->addItem(QStringLiteral("精致玻璃"));
    m_presetCombo->addItem(QStringLiteral("标准"));
    m_presetCombo->addItem(QStringLiteral("简约"));
    m_presetCombo->addItem(QStringLiteral("性能优先"));
    m_presetCombo->addItem(QStringLiteral("自定义"));
    m_presetCombo->setStyleSheet(comboStyle);
    presetRow->addWidget(presetLabel);
    presetRow->addWidget(m_presetCombo, 1);
    root->addLayout(presetRow);

    // 阴影强度行
    auto *shadowRow = new QHBoxLayout;
    shadowRow->setSpacing(12);
    auto *shadowLabel = new QLabel(QStringLiteral("阴影"));
    shadowLabel->setStyleSheet(QStringLiteral("color: %1;").arg(kColorFg));
    shadowLabel->setFixedWidth(56);
    m_shadowCombo = new QComboBox;
    m_shadowCombo->addItem(QStringLiteral("关闭"));
    m_shadowCombo->addItem(QStringLiteral("弱"));
    m_shadowCombo->addItem(QStringLiteral("中"));
    m_shadowCombo->addItem(QStringLiteral("强"));
    m_shadowCombo->setCurrentIndex(fx.shadowLevel);
    m_shadowCombo->setStyleSheet(comboStyle);
    shadowRow->addWidget(shadowLabel);
    shadowRow->addWidget(m_shadowCombo, 1);
    root->addLayout(shadowRow);

    // 玻璃强度行
    auto *glassRow = new QHBoxLayout;
    glassRow->setSpacing(12);
    auto *glassLabel = new QLabel(QStringLiteral("玻璃"));
    glassLabel->setStyleSheet(QStringLiteral("color: %1;").arg(kColorFg));
    glassLabel->setFixedWidth(56);
    m_glassCombo = new QComboBox;
    m_glassCombo->addItem(QStringLiteral("关闭（纯色）"));
    m_glassCombo->addItem(QStringLiteral("弱"));
    m_glassCombo->addItem(QStringLiteral("中"));
    m_glassCombo->addItem(QStringLiteral("强"));
    m_glassCombo->setCurrentIndex(fx.glassLevel);
    m_glassCombo->setStyleSheet(comboStyle);
    glassRow->addWidget(glassLabel);
    glassRow->addWidget(m_glassCombo, 1);
    root->addLayout(glassRow);

    // 动画 + DWM 行
    auto *fxRow = new QHBoxLayout;
    fxRow->setSpacing(20);
    const QString cbStyle = QStringLiteral("color: %1;").arg(kColorFg);
    m_cbAnimations = new QCheckBox(QStringLiteral("动画"));
    m_cbAnimations->setChecked(fx.animations);
    m_cbAnimations->setToolTip(QStringLiteral("切页淡入 / 卡片入场 / 高亮过渡"));
    m_cbAnimations->setStyleSheet(cbStyle);
    m_cbDwm = new QCheckBox(QStringLiteral("DWM 背景（实验）"));
    m_cbDwm->setChecked(fx.dwmBackdrop);
    m_cbDwm->setToolTip(QStringLiteral("Windows 11 22H2+ 启用 Mica/Acrylic 真模糊背景；旧系统自动回退"));
    m_cbDwm->setStyleSheet(cbStyle);
    fxRow->addWidget(m_cbAnimations);
    fxRow->addWidget(m_cbDwm);
    fxRow->addStretch(1);
    root->addLayout(fxRow);

    // 预设联动：选预设 → 自动设置各单项；单项变化 → 预设变「自定义」
    auto applyPresetToControls = [this](UiEffects::Preset p) {
        if (p == UiEffects::Custom)
            return;
        const UiEffects e = UiEffects::fromPreset(p);
        m_updatingPreset = true;
        m_shadowCombo->setCurrentIndex(e.shadowLevel);
        m_glassCombo->setCurrentIndex(e.glassLevel);
        m_cbAnimations->setChecked(e.animations);
        m_cbDwm->setChecked(e.dwmBackdrop);
        m_updatingPreset = false;
    };
    auto syncPresetFromControls = [this] {
        if (m_updatingPreset)
            return;
        const UiEffects cur = uiEffects();
        const int p = (int)cur.preset();
        m_updatingPreset = true;
        m_presetCombo->setCurrentIndex(p >= 0 ? p : 4); // 4 = 自定义
        m_updatingPreset = false;
    };
    connect(m_presetCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
            [applyPresetToControls](int idx) {
                if (idx >= 0 && idx <= 3)
                    applyPresetToControls((UiEffects::Preset)idx);
            });
    connect(m_shadowCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, syncPresetFromControls);
    connect(m_glassCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, syncPresetFromControls);
    connect(m_cbAnimations, &QCheckBox::toggled, this, syncPresetFromControls);
    connect(m_cbDwm, &QCheckBox::toggled, this, syncPresetFromControls);
    // 初始化预设显示
    {
        const UiEffects cur(fx);
        const int p = (int)cur.preset();
        m_presetCombo->setCurrentIndex(p >= 0 ? p : 4);
    }

    // 分隔线
    auto *sep = new QFrame;
    sep->setFrameShape(QFrame::HLine);
    sep->setStyleSheet(QStringLiteral("color: %1;").arg(kColorBorder));
    root->addWidget(sep);

    // ---- 全局快捷键 ----
    auto *title = new QLabel(QStringLiteral("全局快捷键"));
    title->setStyleSheet(QStringLiteral("font-size: 16px; font-weight: 700; color: %1;").arg(kColorFg));
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

QString SettingsDialog::themeId() const
{
    const int i = m_themeCombo->currentIndex();
    if (i >= 0 && i < (int)std::size(kThemes))
        return QLatin1String(kThemes[i].id);
    return QStringLiteral("midnight");
}

UiEffects SettingsDialog::uiEffects() const
{
    UiEffects e;
    e.shadowLevel = m_shadowCombo->currentIndex();
    e.glassLevel = m_glassCombo->currentIndex();
    e.animations = m_cbAnimations->isChecked();
    e.dwmBackdrop = m_cbDwm->isChecked();
    return e;
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
