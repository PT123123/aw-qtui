// syncpage.cpp
#include "syncpage.h"

#include "apiclient.h"
#include "config.h"
#include "mdnsdiscovery.h"
#include "theme.h"
#include "widgets.h"

#include <QColor>
#include <QDateTime>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QInputDialog>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSpinBox>
#include <QTableWidget>
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

    // ---- 设备注册表 + 操作 ----
    auto *devBox = new QGroupBox(QStringLiteral("设备注册表"));
    auto *dl = new QVBoxLayout(devBox);
    m_devTable = new QTableWidget(0, 6);
    m_devTable->setHorizontalHeaderLabels({QStringLiteral("设备"), QStringLiteral("平台"),
                                           QStringLiteral("最后在线"), QStringLiteral("最后同步"),
                                           QStringLiteral("待同步"), QStringLiteral("版本")});
    m_devTable->verticalHeader()->setVisible(false);
    m_devTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_devTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_devTable->horizontalHeader()->setStretchLastSection(true);
    m_devTable->setColumnWidth(0, 200);
    m_devTable->setColumnWidth(1, 80);
    m_devTable->setColumnWidth(2, 140);
    m_devTable->setColumnWidth(3, 140);
    m_devTable->setColumnWidth(4, 70);
    dl->addWidget(m_devTable);
    auto *dlRow = new QHBoxLayout;
    auto *btnSync = new QPushButton(QStringLiteral("立即同步"));
    btnSync->setObjectName(QStringLiteral("PrimaryBtn"));
    connect(btnSync, &QPushButton::clicked, this, &SyncPage::doSync);
    auto *btnHeart = new QPushButton(QStringLiteral("心跳注册"));
    connect(btnHeart, &QPushButton::clicked, this, &SyncPage::heartbeat);
    auto *btnDevRefresh = new QPushButton(QStringLiteral("刷新设备"));
    connect(btnDevRefresh, &QPushButton::clicked, this, &SyncPage::refreshDevices);
    dlRow->addWidget(btnSync);
    dlRow->addWidget(btnHeart);
    dlRow->addWidget(btnDevRefresh);
    dlRow->addStretch(1);
    dl->addLayout(dlRow);
    root->addWidget(devBox);

    // ---- mDNS 自动发现 ----
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
    root->addWidget(mdnsBox);

    // ---- 日志 ----
    auto *logBox = new QGroupBox(QStringLiteral("活动日志"));
    auto *ll = new QVBoxLayout(logBox);
    m_log = new QPlainTextEdit;
    m_log->setReadOnly(true);
    m_log->setMaximumHeight(150);
    ll->addWidget(m_log);
    root->addWidget(logBox, 1);

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
        const auto arr = doc.object().value(QStringLiteral("devices")).toArray();
        for (const auto &v : arr) {
            if (v.isObject())
                m_devices << DeviceInfo::fromJson(v.toObject());
        }
        m_devTable->setRowCount(0);
        int row = 0;
        for (const DeviceInfo &d : m_devices) {
            m_devTable->insertRow(row);
            const auto put = [&](int col, const QString &s) {
                auto *it = new QTableWidgetItem(s);
                if (col == 4 && d.pendingChanges > 0)
                    it->setForeground(QColor(kColorWarn));
                if (d.isCurrent && col == 0)
                    it->setForeground(QColor(kColorAccent));
                m_devTable->setItem(row, col, it);
            };
            put(0, d.name + (d.isCurrent ? QStringLiteral("（本机）") : QString()));
            put(1, d.platform);
            put(2, formatLocal(d.lastSeenAt));
            put(3, formatLocal(d.lastSyncedAt));
            put(4, QString::number(d.pendingChanges));
            put(5, QString::number(d.version));
            ++row;
        }
        if (m_devices.isEmpty())
            log(QStringLiteral("设备表为空"));
        else
            log(QStringLiteral("设备 %1 台，全局版本 %2").arg(m_devices.size()).arg(doc.object()
                .value(QStringLiteral("global_version")).toVariant().toLongLong()));
        m_serverBadge->setState(StatusBadge::State::Connected);
    });
}

void SyncPage::doSync()
{
    if (!m_api)
        return;
    m_serverBadge->setState(StatusBadge::State::Syncing, QStringLiteral("同步中…"));
    log(QStringLiteral("开始同步…"));

    // SyncRequest：与 aw-inbox 对齐
    QJsonObject req;
    req.insert(QStringLiteral("device_id"), m_api->deviceId());
    req.insert(QStringLiteral("base_version"), 0);
    req.insert(QStringLiteral("device_versions"), QJsonObject());
    req.insert(QStringLiteral("last_full_sync_at"), QJsonValue::Null);
    req.insert(QStringLiteral("push_changes"), QJsonArray());
    req.insert(QStringLiteral("pull_limit"), 50);

    QNetworkReply *r = m_api->sync(req);
    connect(r, &QNetworkReply::finished, this, [this, r] {
        QJsonDocument doc;
        QString err;
        if (!ApiClient::parseReply(r, &doc, &err)) {
            m_serverBadge->setState(StatusBadge::State::Error, QStringLiteral("同步失败"));
            log(QStringLiteral("同步失败：%1").arg(err));
            return;
        }
        syncComplete(SyncSummary::fromJson(doc.object()));
        refreshDevices();
    });
}

void SyncPage::syncComplete(const SyncSummary &s)
{
    log(QStringLiteral("同步完成：拉取 %1 条，冲突 %2，全局版本 %3，has_more=%4")
            .arg(s.pulledCount)
            .arg(s.conflictCount)
            .arg(s.currentVersion)
            .arg(s.hasMore ? QStringLiteral("是") : QStringLiteral("否")));
    for (const auto &c : s.conflicts) {
        const QJsonObject o = c.toObject();
        const qint64 serverId = o.value(QStringLiteral("server_note")).toObject()
                                    .value(QStringLiteral("id")).toVariant().toLongLong();
        log(QStringLiteral("  ⚠ 冲突笔记 #%1").arg(serverId));
    }
    m_serverBadge->setState(StatusBadge::State::Connected);
}

void SyncPage::heartbeat(bool quiet)
{
    if (!m_api)
        return;
    if (!quiet)
        log(QStringLiteral("发送心跳…"));
    QNetworkReply *r = m_api->deviceHeartbeat(hostname() + QStringLiteral(" (aw-qtui)"), platform(),
                                              m_devices.isEmpty() ? 0 : m_devices.first().pendingChanges, 0);
    connect(r, &QNetworkReply::finished, this, [this, r, quiet] {
        QJsonDocument doc;
        QString err;
        if (!ApiClient::parseReply(r, &doc, &err)) {
            if (!quiet)
                log(QStringLiteral("心跳失败：%1").arg(err));
            return;
        }
        if (!quiet)
            log(QStringLiteral("心跳已注册，本机已在设备表上线"));
        m_serverBadge->setState(StatusBadge::State::Connected);
        refreshDevices();
    });
}

// ------------------------------------------------------------------ //

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
        if (p.name == name) {
            return;
        }
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
