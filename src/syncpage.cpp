// syncpage.cpp —— 局域网同步页 (aw-sync-rust /api/0/sync)
#include "syncpage.h"

#include "apiclient.h"
#include "config.h"
#include "mdnsdiscovery.h"
#include "theme.h"
#include "widgets.h"

#include <QCheckBox>
#include <QColor>
#include <QComboBox>
#include <QDate>
#include <QDateTime>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QInputDialog>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSettings>
#include <QSpinBox>
#include <QTableWidget>
#include <QTabWidget>
#include <QVBoxLayout>

namespace awqtui {

SyncPage::SyncPage(ApiClient *api, MdnsDiscovery *mdns, QWidget *parent)
    : QWidget(parent), m_api(api), m_mdns(mdns)
{
    buildUi();
    connect(m_api, &ApiClient::destroyed, this, [this] { m_api = nullptr; });

    connect(m_mdns, &MdnsDiscovery::peerFound, this, &SyncPage::onPeerFound);
    connect(m_mdns, &MdnsDiscovery::peerLost, this, &SyncPage::onPeerLost);
    connect(m_mdns, &MdnsDiscovery::statusChanged, this, [this](const QString &m) { log(m); });
}

SyncPage::~SyncPage() = default;

void SyncPage::buildUi()
{
    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(16, 16, 16, 16);
    root->setSpacing(12);

    // ---- 服务端 ----
    auto *serverBox = new QGroupBox(QStringLiteral("服务端"));
    auto *sl = new QHBoxLayout(serverBox);
    sl->setSpacing(8);
    m_serverEdit = new QLineEdit(m_api ? m_api->baseUrl() : kDefaultServerUrl);
    m_serverEdit->setFixedWidth(300);
    sl->addWidget(new QLabel(QStringLiteral("地址")));
    sl->addWidget(m_serverEdit);
    auto *btnSet = new QPushButton(QStringLiteral("应用"));
    connect(btnSet, &QPushButton::clicked, this, [this] {
        setServerUrl(m_serverEdit->text().trimmed());
        refreshDevices();
    });
    sl->addWidget(btnSet);
    m_serverBadge = new StatusBadge;
    sl->addWidget(m_serverBadge);
    sl->addStretch(1);
    root->addWidget(serverBox);

    // ---- 标签页：设备 / 配置 / 日志 / 回收站 ----
    auto *tabs = new QTabWidget;

    // ── 设备页 ──
    auto *devTab = new QWidget;
    auto *devLay = new QVBoxLayout(devTab);

    // 设备注册表
    auto *devBox = new QGroupBox(QStringLiteral("已配对 / 已发现设备"));
    auto *dl = new QVBoxLayout(devBox);
    m_devTable = new QTableWidget(0, 7);
    m_devTable->setHorizontalHeaderLabels({QStringLiteral("设备"), QStringLiteral("类型"),
                                           QStringLiteral("IP"), QStringLiteral("端口"),
                                           QStringLiteral("最后在线"), QStringLiteral("最后同步"),
                                           QStringLiteral("状态")});
    m_devTable->verticalHeader()->setVisible(false);
    m_devTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_devTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_devTable->horizontalHeader()->setStretchLastSection(true);
    m_devTable->setColumnWidth(0, 180);
    m_devTable->setColumnWidth(1, 70);
    m_devTable->setColumnWidth(2, 110);
    m_devTable->setColumnWidth(3, 60);
    m_devTable->setColumnWidth(4, 140);
    m_devTable->setColumnWidth(5, 140);
    dl->addWidget(m_devTable);

    auto *dlRow = new QHBoxLayout;
    m_btnSyncNow = new QPushButton(QStringLiteral("立即同步"));
    m_btnSyncNow->setObjectName(QStringLiteral("PrimaryBtn"));
    connect(m_btnSyncNow, &QPushButton::clicked, this, &SyncPage::onSyncNow);
    m_btnRemoveDevice = new QPushButton(QStringLiteral("移除设备"));
    connect(m_btnRemoveDevice, &QPushButton::clicked, this, &SyncPage::onRemoveDevice);
    m_btnSetAlias = new QPushButton(QStringLiteral("设置别名"));
    connect(m_btnSetAlias, &QPushButton::clicked, this, &SyncPage::onSetAlias);
    m_btnClearAllDevices = new QPushButton(QStringLiteral("清空所有配对"));
    connect(m_btnClearAllDevices, &QPushButton::clicked, this, &SyncPage::onClearAllDevices);
    auto *btnDevRefresh = new QPushButton(QStringLiteral("刷新"));
    connect(btnDevRefresh, &QPushButton::clicked, this, &SyncPage::refreshDevices);
    dlRow->addWidget(m_btnSyncNow);
    dlRow->addWidget(m_btnRemoveDevice);
    dlRow->addWidget(m_btnSetAlias);
    dlRow->addWidget(m_btnClearAllDevices);
    dlRow->addStretch(1);
    dlRow->addWidget(btnDevRefresh);
    dl->addLayout(dlRow);
    devLay->addWidget(devBox);

    // 配对操作
    auto *pairBox = new QGroupBox(QStringLiteral("配对"));
    auto *pl = new QHBoxLayout(pairBox);
    m_btnCreatePairCode = new QPushButton(QStringLiteral("生成配对码"));
    connect(m_btnCreatePairCode, &QPushButton::clicked, this, &SyncPage::onCreatePairCode);
    m_lblPairCode = new QLabel(QStringLiteral("—"));
    m_lblPairCode->setStyleSheet(QStringLiteral("font-weight: bold; color: %1;").arg(kColorAccent));
    m_editPairCode = new QLineEdit;
    m_editPairCode->setPlaceholderText(QStringLiteral("输入对端配对码"));
    m_editPairCode->setFixedWidth(160);
    m_btnJoinDevice = new QPushButton(QStringLiteral("加入设备"));
    connect(m_btnJoinDevice, &QPushButton::clicked, this, &SyncPage::onJoinDevice);
    m_btnInitiatePair = new QPushButton(QStringLiteral("发起配对"));
    connect(m_btnInitiatePair, &QPushButton::clicked, this, &SyncPage::onInitiatePair);
    m_btnAcceptPair = new QPushButton(QStringLiteral("接受配对"));
    connect(m_btnAcceptPair, &QPushButton::clicked, this, &SyncPage::onAcceptPair);
    pl->addWidget(m_btnCreatePairCode);
    pl->addWidget(m_lblPairCode);
    pl->addSpacing(20);
    pl->addWidget(m_editPairCode);
    pl->addWidget(m_btnJoinDevice);
    pl->addSpacing(10);
    pl->addWidget(m_btnInitiatePair);
    pl->addWidget(m_btnAcceptPair);
    pl->addStretch(1);
    devLay->addWidget(pairBox);

    // 快照传输（WiFi 热点点对点：导出本机快照 / 导入合并对端快照）
    auto *snapBox = new QGroupBox(QStringLiteral("快照传输（WiFi 热点点对点）"));
    auto *snapl = new QHBoxLayout(snapBox);
    m_btnExportSnapshot = new QPushButton(QStringLiteral("导出快照"));
    connect(m_btnExportSnapshot, &QPushButton::clicked, this, &SyncPage::onExportSnapshot);
    m_btnImportSnapshot = new QPushButton(QStringLiteral("导入合并"));
    connect(m_btnImportSnapshot, &QPushButton::clicked, this, &SyncPage::onImportSnapshot);
    m_lblSnapshot = new QLabel(QStringLiteral("导出本机全部数据为 JSON，或把对端快照合并进本机"));
    m_lblSnapshot->setWordWrap(true);
    snapl->addWidget(m_btnExportSnapshot);
    snapl->addWidget(m_btnImportSnapshot);
    snapl->addWidget(m_lblSnapshot, 1);
    devLay->addWidget(snapBox);

    // 统计信息
    auto *statsBox = new QGroupBox(QStringLiteral("同步统计"));
    auto *statsLay = new QVBoxLayout(statsBox);
    m_lblStats = new QLabel(QStringLiteral("选择设备查看统计"));
    m_lblStats->setWordWrap(true);
    statsLay->addWidget(m_lblStats);
    devLay->addWidget(statsBox);

    // mDNS 自动发现
    auto *mdnsBox = new QGroupBox(QStringLiteral("mDNS 自动发现（_activitywatch._tcp.local.）"));
    auto *ml = new QVBoxLayout(mdnsBox);
    auto *mlRow = new QHBoxLayout;
    m_btnBrowse = new QPushButton(QStringLiteral("开始浏览"));
    connect(m_btnBrowse, &QPushButton::clicked, this, &SyncPage::onDiscover);
    mlRow->addWidget(m_btnBrowse);
    mlRow->addWidget(new QLabel(QStringLiteral("本机广播端口")));
    m_portEdit = new QLineEdit(QStringLiteral("5600"));
    m_portEdit->setFixedWidth(70);
    mlRow->addWidget(m_portEdit);
    auto *btnReg = new QPushButton(QStringLiteral("注册本机"));
    connect(btnReg, &QPushButton::clicked, this, [this] {
        bool ok = false;
        const int port = m_portEdit->text().toInt(&ok);
        if (!ok || port <= 0 || port > 65535) {
            log(QStringLiteral("端口无效：%1").arg(m_portEdit->text()));
            return;
        }
        const bool r = m_mdns->registerService(port);
        log(r ? QStringLiteral("已注册本机到 %1:%2").arg(hostname(), m_portEdit->text())
              : QStringLiteral("注册失败"));
    });
    mlRow->addWidget(btnReg);
    auto *btnAddPeer = new QPushButton(QStringLiteral("手动添加对端"));
    connect(btnAddPeer, &QPushButton::clicked, this, &SyncPage::onAddPeer);
    mlRow->addWidget(btnAddPeer);
    mlRow->addStretch(1);
    ml->addLayout(mlRow);
    m_peerTable = new QTableWidget(0, 3);
    m_peerTable->setHorizontalHeaderLabels({QStringLiteral("实例"), QStringLiteral("地址"), QStringLiteral("端口")});
    m_peerTable->verticalHeader()->setVisible(false);
    m_peerTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_peerTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_peerTable->horizontalHeader()->setStretchLastSection(true);
    ml->addWidget(m_peerTable);
    devLay->addWidget(mdnsBox);

    tabs->addTab(devTab, QStringLiteral("设备"));

    // ── 配置页 ──
    auto *cfgTab = new QWidget;
    auto *cfgLay = new QVBoxLayout(cfgTab);
    auto *cfgBox = new QGroupBox(QStringLiteral("同步设置"));
    auto *fl = new QFormLayout(cfgBox);
    m_chkEnabled = new QCheckBox(QStringLiteral("启用局域网同步"));
    m_chkHttp = new QCheckBox(QStringLiteral("启用 HTTP 同步"));
    fl->addRow(m_chkEnabled);
    fl->addRow(m_chkHttp);
    auto *syncRangeHint = new QLabel(QStringLiteral(
        "同步范围（收件箱/任务/ActivityWatch）请在「设置 → 同步」中配置"));
    syncRangeHint->setStyleSheet(QStringLiteral("color: %1; font-size: 12px;").arg(kColorFgMuted));
    syncRangeHint->setWordWrap(true);
    fl->addRow(syncRangeHint);
    fl->addRow(QStringLiteral("本机别名"), m_editAlias = new QLineEdit);
    fl->addRow(QStringLiteral("监听端口"), m_editListenPort = new QLineEdit);
    fl->addRow(QStringLiteral("UDP 端口"), m_editUdpPort = new QLineEdit);
    // 自动同步频率：三档预设（狂暴 10s / 平和 5min / 静默 30min），自定义即手动改秒数
    m_cmbSyncInterval = new QComboBox;
    m_cmbSyncInterval->addItem(QStringLiteral("狂暴（每 10 秒）"), 10);
    m_cmbSyncInterval->addItem(QStringLiteral("平和（每 5 分钟）"), 300);
    m_cmbSyncInterval->addItem(QStringLiteral("静默（每 30 分钟）"), 1800);
    m_cmbSyncInterval->setToolTip(QStringLiteral(
        "局域网自动同步间隔：已配对设备间按该频率自动双向同步。\n"
        "狂暴 10 秒适合实时协作；平和 5 分钟为日常使用；静默 30 分钟节省电量。"));
    fl->addRow(QStringLiteral("自动同步频率"), m_cmbSyncInterval);
    m_btnSaveConfig = new QPushButton(QStringLiteral("保存配置"));
    m_btnSaveConfig->setObjectName(QStringLiteral("PrimaryBtn"));
    connect(m_btnSaveConfig, &QPushButton::clicked, this, &SyncPage::onSaveConfig);
    fl->addRow(m_btnSaveConfig);
    cfgLay->addWidget(cfgBox);
    auto *cfgBtnRow = new QHBoxLayout;
    auto *btnRefreshCfg = new QPushButton(QStringLiteral("刷新配置"));
    connect(btnRefreshCfg, &QPushButton::clicked, this, &SyncPage::refreshSyncConfig);
    cfgBtnRow->addWidget(btnRefreshCfg);
    cfgBtnRow->addStretch(1);
    cfgLay->addLayout(cfgBtnRow);
    cfgLay->addStretch(1);
    tabs->addTab(cfgTab, QStringLiteral("配置"));

    // ── 日志页 ──
    auto *logTab = new QWidget;
    auto *logLay = new QVBoxLayout(logTab);
    auto *logBox = new QGroupBox(QStringLiteral("同步日志"));
    auto *ll = new QVBoxLayout(logBox);
    m_log = new QPlainTextEdit;
    m_log->setReadOnly(true);
    m_log->setMaximumHeight(300);
    ll->addWidget(m_log);
    auto *logRow = new QHBoxLayout;
    auto *btnRefreshLog = new QPushButton(QStringLiteral("刷新日志"));
    connect(btnRefreshLog, &QPushButton::clicked, this, [this] {
        if (!m_api) return;
        QNetworkReply *r = m_api->getSyncLogs();
        connect(r, &QNetworkReply::finished, this, [this, r] {
            QJsonDocument doc;
            QString err;
            if (!ApiClient::parseReply(r, &doc, &err)) {
                log(QStringLiteral("获取日志失败：%1").arg(err));
                return;
            }
            const auto obj = doc.object();
            const auto arr = obj.value(QStringLiteral("logs")).toArray();
            m_log->clear();
            int detailCount = 0;
            for (const auto &v : arr) {
                const SyncLogEntry e = SyncLogEntry::fromJson(v.toObject());
                m_log->appendPlainText(QStringLiteral("[%1] %2 %3 %4")
                    .arg(formatLocal(e.timestamp), e.direction, e.eventType, e.message));
                if (!e.hasDetails())
                    continue;
                // 逐条传输明细：某次同步中每条记录的落地结果
                m_log->appendPlainText(QStringLiteral("    ── 传输明细 %1 条 ──").arg(e.details.size()));
                for (const TransferRecord &rec : e.details) {
                    const QString label = rec.title.isEmpty() ? rec.logicalKey : rec.title;
                    QString line = QStringLiteral("    · [%1] %2 %3")
                                       .arg(rec.kind, TransferRecord::actionLabel(rec.action), label);
                    if (!rec.reason.isEmpty())
                        line += QStringLiteral("（%1）").arg(rec.reason);
                    m_log->appendPlainText(line);
                }
                detailCount += e.details.size();
            }
            log(QStringLiteral("日志 %1 条，共 %2 条%3")
                .arg(arr.size())
                .arg(obj.value(QStringLiteral("total")).toVariant().toLongLong())
                .arg(detailCount ? QStringLiteral("，含传输明细 %1 条").arg(detailCount) : QString()));
        });
    });
    m_btnClearLogs = new QPushButton(QStringLiteral("清空日志"));
    connect(m_btnClearLogs, &QPushButton::clicked, this, &SyncPage::onClearLogs);
    logRow->addWidget(btnRefreshLog);
    logRow->addWidget(m_btnClearLogs);
    logRow->addStretch(1);
    ll->addLayout(logRow);
    logLay->addWidget(logBox, 1);
    tabs->addTab(logTab, QStringLiteral("日志"));

    // ── 回收站页 ──
    auto *trashTab = new QWidget;
    auto *trashLay = new QVBoxLayout(trashTab);
    auto *trashBox = new QGroupBox(QStringLiteral("回收站（冲突/删除归档）"));
    auto *tl = new QVBoxLayout(trashBox);
    m_trashTable = new QTableWidget(0, 6);
    m_trashTable->setHorizontalHeaderLabels({QStringLiteral("ID"), QStringLiteral("类型"),
                                              QStringLiteral("逻辑键"), QStringLiteral("原因"),
                                              QStringLiteral("来源设备"), QStringLiteral("归档时间")});
    m_trashTable->verticalHeader()->setVisible(false);
    m_trashTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_trashTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_trashTable->horizontalHeader()->setStretchLastSection(true);
    tl->addWidget(m_trashTable);
    auto *trashRow = new QHBoxLayout;
    auto *btnRefreshTrash = new QPushButton(QStringLiteral("刷新"));
    connect(btnRefreshTrash, &QPushButton::clicked, this, &SyncPage::refreshTrash);
    m_btnRestoreTrash = new QPushButton(QStringLiteral("恢复选中"));
    connect(m_btnRestoreTrash, &QPushButton::clicked, this, &SyncPage::onRestoreTrashRow);
    m_btnDeleteTrash = new QPushButton(QStringLiteral("删除选中"));
    connect(m_btnDeleteTrash, &QPushButton::clicked, this, &SyncPage::onDeleteTrashRow);
    m_btnClearTrash = new QPushButton(QStringLiteral("清空回收站"));
    connect(m_btnClearTrash, &QPushButton::clicked, this, &SyncPage::onClearAllTrash);
    trashRow->addWidget(btnRefreshTrash);
    trashRow->addWidget(m_btnRestoreTrash);
    trashRow->addWidget(m_btnDeleteTrash);
    trashRow->addWidget(m_btnClearTrash);
    trashRow->addStretch(1);
    tl->addLayout(trashRow);
    trashLay->addWidget(trashBox);
    tabs->addTab(trashTab, QStringLiteral("回收站"));


    root->addWidget(tabs, 1);

    // 底部状态栏
    connect(this, &SyncPage::logMessage, this, [this](const QString &line) {
        m_log->appendPlainText(QStringLiteral("[%1] %2").arg(
            QDateTime::currentDateTime().toString(QStringLiteral("HH:mm:ss")), line));
    });
    log(QStringLiteral("设备 ID：%1").arg(m_api ? m_api->deviceId() : deviceId()));
}

// ------------------------------------------------------------------ //

void SyncPage::setServerUrl(const QString &url)
{
    if (!m_api)
        return;
    m_api->setBaseUrl(url);
    m_serverEdit->setText(url);
}

QString SyncPage::serverUrl() const
{
    return m_api ? m_api->baseUrl() : QString();
}

void SyncPage::log(const QString &line)
{
    emit logMessage(line);
}

void SyncPage::refreshDevices()
{
    if (!m_api)
        return;
    m_serverBadge->setState(StatusBadge::State::Syncing, QStringLiteral("查询设备…"));
    QNetworkReply *r = m_api->getSyncDevices();
    connect(r, &QNetworkReply::finished, this, [this, r] {
        QJsonDocument doc;
        QString err;
        if (!ApiClient::parseReply(r, &doc, &err)) {
            m_serverBadge->setState(StatusBadge::State::Disconnected, err);
            log(QStringLiteral("获取设备失败：%1").arg(err));
            return;
        }
        m_devices.clear();
        const auto arr = doc.array();
        for (const auto &v : arr) {
            if (v.isObject())
                m_devices << SyncDevice::fromJson(v.toObject());
        }
        m_devTable->setRowCount(0);
        int row = 0;
        for (const SyncDevice &d : m_devices) {
            m_devTable->insertRow(row);
            const auto put = [&](int col, const QString &s, bool bold = false) {
                auto *it = new QTableWidgetItem(s);
                if (bold)
                    it->setForeground(QColor(kColorAccent));
                m_devTable->setItem(row, col, it);
            };
            put(0, d.displayName(), d.isSelf);
            put(1, d.deviceKind);
            put(2, d.ip);
            put(3, QString::number(d.port));
            put(4, formatLocal(d.lastSeenAt));
            put(5, formatLocal(d.lastSyncAt));
            QString status;
            if (d.isSelf)
                status = QStringLiteral("本机");
            else if (!d.paired)
                status = QStringLiteral("未配对");
            else if (d.isOnline)
                status = QStringLiteral("在线");
            else
                status = QStringLiteral("离线");
            put(6, status);
            ++row;
        }
        if (m_devices.isEmpty())
            log(QStringLiteral("设备表为空"));
        else
            log(QStringLiteral("设备 %1 台").arg(m_devices.size()));
        m_serverBadge->setState(StatusBadge::State::Connected);
    });
}

void SyncPage::doSync()
{
    // 获取选中的设备
    const int row = m_devTable->currentRow();
    if (row < 0 || row >= m_devices.size()) {
        log(QStringLiteral("请先选择要同步的设备"));
        return;
    }
    const QString devId = m_devices[row].id;
    if (m_devices[row].isSelf) {
        log(QStringLiteral("不能与本机同步"));
        return;
    }

    m_serverBadge->setState(StatusBadge::State::Syncing, QStringLiteral("同步中…"));
    log(QStringLiteral("开始与 %1 同步…").arg(m_devices[row].displayName()));

    QNetworkReply *r = m_api->triggerSync(devId);
    connect(r, &QNetworkReply::finished, this, [this, r] {
        QJsonDocument doc;
        QString err;
        if (!ApiClient::parseReply(r, &doc, &err)) {
            m_serverBadge->setState(StatusBadge::State::Error, QStringLiteral("同步失败"));
            log(QStringLiteral("同步失败：%1").arg(err));
            return;
        }
        syncComplete(ApplyResult::fromJson(doc.object().value(QStringLiteral("result")).toObject()));
        refreshDevices();
    });
}

void SyncPage::syncComplete(const ApplyResult &r)
{
    log(QStringLiteral("同步完成：%1").arg(r.summary()));
    for (const TransferRecord &rec : r.records) {
        const QString label = rec.title.isEmpty() ? rec.logicalKey : rec.title;
        QString line = QStringLiteral("  · [%1] %2 %3")
                           .arg(rec.kind, TransferRecord::actionLabel(rec.action), label);
        if (!rec.reason.isEmpty())
            line += QStringLiteral("（%1）").arg(rec.reason);
        log(line);
    }
    for (const QString &e : r.errors)
        log(QStringLiteral("  ✗ %1").arg(e));
    m_serverBadge->setState(StatusBadge::State::Connected);
}

void SyncPage::heartbeat(bool quiet)
{
    if (!m_api)
        return;
    if (!quiet)
        log(QStringLiteral("发送心跳…"));
    // 心跳通过 GET /status 实现（同时拿到同步开关/发现状态/监听端口，展示真实服务端状态）
    QNetworkReply *r = m_api->getSyncStatus();
    connect(r, &QNetworkReply::finished, this, [this, r, quiet] {
        QJsonDocument doc;
        QString err;
        if (!ApiClient::parseReply(r, &doc, &err)) {
            if (!quiet)
                log(QStringLiteral("心跳失败：%1").arg(err));
            return;
        }
        if (!quiet)
            log(QStringLiteral("心跳已发送"));
        const auto o = doc.object();
        const bool enabled = o.value(QStringLiteral("enabled")).toBool();
        const bool discovery = o.value(QStringLiteral("discovery_running")).toBool();
        const int listenPort = o.value(QStringLiteral("listen_port")).toInt(5600);
        QString text = enabled ? QStringLiteral("同步已开启") : QStringLiteral("同步未开启");
        if (enabled)
            text += QStringLiteral(" · 监听 %1").arg(listenPort);
        text += discovery ? QStringLiteral(" · 发现运行中") : QStringLiteral(" · 发现未开启");
        m_serverBadge->setState(StatusBadge::State::Connected, text);
    });
}

// ------------------------------------------------------------------ //
// 配置

void SyncPage::onRefreshConfig()
{
    refreshSyncConfig();
}

void SyncPage::refreshSyncConfig()
{
    if (!m_api)
        return;
    QNetworkReply *r = m_api->getSyncConfig();
    connect(r, &QNetworkReply::finished, this, [this, r] {
        QJsonDocument doc;
        QString err;
        if (!ApiClient::parseReply(r, &doc, &err)) {
            log(QStringLiteral("获取配置失败：%1").arg(err));
            return;
        }
        SyncConfig cfg = SyncConfig::fromJson(doc.object());
        m_chkEnabled->setChecked(cfg.enabled);
        m_chkHttp->setChecked(cfg.httpEnabled);
        // syncInbox / syncActivity / syncTodo 已移至 Settings → 同步 Tab，UI 不再展示
        m_editAlias->setText(cfg.selfAlias);
        m_editListenPort->setText(QString::number(cfg.listenPort));
        m_editUdpPort->setText(QString::number(cfg.udpPort));
        // 自动同步频率：匹配三档预设值，未匹配时默认选中「狂暴」
        {
            int idx = m_cmbSyncInterval->findData(static_cast<quint64>(cfg.syncInterval));
            if (idx < 0) {
                // 自定义值：狂暴(10) / 平和(300) / 静默(1800) 之外的选择最近一档
                if (cfg.syncInterval <= 60)
                    idx = 0;
                else if (cfg.syncInterval <= 900)
                    idx = 1;
                else
                    idx = 2;
            }
            m_cmbSyncInterval->setCurrentIndex(idx);
        }
        log(QStringLiteral("同步配置已刷新（同步范围请在「设置 → 同步」中查看）"));
    });
}

void SyncPage::onSaveConfig()
{
    if (!m_api)
        return;
    SyncConfig cfg;
    cfg.enabled = m_chkEnabled->isChecked();
    cfg.httpEnabled = m_chkHttp->isChecked();
    // syncInbox / syncActivity / syncTodo 由 SettingsDialog 统一管理，此处不修改
    cfg.selfAlias = m_editAlias->text().trimmed();
    cfg.listenPort = m_editListenPort->text().toInt();
    cfg.udpPort = m_editUdpPort->text().toInt();
    cfg.discoveryMethod = QStringLiteral("broadcast");
    cfg.syncInterval = m_cmbSyncInterval->currentData().toULongLong();

    QNetworkReply *r = m_api->setSyncConfig(cfg.toJson());
    connect(r, &QNetworkReply::finished, this, [this, r] {
        QJsonDocument doc;
        QString err;
        if (!ApiClient::parseReply(r, &doc, &err)) {
            log(QStringLiteral("保存配置失败：%1").arg(err));
            return;
        }
        log(QStringLiteral("同步配置已保存"));
        refreshSyncConfig();
    });
}

// ------------------------------------------------------------------ //
// 配对

void SyncPage::onCreatePairCode()
{
    if (!m_api)
        return;
    QNetworkReply *r = m_api->createPairCode();
    connect(r, &QNetworkReply::finished, this, [this, r] {
        QJsonDocument doc;
        QString err;
        if (!ApiClient::parseReply(r, &doc, &err)) {
            log(QStringLiteral("生成配对码失败：%1").arg(err));
            return;
        }
        const auto obj = doc.object();
        m_currentPairCode = obj.value(QStringLiteral("code")).toString();
        const auto expires = obj.value(QStringLiteral("expires_at")).toString();
        m_lblPairCode->setText(m_currentPairCode);
        log(QStringLiteral("配对码已生成：%1（有效期至 %2）")
            .arg(m_currentPairCode, formatLocal(expires)));
    });
}

void SyncPage::onJoinDevice()
{
    if (!m_api)
        return;
    const QString code = m_editPairCode->text().trimmed();
    if (code.isEmpty()) {
        log(QStringLiteral("请先输入配对码"));
        return;
    }
    QJsonObject device;
    device.insert(QStringLiteral("id"), m_api->deviceId());
    device.insert(QStringLiteral("name"), hostname() + QStringLiteral(" (aw-qtui)"));
    device.insert(QStringLiteral("device_kind"), QStringLiteral("windows"));
    device.insert(QStringLiteral("ip"), QString());
    device.insert(QStringLiteral("port"), 5600);

    QNetworkReply *r = m_api->joinWithCode(code, device);
    connect(r, &QNetworkReply::finished, this, [this, r] {
        QJsonDocument doc;
        QString err;
        if (!ApiClient::parseReply(r, &doc, &err)) {
            log(QStringLiteral("加入设备失败：%1").arg(err));
            return;
        }
        const auto obj = doc.object();
        log(QStringLiteral("已加入设备，对端：%1")
            .arg(obj.value(QStringLiteral("device")).toObject()
                 .value(QStringLiteral("name")).toString()));
        refreshDevices();
    });
}

void SyncPage::onInitiatePair()
{
    if (!m_api)
        return;
    const int row = m_devTable->currentRow();
    if (row < 0 || row >= m_devices.size()) {
        log(QStringLiteral("请先选择目标设备"));
        return;
    }
    const QString devId = m_devices[row].id;
    QNetworkReply *r = m_api->initiatePair(devId);
    connect(r, &QNetworkReply::finished, this, [this, r] {
        QJsonDocument doc;
        QString err;
        if (!ApiClient::parseReply(r, &doc, &err)) {
            log(QStringLiteral("发起配对失败：%1").arg(err));
            return;
        }
        log(QStringLiteral("已发起配对请求"));
        refreshDevices();
    });
}

void SyncPage::onAcceptPair()
{
    if (!m_api)
        return;
    const int row = m_devTable->currentRow();
    if (row < 0 || row >= m_devices.size()) {
        log(QStringLiteral("请先选择要接受的设备"));
        return;
    }
    const QString devId = m_devices[row].id;
    QNetworkReply *r = m_api->acceptPair(devId);
    connect(r, &QNetworkReply::finished, this, [this, r] {
        QJsonDocument doc;
        QString err;
        if (!ApiClient::parseReply(r, &doc, &err)) {
            log(QStringLiteral("接受配对失败：%1").arg(err));
            return;
        }
        log(QStringLiteral("已接受配对"));
        refreshDevices();
    });
}

// ------------------------------------------------------------------ //
// 设备操作

void SyncPage::onSyncNow()
{
    doSync();
}

void SyncPage::onRemoveDevice()
{
    if (!m_api)
        return;
    const int row = m_devTable->currentRow();
    if (row < 0 || row >= m_devices.size()) {
        log(QStringLiteral("请先选择要移除的设备"));
        return;
    }
    const QString devId = m_devices[row].id;
    QNetworkReply *r = m_api->removeDevice(devId);
    connect(r, &QNetworkReply::finished, this, [this, r] {
        QJsonDocument doc;
        QString err;
        if (!ApiClient::parseReply(r, &doc, &err)) {
            log(QStringLiteral("移除设备失败：%1").arg(err));
            return;
        }
        log(QStringLiteral("设备已移除"));
        refreshDevices();
    });
}

void SyncPage::onSetAlias()
{
    if (!m_api)
        return;
    const int row = m_devTable->currentRow();
    if (row < 0 || row >= m_devices.size()) {
        log(QStringLiteral("请先选择设备"));
        return;
    }
    const QString devId = m_devices[row].id;
    bool ok = false;
    const QString alias = QInputDialog::getText(this, QStringLiteral("设置设备别名"),
                                                QStringLiteral("别名："),
                                                QLineEdit::Normal,
                                                m_devices[row].alias, &ok);
    if (!ok)
        return;
    QNetworkReply *r = m_api->setDeviceAlias(devId, alias.trimmed());
    connect(r, &QNetworkReply::finished, this, [this, r] {
        QJsonDocument doc;
        QString err;
        if (!ApiClient::parseReply(r, &doc, &err)) {
            log(QStringLiteral("设置别名失败：%1").arg(err));
            return;
        }
        log(QStringLiteral("别名已更新"));
        refreshDevices();
    });
}

void SyncPage::onClearAllDevices()
{
    if (!m_api)
        return;
    const auto ret = QMessageBox::warning(
        this, QStringLiteral("清空所有配对"),
        QStringLiteral("将清除本机所有已配对设备信息，已同步的数据不受影响。确定继续？"),
        QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
    if (ret != QMessageBox::Yes)
        return;
    QNetworkReply *r = m_api->clearAllDevices();
    connect(r, &QNetworkReply::finished, this, [this, r] {
        QJsonDocument doc;
        QString err;
        if (!ApiClient::parseReply(r, &doc, &err)) {
            log(QStringLiteral("清空配对失败：%1").arg(err));
            return;
        }
        const int n = doc.object().value(QStringLiteral("cleared")).toInt();
        log(QStringLiteral("已清除 %1 台配对设备").arg(n));
        refreshDevices();
    });
}

void SyncPage::onClearLogs()
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
        m_log->clear();
    });
}

void SyncPage::onClearAllTrash()
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

// 回收站单条恢复 / 删除（restoreTrash / deleteTrash）
void SyncPage::onRestoreTrashRow()
{
    if (!m_api)
        return;
    const int row = m_trashTable->currentRow();
    if (row < 0 || m_trashTable->item(row, 0) == nullptr) {
        log(QStringLiteral("请先选择回收站条目"));
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
        log(QStringLiteral("已恢复回收站条目 #%1").arg(doc.object().value(QStringLiteral("id")).toVariant().toLongLong()));
        refreshTrash();
    });
}

void SyncPage::onDeleteTrashRow()
{
    if (!m_api)
        return;
    const int row = m_trashTable->currentRow();
    if (row < 0 || m_trashTable->item(row, 0) == nullptr) {
        log(QStringLiteral("请先选择回收站条目"));
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
        log(QStringLiteral("已永久删除回收站条目 #%1").arg(doc.object().value(QStringLiteral("id")).toVariant().toLongLong()));
        refreshTrash();
    });
}

// ------------------------------------------------------------------ //
// 快照传输（WiFi 热点点对点）

void SyncPage::onExportSnapshot()
{
    if (!m_api)
        return;
    const QString path = QFileDialog::getSaveFileName(
        this, QStringLiteral("导出快照"),
        QStringLiteral("aw-snapshot-%1.json").arg(QDate::currentDate().toString(QStringLiteral("yyyyMMdd"))),
        QStringLiteral("JSON (*.json)"));
    if (path.isEmpty())
        return;
    m_lblSnapshot->setText(QStringLiteral("正在导出…"));
    QNetworkReply *r = m_api->getSyncSnapshot();
    connect(r, &QNetworkReply::finished, this, [this, r, path] {
        QJsonDocument doc;
        QString err;
        if (!ApiClient::parseReply(r, &doc, &err)) {
            m_lblSnapshot->setText(QStringLiteral("导出失败：%1").arg(err));
            return;
        }
        QFile f(path);
        if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
            m_lblSnapshot->setText(QStringLiteral("写入文件失败：%1").arg(f.errorString()));
            return;
        }
        f.write(doc.toJson(QJsonDocument::Indented));
        f.close();
        m_lblSnapshot->setText(QStringLiteral("已导出 %1 bytes → %2")
                                   .arg(doc.toJson(QJsonDocument::Compact).size())
                                   .arg(QFileInfo(path).fileName()));
    });
}

void SyncPage::onImportSnapshot()
{
    if (!m_api)
        return;
    const QString path = QFileDialog::getOpenFileName(
        this, QStringLiteral("导入快照（合并到本机）"), QString(),
        QStringLiteral("JSON (*.json)"));
    if (path.isEmpty())
        return;
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) {
        m_lblSnapshot->setText(QStringLiteral("读取文件失败：%1").arg(f.errorString()));
        return;
    }
    QJsonParseError perr;
    const QJsonDocument doc = QJsonDocument::fromJson(f.readAll(), &perr);
    f.close();
    if (perr.error != QJsonParseError::NoError || !doc.isObject()) {
        m_lblSnapshot->setText(QStringLiteral("快照文件解析失败：%1").arg(perr.errorString()));
        return;
    }
    m_lblSnapshot->setText(QStringLiteral("正在导入合并…"));
    QNetworkReply *r = m_api->applySnapshot(doc.object());
    connect(r, &QNetworkReply::finished, this, [this, r] {
        QJsonDocument doc;
        QString err;
        if (!ApiClient::parseReply(r, &doc, &err)) {
            m_lblSnapshot->setText(QStringLiteral("导入失败：%1").arg(err));
            return;
        }
        const auto o = doc.object();
        const int applied = o.value(QStringLiteral("applied")).toInt();
        const int created = o.value(QStringLiteral("created")).toInt();
        const int updated = o.value(QStringLiteral("updated")).toInt();
        const int ignored = o.value(QStringLiteral("ignored")).toInt();
        const int conflicts = o.value(QStringLiteral("conflicts")).toInt();
        m_lblSnapshot->setText(QStringLiteral("导入完成：落库 %1（新增 %2 · 更新 %3）· 忽略 %4 · 冲突 %5")
                                   .arg(applied).arg(created).arg(updated).arg(ignored).arg(conflicts));
        refreshTrash();
        refreshDevices();
    });
}

// ------------------------------------------------------------------ //
// 回收站

void SyncPage::refreshTrash()
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
        m_trashTable->setRowCount(0);
        int row = 0;
        for (const auto &v : arr) {
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
        log(QStringLiteral("回收站 %1 条").arg(arr.size()));
    });
}

void SyncPage::refreshDeviceStats(const QString &deviceId)
{
    if (!m_api)
        return;
    QNetworkReply *r = m_api->getDeviceStats(deviceId);
    connect(r, &QNetworkReply::finished, this, [this, r] {
        QJsonDocument doc;
        QString err;
        if (!ApiClient::parseReply(r, &doc, &err)) {
            m_lblStats->setText(QStringLiteral("获取统计失败：%1").arg(err));
            return;
        }
        const SyncStats s = SyncStats::fromJson(doc.object());
        m_lblStats->setText(QStringLiteral("待同步：%1 条  |  待解决冲突：%2 条\n"
                                         "累计同步：%3 条（%4 bytes）\n"
                                         "本地笔记：%5 条  |  远端笔记：%6 条\n"
                                         "上次同步：%7  |  全量同步：%8\n"
                                         "最近错误：%9")
            .arg(s.pendingPushCount)
            .arg(s.pendingConflictCount)
            .arg(s.totalSyncedCount)
            .arg(s.totalSyncedSize)
            .arg(s.localNoteCount)
            .arg(s.remoteNoteCount)
            .arg(formatLocal(s.lastSyncAt))
            .arg(formatLocal(s.lastFullSyncAt))
            .arg(s.lastError.isEmpty() ? QStringLiteral("无") : s.lastError));
    });
}

// ------------------------------------------------------------------ //
// mDNS 自动发现

void SyncPage::onDiscover()
{
    if (!m_mdns)
        return;
    if (m_browsing) {
        m_mdns->stopBrowse();
        m_btnBrowse->setText(QStringLiteral("开始浏览"));
        m_browsing = false;
        return;
    }
    m_mdns->startBrowse();
    m_btnBrowse->setText(QStringLiteral("停止浏览"));
    m_browsing = true;
    log(QStringLiteral("开始浏览 %1").arg(kMdnsServiceType));
}

void SyncPage::rebuildPeerTable()
{
    m_peerTable->setRowCount(0);
    int row = 0;
    for (const SyncPeer &p : m_peers) {
        m_peerTable->insertRow(row);
        m_peerTable->setItem(row, 0, new QTableWidgetItem(p.name));
        m_peerTable->setItem(row, 1, new QTableWidgetItem(p.host));
        m_peerTable->setItem(row, 2, new QTableWidgetItem(QString::number(p.port)));
        ++row;
    }
}

void SyncPage::onPeerFound(const QString &name, const QString &host, int port)
{
    for (const SyncPeer &p : m_peers) {
        if (p.name == name)
            return;
    }
    m_peers.append({name, host, port});
    rebuildPeerTable();
    log(QStringLiteral("发现对端：%1 @ %2:%3").arg(name, host).arg(port));
}

void SyncPage::onPeerLost(const QString &name)
{
    for (int i = 0; i < m_peers.size(); ++i) {
        if (m_peers[i].name == name) {
            m_peers.removeAt(i);
            rebuildPeerTable();
            log(QStringLiteral("对端离线：%1").arg(name));
            return;
        }
    }
}

void SyncPage::onAddPeer()
{
    bool ok = false;
    const QString input = QInputDialog::getText(this, QStringLiteral("手动添加对端"),
                                                QStringLiteral("格式：主机:端口（如 192.168.1.5:5600）"),
                                                QLineEdit::Normal, QString(), &ok);
    if (!ok || input.trimmed().isEmpty())
        return;
    const QStringList parts = input.trimmed().split(QLatin1Char(':'));
    if (parts.size() != 2) {
        log(QStringLiteral("地址格式错误：%1").arg(input));
        return;
    }
    bool portOk = false;
    const int port = parts[1].toInt(&portOk);
    if (!portOk) {
        log(QStringLiteral("端口错误：%1").arg(input));
        return;
    }
    m_peers.append({parts[0], parts[0], port});
    rebuildPeerTable();
    log(QStringLiteral("已手动添加对端 %1").arg(input));
}

} // namespace awqtui
