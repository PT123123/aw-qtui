// syncdetailspage.h —— 同步详情独立页（日志筛选 / 分页 / 明细展开 / 回收站）
#pragma once

#include <QWidget>

#include "models.h"

class QLabel;
class QLineEdit;
class QPlainTextEdit;
class QPushButton;
class QTableWidget;
class QTabWidget;
class QComboBox;
class QSpinBox;

namespace awqtui {

class ApiClient;

class SyncDetailsPage : public QWidget
{
    Q_OBJECT
public:
    explicit SyncDetailsPage(ApiClient *api, QWidget *parent = nullptr);
    ~SyncDetailsPage() override;

    void setServerUrl(const QString &url);
    void refreshLogs();
    void refreshTrash();
    void applyTheme();

signals:
    void logMessage(const QString &line);
    void backToSync();

private slots:
    void onRefreshLogs();
    void onClearLogs();
    void onLogFiltersChanged();
    void onLogRowExpanded(int row, int column);
    void onRefreshTrash();
    void onClearTrash();
    void onRestoreTrashRow();
    void onDeleteTrashRow();
    void onBack();

private:
    void buildUi();
    void log(const QString &line);
    void populateLogTable(const QJsonArray &logs, qint64 total, const QString &kindFilter);
    void populateTrashTable(const QJsonArray &arr);

    ApiClient *m_api;

    // 服务端
    QLineEdit *m_serverEdit;
    QLabel *m_statusLabel;
    QPushButton *m_backBtn;

    QTabWidget *m_tabs;

    // 日志 Tab
    QTableWidget *m_logTable;
    QComboBox *m_filterDirection;
    QComboBox *m_filterProtocol;
    QComboBox *m_filterEvent;
    QComboBox *m_filterKind;
    QSpinBox *m_pageSpin;
    QPushButton *m_btnPrevPage;
    QPushButton *m_btnNextPage;
    QPushButton *m_btnRefreshLog;
    QPushButton *m_btnClearLogs;
    QPlainTextEdit *m_detailPanel;
    QLabel *m_logCountLabel;
    int m_offset = 0;
    int m_limit = 50;
    qint64 m_totalLogs = 0;
    QVector<SyncLogEntry> m_logEntries;

    // 回收站 Tab
    QTableWidget *m_trashTable;
    QPushButton *m_btnRefreshTrash;
    QPushButton *m_btnClearTrash;
    QPushButton *m_btnRestoreTrash;
    QPushButton *m_btnDeleteTrash;
};

} // namespace awqtui
