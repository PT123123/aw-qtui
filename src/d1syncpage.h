// d1syncpage.h —— Cloudflare D1 云同步配置页（aw-sync-rust /api/0/sync/d1/*）
#pragma once

#include <QWidget>

class QCheckBox;
class QLabel;
class QLineEdit;
class QPlainTextEdit;
class QPushButton;
class QSpinBox;

// Qt Designer 布局（d1syncpage.ui），全局命名空间
namespace Ui { class D1SyncPage; }

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
    void onFullSync();
    void onResetCheckpoint();
    void onRefreshD1Logs();

private:
    // Qt Designer 生成的布局对象（d1syncpage.ui -> ui_d1syncpage.h）
    Ui::D1SyncPage *ui = nullptr;
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
    QPushButton *m_btnFullSync;
    QPushButton *m_btnResetCheckpoint;
    QPushButton *m_btnRefreshD1Logs;

    QLabel *m_lblStatus;
    QPlainTextEdit *m_log;

    // 上次状态轮询时间，避免每次进页面都立刻打一次
    qint64 m_lastStatusAtMs = 0;
};

} // namespace awqtui