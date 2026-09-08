// inboxsettingspage.cpp —— 收件箱设置页实现（含回收站子标签）
#include "inboxsettingspage.h"
#include "ui_inboxsettingspage.h"

#include "appsettings.h"
#include "config.h"
#include "localstore.h"
#include "theme.h"
#include "trashpage.h"

#include <QCheckBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QTabWidget>
#include <QVBoxLayout>

namespace awqtui {

InboxSettingsPage::InboxSettingsPage(LocalStore *store, QWidget *parent)
    : QWidget(parent)
{
    buildUi(store);
}

InboxSettingsPage::~InboxSettingsPage()
{
    delete ui;
}

void InboxSettingsPage::buildUi(LocalStore *store)
{
    // 静态布局来自 Qt Designer（inboxsettingspage.ui -> ui_inboxsettingspage.h），
    // .ui 中边距/间距为基准值，si() 缩放几何在此重设
    ui = new Ui::InboxSettingsPage;
    ui->setupUi(this);

    ui->rootLayout->setContentsMargins(si(20), si(16), si(20), si(16));
    ui->rootLayout->setSpacing(si(12));
    ui->generalLay->setContentsMargins(si(16), si(16), si(16), si(16));
    ui->generalLay->setSpacing(si(10));
    ui->gap->changeSize(0, si(8), QSizePolicy::Fixed);
    for (QLabel *l : {ui->nameLabel, ui->idLabel, ui->platLabel})
        l->setMinimumWidth(si(80));

    // ── 成员别名 + 运行时取值 ──
    m_tabs = ui->tabs;
    m_deviceName = ui->deviceName;
    m_deviceName->setPlaceholderText(hostname());
    m_deviceName->setText(loadDeviceName());
    m_deviceId = ui->deviceId;
    m_deviceId->setText(deviceId());
    m_platform = ui->platformLabel;
    m_platform->setText(platform());
    m_autoManage = ui->autoManage;
    m_autoManage->setChecked(loadServerAutoManage());
    m_autoManage->setCursor(Qt::PointingHandCursor);
    m_autostart = ui->autostart;
    m_autostart->setChecked(loadServerAutostart());
    m_autostart->setCursor(Qt::PointingHandCursor);
    m_status = ui->status;

    // 回收站子页构造需要 LocalStore*，无法由 uic 创建：.ui 中放容器，运行时装入
    m_trash = new TrashPage(store);
    ui->trashHostLay->addWidget(m_trash);

    connect(m_deviceName, &QLineEdit::editingFinished, this, &InboxSettingsPage::onDeviceNameChanged);
    connect(m_autoManage, &QCheckBox::toggled, this, &InboxSettingsPage::onAutoManageToggled);
    connect(m_autostart, &QCheckBox::toggled, this, &InboxSettingsPage::onAutostartToggled);

    applyStyle();
}

void InboxSettingsPage::applyStyle()
{
    // 标题/表单标签/状态用 sp() 动态字号，随缩放重设（控件本体在 .ui 中）
    ui->title->setStyleSheet(QStringLiteral("font-size: %1; font-weight: 700; color: %2;")
                                 .arg(sp(16), QString::fromLatin1(kColorFg)));
    const QString formLabel = QStringLiteral("color: %1; font-size: %2;")
                                  .arg(QString::fromLatin1(kColorFgMuted), sp(12));
    ui->nameLabel->setStyleSheet(formLabel);
    ui->idLabel->setStyleSheet(formLabel);
    ui->platLabel->setStyleSheet(formLabel);
    const QString valueText = QStringLiteral("color: %1; font-size: %2;")
                                  .arg(QString::fromLatin1(kColorFg), sp(12));
    ui->deviceId->setStyleSheet(valueText);
    ui->platformLabel->setStyleSheet(valueText);
    ui->status->setStyleSheet(QStringLiteral("color: %1; font-size: %2;")
                                  .arg(QString::fromLatin1(kColorOk), sp(11)));
    if (!m_tabs)
        return;
    m_tabs->setStyleSheet(QStringLiteral(
        "QTabWidget::pane { border: 1px solid %1; border-radius: %5px; background: %2; top: -1px; }"
        "QTabBar::tab { padding: %6px %7px; margin-right: %8px; color: %3;"
        "  border: 1px solid transparent; border-radius: %5px; font-size: %9px; }"
        "QTabBar::tab:selected { color: %4; border: 1px solid %1; background: %10; }"
        "QTabBar::tab:hover:!selected { color: %4; }")
        .arg(kColorBorder)
        .arg(kColorBgElev)
        .arg(kColorFgMuted)
        .arg(kColorAccent)
        .arg(si(8))
        .arg(si(6))
        .arg(si(12))
        .arg(si(4))
        .arg(si(13))
        .arg(kColorBgElev2));
}

void InboxSettingsPage::applyUiScale()
{
    applyStyle();
    if (m_trash)
        m_trash->applyUiScale();
}

void InboxSettingsPage::onDeviceNameChanged()
{
    saveDeviceName(m_deviceName->text().trimmed());
    m_status->setText(QStringLiteral("✓ 设备名已保存"));
}

void InboxSettingsPage::onAutoManageToggled(bool on)
{
    saveServerAutoManage(on);
    m_status->setText(on ? QStringLiteral("✓ 已启用自动管理服务端") : QStringLiteral("✓ 已关闭自动管理"));
}

void InboxSettingsPage::onAutostartToggled(bool on)
{
    saveServerAutostart(on);
    m_status->setText(on ? QStringLiteral("✓ 已启用开机自启") : QStringLiteral("✓ 已关闭开机自启"));
}

} // namespace awqtui