// inboxsettingspage.h —— 收件箱设置页（设备名 / 服务端管理 / 开机自启 / 回收站）
#pragma once

#include <QWidget>

class QCheckBox;
class QLineEdit;
class QLabel;
class QTabWidget;

// Qt Designer 布局（inboxsettingspage.ui），全局命名空间
namespace Ui { class InboxSettingsPage; }

namespace awqtui {

class LocalStore;
class TrashPage;

class InboxSettingsPage : public QWidget
{
    Q_OBJECT
public:
    explicit InboxSettingsPage(LocalStore *store, QWidget *parent = nullptr);
    ~InboxSettingsPage() override;
    void applyUiScale();

private slots:
    void onDeviceNameChanged();
    void onAutoManageToggled(bool on);
    void onAutostartToggled(bool on);

private:
    // Qt Designer 生成的布局对象（inboxsettingspage.ui -> ui_inboxsettingspage.h）
    Ui::InboxSettingsPage *ui = nullptr;
    void buildUi(LocalStore *store);
    void applyStyle();

    QTabWidget *m_tabs = nullptr;
    QLineEdit *m_deviceName = nullptr;
    QLabel *m_deviceId = nullptr;
    QLabel *m_platform = nullptr;
    QCheckBox *m_autoManage = nullptr;
    QCheckBox *m_autostart = nullptr;
    QLabel *m_status = nullptr;
    TrashPage *m_trash = nullptr;
};

} // namespace awqtui