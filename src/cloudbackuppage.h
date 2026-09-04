// cloudbackuppage.h —— 云备份（冷备）页：WebDAV / S3 低频冷备份
#pragma once

#include <QWidget>

class QCheckBox;
class QComboBox;
class QLabel;
class QLineEdit;
class QPlainTextEdit;
class QPushButton;
class QSpinBox;
class QTabWidget;

namespace awqtui {

class ApiClient;

class CloudBackupPage : public QWidget
{
    Q_OBJECT
public:
    explicit CloudBackupPage(ApiClient *api, QWidget *parent = nullptr);
    ~CloudBackupPage() override;

    void setServerUrl(const QString &url);
    void applyUiScale();
    void refreshStatus();
    void applyTheme();

signals:
    void logMessage(const QString &line);

private slots:
    void onKindChanged(int idx);
    void onTestConnection();
    void onSaveConfig();
    void onBackupNow();
    void onAutoBackupToggled(bool on);

private:
    void buildUi();
    void log(const QString &line);
    void setStatus(const QString &text, bool ok);
    void loadSavedConfig();

    ApiClient *m_api;
    QLineEdit *m_serverEdit;

    QTabWidget *m_tabs;
    // 协议选择 + 连接配置
    QComboBox *m_kind;
    QLineEdit *m_webdavUrl;
    QLineEdit *m_webdavUser;
    QLineEdit *m_webdavPass;
    QLineEdit *m_webdavPath;
    QLineEdit *m_s3Endpoint;
    QLineEdit *m_s3AccessKey;
    QLineEdit *m_s3SecretKey;
    QLineEdit *m_s3Bucket;
    QLineEdit *m_s3Region;
    QLineEdit *m_s3Path;
    QCheckBox *m_s3PathStyle;
    QCheckBox *m_s3Tls;
    QPushButton *m_btnTest;
    QPushButton *m_btnSave;
    QPushButton *m_btnBackupNow;
    QLabel *m_lblStatus;

    // 冷备设置
    QCheckBox *m_chkAutoBackup;
    QSpinBox *m_intervalHours;
    QLabel *m_lblLastBackup;
    QPushButton *m_btnClearSchedule;

    // 日志
    QPlainTextEdit *m_log;
};

} // namespace awqtui
