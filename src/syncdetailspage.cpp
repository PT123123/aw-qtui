// syncdetailspage.cpp —— 同步详情独立页
#include "syncdetailspage.h"

#include "apiclient.h"
#include "config.h"
#include "theme.h"
#include "widgets.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDate>
#include <QDateTime>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSpinBox>
#include <QTableWidget>
#include <QTabWidget>
#include <QVBoxLayout>

namespace awqtui {

SyncDetailsPage::SyncDetailsPage(ApiClient *api, QWidget *parent)
    : QWidget(parent), m_api(api)
{
    buildUi();
    applyTheme();
}

SyncDetailsPage::~SyncDetailsPage() = default;

void SyncDetailsPage::applyTheme()
{
    setStyleSheet(QStringLiteral(R"(
        QGroupBox {
            background: %1;
            border: 1px solid %2;
            border-radius: 8px;
            margin-top: 12px;
            padding-top: 10px;
        }
        QGroupBox::title {
            subcontrol-origin: margin;
            left: 10px;
            padding: 0 6px;
            color: %3;
        }
        QLabel#StatusLabel { color: %3; font-size: 12px; }
        QPushButton#ToolBtn {
            background: %4; border: 1px solid %2; border-radius: 6px;
            padding: 5px 12px; color: %5; font-size: 12px;
        }
        QPushButton#ToolBtn:hover { background: %6; border-color: %7; }
        QPushButton#NavBtn {
            background: transparent; border: 1px solid %2; border-radius: 4px;
            padding: 4px 10px; color: %5; font-size: 12px;
        }
        QPushButton#NavBtn:hover { background: %4; border-color: %7; }
    )")
                                  .arg(kColorBgElev, kColorBorder, kColorFgMuted,
                                       kColorBgElev2, kColorFgSoft, kColorBgElev2, kColorAccent));
}

void SyncDetailsPage::buildUi()
{
    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(16, 16, 16, 16);
    root->setSpacing(12);

    // ── 顶部工具栏 ──
    auto *toolbar = new QHBoxLayout;
    toolbar->setSpacing(8);

    m_backBtn = new QPushButton(QStringLiteral("← 返回同步"));
    m_backBtn->setObjectName(QStringLiteral("NavBtn"));
    connect(m_backBtn, &QPushButton::clicked, this, &SyncDetailsPage::onBack);
    toolbar->addWidget(m_backBtn);

    toolbar->addSpacing(16);

    m_serverEdit = new QLineEdit(m_api ? m_api->baseUrl() : kDefaultServerUrl);
    m_serverEdit->setFixedWidth(280);
    toolbar->addWidget(new QLabel(QStringLiteral("服务端")));
    toolbar->addWidget(m_serverEdit);

    auto *btnApply = new QPushButton(QStringLiteral("应用"));
    btnApply->setObjectName(QStringLiteral("ToolBtn"));
    connect(btnApply, &QPushButton::clicked, this, [this] {
        setServerUrl(m_serverEdit->text().trimmed());
    });
    toolbar->addWidget(btnApply);

    toolbar->addSpacing(12);

    m_statusLabel = new QLabel(QStringLiteral("—"));
    m_statusLabel->setObjectName(QStringLiteral("StatusLabel"));
    toolbar->addWidget(m_statusLabel);

    toolbar->addStretch(1);

    root->addLayout(toolbar);

    // ── 标签页 ──
    m_tabs = new QTabWidget;

    // ── 日志 Tab ──
    auto *logTab = new QWidget;
    auto *logLay = new QVBoxLayout(logTab);
    logLay->setContentsMargins(0, 0, 0, 0);
    logLay->setSpacing(8);

    // 筛选栏
    auto *filterBar = new QHBoxLayout;
    filterBar->setSpacing(8);

    filterBar->addWidget(new QLabel(QStringLiteral("方向")));
    m_filterDirection = new QComboBox;
    m_filterDirection->addItem(QStringLiteral("全部"), QString());
    m_filterDirection->addItem(QStringLiteral("发送"), QStringLiteral("send"));
    m_filterDirection->addItem(QStringLiteral("接收"), QStringLiteral("recv"));
    filterBar->addWidget(m_filterDirection);

    filterBar->addWidget(new QLabel(QStringLiteral("协议")));
    m_filterProtocol = new QComboBox;
    m_filterProtocol->addItem(QStringLiteral("全部"), QString());
    m_filterProtocol->addItem(QStringLiteral("LAN"), QStringLiteral("lan"));
    m_filterProtocol->addItem(QStringLiteral("WebDAV"), QStringLiteral("webdav"));
    m_filterProtocol->addItem(QStringLiteral("S3"), QStringLiteral("s3"));
    filterBar->addWidget(m_filterProtocol);

    filterBar->addWidget(new QLabel(QStringLiteral("事件")));
    m_filterEvent = new QComboBox;
    m_filterEvent->addItem(QStringLiteral("全部"), QString());
    m_filterEvent->addItem(QStringLiteral("sync"), QStringLiteral("sync"));
    m_filterEvent->addItem(QStringLiteral("pair"), QStringLiteral("pair"));
    m_filterEvent->addItem(QStringLiteral("error"), QStringLiteral("error"));
    filterBar->addWidget(m_filterEvent);

    filterBar->addStretch(1);

    m_logCountLabel = new QLabel(QStringLiteral("0 条"));
    m_logCountLabel->setObjectName(QStringLiteral("StatusLabel"));
    filterBar->addWidget(m_logCountLabel);

    connect(m_filterDirection, QOverload<int>::of(&QComboBox::activated), this, &SyncDetailsPage::onLogFiltersChanged);
    connect(m_filterProtocol, QOverload<int>::of(&QComboBox::activated), this, &SyncDetailsPage::onLogFiltersChanged);
    connect(m_filterEvent, QOverload<int>::of(&QComboBox::activated), this, &SyncDetailsPage::onLogFiltersChanged);

    logLay->addLayout(filterBar);

    // 日志表格
    m_logTable = new QTableWidget(0, 6);
    m_logTable->setHorizontalHeaderLabels({QStringLiteral("时间"), QStringLiteral("方向"),
                                           QStringLiteral("协议"), QStringLiteral("事件"),
                                           QStringLiteral("状态"), QStringLiteral("消息")});
    m_logTable->verticalHeader()->setVisible(false);
    m_logTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_logTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_logTable->horizontalHeader()->setStretchLastSection(true);
    m_logTable->setColumnWidth(0, 140);
    m_logTable->setColumnWidth(1, 60);
    m_logTable->setColumnWidth(2, 70);
    m_logTable->setColumnWidth(3, 70);
    m_logTable->setColumnWidth(4, 70);
    connect(m_logTable, &QTableWidget::cellDoubleClicked, this, &SyncDetailsPage::onLogRowExpanded);
    logLay->addWidget(m_logTable, 1);

    // 分页栏
    auto *pageBar = new QHBoxLayout;
    m_btnPrevPage = new QPushButton(QStringLiteral("◀ 上一页"));
    m_btnPrevPage->setObjectName(QStringLiteral("ToolBtn"));
    m_btnPrevPage->setEnabled(false);
    connect(m_btnPrevPage, &QPushButton::clicked, this, [this] {
        m_offset = qMax(0, m_offset - m_limit);
        refreshLogs();
    });
    pageBar->addWidget(m_btnPrevPage);

    m_pageSpin = new QSpinBox;
    m_pageSpin->setMinimum(1);
    m_pageSpin->setMaximum(1);
    m_pageSpin->setSuffix(QStringLiteral(" / 1"));
    connect(m_pageSpin, QOverload<int>::of(&QSpinBox::valueChanged), this, [this](int page) {
        m_offset = (page - 1) * m_limit;
        refreshLogs();
    });
    pageBar->addWidget(m_pageSpin);

    m_btnNextPage = new QPushButton(QStringLiteral("下一页 ▶"));
    m_btnNextPage->setObjectName(QStringLiteral("ToolBtn"));
    m_btnNextPage->setEnabled(false);
    connect(m_btnNextPage, &QPushButton::clicked, this, [this] {
        m_offset += m_limit;
        refreshLogs();
    });
    pageBar->addWidget(m_btnNextPage);

    pageBar->addStretch(1);

    m_btnRefreshLog = new QPushButton(QStringLiteral("↻ 刷新"));
    m_btnRefreshLog->setObjectName(QStringLiteral("ToolBtn"));
    connect(m_btnRefreshLog, &QPushButton::clicked, this, &SyncDetailsPage::onRefreshLogs);
    pageBar->addWidget(m_btnRefreshLog);

    m_btnClearLogs = new QPushButton(QStringLiteral("🗑 清空"));
    m_btnClearLogs->setObjectName(QStringLiteral("ToolBtn"));
    connect(m_btnClearLogs, &QPushButton::clicked, this, &SyncDetailsPage::onClearLogs);
    pageBar->addWidget(m_btnClearLogs);

    logLay->addLayout(pageBar);

    // 明细面板
    auto *detailBox = new QGroupBox(QStringLiteral("传输明细（双击日志行展开）"));
    auto *dl = new QVBoxLayout(detailBox);
    m_detailPanel = new QPlainTextEdit;
    m_detailPanel->setReadOnly(true);
    m_detailPanel->setMaximumHeight(200);
    m_detailPanel->setPlaceholderText(QStringLiteral("双击上方日志行查看传输明细…"));
    dl->addWidget(m_detailPanel);
    logLay->addWidget(detailBox);

    m_tabs->addTab(logTab, QStringLiteral("同步日志"));

    // ── 回收站 Tab ──
    auto *trashTab = new QWidget;
    auto *trashLay = new QVBoxLayout(trashTab);
    trashLay->setContentsMargins(0, 0, 0, 0);
    trashLay->setSpacing(8);

    auto *trashBar = new QHBoxLayout;
    m_btnRefreshTrash = new QPushButton(QStringLiteral("↻ 刷新"));
    m_btnRefreshTrash->setObjectName(QStringLiteral("ToolBtn"));
    connect(m_btnRefreshTrash, &QPushButton::clicked, this, &SyncDetailsPage::onRefreshTrash);
    trashBar->addWidget(m_btnRefreshTrash);

    m_btnClearTrash = new QPushButton(QStringLiteral("🗑 清空"));
    m_btnClearTrash->setObjectName(QStringLiteral("ToolBtn"));
    connect(m_btnClearTrash, &QPushButton::clicked, this, &SyncDetailsPage::onClearTrash);
    trashBar->addWidget(m_btnClearTrash);

    trashBar->addStretch(1);

    m_btnRestoreTrash = new QPushButton(QStringLiteral("↩ 恢复"));
    m_btnRestoreTrash->setObjectName(QStringLiteral("ToolBtn"));
    connect(m_btnRestoreTrash, &QPushButton::clicked, this, &SyncDetailsPage::onRestoreTrashRow);
    trashBar->addWidget(m_btnRestoreTrash);

    m_btnDeleteTrash = new QPushButton(QStringLiteral("✗ 永久删除"));
    m_btnDeleteTrash->setObjectName(QStringLiteral("ToolBtn"));
    connect(m_btnDeleteTrash, &QPushButton::clicked, this, &SyncDetailsPage::onDeleteTrashRow);
    trashBar->addWidget(m_btnDeleteTrash);

    trashLay->addLayout(trashBar);

    m_trashTable = new QTableWidget(0, 6);
    m_trashTable->setHorizontalHeaderLabels({QStringLiteral("ID"), QStringLiteral("类型"),
                                              QStringLiteral("逻辑键"), QStringLiteral("原因"),
                                              QStringLiteral("来源设备"), QStringLiteral("归档时间")});
    m_trashTable->verticalHeader()->setVisible(false);
    m_trashTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_trashTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_trashTable->horizontalHeader()->setStretchLastSection(true);
    trashLay->addWidget(m_trashTable, 1);

    m_tabs->addTab(trashTab, QStringLiteral("回收站"));

    root->addWidget(m_tabs, 1);
}

void SyncDetailsPage::setServerUrl(const QString &url)
{
    if (m_api) {
        m_api->setBaseUrl(url);
        m_statusLabel->setText(QStringLiteral("已切换到: %1").arg(url));
        refreshLogs();
        refreshTrash();
    }
}

void SyncDetailsPage::refreshLogs()
{
    if (!m_api)
        return;
    QString direction = m_filterDirection->currentData().toString();
    QString protocol = m_filterProtocol->currentData().toString();
    QString eventType = m_filterEvent->currentData().toString();

    QNetworkReply *r = m_api->getSyncLogs(direction, protocol, eventType, m_limit, m_offset);
    connect(r, &QNetworkReply::finished, this, [this, r] {
        QJsonDocument doc;
        QString err;
        if (!ApiClient::parseReply(r, &doc, &err)) {
            log(QStringLiteral("获取日志失败：%1").arg(err));
            return;
        }
        const auto obj = doc.object();
        const auto arr = obj.value(QStringLiteral("logs")).toArray();
        m_totalLogs = obj.value(QStringLiteral("total")).toVariant().toLongLong();
        populateLogTable(arr, m_totalLogs);
    });
}

void SyncDetailsPage::refreshTrash()
{
    if (!m_api)
        return;
    QNetworkReply *r = m_api->getTrash();
    connect(r, &QNetworkReply::finished, this, [this, r] {
        QJsonDocument doc;
        QString err;
        if (!ApiClient::parseReply(r, &doc, &err)) {
            log(QStringLiteral("获取回收站失败：%1").arg(err));
            return;
        }
        const auto arr = doc.array();
        populateTrashTable(arr);
        log(QStringLiteral("回收站 %1 条").arg(arr.size()));
    });
}

void SyncDetailsPage::populateLogTable(const QJsonArray &logs, qint64 total)
{
    m_logTable->setRowCount(0);
    m_logEntries.clear();

    int row = 0;
    for (const auto &v : logs) {
        const SyncLogEntry e = SyncLogEntry::fromJson(v.toObject());
        m_logEntries.append(e);

        m_logTable->insertRow(row);
        m_logTable->setItem(row, 0, new QTableWidgetItem(formatLocal(e.timestamp)));
        m_logTable->setItem(row, 1, new QTableWidgetItem(e.direction));
        m_logTable->setItem(row, 2, new QTableWidgetItem(e.protocol));
        m_logTable->setItem(row, 3, new QTableWidgetItem(e.eventType));
        m_logTable->setItem(row, 4, new QTableWidgetItem(e.status));
        m_logTable->setItem(row, 5, new QTableWidgetItem(e.message));

        // 错误行标红
        if (e.status == QStringLiteral("error")) {
            for (int c = 0; c < 6; ++c) {
                if (auto *it = m_logTable->item(row, c))
                    it->setForeground(QColor(kColorDanger));
            }
        }
        ++row;
    }

    // 更新分页
    int totalPages = (total + m_limit - 1) / m_limit;
    if (totalPages < 1) totalPages = 1;
    m_pageSpin->setMaximum(totalPages);
    m_pageSpin->setSuffix(QStringLiteral(" / %1").arg(totalPages));
    m_pageSpin->setValue(m_offset / m_limit + 1);
    m_btnPrevPage->setEnabled(m_offset > 0);
    m_btnNextPage->setEnabled(m_offset + m_limit < total);

    m_logCountLabel->setText(QStringLiteral("%1 条（共 %2）").arg(logs.size()).arg(total));
}

void SyncDetailsPage::populateTrashTable(const QJsonArray &arr)
{
    m_trashTable->setRowCount(0);
    int row = 0;
    for (const auto &v : arr) {
        const auto o = v.toObject();
        m_trashTable->insertRow(row);
        m_trashTable->setItem(row, 0, new QTableWidgetItem(
            QString::number(o.value(QStringLiteral("id")).toVariant().toLongLong())));
        m_trashTable->setItem(row, 1, new QTableWidgetItem(o.value(QStringLiteral("kind")).toString()));
        m_trashTable->setItem(row, 2, new QTableWidgetItem(o.value(QStringLiteral("logical_key")).toString()));
        m_trashTable->setItem(row, 3, new QTableWidgetItem(o.value(QStringLiteral("reason")).toString()));
        m_trashTable->setItem(row, 4, new QTableWidgetItem(o.value(QStringLiteral("source_device")).toString()));
        m_trashTable->setItem(row, 5, new QTableWidgetItem(
            formatLocal(o.value(QStringLiteral("archived_at")).toString())));
        ++row;
    }
}

void SyncDetailsPage::onRefreshLogs()
{
    refreshLogs();
}

void SyncDetailsPage::onClearLogs()
{
    if (!m_api)
        return;
    QNetworkReply *r = m_api->clearSyncLogs();
    connect(r, &QNetworkReply::finished, this, [this, r] {
        QJsonDocument doc;
        QString err;
        if (!ApiClient::parseReply(r, &doc, &err)) {
            log(QStringLiteral("清空日志失败：%1").arg(err));
            return;
        }
        log(QStringLiteral("同步日志已清空"));
        refreshLogs();
    });
}

void SyncDetailsPage::onLogFiltersChanged()
{
    m_offset = 0;
    refreshLogs();
}

void SyncDetailsPage::onLogRowExpanded(int row, int /*column*/)
{
    if (row < 0 || row >= m_logEntries.size())
        return;
    const SyncLogEntry &e = m_logEntries[row];
    if (!e.hasDetails()) {
        m_detailPanel->setPlainText(QStringLiteral("该日志条目无传输明细。"));
        return;
    }
    QString text = QStringLiteral("[%1] %2 %3 %4\n")
        .arg(formatLocal(e.timestamp)).arg(e.direction).arg(e.eventType).arg(e.message);
    text += QStringLiteral("── 传输明细 %1 条 ──\n").arg(e.details.size());
    for (const TransferRecord &rec : e.details) {
        const QString label = rec.title.isEmpty() ? rec.logicalKey : rec.title;
        text += QStringLiteral("  · [%1] %2 %3").arg(rec.kind).arg(TransferRecord::actionLabel(rec.action)).arg(label);
        if (!rec.reason.isEmpty())
            text += QStringLiteral("（%1）").arg(rec.reason);
        text += QChar('\n');
    }
    m_detailPanel->setPlainText(text);
}

void SyncDetailsPage::onRefreshTrash()
{
    refreshTrash();
}

void SyncDetailsPage::onClearTrash()
{
    if (!m_api)
        return;
    QNetworkReply *r = m_api->clearAllTrash();
    connect(r, &QNetworkReply::finished, this, [this, r] {
        QJsonDocument doc;
        QString err;
        if (!ApiClient::parseReply(r, &doc, &err)) {
            log(QStringLiteral("清空回收站失败：%1").arg(err));
            return;
        }
        log(QStringLiteral("回收站已清空"));
        refreshTrash();
    });
}

void SyncDetailsPage::onRestoreTrashRow()
{
    if (!m_api)
        return;
    const int row = m_trashTable->currentRow();
    if (row < 0 || m_trashTable->item(row, 0) == nullptr) {
        QMessageBox::warning(this, QStringLiteral("提示"), QStringLiteral("请先选择回收站条目"));
        return;
    }
    const qint64 id = m_trashTable->item(row, 0)->text().toLongLong();
    QNetworkReply *r = m_api->restoreTrash(id);
    connect(r, &QNetworkReply::finished, this, [this, r] {
        QJsonDocument doc;
        QString err;
        if (!ApiClient::parseReply(r, &doc, &err)) {
            log(QStringLiteral("恢复失败：%1").arg(err));
            return;
        }
        log(QStringLiteral("已恢复回收站条目 #%1").arg(
            doc.object().value(QStringLiteral("id")).toVariant().toLongLong()));
        refreshTrash();
    });
}

void SyncDetailsPage::onDeleteTrashRow()
{
    if (!m_api)
        return;
    const int row = m_trashTable->currentRow();
    if (row < 0 || m_trashTable->item(row, 0) == nullptr) {
        QMessageBox::warning(this, QStringLiteral("提示"), QStringLiteral("请先选择回收站条目"));
        return;
    }
    const qint64 id = m_trashTable->item(row, 0)->text().toLongLong();
    QNetworkReply *r = m_api->deleteTrash(id);
    connect(r, &QNetworkReply::finished, this, [this, r] {
        QJsonDocument doc;
        QString err;
        if (!ApiClient::parseReply(r, &doc, &err)) {
            log(QStringLiteral("删除失败：%1").arg(err));
            return;
        }
        log(QStringLiteral("已永久删除回收站条目 #%1").arg(
            doc.object().value(QStringLiteral("id")).toVariant().toLongLong()));
        refreshTrash();
    });
}

void SyncDetailsPage::onBack()
{
    emit backToSync();
}

void SyncDetailsPage::log(const QString &line)
{
    emit logMessage(line);
    m_statusLabel->setText(line);
}

} // namespace awqtui
