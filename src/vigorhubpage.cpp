// vigorhubpage.cpp —— 「同步 → VigorHub」标签页实现
#include "vigorhubpage.h"

#include <QCheckBox>
#include <QComboBox>
#include <QFormLayout>
#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QTime>
#include <QVBoxLayout>
#include <QWidget>

#include "theme.h"
#include "vigorhubrelay.h"
#include "widgets.h"

namespace awqtui {

namespace {
QFrame *makeHint(const QString &text)
{
    auto *f = new QFrame;
    f->setObjectName(QStringLiteral("vigorhubHint"));
    f->setStyleSheet(scaleQss(QStringLiteral(
        "QFrame#vigorhubHint { background: rgba(127,127,127,0.10); border-radius: 6px; }"
        "QFrame#vigorhubHint QLabel { color: rgba(127,127,127,0.95); }")));
    auto *lay = new QVBoxLayout(f);
    lay->setContentsMargins(10, 8, 10, 8);
    auto *lbl = new QLabel(text);
    lbl->setWordWrap(true);
    lay->addWidget(lbl);
    return f;
}
} // namespace

VigorHubPage::VigorHubPage(VigorHubRelay *relay, QWidget *parent)
    : QWidget(parent)
    , m_relay(relay)
{
    buildUi();
    loadConfigIntoUi();
    refreshStatus();

    connect(m_relay, &VigorHubRelay::syncStarted, this, &VigorHubPage::onSyncStarted);
    connect(m_relay, &VigorHubRelay::syncFinished, this, &VigorHubPage::onSyncFinished);
    connect(m_relay, &VigorHubRelay::logLine, this, &VigorHubPage::log);
}

void VigorHubPage::buildUi()
{
    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(16, 14, 16, 14);
    root->setSpacing(10);

    auto *head = new QHBoxLayout;
    auto *title = new QLabel(QStringLiteral("🖥 VigorHub 推送"));
    title->setStyleSheet(scaleQss(QStringLiteral("font-size: 16px; font-weight: 600;")));
    head->addWidget(title);
    head->addStretch(1);
    m_badge = new StatusBadge;
    head->addWidget(m_badge);
    root->addLayout(head);

    root->addWidget(makeHint(QStringLiteral(
        "把本机的应用使用时长（自动裁掉「离开」时段）推送给 VigorHub「健康运动」桌面端。"
        "VigorHub 侧需开启「局域网同步」（默认端口 18765）。推送按天全量幂等，"
        "重复推送不会重复累计；本应用关闭期间的使用时长会在下次推送时一并补上。")));

    // ---- 配置区 ----
    auto *form = new QFormLayout;
    form->setLabelAlignment(Qt::AlignRight | Qt::AlignVCenter);
    form->setHorizontalSpacing(12);
    form->setVerticalSpacing(8);

    m_chkEnabled = new QCheckBox(QStringLiteral("自动推送屏幕使用数据"));
    form->addRow(QStringLiteral("推送开关"), m_chkEnabled);

    m_editAddress = new QLineEdit;
    m_editAddress->setPlaceholderText(QStringLiteral("http://127.0.0.1:18765"));
    m_editAddress->setMaximumWidth(360);
    form->addRow(QStringLiteral("桌面地址"), m_editAddress);

    m_cmbInterval = new QComboBox;
    // data(): 自动推送间隔秒数；0 = 仅手动
    m_cmbInterval->addItem(QStringLiteral("仅手动"), 0);
    m_cmbInterval->addItem(QStringLiteral("每 1 分钟"), 60);
    m_cmbInterval->addItem(QStringLiteral("每 5 分钟"), 300);
    m_cmbInterval->addItem(QStringLiteral("每 15 分钟"), 900);
    m_cmbInterval->addItem(QStringLiteral("每 30 分钟"), 1800);
    m_cmbInterval->setMaximumWidth(180);
    form->addRow(QStringLiteral("自动间隔"), m_cmbInterval);

    m_btnSave = new QPushButton(QStringLiteral("保存并应用"));
    m_btnSave->setMaximumWidth(140);
    form->addRow(QString(), m_btnSave);
    root->addLayout(form);

    // ---- 状态 + 手动同步 ----
    auto *statusRow = new QHBoxLayout;
    m_btnSyncNow = new QPushButton(QStringLiteral("立即同步"));
    m_btnSyncNow->setMaximumWidth(140);
    statusRow->addWidget(m_btnSyncNow);
    m_lblStatus = new QLabel;
    m_lblStatus->setWordWrap(true);
    statusRow->addWidget(m_lblStatus, 1);
    root->addLayout(statusRow);

    // ---- 日志 ----
    m_log = new QPlainTextEdit;
    m_log->setReadOnly(true);
    m_log->setMaximumHeight(140);
    m_log->setPlaceholderText(QStringLiteral("推送日志"));
    root->addWidget(m_log, 1);

    connect(m_btnSave, &QPushButton::clicked, this, &VigorHubPage::onSaveConfig);
    connect(m_btnSyncNow, &QPushButton::clicked, this, &VigorHubPage::onSyncNow);
}

void VigorHubPage::loadConfigIntoUi()
{
    const VigorHubRelayConfig cfg = m_relay->config();
    m_chkEnabled->setChecked(cfg.enabled);
    m_editAddress->setText(cfg.address);
    const int idx = m_cmbInterval->findData(cfg.intervalSec);
    m_cmbInterval->setCurrentIndex(idx >= 0 ? idx : 2); // 默认每 5 分钟
}

void VigorHubPage::refreshStatus()
{
    const bool ok = m_relay->lastOk();
    if (m_relay->lastResult().isEmpty()) {
        m_badge->setState(StatusBadge::State::Unknown, QStringLiteral("未同步"));
        m_lblStatus->setText(QStringLiteral("尚未推送过"));
    } else {
        m_badge->setState(ok ? StatusBadge::State::Connected : StatusBadge::State::Error,
                          ok ? QStringLiteral("正常") : QStringLiteral("失败"));
        m_lblStatus->setText(m_relay->lastResult());
    }
}

void VigorHubPage::onSaveConfig()
{
    VigorHubRelayConfig cfg;
    cfg.enabled = m_chkEnabled->isChecked();
    cfg.address = m_editAddress->text().trimmed();
    cfg.intervalSec = m_cmbInterval->currentData().toInt();
    if (cfg.address.isEmpty())
        cfg.address = QStringLiteral("http://127.0.0.1:18765");
    m_relay->setConfig(cfg);
    log(QStringLiteral("[vigorhub] 配置已应用"));
}

void VigorHubPage::onSyncNow()
{
    m_relay->syncNow(true);
}

void VigorHubPage::onSyncStarted()
{
    m_btnSyncNow->setEnabled(false);
    m_badge->setState(StatusBadge::State::Syncing, QStringLiteral("同步中"));
}

void VigorHubPage::onSyncFinished(bool ok, const QString &msg)
{
    m_btnSyncNow->setEnabled(true);
    Q_UNUSED(ok);
    Q_UNUSED(msg);
    refreshStatus();
}

void VigorHubPage::log(const QString &line)
{
    m_log->appendPlainText(QStringLiteral("[%1] %2")
                               .arg(QTime::currentTime().toString(QStringLiteral("hh:mm:ss")), line));
}

} // namespace awqtui
