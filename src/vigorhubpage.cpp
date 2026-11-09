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
    auto *title = new QLabel(QStringLiteral("🖥 VigorHub 同步"));
    title->setStyleSheet(scaleQss(QStringLiteral("font-size: 16px; font-weight: 600;")));
    head->addWidget(title);
    head->addStretch(1);
    m_badge = new StatusBadge;
    head->addWidget(m_badge);
    root->addLayout(head);

    root->addWidget(makeHint(QStringLiteral(
        "把本机的应用使用时长（自动裁掉「离开」时段）交给 VigorHub「健康运动」桌面端，两条路同时开着："
        "① 按下面的「桌面地址」主动推送（默认本机 18765；该端口被占用时桌面端会顺延到 18766+，"
        "本应用会自动探测并记住实际端口）；② 在局域网里宣告自己，桌面端发现后主动来拉，"
        "这条路不依赖上面的地址，本机传输和局域网其它主机走同一套代码。"
        "桌面端侧需开启「局域网同步」。两条路都按天全量幂等，重复同步不会重复累计；"
        "本应用关闭期间的使用时长会在下次同步时一并补上。")));

    // ---- 配置区 ----
    auto *form = new QFormLayout;
    form->setLabelAlignment(Qt::AlignRight | Qt::AlignVCenter);
    form->setHorizontalSpacing(12);
    form->setVerticalSpacing(8);

    m_chkEnabled = new QCheckBox(QStringLiteral("同步屏幕使用数据（推送 + 允许桌面端来拉）"));
    form->addRow(QStringLiteral("同步开关"), m_chkEnabled);

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

    m_editAlias = new QLineEdit;
    m_editAlias->setPlaceholderText(QStringLiteral("ActivityWatch（本机主机名）"));
    m_editAlias->setMaximumWidth(360);
    form->addRow(QStringLiteral("本机名称"), m_editAlias);

    m_editSource = new QLineEdit;
    m_editSource->setPlaceholderText(QStringLiteral("desktop"));
    m_editSource->setMaximumWidth(200);
    form->addRow(QStringLiteral("来源标识"), m_editSource);
    auto *srcHint = new QLabel(QStringLiteral(
        "桌面端按「日期 + 来源标识 + 应用」整条替换。局域网里有多台电脑跑 ActivityWatch 时，"
        "除本机外每台都要填一个不同的值，否则两台机器的时长会互相覆盖。本机留空即可。"));
    srcHint->setWordWrap(true);
    srcHint->setStyleSheet(scaleQss(QStringLiteral("color: rgba(127,127,127,0.95); font-size: 12px;")));
    form->addRow(QString(), srcHint);

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

    // 底栏读数：伴随服务在不在跑、桌面端有没有真的来拉过。
    m_lblPeer = new QLabel;
    m_lblPeer->setWordWrap(true);
    m_lblPeer->setStyleSheet(scaleQss(QStringLiteral("color: rgba(127,127,127,0.95); font-size: 12px;")));
    root->addWidget(m_lblPeer);
    refreshPeerStatus();

    connect(m_btnSave, &QPushButton::clicked, this, &VigorHubPage::onSaveConfig);
    connect(m_btnSyncNow, &QPushButton::clicked, this, &VigorHubPage::onSyncNow);
    connect(m_relay, &VigorHubRelay::peerStateChanged, this, &VigorHubPage::refreshPeerStatus);
}

void VigorHubPage::refreshPeerStatus()
{
    const quint16 port = m_relay->peerPort();
    if (port == 0) {
        m_lblPeer->setText(QStringLiteral("局域网伴随服务：未运行（同步开关关闭时不宣告、不接受拉取）"));
        return;
    }
    const QString pulled = m_relay->lastPullAt();
    m_lblPeer->setText(QStringLiteral("局域网伴随服务：本机端口 %1 · 组播 224.0.0.167:53317 每 2 秒宣告 · %2")
                           .arg(port)
                           .arg(pulled.isEmpty() ? QStringLiteral("桌面端还没来拉过")
                                                 : QStringLiteral("桌面端最近拉取 %1").arg(pulled)));
}

void VigorHubPage::loadConfigIntoUi()
{
    const VigorHubRelayConfig cfg = m_relay->config();
    m_chkEnabled->setChecked(cfg.enabled);
    m_editAddress->setText(cfg.address);
    const int idx = m_cmbInterval->findData(cfg.intervalSec);
    m_cmbInterval->setCurrentIndex(idx >= 0 ? idx : 2); // 默认每 5 分钟
    m_editAlias->setText(cfg.alias);
    m_editSource->setText(cfg.sourceLabel);
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
    cfg.alias = m_editAlias->text().trimmed();
    cfg.sourceLabel = m_editSource->text().trimmed();
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
