// syncpage.cpp —— 局域网同步页视图 (aw-sync-rust /api/0/sync)
#include "syncpage.h"
#include "ui_syncpage.h"

#include "apiclient.h"
#include "config.h"
#include "mdnsdiscovery.h"
#include "syncservice.h"
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
#include <QMenu>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSettings>
#include <QSpinBox>
#include <QTableWidget>
#include <QTabWidget>
#include <QTimer>
#include <QToolButton>
#include <QVBoxLayout>

namespace awqtui {

SyncPage::SyncPage(ApiClient *api, SyncService *service, QWidget *parent)
    : QWidget(parent), m_api(api), m_service(service)
{
    buildUi();
    connect(m_api, &ApiClient::destroyed, this, [this] { m_api = nullptr; });

    // 引擎信号 → 视图：设备表 / 徽标 / 日志 / 配对横幅
    connect(m_service, &SyncService::devicesChanged, this, &SyncPage::renderDevices);
    connect(m_service, &SyncService::statusUpdated, this,
            [this](const QString &text) { m_lastStatusText = text; applyBadgeState(0, text); });
    connect(m_service, &SyncService::statusError, this,
            [this](const QString &err) { applyBadgeState(3, err); });
    connect(m_service, &SyncService::syncBusy, this,
            [this](const QString &name) {
                applyBadgeState(1, name.isEmpty() ? QStringLiteral("查询设备…")
                                                  : QStringLiteral("同步中…"));
            });
    connect(m_service, &SyncService::syncIdle, this,
            [this] { applyBadgeState(0, m_lastStatusText); });
    connect(m_service, &SyncService::logLine, this, &SyncPage::log);
}

SyncPage::~SyncPage()
{
    delete ui;
}

// 徽标状态映射（0=已连接 1=同步中 2=断开 3=错误）
void SyncPage::applyBadgeState(int state, const QString &text)
{
    if (!m_serverBadge)
        return;
    auto s = StatusBadge::State::Connected;
    if (state == 1) s = StatusBadge::State::Syncing;
    else if (state == 2) s = StatusBadge::State::Disconnected;
    else if (state == 3) s = StatusBadge::State::Error;
    m_serverBadge->setState(s, text);
}

void SyncPage::onEnteredSyncPage()
{
    // 进入局域网同步界面时启动服务端发现广播（aw-server-rust 9bcbc01；桌面端实为常驻）
    if (m_api)
        m_api->discoveryStart();
    // 引擎：局域网自动开启同步 + 立即拉取设备/心跳
    if (m_service)
        m_service->onSyncPageOpened();
}

void SyncPage::refreshDevices()
{
    if (m_service)
        m_service->refreshDevices();
}

void SyncPage::heartbeat(bool quiet)
{
    if (m_service)
        m_service->heartbeat(quiet);
}

void SyncPage::buildUi()
{
    // 静态布局来自 Qt Designer（syncpage.ui -> ui_syncpage.h）
    ui = new Ui::SyncPage;
    ui->setupUi(this);

    // ── 成员别名：静态布局归属 .ui 文件 ──
    m_serverEdit = ui->serverEdit;
    m_serverBadge = ui->serverBadge;
    m_devTable = ui->devTable;
    m_chkEnabled = ui->chkEnabled;
    m_chkHttp = ui->chkHttp;
    m_editAlias = ui->editAlias;
    m_editListenPort = ui->editListenPort;
    m_editUdpPort = ui->editUdpPort;
    m_cmbSyncInterval = ui->cmbSyncInterval;
    m_btnSaveConfig = ui->btnSaveConfig;
    m_btnSyncNow = ui->btnSyncNow;
    m_btnRemoveDevice = ui->btnRemoveDevice;
    m_btnClearLogs = ui->btnClearLogs;
    m_log = ui->logView;
    m_lblStats = ui->lblStats;
    m_trashTable = ui->trashTable;
    m_btnRestoreTrash = ui->btnRestoreTrash;
    m_btnDeleteTrash = ui->btnDeleteTrash;
    m_btnClearTrash = ui->btnClearTrash;
    m_pairBanner = ui->pairBanner;
    m_pairBannerLbl = ui->pairBannerLbl;

    // ── 运行时才能确定的内容：主题色、DPI 缩放、菜单、档位数据 ──
    m_pairBanner->setStyleSheet(QStringLiteral(
        "QWidget { background: rgba(76,139,245,0.14); border: 1px solid %1; border-radius: 6px; }")
                                    .arg(kColorAccent));
    ui->discoverHint->setStyleSheet(QStringLiteral("color: %1; font-size: 12px;").arg(kColorFgMuted));
    ui->syncRangeHint->setStyleSheet(QStringLiteral("color: %1; font-size: 12px;").arg(kColorFgMuted));

    m_btnSyncNow->setObjectName(QStringLiteral("PrimaryBtn"));
    ui->btnAcceptPair->setObjectName(QStringLiteral("PrimaryBtn"));
    m_btnSaveConfig->setObjectName(QStringLiteral("PrimaryBtn"));

    m_devTable->verticalHeader()->setDefaultSectionSize(si(42));
    m_devTable->setColumnWidth(0, 180);
    m_devTable->setColumnWidth(1, 70);
    m_devTable->setColumnWidth(2, 110);
    m_devTable->setColumnWidth(3, 60);
    m_devTable->setColumnWidth(4, 140);
    m_devTable->setColumnWidth(5, 140);
    m_devTable->setColumnWidth(7, si(240));

    m_cmbSyncInterval->setItemData(0, 10);
    m_cmbSyncInterval->setItemData(1, 60);
    m_cmbSyncInterval->setItemData(2, 300);
    m_cmbSyncInterval->setItemData(3, 0);

    auto *moreMenu = new QMenu(ui->btnMore);
    moreMenu->addAction(QStringLiteral("设置别名…"), this, &SyncPage::onSetAlias);
    moreMenu->addAction(QStringLiteral("使用配对码配对…"), this, &SyncPage::onUsePairCode);
    moreMenu->addSeparator();
    moreMenu->addAction(QStringLiteral("导出快照（热点直连）"), this, &SyncPage::onExportSnapshot);
    moreMenu->addAction(QStringLiteral("导入合并快照…"), this, &SyncPage::onImportSnapshot);
    moreMenu->addSeparator();
    moreMenu->addAction(QStringLiteral("清空所有配对"), this, &SyncPage::onClearAllDevices);
    ui->btnMore->setMenu(moreMenu);

    m_serverEdit->setText(m_api ? m_api->baseUrl() : kDefaultServerUrl);

    // ── 信号连接 ──
    connect(ui->btnApplyServer, &QPushButton::clicked, this, [this] {
        setServerUrl(m_serverEdit->text().trimmed());
        refreshDevices();
    });
    connect(m_btnSyncNow, &QPushButton::clicked, this, &SyncPage::onSyncNow);
    connect(m_btnRemoveDevice, &QPushButton::clicked, this, &SyncPage::onRemoveDevice);
    connect(ui->btnRefreshDevices, &QPushButton::clicked, this, &SyncPage::refreshDevices);
    connect(ui->btnAcceptPair, &QPushButton::clicked, this, &SyncPage::onAcceptPair);
    connect(ui->btnIgnorePair, &QPushButton::clicked, this, [this] {
        if (m_service && !m_pairBannerId.isEmpty())
            m_service->markPairIgnored(m_pairBannerId);
        m_pairBanner->setVisible(false);
    });
    connect(m_btnSaveConfig, &QPushButton::clicked, this, &SyncPage::onSaveConfig);
    connect(ui->btnRefreshConfig, &QPushButton::clicked, this, &SyncPage::refreshSyncConfig);
    connect(ui->btnRefreshLogs, &QPushButton::clicked, this, [this] {
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
    connect(m_btnClearLogs, &QPushButton::clicked, this, &SyncPage::onClearLogs);
    connect(ui->btnRefreshTrash, &QPushButton::clicked, this, &SyncPage::refreshTrash);
    connect(m_btnRestoreTrash, &QPushButton::clicked, this, &SyncPage::onRestoreTrashRow);
    connect(m_btnDeleteTrash, &QPushButton::clicked, this, &SyncPage::onDeleteTrashRow);
    connect(m_btnClearTrash, &QPushButton::clicked, this, &SyncPage::onClearAllTrash);

    // 底部状态栏（本页操作日志）；引擎日志经 SyncService::logLine 也汇入这里
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

// 渲染设备表（引擎 devicesChanged 回调 / 页面主动刷新后）
void SyncPage::renderDevices(const QList<SyncDevice> &devices)
{
    m_devices = devices;
    m_devTable->setRowCount(0);
    int row = 0;
    for (const SyncDevice &d : m_devices) {
        m_devTable->insertRow(row);
        m_devTable->setRowHeight(row, si(42)); // 逐行显式设高，保证操作列按钮完整可见
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
        // 在线状态（对齐 Android Device.isEffectivelyOnline）：
        bool online = d.isOnline;
        if (!d.paired && !d.isSelf && !d.lastSeenAt.isEmpty()) {
            const QDateTime seen = QDateTime::fromString(d.lastSeenAt, Qt::ISODate);
            online = seen.isValid() && seen.secsTo(QDateTime::currentDateTimeUtc()) < 30;
        }
        QString status;
        if (d.isSelf)
            status = QStringLiteral("本机");
        else if (!d.paired)
            status = online ? QStringLiteral("在线 · 未配对") : QStringLiteral("离线 · 未配对");
        else if (online)
            status = QStringLiteral("在线");
        else
            status = QStringLiteral("离线");
        put(6, status);
        auto *opCell = new QWidget;
        auto *opLay = new QHBoxLayout(opCell);
        opLay->setContentsMargins(4, 0, 4, 0);
        opLay->setSpacing(4);
        if (!d.isSelf && d.paired && online) {
            auto *btnSync = new QPushButton(QStringLiteral("同步"));
            btnSync->setProperty("deviceId", d.id);
            connect(btnSync, &QPushButton::clicked, this, [this] {
                auto *b = qobject_cast<QPushButton*>(sender());
                if (b && m_service)
                    m_service->syncDevice(b->property("deviceId").toString());
            });
            btnSync->setMinimumWidth(si(64));
            btnSync->setFixedHeight(si(34));
            opLay->addWidget(btnSync);
        }
        if (!d.isSelf && !d.paired && online) {
            auto *btnInitiate = new QPushButton(QStringLiteral("发起配对"));
            btnInitiate->setProperty("deviceId", d.id);
            connect(btnInitiate, &QPushButton::clicked, this, &SyncPage::onInitiatePair);
            btnInitiate->setMinimumWidth(si(110));
            btnInitiate->setFixedHeight(si(34));
            QFont f1 = btnInitiate->font();
            f1.setPixelSize(si(14));
            f1.setWeight(QFont::Medium);
            btnInitiate->setFont(f1);
            opLay->addWidget(btnInitiate);
        }
        if (!d.isSelf && !d.paired && online && d.pairRequestPending) {
            auto *btnAccept = new QPushButton(QStringLiteral("接受配对"));
            btnAccept->setProperty("deviceId", d.id);
            connect(btnAccept, &QPushButton::clicked, this, &SyncPage::onAcceptPair);
            btnAccept->setMinimumWidth(si(110));
            btnAccept->setFixedHeight(si(34));
            QFont f2 = btnAccept->font();
            f2.setPixelSize(si(14));
            f2.setWeight(QFont::Medium);
            btnAccept->setFont(f2);
            opLay->addWidget(btnAccept);
        }
        opLay->addStretch(1);
        m_devTable->setCellWidget(row, 7, opCell);
        ++row;
    }
    if (m_devices.isEmpty())
        log(QStringLiteral("设备表为空"));
    else
        log(QStringLiteral("设备 %1 台").arg(m_devices.size()));
    updatePairBanner();
}

void SyncPage::updatePairBanner()
{
    QString pendingId, pendingName;
    for (const SyncDevice &d : m_devices) {
        if (!d.isSelf && !d.paired && d.pairRequestPending) {
            pendingId = d.id;
            pendingName = d.displayName();
            break;
        }
    }
    if (pendingId.isEmpty()) {
        m_pairBanner->setVisible(false);
        return;
    }
    m_pairBannerId = pendingId;
    m_pairBannerLbl->setText(QStringLiteral("设备「%1」想与本机配对").arg(pendingName));
    // 横幅按钮复用 onAcceptPair：从 sender 的 deviceId 属性取目标
    for (QPushButton *b : m_pairBanner->findChildren<QPushButton*>()) {
        if (b->text() == QStringLiteral("接受"))
            b->setProperty("deviceId", pendingId);
    }
    m_pairBanner->setVisible(true);
}

void SyncPage::onUsePairCode()
{
    if (!m_api)
        return;
    // ① 展示本机配对码（对端在「使用配对码配对」中输入它）
    QNetworkReply *rc = m_api->createPairCode();
    connect(rc, &QNetworkReply::finished, this, [this, rc] {
        QJsonDocument doc;
        QString err;
        if (!ApiClient::parseReply(rc, &doc, &err)) {
            log(QStringLiteral("生成配对码失败：%1").arg(err));
            return;
        }
        const QString myCode = doc.object().value(QStringLiteral("code")).toString();
        // ② 输入对端的配对码
        bool ok = false;
        const QString peerCode = QInputDialog::getText(
            this, QStringLiteral("使用配对码配对"),
            QStringLiteral("本机配对码：%1\n（在对端设备上输入此码）\n\n请输入对端设备的配对码：").arg(myCode),
            QLineEdit::Normal, QString(), &ok);
        if (!ok || peerCode.trimmed().isEmpty())
            return;
        // ③ 带上本机设备信息加入
        QJsonObject self;
        for (const SyncDevice &d : m_devices)
            if (d.isSelf)
                self = d.toJson();
        QNetworkReply *rj = m_api->joinWithCode(peerCode.trimmed(), self);
        connect(rj, &QNetworkReply::finished, this, [this, rj] {
            QJsonDocument doc2;
            QString err2;
            if (!ApiClient::parseReply(rj, &doc2, &err2)) {
                log(QStringLiteral("配对码配对失败：%1").arg(err2));
                return;
            }
            log(QStringLiteral("配对码配对成功"));
            refreshDevices();
            // 配对成功立即补一次全量同步
            if (m_service)
                QTimer::singleShot(800, this, [this] { m_service->doSync(); });
        });
    });
}

void SyncPage::onSyncNow()
{
    // 选中了有效设备 → 只同步它；否则同步全部已配对在线设备
    const int row = m_devTable->currentRow();
    if (row >= 0 && row < m_devices.size() && !m_devices[row].isSelf && m_devices[row].paired) {
        if (m_service)
            m_service->syncDevice(m_devices[row].id);
        return;
    }
    if (m_service)
        m_service->doSync();
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
        m_editAlias->setText(cfg.selfAlias);
        m_editListenPort->setText(QString::number(cfg.listenPort));
        m_editUdpPort->setText(QString::number(cfg.udpPort));
        {
            const quint64 val = cfg.syncInterval;
            while (m_cmbSyncInterval->count() > 4)
                m_cmbSyncInterval->removeItem(m_cmbSyncInterval->count() - 1);
            int idx = m_cmbSyncInterval->findData(val);
            if (idx < 0) {
                m_cmbSyncInterval->addItem(QStringLiteral("自定义（每 %1 秒）").arg(val), val);
                idx = m_cmbSyncInterval->count() - 1;
            }
            m_cmbSyncInterval->setCurrentIndex(idx);
        }
        if (m_service)
            m_service->setEnabled(cfg.enabled);
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
        if (m_service)
            m_service->setEnabled(m_chkEnabled->isChecked());
        log(QStringLiteral("同步配置已保存"));
        refreshSyncConfig();
    });
}

// ------------------------------------------------------------------ //
// 配对

void SyncPage::onInitiatePair()
{
    if (!m_api)
        return;
    auto *btn = qobject_cast<QPushButton*>(sender());
    const QString devId = btn ? btn->property("deviceId").toString() : QString();
    if (devId.isEmpty()) {
        log(QStringLiteral("请先选择目标设备"));
        return;
    }
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
    auto *btn = qobject_cast<QPushButton*>(sender());
    const QString devId = btn ? btn->property("deviceId").toString() : QString();
    if (devId.isEmpty()) {
        log(QStringLiteral("请先选择要接受的设备"));
        return;
    }
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

// 回收站单条恢复 / 删除
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
    log(QStringLiteral("正在导出快照…"));
    QNetworkReply *r = m_api->getSyncSnapshot();
    connect(r, &QNetworkReply::finished, this, [this, r, path] {
        QJsonDocument doc;
        QString err;
        if (!ApiClient::parseReply(r, &doc, &err)) {
            log(QStringLiteral("导出失败：%1").arg(err));
            return;
        }
        QFile f(path);
        if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
            log(QStringLiteral("写入文件失败：%1").arg(f.errorString()));
            return;
        }
        f.write(doc.toJson(QJsonDocument::Indented));
        f.close();
        log(QStringLiteral("已导出 %1 bytes → %2")
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
        log(QStringLiteral("读取文件失败：%1").arg(f.errorString()));
        return;
    }
    QJsonParseError perr;
    const QJsonDocument doc = QJsonDocument::fromJson(f.readAll(), &perr);
    f.close();
    if (perr.error != QJsonParseError::NoError || !doc.isObject()) {
        log(QStringLiteral("快照文件解析失败：%1").arg(perr.errorString()));
        return;
    }
    log(QStringLiteral("正在导入合并…"));
    QNetworkReply *r = m_api->applySnapshot(doc.object());
    connect(r, &QNetworkReply::finished, this, [this, r] {
        QJsonDocument doc;
        QString err;
        if (!ApiClient::parseReply(r, &doc, &err)) {
            log(QStringLiteral("导入失败：%1").arg(err));
            return;
        }
        const auto o = doc.object();
        const int applied = o.value(QStringLiteral("applied")).toInt();
        const int created = o.value(QStringLiteral("created")).toInt();
        const int updated = o.value(QStringLiteral("updated")).toInt();
        const int ignored = o.value(QStringLiteral("ignored")).toInt();
        const int conflicts = o.value(QStringLiteral("conflicts")).toInt();
        log(QStringLiteral("导入完成：落库 %1（新增 %2 · 更新 %3）· 忽略 %4 · 冲突 %5")
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

} // namespace awqtui