// d1syncpage.cpp —— Cloudflare D1 云同步配置页
//
// 后端能力由 aw-server-rust 提供（commit 3269968）：
//   GET   /api/0/sync/d1/status   -> { enabled, configured, last_sync }
//   POST  /api/0/sync/d1/test     -> { ok, message }      测试连接
//   POST  /api/0/sync/d1/sync     -> { ok, pushed_notes, pushed_todos, pulled_notes, pulled_todos, conflicts, errors }
//
// 配置字段复用 SyncConfig 里的 d1_*（aw-server-rust 持久化在 sync.db 的 kv 表里）：
//   d1_enabled, d1_account_id, d1_database_id, d1_api_token, d1_sync_interval
#include "d1syncpage.h"

#include "apiclient.h"
#include "models.h"
#include "theme.h"
#include "widgets.h"

#include <QCheckBox>
#include <QDateTime>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QNetworkReply>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSpinBox>
#include <QTime>
#include <QUrl>
#include <QVBoxLayout>

namespace awqtui {

D1SyncPage::D1SyncPage(ApiClient *api, QWidget *parent)
    : QWidget(parent), m_api(api)
{
    buildUi();
    if (m_api) {
        connect(m_api, &ApiClient::destroyed, this, [this] { m_api = nullptr; });
    }
    refreshStatus();
}

D1SyncPage::~D1SyncPage() = default;

void D1SyncPage::buildUi()
{
    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(16, 16, 16, 16);
    root->setSpacing(12);

    auto *cfgBox = new QGroupBox(QStringLiteral("Cloudflare D1 云同步"));
    auto *cfgForm = new QFormLayout(cfgBox);

    m_accountId = new QLineEdit;
    m_accountId->setPlaceholderText(QStringLiteral("例如：a1b2c3d4e5f6..."));

    m_databaseId = new QLineEdit;
    m_databaseId->setPlaceholderText(QStringLiteral("D1 数据库 UUID"));

    m_apiToken = new QLineEdit;
    m_apiToken->setEchoMode(QLineEdit::Password);
    m_apiToken->setPlaceholderText(QStringLiteral("Cloudflare API Token（需含 D1 读写权限）"));

    m_chkEnabled = new QCheckBox(QStringLiteral("启用 D1 云同步"));
    m_interval = new QSpinBox;
    m_interval->setRange(30, 24 * 3600);
    m_interval->setSingleStep(30);
    m_interval->setSuffix(QStringLiteral(" 秒"));
    m_interval->setValue(300);
    m_interval->setToolTip(QStringLiteral("后台周期同步间隔，最小 30 秒"));

    cfgForm->addRow(QStringLiteral("Account ID"), m_accountId);
    cfgForm->addRow(QStringLiteral("Database ID"), m_databaseId);
    cfgForm->addRow(QStringLiteral("API Token"), m_apiToken);
    cfgForm->addRow(m_chkEnabled);
    cfgForm->addRow(QStringLiteral("同步间隔"), m_interval);

    auto *btnRow = new QHBoxLayout;
    m_btnTest = new QPushButton(QStringLiteral("测试连接"));
    m_btnTest->setObjectName(QStringLiteral("PrimaryBtn"));
    m_btnSave = new QPushButton(QStringLiteral("保存配置"));
    m_btnSyncNow = new QPushButton(QStringLiteral("立即同步"));
    btnRow->addWidget(m_btnTest);
    btnRow->addWidget(m_btnSave);
    btnRow->addWidget(m_btnSyncNow);
    btnRow->addStretch(1);
    cfgForm->addRow(btnRow);

    m_lblStatus = new QLabel(QStringLiteral("未配置"));
    m_lblStatus->setWordWrap(true);
    m_lblStatus->setTextInteractionFlags(Qt::TextSelectableByMouse);
    cfgForm->addRow(QStringLiteral("状态"), m_lblStatus);

    root->addWidget(cfgBox);

    // 说明
    auto *helpBox = new QGroupBox(QStringLiteral("说明"));
    auto *helpLay = new QVBoxLayout(helpBox);
    auto *help = new QLabel(QStringLiteral(
        "D1 是 Cloudflare 的托管 SQLite 数据库。本配置启用后，服务端会按设定间隔 "
        "把本机 Inbox 笔记和 Todo 推送到 D1，并拉取其他设备的更新，实现多设备间 "
        "Inbox/TODO 的云端同步。\n"
        "• Account ID：Cloudflare 账户的 32 位十六进制 ID（在账户首页右侧可见）。\n"
        "• Database ID：D1 数据库 UUID（wrangler d1 list 或控制台可见）。\n"
        "• API Token：在 Cloudflare 控制台创建，需授予 D1:Edit 权限。\n"
        "保存后请先点「测试连接」验证凭据与 D1 初始化状态，再点「立即同步」触发一次。"));
    help->setWordWrap(true);
    help->setStyleSheet(QStringLiteral("color: %1;").arg(kColorFgMuted));
    helpLay->addWidget(help);
    root->addWidget(helpBox);

    // 日志
    auto *logBox = new QGroupBox(QStringLiteral("日志"));
    auto *logLay = new QVBoxLayout(logBox);
    m_log = new QPlainTextEdit;
    m_log->setReadOnly(true);
    m_log->setMaximumBlockCount(500);
    m_log->setMinimumHeight(140);
    logLay->addWidget(m_log);
    root->addWidget(logBox, 1);

    connect(m_btnSave, &QPushButton::clicked, this, &D1SyncPage::onSave);
    connect(m_btnTest, &QPushButton::clicked, this, &D1SyncPage::onTest);
    connect(m_btnSyncNow, &QPushButton::clicked, this, &D1SyncPage::onSyncNow);
}

void D1SyncPage::applyUiScale()
{
    // 复用全局 QSS 缩放（基类样式已通过 mainwindow.applyUiScale 重建），如需定制可在此加
    if (m_log)
        m_log->setMinimumHeight(140);
}

void D1SyncPage::log(const QString &line)
{
    if (!m_log)
        return;
    const QString ts = QTime::currentTime().toString(QStringLiteral("HH:mm:ss"));
    m_log->appendPlainText(QStringLiteral("[%1] %2").arg(ts, line));
}

void D1SyncPage::setStatus(const QString &text, bool ok)
{
    if (!m_lblStatus)
        return;
    const QString color = ok ? QString::fromLatin1(kColorOk) : QString::fromLatin1(kColorWarn);
    m_lblStatus->setText(QStringLiteral("<span style=\"color:%1;\">%2</span>").arg(color, text.toHtmlEscaped()));
}

void D1SyncPage::refreshStatus()
{
    if (!m_api)
        return;
    // 先拉取 SyncConfig（拿到已保存的字段填表单）
    QNetworkReply *rc = m_api->getSyncConfig();
    connect(rc, &QNetworkReply::finished, this, [this, rc] {
        QJsonDocument doc;
        QString err;
        if (ApiClient::parseReply(rc, &doc, &err)) {
            SyncConfig cfg = SyncConfig::fromJson(doc.object());
            m_accountId->setText(cfg.d1AccountId);
            m_databaseId->setText(cfg.d1DatabaseId);
            m_apiToken->setText(cfg.d1ApiToken);
            m_chkEnabled->setChecked(cfg.d1Enabled);
            m_interval->setValue(qMax(30, static_cast<int>(cfg.d1SyncInterval)));
        }
        rc->deleteLater();
    });

    // 再请求 D1 服务端状态
    QNetworkReply *rs = m_api->d1Status();
    connect(rs, &QNetworkReply::finished, this, [this, rs] {
        QJsonDocument doc;
        QString err;
        if (!ApiClient::parseReply(rs, &doc, &err)) {
            setStatus(QStringLiteral("状态获取失败：%1").arg(err), false);
            rs->deleteLater();
            return;
        }
        const QJsonObject o = doc.object();
        const bool enabled = o.value(QStringLiteral("enabled")).toBool();
        const bool configured = o.value(QStringLiteral("configured")).toBool();
        const QString last = o.value(QStringLiteral("last_sync")).toString();
        QString text;
        if (!configured)
            text = QStringLiteral("未配置（缺少 Account/Database/Token）");
        else if (!enabled)
            text = QStringLiteral("已配置但未启用");
        else
            text = QStringLiteral("已启用");
        if (!last.isEmpty())
            text += QStringLiteral(" · 上次同步：%1").arg(last);
        setStatus(text, enabled);
        rs->deleteLater();
    });
}

void D1SyncPage::onSave()
{
    if (!m_api)
        return;
    // 先 GET 现有配置，保留其他字段不被覆盖
    QNetworkReply *rg = m_api->getSyncConfig();
    connect(rg, &QNetworkReply::finished, this, [this, rg] {
        QJsonDocument doc;
        QString err;
        SyncConfig cfg;
        if (ApiClient::parseReply(rg, &doc, &err))
            cfg = SyncConfig::fromJson(doc.object());
        rg->deleteLater();

        cfg.d1Enabled = m_chkEnabled->isChecked();
        cfg.d1AccountId = m_accountId->text().trimmed();
        cfg.d1DatabaseId = m_databaseId->text().trimmed();
        cfg.d1ApiToken = m_apiToken->text(); // 不过滤空白，服务端保留原样
        cfg.d1SyncInterval = static_cast<quint32>(qMax(30, m_interval->value()));

        QNetworkReply *rp = m_api->setSyncConfig(cfg.toJson());
        connect(rp, &QNetworkReply::finished, this, [this, rp] {
            QJsonDocument d2;
            QString e2;
            if (!ApiClient::parseReply(rp, &d2, &e2)) {
                log(QStringLiteral("保存失败：%1").arg(e2));
                QMessageBox::warning(this, QStringLiteral("D1 配置"), e2);
                rp->deleteLater();
                return;
            }
            log(QStringLiteral("D1 配置已保存"));
            refreshStatus();
            rp->deleteLater();
        });
    });
}

void D1SyncPage::onTest()
{
    if (!m_api)
        return;
    // 必须先保存，否则服务端看不到最新字段
    if (m_apiToken->text().trimmed().isEmpty()
        || m_accountId->text().trimmed().isEmpty()
        || m_databaseId->text().trimmed().isEmpty()) {
        QMessageBox::information(this, QStringLiteral("D1 测试"),
            QStringLiteral("请先填写 Account ID / Database ID / API Token，并保存配置。"));
        return;
    }
    log(QStringLiteral("正在测试 D1 连接..."));
    m_btnTest->setEnabled(false);
    QNetworkReply *r = m_api->d1Test();
    connect(r, &QNetworkReply::finished, this, [this, r] {
        m_btnTest->setEnabled(true);
        QJsonDocument doc;
        QString err;
        if (!ApiClient::parseReply(r, &doc, &err)) {
            log(QStringLiteral("测试失败：%1").arg(err));
            setStatus(QStringLiteral("测试失败：%1").arg(err), false);
        } else {
            const QJsonObject o = doc.object();
            const QString msg = o.value(QStringLiteral("message")).toString(QStringLiteral("D1 连接成功"));
            log(QStringLiteral("测试成功：%1").arg(msg));
            setStatus(msg, true);
        }
        r->deleteLater();
    });
}

void D1SyncPage::onSyncNow()
{
    if (!m_api)
        return;
    log(QStringLiteral("触发一次 D1 云同步..."));
    m_btnSyncNow->setEnabled(false);
    QNetworkReply *r = m_api->d1SyncNow();
    connect(r, &QNetworkReply::finished, this, [this, r] {
        m_btnSyncNow->setEnabled(true);
        QJsonDocument doc;
        QString err;
        if (!ApiClient::parseReply(r, &doc, &err)) {
            log(QStringLiteral("同步失败：%1").arg(err));
            setStatus(QStringLiteral("同步失败：%1").arg(err), false);
            r->deleteLater();
            return;
        }
        const QJsonObject o = doc.object();
        const int pushedNotes = o.value(QStringLiteral("pushed_notes")).toInt();
        const int pushedTodos = o.value(QStringLiteral("pushed_todos")).toInt();
        const int pulledNotes = o.value(QStringLiteral("pulled_notes")).toInt();
        const int pulledTodos = o.value(QStringLiteral("pulled_todos")).toInt();
        const int conflicts = o.value(QStringLiteral("conflicts")).toInt();
        const int errors = o.value(QStringLiteral("errors")).toInt();
        log(QStringLiteral("同步完成：推送笔记 %1 条 / Todo %2 条，拉取笔记 %3 条 / Todo %4 条，冲突 %5，错误 %6")
                .arg(pushedNotes).arg(pushedTodos).arg(pulledNotes).arg(pulledTodos).arg(conflicts).arg(errors));
        setStatus(QStringLiteral("同步完成 · 上次同步：%1")
                      .arg(QDateTime::currentDateTime().toString(QStringLiteral("yyyy-MM-dd HH:mm:ss"))),
                  errors == 0);
        r->deleteLater();
    });
}

} // namespace awqtui