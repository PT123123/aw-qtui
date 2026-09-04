// syncdetailspage.cpp —— 同步详情独立页（最近一次同步 / 日志 / 回收站）
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
            font-weight: 600;
        }
        QLabel#StatusLabel { color: %3; font-size: 12px; }
        QLabel#Heading {
            color: %5;
            font-size: 16px;
            font-weight: 700;
        }
        QLabel#SubHeading {
            color: %3;
            font-size: 12px;
        }
        QLabel#SummaryLabel {
            color: %5;
            font-size: 13px;
            font-weight: 600;
            padding: 8px 12px;
            background: %4;
            border-radius: 6px;
        }
        QPushButton#ToolBtn {
            background: %4; border: 1px solid %2; border-radius: 6px;
            padding: 5px 14px; color: %5; font-size: 12px;
        }
        QPushButton#ToolBtn:hover { background: %6; border-color: %7; }
        QPushButton#NavBtn {
            background: transparent; border: 1px solid %2; border-radius: 6px;
            padding: 5px 14px; color: %5; font-size: 12px;
        }
        QPushButton#NavBtn:hover { background: %4; border-color: %7; }
        QPushButton#DangerBtn {
            background: %9; border: 1px solid %9; border-radius: 6px;
            padding: 5px 14px; color: white; font-size: 12px;
        }
        QPushButton#DangerBtn:hover { background: %10; border-color: %10; }
        QTabWidget::pane { border: 1px solid %2; border-radius: 8px; background: %1; }
        QTabBar::tab {
            background: %4; border: 1px solid %2; border-bottom: none;
            border-top-left-radius: 6px; border-top-right-radius: 6px;
            padding: 6px 14px; margin-right: 2px; color: %3;
        }
        QTabBar::tab:selected { background: %1; color: %5; border-bottom: 2px solid %7; }
        QTabBar::tab:hover { background: %6; }
        QTableWidget { gridline-color: %2; }
        QHeaderView::section {
            background: %4;
            border: none;
            border-bottom: 1px solid %2;
            padding: 6px;
            color: %3;
            font-weight: 600;
        }
    )")
                          .arg(kColorBgElev, kColorBorder, kColorFgMuted,
                               kColorBgElev2, kColorFgSoft, kColorBgElev2, kColorAccent, kColorAccentHover,
                               kColorDanger, kColorDanger));
}

void SyncDetailsPage::buildUi()
{
    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(20, 20, 20, 20);
    root->setSpacing(14);

    // ── 顶部工具栏 ──
    auto *toolbar = new QHBoxLayout;
    toolbar->setSpacing(8);

    m_backBtn = new QPushButton(QStringLiteral("← 返回同步"));
    m_backBtn->setObjectName(QStringLiteral("NavBtn"));
    connect(m_backBtn, &QPushButton::clicked, this, &SyncDetailsPage::onBack);
    toolbar->addWidget(m_backBtn);

    toolbar->addSpacing(16);

    auto *title = new QLabel(QStringLiteral("📋 同步详情"));
    title->setObjectName(QStringLiteral("Heading"));
    toolbar->addWidget(title);

    auto *subtitle = new QLabel(QStringLiteral("传输明细 / 日志 / 回收站"));
    subtitle->setObjectName(QStringLiteral("SubHeading"));
    toolbar->addWidget(subtitle);
    toolbar->addStretch(1);

    m_serverEdit = new QLineEdit(m_api ? m_api->baseUrl() : kDefaultServerUrl);
    m_serverEdit->setFixedWidth(240);
    toolbar->addWidget(new QLabel(QStringLiteral("服务端")));
    toolbar->addWidget(m_serverEdit);

    auto *btnApply = new QPushButton(QStringLiteral("应用"));
    btnApply->setObjectName(QStringLiteral("ToolBtn"));
    connect(btnApply, &QPushButton::clicked, this, [this] {
        setServerUrl(m_serverEdit->text().trimmed());
    });
    toolbar->addWidget(btnApply);

    root->addLayout(toolbar);

    // ── 标签页 ──
    m_tabs = new QTabWidget;

    // ── 最近一次同步 Tab ──
    m_latestSyncTab = new QWidget;
    auto *latestLay = new QVBoxLayout(m_latestSyncTab);
    latestLay->setContentsMargins(12, 12, 12, 12);
    latestLay->setSpacing(8);

    auto *latestToolbar = new QHBoxLayout;
    latestToolbar->setSpacing(8);
    m_btnRefreshLatestSync = new QPushButton(QStringLiteral("↻ 刷新"));
    m_btnRefreshLatestSync->setObjectName(QStringLiteral("ToolBtn"));
    connect(m_btnRefreshLatestSync, &QPushButton::clicked, this, &SyncDetailsPage::onRefreshLatestSync);
    latestToolbar->addWidget(m_btnRefreshLatestSync);
    latestToolbar->addStretch(1);
    latestLay->addLayout(latestToolbar);

    m_lblLatestSummary = new QLabel(QStringLiteral("暂无同步记录"));
    m_lblLatestSummary->setObjectName(QStringLiteral("SummaryLabel"));
    m_lblLatestSummary->setWordWrap(true);
    latestLay->addWidget(m_lblLatestSummary);

    auto *latestBox = new QGroupBox(QStringLiteral("传输明细"));
    auto *latestBoxLay = new QVBoxLayout(latestBox);
    m_latestRecordsTable = new QTableWidget(0, 5);
    m_latestRecordsTable->setHorizontalHeaderLabels({QStringLiteral("类型"),
                                                      QStringLiteral("逻辑键"),
                                                      QStringLiteral("标题"),
                                                      QStringLiteral("操作"),
                                                      QStringLiteral("原因")});
    m_latestRecordsTable->verticalHeader()->setVisible(false);
    m_latestRecordsTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_latestRecordsTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_latestRecordsTable->horizontalHeader()->setStretchLastSection(true);
    m_latestRecordsTable->setColumnWidth(0, 80);
    m_latestRecordsTable->setColumnWidth(1, 180);
    m_latestRecordsTable->setColumnWidth(2, 180);
    m_latestRecordsTable->setColumnWidth(3, 90);
    latestBoxLay->addWidget(m_latestRecordsTable);
    latestLay->addWidget(latestBox, 1);

    m_tabs->addTab(m_latestSyncTab, QStringLiteral("📊 最近同步"));

    // ── 日志 Tab ──
    auto *logTab = new QWidget;
    auto *logLay = new QVBoxLayout(logTab);
    logLay->setContentsMargins(12, 12, 12, 12);
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

    filterBar->addWidget(new QLabel(QStringLiteral("类型")));
    m_filterKind = new QComboBox;
    m_filterKind->addItem(QStringLiteral("ActivityWatch（日志）"), QStringLiteral("activity"));
    m_filterKind->addItem(QStringLiteral("收件箱（已禁用）"), QStringLiteral("note"));
    m_filterKind->addItem(QStringLiteral("任务（已禁用）"), QStringLiteral("todo"));
    m_filterKind->addItem(QStringLiteral("全部"), QString());
    m_filterKind->setCurrentIndex(0);
    m_filterKind->setToolTip(QStringLiteral("局域网同步详情默认只显示 ActivityWatch 日志；收件箱/任务由 D1 云同步管理"));
    filterBar->addWidget(m_filterKind);

    filterBar->addStretch(1);

    m_logCountLabel = new QLabel(QStringLiteral("0 条"));
    m_logCountLabel->setObjectName(QStringLiteral("StatusLabel"));
    filterBar->addWidget(m_logCountLabel);

    connect(m_filterDirection, QOverload<int>::of(&QComboBox::activated), this, &SyncDetailsPage::onLogFiltersChanged);
    connect(m_filterProtocol, QOverload<int>::of(&QComboBox::activated), this, &SyncDetailsPage::onLogFiltersChanged);
    connect(m_filterEvent, QOverload<int>::of(&QComboBox::activated), this, &SyncDetailsPage::onLogFiltersChanged);
    connect(m_filterKind, QOverload<int>::of(&QComboBox::activated), this, &SyncDetailsPage::onLogFiltersChanged);

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
    pageBar->setSpacing(8);
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
    m_btnClearLogs->setObjectName(QStringLiteral("DangerBtn"));
    connect(m_btnClearLogs, &QPushButton::clicked, this, &SyncDetailsPage::onClearLogs);
    pageBar->addWidget(m_btnClearLogs);

    logLay->addLayout(pageBar);

    // 明细面板
    auto *detailBox = new QGroupBox(QStringLiteral("传输明细（双击日志行展开）"));
    auto *dl = new QVBoxLayout(detailBox);
    m_detailPanel = new QPlainTextEdit;
    m_detailPanel->setReadOnly(true);
    m_detailPanel->setMaximumHeight(180);
    m_detailPanel->setPlaceholderText(QStringLiteral("双击上方日志行查看传输明细…"));
    dl->addWidget(m_detailPanel);
    logLay->addWidget(detailBox);

    m_tabs->addTab(logTab, QStringLiteral("📜 同步日志"));

    // ── 回收站 Tab ──
    auto *trashTab = new QWidget;
    auto *trashLay = new QVBoxLayout(trashTab);
    trashLay->setContentsMargins(12, 12, 12, 12);
    trashLay->setSpacing(8);

    auto *trashBar = new QHBoxLayout;
    trashBar->setSpacing(8);
    m_btnRefreshTrash = new QPushButton(QStringLiteral("↻ 刷新"));
    m_btnRefreshTrash->setObjectName(QStringLiteral("ToolBtn"));
    connect(m_btnRefreshTrash, &QPushButton::clicked, this, &SyncDetailsPage::onRefreshTrash);
    trashBar->addWidget(m_btnRefreshTrash);

    m_btnClearTrash = new QPushButton(QStringLiteral("🗑 清空"));
    m_btnClearTrash->setObjectName(QStringLiteral("DangerBtn"));
    connect(m_btnClearTrash, &QPushButton::clicked, this, &SyncDetailsPage::onClearTrash);
    trashBar->addWidget(m_btnClearTrash);

    trashBar->addStretch(1);

    m_btnRestoreTrash = new QPushButton(QStringLiteral("↩ 恢复"));
    m_btnRestoreTrash->setObjectName(QStringLiteral("ToolBtn"));
    connect(m_btnRestoreTrash, &QPushButton::clicked, this, &SyncDetailsPage::onRestoreTrashRow);
    trashBar->addWidget(m_btnRestoreTrash);

    m_btnDeleteTrash = new QPushButton(QStringLiteral("✗ 永久删除"));
    m_btnDeleteTrash->setObjectName(QStringLiteral("DangerBtn"));
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

    m_tabs->addTab(trashTab, QStringLiteral("🗑 回收站"));

    root->addWidget(m_tabs, 1);
}

void SyncDetailsPage::setServerUrl(const QString &url)
{
    if (m_api) {
        m_api->setBaseUrl(url);
        m_statusLabel->setText(QStringLiteral("已切换到: %1").arg(url));
        refreshLogs();
        refreshTrash();
        refreshLatestSync();
    }
}

void SyncDetailsPage::onRefreshLatestSync()
{
    refreshLatestSync();
}

void SyncDetailsPage::refreshLatestSync()
{
    if (!m_api)
        return;
    QNetworkReply *r = m_api->getSyncLogs(QString(), QString(), QString("sync"), 1, 0);
    connect(r, &QNetworkReply::finished, this, [this, r] {
        QJsonDocument doc;
        QString err;
        if (!ApiClient::parseReply(r, &doc, &err)) {
            m_lblLatestSummary->setText(QStringLiteral("获取最近同步记录失败：%1").arg(err));
            return;
        }
        const auto obj = doc.object();
        const auto arr = obj.value(QStringLiteral("logs")).toArray();
        if (arr.isEmpty()) {
            m_lblLatestSummary->setText(QStringLiteral("暂无同步记录"));
            m_latestRecordsTable->setRowCount(0);
            return;
        }
        const auto logEntry = arr.first().toObject();
        const auto result = logEntry.value(QStringLiteral("result")).toObject();
        if (result.isEmpty()) {
            m_lblLatestSummary->setText(QStringLiteral("最近一条同步无结果数据"));
            m_latestRecordsTable->setRowCount(0);
            return;
        }
        populateLatestSync(result);
    });
}

void SyncDetailsPage::populateLatestSync(const QJsonObject &result)
{
    const ApplyResult r = ApplyResult::fromJson(result);

    m_lblLatestSummary->setText(
        QStringLiteral("应用 %1 条（新增 %2 / 更新 %3 / 删除 %4）· 忽略 %5 · 归档 %6 · 错误 %7")
            .arg(r.applied).arg(r.created).arg(r.updated).arg(r.deleted)
            .arg(r.ignored).arg(r.archived).arg(r.errors.size()));

    m_latestRecordsTable->setRowCount(0);
    int row = 0;
    for (const TransferRecord &rec : r.records) {
        m_latestRecordsTable->insertRow(row);
        m_latestRecordsTable->setItem(row, 0, new QTableWidgetItem(rec.kind));
        m_latestRecordsTable->setItem(row, 1, new QTableWidgetItem(rec.logicalKey));
        m_latestRecordsTable->setItem(row, 2, new QTableWidgetItem(rec.title));
        m_latestRecordsTable->setItem(row, 3, new QTableWidgetItem(TransferRecord::actionLabel(rec.action)));
        m_latestRecordsTable->setItem(row, 4, new QTableWidgetItem(rec.reason));
        QColor actionColor;
        if (rec.action == QLatin1String("created"))
            actionColor = QColor(QString::fromLatin1(kColorOk));
        else if (rec.action == QLatin1String("updated"))
            actionColor = QColor(QString::fromLatin1(kColorAccent));
        else if (rec.action == QLatin1String("deleted"))
            actionColor = QColor(QString::fromLatin1(kColorDanger));
        else if (rec.action == QLatin1String("archived"))
            actionColor = QColor(QString::fromLatin1(kColorWarn));
        else if (rec.action == QLatin1String("conflict"))
            actionColor = QColor(QString::fromLatin1(kColorDanger));
        for (int c = 0; c < 5; ++c) {
            if (auto *it = m_latestRecordsTable->item(row, c))
                it->setForeground(actionColor);
        }
        ++row;
    }
    if (r.records.isEmpty()) {
        if (!r.errors.isEmpty()) {
            m_lblLatestSummary->setText(m_lblLatestSummary->text() +
                                        QStringLiteral("\n错误：") + r.errors.join(QStringLiteral("；")));
        }
    }
}

void SyncDetailsPage::refreshLogs()
{
    if (!m_api)
        return;
    const QString dir = m_filterDirection->currentData().toString();
    const QString proto = m_filterProtocol->currentData().toString();
    const QString eventT = m_filterEvent->currentData().toString();
    QNetworkReply *r = m_api->getSyncLogs(dir, proto, eventT, m_limit, m_offset);
    connect(r, &QNetworkReply::finished, this, [this, r] {
        QJsonDocument doc;
        QString err;
        if (!ApiClient::parseReply(r, &doc, &err)) {
            log(QStringLiteral("获取日志失败：%1").arg(err));
            return;
        }
        const auto obj = doc.object();
        const auto arr = obj.value(QStringLiteral("logs")).toArray();
        const qint64 total = obj.value(QStringLiteral("total")).toVariant().toLongLong();
        const QString kindFilter = m_filterKind->currentData().toString();
        populateLogTable(arr, total, kindFilter);
    });
}

void SyncDetailsPage::populateLogTable(const QJsonArray &logs, qint64 total, const QString &kindFilter)
{
    m_logEntries.clear();
    m_logTable->setRowCount(0);
    int row = 0;
    for (const auto &v : logs) {
        if (!v.isObject())
            continue;
        const auto o = v.toObject();
        if (!kindFilter.isEmpty()) {
            const QString kind = o.value(QStringLiteral("event_type")).toString();
            if (kind != kindFilter && !(kindFilter == QStringLiteral("activity") && kind.isEmpty()))
                continue;
        }
        const SyncLogEntry e = SyncLogEntry::fromJson(o);
        m_logEntries.append(e);
        m_logTable->insertRow(row);
        m_logTable->setItem(row, 0, new QTableWidgetItem(formatLocal(e.timestamp)));
        m_logTable->setItem(row, 1, new QTableWidgetItem(e.direction));
        m_logTable->setItem(row, 2, new QTableWidgetItem(e.protocol));
        m_logTable->setItem(row, 3, new QTableWidgetItem(e.eventType));
        m_logTable->setItem(row, 4, new QTableWidgetItem(e.status));
        QString msg = e.message;
        if (e.dataSize > 0)
            msg += QStringLiteral(" (%1 bytes)").arg(e.dataSize);
        m_logTable->setItem(row, 5, new QTableWidgetItem(msg));
        ++row;
    }
    m_totalLogs = total;
    const int totalPages = (total + m_limit - 1) / m_limit;
    m_logCountLabel->setText(QStringLiteral("%1 条（第 %2/%3 页）")
                                  .arg(total)
                                  .arg(qBound(1, (m_offset / m_limit) + 1, qMax(1, totalPages)))
                                  .arg(totalPages));
    m_pageSpin->setMaximum(qMax(1, totalPages));
    m_pageSpin->setSuffix(QStringLiteral(" / %1").arg(totalPages));
    m_pageSpin->setValue(qBound(1, (m_offset / m_limit) + 1, qMax(1, totalPages)));
    m_btnPrevPage->setEnabled(m_offset > 0);
    m_btnNextPage->setEnabled(m_offset + m_limit < total);
}

void SyncDetailsPage::onRefreshLogs()
{
    m_offset = 0;
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
        log(QStringLiteral("日志已清空"));
        refreshLogs();
    });
}

void SyncDetailsPage::onLogFiltersChanged()
{
    m_offset = 0;
    refreshLogs();
}

void SyncDetailsPage::onLogRowExpanded(int row, int column)
{
    Q_UNUSED(column);
    if (row < 0 || row >= m_logEntries.size())
        return;
    const SyncLogEntry &e = m_logEntries[row];
    QString text;
    text += QStringLiteral("时间：%1\n").arg(e.timestamp);
    text += QStringLiteral("方向：%1\n").arg(e.direction);
    text += QStringLiteral("协议：%1\n").arg(e.protocol);
    text += QStringLiteral("事件：%1\n").arg(e.eventType);
    text += QStringLiteral("状态：%1\n").arg(e.status);
    text += QStringLiteral("消息：%1\n").arg(e.message.isEmpty() ? QStringLiteral("—") : e.message);
    if (e.dataSize > 0)
        text += QStringLiteral("数据大小：%1 bytes\n").arg(e.dataSize);
    if (e.hasDetails()) {
        text += QStringLiteral("\n--- 传输明细 ---\n");
        for (const TransferRecord &rec : e.details) {
            text += QStringLiteral("  · [%1] %2 %3\n")
                        .arg(rec.kind, TransferRecord::actionLabel(rec.action),
                             rec.title.isEmpty() ? rec.logicalKey : rec.title);
            if (!rec.reason.isEmpty())
                text += QStringLiteral("    原因：%1\n").arg(rec.reason);
        }
    }
    m_detailPanel->setPlainText(text);
}

void SyncDetailsPage::onRefreshTrash()
{
    refreshTrash();
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
        const auto obj = doc.object();
        const auto arr = obj.value(QStringLiteral("trash")).toArray();
        populateTrashTable(arr);
        log(QStringLiteral("回收站 %1 条").arg(arr.size()));
    });
}

void SyncDetailsPage::populateTrashTable(const QJsonArray &arr)
{
    m_trashTable->setRowCount(0);
    int row = 0;
    for (const auto &v : arr) {
        if (!v.isObject())
            continue;
        const auto o = v.toObject();
        const TrashEntry t = TrashEntry::fromJson(o);
        m_trashTable->insertRow(row);
        m_trashTable->setItem(row, 0, new QTableWidgetItem(QString::number(t.id)));
        m_trashTable->setItem(row, 1, new QTableWidgetItem(t.kind));
        m_trashTable->setItem(row, 2, new QTableWidgetItem(t.logicalKey));
        m_trashTable->setItem(row, 3, new QTableWidgetItem(t.reason));
        m_trashTable->setItem(row, 4, new QTableWidgetItem(t.sourceDevice));
        m_trashTable->setItem(row, 5, new QTableWidgetItem(formatLocal(t.archivedAt)));
        ++row;
    }
}

void SyncDetailsPage::onClearTrash()
{
    if (!m_api)
        return;
    QMessageBox box(this);
    box.setWindowTitle(QStringLiteral("清空回收站"));
    box.setText(QStringLiteral("确定永久删除回收站中的全部归档？此操作不可恢复。"));
    box.setStandardButtons(QMessageBox::Yes | QMessageBox::Cancel);
    if (box.exec() != QMessageBox::Yes)
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
    const int row = m_trashTable->currentRow();
    if (row < 0)
        return;
    const qint64 id = m_trashTable->item(row, 0)->text().toLongLong();
    if (!m_api)
        return;
    QNetworkReply *r = m_api->restoreTrash(id);
    connect(r, &QNetworkReply::finished, this, [this, r] {
        QJsonDocument doc;
        QString err;
        if (!ApiClient::parseReply(r, &doc, &err)) {
            log(QStringLiteral("恢复失败：%1").arg(err));
            return;
        }
        log(QStringLiteral("已恢复归档项"));
        refreshTrash();
    });
}

void SyncDetailsPage::onDeleteTrashRow()
{
    const int row = m_trashTable->currentRow();
    if (row < 0)
        return;
    const qint64 id = m_trashTable->item(row, 0)->text().toLongLong();
    if (!m_api)
        return;
    QMessageBox box(this);
    box.setWindowTitle(QStringLiteral("永久删除"));
    box.setText(QStringLiteral("确定永久删除此归档项？此操作不可恢复。"));
    box.setStandardButtons(QMessageBox::Yes | QMessageBox::Cancel);
    if (box.exec() != QMessageBox::Yes)
        return;
    QNetworkReply *r = m_api->deleteTrash(id);
    connect(r, &QNetworkReply::finished, this, [this, r] {
        QJsonDocument doc;
        QString err;
        if (!ApiClient::parseReply(r, &doc, &err)) {
            log(QStringLiteral("删除失败：%1").arg(err));
            return;
        }
        log(QStringLiteral("已永久删除"));
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
}

} // namespace awqtui
