// d1syncpage.h —— Cloudflare D1 云同步配置页（aw-sync-rust /api/0/sync/d1/*）
#pragma once

#include <QWidget>

class QCheckBox;
class QLabel;
class QLineEdit;
class QPlainTextEdit;
class QPushButton;
class QSpinBox;

namespace awqtui {

class ApiClient;

class D1SyncPage : public QWidget
{
    Q_OBJECT
public:
    explicit D1SyncPage(ApiClient *api, QWidget *parent = nullptr);
    ~D1SyncPage() override;

    void applyUiScale();
    void refreshStatus();

private slots:
    void onSave();
    void onTest();
    void onSyncNow();

private:
    void buildUi();
    void log(const QString &line);
    void setStatus(const QString &text, bool ok);

    ApiClient *m_api;

    QLineEdit *m_accountId;
    QLineEdit *m_databaseId;
    QLineEdit *m_apiToken;
    QCheckBox *m_chkEnabled;
    QSpinBox *m_interval;
    QPushButton *m_btnSave;
    QPushButton *m_btnTest;
    QPushButton *m_btnSyncNow;

    QLabel *m_lblStatus;
    QPlainTextEdit *m_log;

    // 上次状态轮询时间，避免每次进页面都立刻打一次
    qint64 m_lastStatusAtMs = 0;
};

} // namespace awqtui