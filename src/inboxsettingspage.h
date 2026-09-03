// inboxsettingspage.h —— 收件箱设置页（设备名 / 服务端管理 / 开机自启）
#pragma once

#include <QWidget>

class QCheckBox;
class QLineEdit;
class QLabel;

namespace awqtui {

class InboxSettingsPage : public QWidget
{
    Q_OBJECT
public:
    explicit InboxSettingsPage(QWidget *parent = nullptr);
    void applyUiScale();

private slots:
    void onDeviceNameChanged();
    void onAutoManageToggled(bool on);
    void onAutostartToggled(bool on);

private:
    void buildUi();
    void applyStyle();

    QLineEdit *m_deviceName = nullptr;
    QLabel *m_deviceId = nullptr;
    QLabel *m_platform = nullptr;
    QCheckBox *m_autoManage = nullptr;
    QCheckBox *m_autostart = nullptr;
    QLabel *m_status = nullptr;
};

} // namespace awqtui
