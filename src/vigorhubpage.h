// vigorhubpage.h —— 「同步 → VigorHub」标签页：VigorHubRelay 的配置与状态视图
//
// 推送引擎（VigorHubRelay）随主窗口常驻，本页只是它的视图：改配置、
// 看最近推送结果、手动「立即同步」。页可淘汰，引擎不受影响。
#pragma once

#include <QWidget>

class QCheckBox;
class QComboBox;
class QLabel;
class QLineEdit;
class QPlainTextEdit;
class QPushButton;

namespace awqtui {

class VigorHubRelay;
class StatusBadge;

class VigorHubPage : public QWidget
{
    Q_OBJECT
public:
    explicit VigorHubPage(VigorHubRelay *relay, QWidget *parent = nullptr);

private slots:
    void onSaveConfig();
    void onSyncNow();
    void onSyncStarted();
    void onSyncFinished(bool ok, const QString &msg);
    void refreshPeerStatus();

private:
    void buildUi();
    void loadConfigIntoUi();
    void refreshStatus();
    void log(const QString &line);

    VigorHubRelay *m_relay = nullptr;

    QCheckBox *m_chkEnabled = nullptr;
    QLineEdit *m_editAddress = nullptr;
    QComboBox *m_cmbInterval = nullptr;
    QLineEdit *m_editAlias = nullptr;   // 组播宣告里的显示名
    QLineEdit *m_editSource = nullptr;  // 使用聚合的来源标识（多台 AW 主机必须各不相同）
    QPushButton *m_btnSave = nullptr;
    QPushButton *m_btnSyncNow = nullptr;
    StatusBadge *m_badge = nullptr;
    QLabel *m_lblStatus = nullptr;   // 最近一次推送结果
    QLabel *m_lblPeer = nullptr;     // 伴随服务端口 + 最近被拉取时间（底栏读数）
    QPlainTextEdit *m_log = nullptr; // 本页日志（引擎 logLine 转发到此）
};

} // namespace awqtui
