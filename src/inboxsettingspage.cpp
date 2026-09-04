// inboxsettingspage.cpp —— 收件箱设置页实现（含回收站子标签）
#include "inboxsettingspage.h"

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

void InboxSettingsPage::buildUi(LocalStore *store)
{
    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(si(20), si(16), si(20), si(16));
    root->setSpacing(si(12));

    auto *title = new QLabel(QStringLiteral("收件箱设置"));
    title->setObjectName(QStringLiteral("Heading"));
    title->setStyleSheet(QStringLiteral("font-size: %1; font-weight: 700; color: %2;")
                             .arg(sp(16), QString::fromLatin1(kColorFg)));
    root->addWidget(title);

    // 把「通用」表单与「回收站」收纳进同一页面的子标签，
    // 侧边栏只保留「收件箱设置」一个入口
    m_tabs = new QTabWidget;
    m_tabs->setDocumentMode(true);

    // ---- 子标签 1：通用 ----
    auto *general = new QWidget;
    auto *gl = new QVBoxLayout(general);
    gl->setContentsMargins(si(16), si(16), si(16), si(16));
    gl->setSpacing(si(10));

    // 设备名
    auto *nameRow = new QHBoxLayout;
    auto *nameLabel = new QLabel(QStringLiteral("设备名"));
    nameLabel->setMinimumWidth(si(80));
    nameLabel->setStyleSheet(QStringLiteral("color: %1; font-size: %2;")
                                 .arg(QString::fromLatin1(kColorFgMuted), sp(12)));
    nameRow->addWidget(nameLabel);
    m_deviceName = new QLineEdit;
    m_deviceName->setPlaceholderText(hostname());
    m_deviceName->setText(loadDeviceName());
    m_deviceName->setClearButtonEnabled(true);
    nameRow->addWidget(m_deviceName, 1);
    gl->addLayout(nameRow);

    // 设备 ID（只读）
    auto *idRow = new QHBoxLayout;
    auto *idLabel = new QLabel(QStringLiteral("设备 ID"));
    idLabel->setMinimumWidth(si(80));
    idLabel->setStyleSheet(QStringLiteral("color: %1; font-size: %2;")
                               .arg(QString::fromLatin1(kColorFgMuted), sp(12)));
    idRow->addWidget(idLabel);
    m_deviceId = new QLabel(deviceId());
    m_deviceId->setStyleSheet(QStringLiteral("color: %1; font-size: %2;")
                                  .arg(QString::fromLatin1(kColorFg), sp(12)));
    idRow->addWidget(m_deviceId, 1);
    gl->addLayout(idRow);

    // 平台（只读）
    auto *platRow = new QHBoxLayout;
    auto *platLabel = new QLabel(QStringLiteral("平台"));
    platLabel->setMinimumWidth(si(80));
    platLabel->setStyleSheet(QStringLiteral("color: %1; font-size: %2;")
                                 .arg(QString::fromLatin1(kColorFgMuted), sp(12)));
    platRow->addWidget(platLabel);
    m_platform = new QLabel(platform());
    m_platform->setStyleSheet(QStringLiteral("color: %1; font-size: %2;")
                                  .arg(QString::fromLatin1(kColorFg), sp(12)));
    platRow->addWidget(m_platform, 1);
    gl->addLayout(platRow);

    gl->addSpacing(si(8));

    // 服务端管理
    m_autoManage = new QCheckBox(QStringLiteral("自动管理本地服务端（探测 / 拉起 / 看护）"));
    m_autoManage->setChecked(loadServerAutoManage());
    m_autoManage->setCursor(Qt::PointingHandCursor);
    gl->addWidget(m_autoManage);

    m_autostart = new QCheckBox(QStringLiteral("开机自启（Task Scheduler ONLOGON）"));
    m_autostart->setChecked(loadServerAutostart());
    m_autostart->setCursor(Qt::PointingHandCursor);
    gl->addWidget(m_autostart);

    m_status = new QLabel;
    m_status->setStyleSheet(QStringLiteral("color: %1; font-size: %2;")
                                .arg(QString::fromLatin1(kColorOk), sp(11)));
    gl->addWidget(m_status);

    gl->addStretch(1);

    m_tabs->addTab(general, QStringLiteral("⚙ 通用"));

    // ---- 子标签 2：回收站 ----
    m_trash = new TrashPage(store);
    m_tabs->addTab(m_trash, QStringLiteral("🗑 回收站"));

    root->addWidget(m_tabs, 1);

    connect(m_deviceName, &QLineEdit::editingFinished, this, &InboxSettingsPage::onDeviceNameChanged);
    connect(m_autoManage, &QCheckBox::toggled, this, &InboxSettingsPage::onAutoManageToggled);
    connect(m_autostart, &QCheckBox::toggled, this, &InboxSettingsPage::onAutostartToggled);

    applyStyle();
}

void InboxSettingsPage::applyStyle()
{
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