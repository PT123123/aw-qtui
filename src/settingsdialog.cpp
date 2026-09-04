// settingsdialog.cpp —— 设置界面实现（Tab 分页：外观 / 边缘修复 / 快捷键 / 关于）
#include "settingsdialog.h"

#include "config.h"
#include "theme.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QFocusEvent>
#include <QFormLayout>
#include <QFrame>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QMessageBox>
#include <QPushButton>
#include <QTabWidget>
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

// 平台标识（config::platform）转可读的操作系统名
static QString osDisplayName()
{
    const QString p = platform();
    if (p == QLatin1String("windows"))
        return QStringLiteral("Windows");
    if (p == QLatin1String("darwin"))
        return QStringLiteral("macOS");
    if (p == QLatin1String("linux"))
        return QStringLiteral("Linux");
    return QStringLiteral("Unknown");
}

// 主题下拉框（外观 Tab 内公共构建）
static QComboBox *buildThemeCombo(const QString &currentTheme, int &currentIndex)
{
    auto *combo = new QComboBox;
    combo->setMinimumWidth(260);
    combo->setIconSize(QSize(24, 24));
    currentIndex = 0;
    for (int i = 0; i < (int)std::size(kThemes); ++i) {
        const Theme &t = kThemes[i];
        combo->addItem(makeEmojiIcon(QString::fromUtf8(t.emoji)),
                       QStringLiteral("%1  %2").arg(QString::fromUtf8(t.emoji),
                                                   QString::fromUtf8(t.name)));
        if (QLatin1String(t.id) == currentTheme)
            currentIndex = i;
    }
    combo->setCurrentIndex(currentIndex);
    return combo;
}

SettingsDialog::SettingsDialog(const ShortcutConfig &cfg, const QString &themeId,
                               const UiEffects &fx, const QString &appIconId, QWidget *parent)
    : QDialog(parent)
{
    setWindowTitle(QStringLiteral("设置"));
    setMinimumWidth(520);

    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(16, 12, 16, 12);
    root->setSpacing(10);

    // ---- Tab 容器：外观 / 边缘修复 / 快捷键 / 关于 ----
    auto *tabs = new QTabWidget;
    tabs->setDocumentMode(true);
    tabs->setStyleSheet(QStringLiteral(
        "QTabWidget::pane { border: 1px solid %1; border-radius: 8px; background: %2; top: -1px; }"
        "QTabBar::tab { background: transparent; color: %3; padding: 8px 16px; border: none;"
        "  border-bottom: 2px solid transparent; font-size: 13px; }"
        "QTabBar::tab:hover { color: %4; }"
        "QTabBar::tab:selected { color: %4; border-bottom: 2px solid %5; font-weight: 600; }")
        .arg(kColorBorder, kColorBgElev, kColorFgMuted, kColorFg, kColorAccent));

    const QString keyStyle = QStringLiteral("color: %1;").arg(kColorFgMuted);
    const QString valStyle = QStringLiteral("color: %1;").arg(kColorFg);
    const QString sectionTitleStyle =
        QStringLiteral("font-size: 14px; font-weight: 700; color: %1;").arg(kColorFg);
    const QString hintStyle = QStringLiteral("color: %1; font-size: 11px;").arg(kColorFgMuted);
    const QString rowLabelStyle = QStringLiteral("color: %1;").arg(kColorFg);
    const QString comboStyle = QStringLiteral(
        "QComboBox{background:%1;color:%2;border:1px solid %3;border-radius:6px;padding:4px 8px;min-height:20px;}"
        "QComboBox:hover{border-color:%4;}"
        "QComboBox QAbstractItemView{background:%1;color:%2;border:1px solid %3;selection-background-color:%4;}")
        .arg(kColorBgElev, kColorFg, kColorBorder, kColorAccent);
    const QString cbStyle = QStringLiteral("color: %1;").arg(kColorFg);

    // ================================================================ //
    // Tab 1：外观 —— 主题 + 界面效果
    // ================================================================ //
    auto *appearancePage = new QWidget;
    auto *appearanceLayout = new QVBoxLayout(appearancePage);
    appearanceLayout->setContentsMargins(8, 12, 8, 10);
    appearanceLayout->setSpacing(10);

    // 主题
    auto *themeTitle = new QLabel(QStringLiteral("主题"));
    themeTitle->setStyleSheet(sectionTitleStyle);
    appearanceLayout->addWidget(themeTitle);

    auto *themeRow = new QHBoxLayout;
    themeRow->setSpacing(12);
    auto *themeLabel = new QLabel(QStringLiteral("主题"));
    themeLabel->setStyleSheet(rowLabelStyle);
    int cur = 0;
    m_themeCombo = buildThemeCombo(themeId, cur);
    themeRow->addWidget(themeLabel);
    themeRow->addWidget(m_themeCombo, 1);
    appearanceLayout->addLayout(themeRow);

    m_themeDesc = new QLabel;
    m_themeDesc->setWordWrap(true);
    m_themeDesc->setStyleSheet(hintStyle);
    appearanceLayout->addWidget(m_themeDesc);

    auto updateThemeDesc = [this] {
        const int i = m_themeCombo->currentIndex();
        if (i >= 0 && i < (int)std::size(kThemes))
            m_themeDesc->setText(QString::fromUtf8(kThemes[i].desc));
    };
    connect(m_themeCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
            [updateThemeDesc](int) { updateThemeDesc(); });
    updateThemeDesc();

    // 程序图标（对齐 aw-android-native 可选启动图标：窗口 / 托盘同步生效）
    auto *iconTitle = new QLabel(QStringLiteral("程序图标"));
    iconTitle->setStyleSheet(sectionTitleStyle);
    appearanceLayout->addWidget(iconTitle);

    auto *iconRow = new QHBoxLayout;
    iconRow->setSpacing(12);
    auto *iconLabel = new QLabel(QStringLiteral("图标"));
    iconLabel->setStyleSheet(rowLabelStyle);
    m_iconCombo = new QComboBox;
    m_iconCombo->setMinimumWidth(260);
    m_iconCombo->setIconSize(QSize(24, 24));
    m_iconCombo->setStyleSheet(comboStyle);
    for (int i = 0; i < (int)std::size(kAppIconVariants); ++i) {
        const AppIconVariant &v = kAppIconVariants[i];
        m_iconCombo->addItem(makeAppIcon(&v),
                             QString::fromUtf8(v.name));
        if (QLatin1String(v.id) == appIconId)
            m_iconCombo->setCurrentIndex(i);
    }
    iconRow->addWidget(iconLabel);
    iconRow->addWidget(m_iconCombo, 1);
    appearanceLayout->addLayout(iconRow);

    auto *iconHint = new QLabel(
        QStringLiteral("窗口 / 任务栏 / 托盘图标同步生效，保存后立即应用。配色与 Android 端可选启动图标一致。"));
    iconHint->setWordWrap(true);
    iconHint->setStyleSheet(hintStyle);
    appearanceLayout->addWidget(iconHint);

    // 界面效果
    auto *fxTitle = new QLabel(QStringLiteral("界面效果"));
    fxTitle->setStyleSheet(sectionTitleStyle);
    appearanceLayout->addWidget(fxTitle);

    auto *fxHint = new QLabel(QStringLiteral(
        "玻璃为半透明高光面板，阴影为投影强度；主要作用于收件箱卡片、悬浮按钮与任务页面。"
        "选择预设可一键配置，手动调整单项后自动变为「自定义」。"));
    fxHint->setWordWrap(true);
    fxHint->setStyleSheet(hintStyle);
    appearanceLayout->addWidget(fxHint);

    // 预设行
    auto *presetRow = new QHBoxLayout;
    presetRow->setSpacing(12);
    auto *presetLabel = new QLabel(QStringLiteral("预设"));
    presetLabel->setStyleSheet(rowLabelStyle);
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
    appearanceLayout->addLayout(presetRow);

    // 阴影强度行
    auto *shadowRow = new QHBoxLayout;
    shadowRow->setSpacing(12);
    auto *shadowLabel = new QLabel(QStringLiteral("阴影"));
    shadowLabel->setStyleSheet(rowLabelStyle);
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
    appearanceLayout->addLayout(shadowRow);

    // 玻璃强度行
    auto *glassRow = new QHBoxLayout;
    glassRow->setSpacing(12);
    auto *glassLabel = new QLabel(QStringLiteral("玻璃"));
    glassLabel->setStyleSheet(rowLabelStyle);
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
    appearanceLayout->addLayout(glassRow);

    // 动画 + DWM 行
    auto *fxRow = new QHBoxLayout;
    fxRow->setSpacing(20);
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
    appearanceLayout->addLayout(fxRow);

    appearanceLayout->addStretch(1);
    tabs->addTab(appearancePage, QStringLiteral("外观"));

    // ================================================================ //
    // Tab 2：边缘修复（实验）
    // ================================================================ //
    auto *fixPage = new QWidget;
    auto *fixLayout = new QVBoxLayout(fixPage);
    fixLayout->setContentsMargins(8, 12, 8, 10);
    fixLayout->setSpacing(10);

    auto *fixTitle = new QLabel(QStringLiteral("实验开关"));
    fixTitle->setStyleSheet(sectionTitleStyle);
    fixLayout->addWidget(fixTitle);

    auto *fixHint = new QLabel(QStringLiteral(
        "针对控件边缘 / 阴影观感的修复项，逐项可开关。默认全开（修复后观感）；"
        "关闭某项即恢复旧行为，便于对比哪项更好。"));
    fixHint->setWordWrap(true);
    fixHint->setStyleSheet(hintStyle);
    fixLayout->addWidget(fixHint);

    auto *fixRow1 = new QHBoxLayout;
    fixRow1->setSpacing(20);
    m_cbFixEdge = new QCheckBox(QStringLiteral("低对比描边"));
    m_cbFixEdge->setChecked(fx.fixEdgeLowContrast);
    m_cbFixEdge->setToolTip(QStringLiteral("玻璃/卡片描边改用低对比主题色，替代发白发亮的高亮描边"));
    m_cbFixEdge->setStyleSheet(cbStyle);
    fixRow1->addWidget(m_cbFixEdge);
    m_cbFixGlass = new QCheckBox(QStringLiteral("玻璃防叠影"));
    m_cbFixGlass->setChecked(fx.fixGlassOpaque);
    m_cbFixGlass->setToolTip(QStringLiteral("玻璃底色更实（更高不透明度），避免相邻控件半透明叠影成色带"));
    m_cbFixGlass->setStyleSheet(cbStyle);
    fixRow1->addWidget(m_cbFixGlass);
    fixRow1->addStretch(1);
    fixLayout->addLayout(fixRow1);

    auto *fixRow2 = new QHBoxLayout;
    fixRow2->setSpacing(20);
    m_cbFixZoom = new QCheckBox(QStringLiteral("缩放对齐"));
    m_cbFixZoom->setChecked(fx.fixSnapZoom);
    m_cbFixZoom->setToolTip(QStringLiteral("缩放吸附到干净档位（如 125% / 150%），避免非整数缩放导致边缘发虚"));
    m_cbFixZoom->setStyleSheet(cbStyle);
    fixRow2->addWidget(m_cbFixZoom);
    m_cbFixShadow = new QCheckBox(QStringLiteral("投影随主题"));
    m_cbFixShadow->setChecked(fx.fixShadowAdaptive);
    m_cbFixShadow->setToolTip(QStringLiteral("投影颜色/偏移随主题自适应，避免深色主题下纯黑硬边"));
    m_cbFixShadow->setStyleSheet(cbStyle);
    fixRow2->addWidget(m_cbFixShadow);
    fixRow2->addStretch(1);
    fixLayout->addLayout(fixRow2);

    fixLayout->addStretch(1);
    tabs->addTab(fixPage, QStringLiteral("边缘修复"));

    // ================================================================ //
    // Tab 3：同步设置 —— 同步范围限制说明
    //   收件箱 / 任务 → D1 云同步（Cloudflare D1，高频率变更）
    //   ActivityWatch → 局域网同步（aw-sync-rust LAN，高频率数据）
    //   冷备（长期定期备份）→ S3 / WebDAV（在 SyncPage 云存储页配置）
    // ================================================================ //
    auto *syncPage = new QWidget;
    auto *syncLayout = new QVBoxLayout(syncPage);
    syncLayout->setContentsMargins(8, 12, 8, 10);
    syncLayout->setSpacing(10);

    auto *syncTitle = new QLabel(QStringLiteral("同步范围设置"));
    syncTitle->setStyleSheet(sectionTitleStyle);
    syncLayout->addWidget(syncTitle);

    auto *syncHint = new QLabel(QStringLiteral(
        "设置各数据类型走哪条同步路径。"
        "收件箱和任务建议使用 D1 云同步（高频变更），"
        "ActivityWatch 数据走局域网同步（高频上报），"
        "长期冷备请在「云存储」页配置 S3 / WebDAV（定期备份）。"));
    syncHint->setWordWrap(true);
    syncHint->setStyleSheet(hintStyle);
    syncLayout->addWidget(syncHint);

    auto *syncBox = new QGroupBox;
    auto *syncForm = new QVBoxLayout(syncBox);

    m_cbSyncInbox = new QCheckBox(QStringLiteral("收件箱（Inbox）"));
    m_cbSyncInbox->setChecked(true);
    m_cbSyncInbox->setToolTip(QStringLiteral("通过 D1 云同步变更（高频），局域网同步默认不包含收件箱"));
    m_cbSyncInbox->setStyleSheet(cbStyle);
    syncForm->addWidget(m_cbSyncInbox);

    m_cbSyncActivity = new QCheckBox(QStringLiteral("ActivityWatch（活动记录）"));
    m_cbSyncActivity->setChecked(true);
    m_cbSyncActivity->setToolTip(QStringLiteral("通过局域网同步（aw-sync-rust），高频上报活动数据"));
    m_cbSyncActivity->setStyleSheet(cbStyle);
    syncForm->addWidget(m_cbSyncActivity);

    m_cbSyncTodo = new QCheckBox(QStringLiteral("任务（Todo）"));
    m_cbSyncTodo->setChecked(true);
    m_cbSyncTodo->setToolTip(QStringLiteral("通过 D1 云同步变更（高频），局域网同步默认不包含任务"));
    m_cbSyncTodo->setStyleSheet(cbStyle);
    syncForm->addWidget(m_cbSyncTodo);

    syncLayout->addWidget(syncBox);
    syncLayout->addStretch(1);
    tabs->addTab(syncPage, QStringLiteral("同步"));

    // ================================================================ //
    // Tab 4：快捷键
    // ================================================================ //
    auto *shortcutPage = new QWidget;
    auto *shortcutLayout = new QVBoxLayout(shortcutPage);
    shortcutLayout->setContentsMargins(8, 12, 8, 10);
    shortcutLayout->setSpacing(10);

    auto *title = new QLabel(QStringLiteral("全局快捷键"));
    title->setStyleSheet(sectionTitleStyle);
    shortcutLayout->addWidget(title);

    auto *hint = new QLabel(QStringLiteral(
        "全局快捷键在其它应用处于前台、本窗口最小化时也能触发。建议使用 Alt 或 Ctrl 组合键。"));
    hint->setWordWrap(true);
    hint->setStyleSheet(hintStyle);
    shortcutLayout->addWidget(hint);

    auto *form = new QFormLayout;
    form->setContentsMargins(0, 8, 0, 0);
    form->setHorizontalSpacing(16);
    form->setVerticalSpacing(12);

    m_add = new ShortcutEdit;
    m_add->setSequence(cfg.addNote);
    auto *addLabel = new QLabel(QStringLiteral("添加记录"));
    addLabel->setStyleSheet(rowLabelStyle);
    form->addRow(addLabel, m_add);

    m_inbox = new ShortcutEdit;
    m_inbox->setSequence(cfg.showInbox);
    auto *inboxLabel = new QLabel(QStringLiteral("唤醒并跳转收件箱"));
    inboxLabel->setStyleSheet(rowLabelStyle);
    form->addRow(inboxLabel, m_inbox);

    shortcutLayout->addLayout(form);

    auto *footHint = new QLabel(QStringLiteral(
        "快捷键留空（按 Esc）表示不启用该全局热键。保存后立即生效，冲突（被其它程序占用）会提示。"));
    footHint->setWordWrap(true);
    footHint->setStyleSheet(hintStyle);
    shortcutLayout->addWidget(footHint);

    shortcutLayout->addStretch(1);
    tabs->addTab(shortcutPage, QStringLiteral("快捷键"));

    // ================================================================ //
    // Tab 5：关于（设备信息，只读）
    // ================================================================ //
    auto *aboutPage = new QWidget;
    auto *aboutLayout = new QVBoxLayout(aboutPage);
    aboutLayout->setContentsMargins(8, 12, 8, 10);
    aboutLayout->setSpacing(10);

    auto *deviceTitle = new QLabel(QStringLiteral("设备信息"));
    deviceTitle->setStyleSheet(sectionTitleStyle);
    aboutLayout->addWidget(deviceTitle);

    auto *deviceForm = new QFormLayout;
    deviceForm->setContentsMargins(0, 8, 0, 0);
    deviceForm->setHorizontalSpacing(16);
    deviceForm->setVerticalSpacing(10);

    auto *devNameVal = new QLabel(hostname());
    devNameVal->setStyleSheet(valStyle);
    auto *devNameKey = new QLabel(QStringLiteral("设备名称"));
    devNameKey->setStyleSheet(keyStyle);
    deviceForm->addRow(devNameKey, devNameVal);

    auto *osVal = new QLabel(osDisplayName());
    osVal->setStyleSheet(valStyle);
    auto *osKey = new QLabel(QStringLiteral("操作系统"));
    osKey->setStyleSheet(keyStyle);
    deviceForm->addRow(osKey, osVal);

    auto *idVal = new QLabel(deviceId());
    idVal->setStyleSheet(valStyle);
    idVal->setTextInteractionFlags(Qt::TextSelectableByMouse);
    auto *idKey = new QLabel(QStringLiteral("设备 ID"));
    idKey->setStyleSheet(keyStyle);
    deviceForm->addRow(idKey, idVal);

    aboutLayout->addLayout(deviceForm);
    aboutLayout->addStretch(1);
    tabs->addTab(aboutPage, QStringLiteral("关于"));

    root->addWidget(tabs);

    // ---- 预设联动：选预设 → 自动设置各单项；单项变化 → 预设变「自定义」----
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
    // 修复开关也参与预设联动：改动任一项 → 预设显示为「自定义」
    connect(m_cbFixEdge, &QCheckBox::toggled, this, syncPresetFromControls);
    connect(m_cbFixGlass, &QCheckBox::toggled, this, syncPresetFromControls);
    connect(m_cbFixZoom, &QCheckBox::toggled, this, syncPresetFromControls);
    connect(m_cbFixShadow, &QCheckBox::toggled, this, syncPresetFromControls);
    // 初始化预设显示
    {
        const UiEffects cur(fx);
        const int p = (int)cur.preset();
        m_presetCombo->setCurrentIndex(p >= 0 ? p : 4);
    }

    // ---- 底部按钮 ----
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

QString SettingsDialog::appIconId() const
{
    const int i = m_iconCombo->currentIndex();
    if (i >= 0 && i < (int)std::size(kAppIconVariants))
        return QLatin1String(kAppIconVariants[i].id);
    return QStringLiteral("amber");
}

UiEffects SettingsDialog::uiEffects() const
{
    UiEffects e;
    e.shadowLevel = m_shadowCombo->currentIndex();
    e.glassLevel = m_glassCombo->currentIndex();
    e.animations = m_cbAnimations->isChecked();
    e.dwmBackdrop = m_cbDwm->isChecked();
    e.fixEdgeLowContrast = m_cbFixEdge->isChecked();
    e.fixGlassOpaque = m_cbFixGlass->isChecked();
    e.fixSnapZoom = m_cbFixZoom->isChecked();
    e.fixShadowAdaptive = m_cbFixShadow->isChecked();
    return e;
}

SyncSettingsConfig SettingsDialog::syncSettings() const
{
    SyncSettingsConfig c;
    c.syncInbox = m_cbSyncInbox->isChecked();
    c.syncActivity = m_cbSyncActivity->isChecked();
    c.syncTodo = m_cbSyncTodo->isChecked();
    return c;
}

void SettingsDialog::setSyncSettings(const SyncSettingsConfig &s)
{
    m_cbSyncInbox->setChecked(s.syncInbox);
    m_cbSyncActivity->setChecked(s.syncActivity);
    m_cbSyncTodo->setChecked(s.syncTodo);
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
