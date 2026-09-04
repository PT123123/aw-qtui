// cloudbackuppage.cpp —— 云备份（冷备）页实现
#include "cloudbackuppage.h"

#include "apiclient.h"
#include "config.h"
#include "models.h"
#include "theme.h"
#include "widgets.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDateTime>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSettings>
#include <QSpinBox>
#include <QTabWidget>
#include <QVBoxLayout>

namespace awqtui {

CloudBackupPage::CloudBackupPage(ApiClient *api, QWidget *parent)
    : QWidget(parent), m_api(api)
{
    buildUi();
    loadSavedConfig();
    applyTheme();
}

CloudBackupPage::~CloudBackupPage() = default;

void CloudBackupPage::applyTheme()
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
        QPushButton#ToolBtn {
            background: %4; border: 1px solid %2; border-radius: 6px;
            padding: 5px 14px; color: %5; font-size: 12px;
        }
        QPushButton#ToolBtn:hover { background: %6; border-color: %7; }
        QPushButton#PrimaryBtn {
            background: %7; border: 1px solid %7; border-radius: 6px;
            padding: 5px 14px; color: white; font-size: 12px; font-weight: 600;
        }
        QPushButton#PrimaryBtn:hover { background: %8; border-color: %8; }
        QTabWidget::pane { border: 1px solid %2; border-radius: 8px; background: %1; }
        QTabBar::tab {
            background: %4; border: 1px solid %2; border-bottom: none;
            border-top-left-radius: 6px; border-top-right-radius: 6px;
            padding: 6px 14px; margin-right: 2px; color: %3;
        }
        QTabBar::tab:selected { background: %1; color: %5; border-bottom: 2px solid %7; }
        QTabBar::tab:hover { background: %6; }
    )")
                          .arg(kColorBgElev, kColorBorder, kColorFgMuted,
                               kColorBgElev2, kColorFgSoft, kColorBgElev2, kColorAccent, kColorAccentHover));
}

void CloudBackupPage::buildUi()
{
    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(20, 20, 20, 20);
    root->setSpacing(14);

    // ── 顶部工具栏 ──
    auto *toolbar = new QHBoxLayout;
    toolbar->setSpacing(8);

    auto *title = new QLabel(QStringLiteral("❄ 云备份（冷备）"));
    title->setObjectName(QStringLiteral("Heading"));
    toolbar->addWidget(title);

    auto *subtitle = new QLabel(QStringLiteral("WebDAV / S3 低频冷备份"));
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

    // ── 状态栏 ──
    auto *statusBar = new QHBoxLayout;
    statusBar->setSpacing(8);
    m_lblStatus = new QLabel(QStringLiteral("未配置"));
    m_lblStatus->setObjectName(QStringLiteral("StatusLabel"));
    statusBar->addWidget(m_lblStatus);
    statusBar->addStretch(1);
    root->addLayout(statusBar);

    // ── Tab 区域 ──
    m_tabs = new QTabWidget;

    // ── 连接配置 Tab ──
    auto *connTab = new QWidget;
    auto *connLay = new QVBoxLayout(connTab);
    connLay->setContentsMargins(12, 12, 12, 12);
    connLay->setSpacing(8);

    auto *connBox = new QGroupBox(QStringLiteral("备份目标"));
    auto *connForm = new QFormLayout(connBox);
    connForm->setSpacing(10);

    m_kind = new QComboBox;
    m_kind->addItem(QStringLiteral("未启用"), static_cast<int>(CloudNone));
    m_kind->addItem(QStringLiteral("WebDAV"), static_cast<int>(CloudWebDAV));
    m_kind->addItem(QStringLiteral("S3 / MinIO"), static_cast<int>(CloudS3));
    connForm->addRow(QStringLiteral("协议"), m_kind);

    // WebDAV 字段
    m_webdavUrl = new QLineEdit;
    m_webdavUrl->setPlaceholderText(QStringLiteral("https://dav.example.com/"));
    m_webdavUser = new QLineEdit;
    m_webdavPass = new QLineEdit;
    m_webdavPass->setEchoMode(QLineEdit::Password);
    m_webdavPath = new QLineEdit(QStringLiteral("/aw-qtui/"));
    connForm->addRow(QStringLiteral("WebDAV URL"), m_webdavUrl);
    connForm->addRow(QStringLiteral("用户名"), m_webdavUser);
    connForm->addRow(QStringLiteral("密码"), m_webdavPass);
    connForm->addRow(QStringLiteral("远程路径"), m_webdavPath);

    // S3 字段
    m_s3Endpoint = new QLineEdit;
    m_s3Endpoint->setPlaceholderText(QStringLiteral("https://s3.amazonaws.com"));
    m_s3AccessKey = new QLineEdit;
    m_s3SecretKey = new QLineEdit;
    m_s3SecretKey->setEchoMode(QLineEdit::Password);
    m_s3Bucket = new QLineEdit;
    m_s3Region = new QLineEdit(QStringLiteral("us-east-1"));
    m_s3Path = new QLineEdit(QStringLiteral("aw-qtui/"));
    m_s3PathStyle = new QCheckBox(QStringLiteral("路径风格（MinIO）"));
    m_s3PathStyle->setChecked(true);
    m_s3Tls = new QCheckBox(QStringLiteral("使用 TLS"));
    m_s3Tls->setChecked(true);
    connForm->addRow(QStringLiteral("Endpoint"), m_s3Endpoint);
    connForm->addRow(QStringLiteral("Access Key"), m_s3AccessKey);
    connForm->addRow(QStringLiteral("Secret Key"), m_s3SecretKey);
    connForm->addRow(QStringLiteral("Bucket"), m_s3Bucket);
    connForm->addRow(QStringLiteral("Region"), m_s3Region);
    connForm->addRow(QStringLiteral("对象前缀"), m_s3Path);
    connForm->addRow(m_s3PathStyle);
    connForm->addRow(m_s3Tls);

    // 操作行
    auto *btnRow = new QHBoxLayout;
    btnRow->setSpacing(8);
    m_btnTest = new QPushButton(QStringLiteral("测试连接"));
    m_btnTest->setObjectName(QStringLiteral("PrimaryBtn"));
    m_btnSave = new QPushButton(QStringLiteral("保存配置"));
    m_btnSave->setObjectName(QStringLiteral("ToolBtn"));
    m_btnBackupNow = new QPushButton(QStringLiteral("立即备份"));
    m_btnBackupNow->setObjectName(QStringLiteral("ToolBtn"));
    btnRow->addWidget(m_btnTest);
    btnRow->addWidget(m_btnSave);
    btnRow->addWidget(m_btnBackupNow);
    btnRow->addStretch(1);
    connForm->addRow(btnRow);

    connLay->addWidget(connBox);
    connLay->addStretch(1);

    m_tabs->addTab(connTab, QStringLiteral("🔌 连接"));

    // ── 冷备设置 Tab ──
    auto *settingsTab = new QWidget;
    auto *settingsLay = new QVBoxLayout(settingsTab);
    settingsLay->setContentsMargins(12, 12, 12, 12);
    settingsLay->setSpacing(8);

    auto *schedBox = new QGroupBox(QStringLiteral("自动备份计划"));
    auto *schedForm = new QFormLayout(schedBox);
    schedForm->setSpacing(10);

    m_chkAutoBackup = new QCheckBox(QStringLiteral("启用自动备份"));
    schedForm->addRow(m_chkAutoBackup);

    m_intervalHours = new QSpinBox;
    m_intervalHours->setMinimum(1);
    m_intervalHours->setMaximum(720);
    m_intervalHours->setSuffix(QStringLiteral(" 小时"));
    m_intervalHours->setValue(24);
    schedForm->addRow(QStringLiteral("备份间隔"), m_intervalHours);

    m_lblLastBackup = new QLabel(QStringLiteral("从未备份"));
    schedForm->addRow(QStringLiteral("上次备份"), m_lblLastBackup);

    auto *hint = new QLabel(QStringLiteral("冷备按小时间隔自动执行，不影响高频的局域网/D1 同步。"));
    hint->setStyleSheet(QStringLiteral("color: %1; font-size: 11px;").arg(kColorFgMuted));
    schedForm->addRow(hint);

    settingsLay->addWidget(schedBox);
    settingsLay->addStretch(1);

    m_tabs->addTab(settingsTab, QStringLiteral("⏰ 计划"));

    // ── 日志 Tab ──
    auto *logTab = new QWidget;
    auto *logLay = new QVBoxLayout(logTab);
    logLay->setContentsMargins(12, 12, 12, 12);
    logLay->setSpacing(8);

    m_log = new QPlainTextEdit;
    m_log->setReadOnly(true);
    m_log->setPlaceholderText(QStringLiteral("操作日志将在此显示…"));
    logLay->addWidget(m_log, 1);

    m_tabs->addTab(logTab, QStringLiteral("📜 日志"));

    root->addWidget(m_tabs, 1);

    // 信号连接
    connect(m_kind, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &CloudBackupPage::onKindChanged);
    connect(m_btnTest, &QPushButton::clicked, this, &CloudBackupPage::onTestConnection);
    connect(m_btnSave, &QPushButton::clicked, this, &CloudBackupPage::onSaveConfig);
    connect(m_btnBackupNow, &QPushButton::clicked, this, &CloudBackupPage::onBackupNow);
    connect(m_chkAutoBackup, &QCheckBox::toggled, this, &CloudBackupPage::onAutoBackupToggled);
}

void CloudBackupPage::loadSavedConfig()
{
    CloudStorageConfig cfg;
    QSettings s;
    s.beginGroup("cloud");
    if (s.contains("config")) {
        cfg = CloudStorageConfig::fromJson(
            QJsonDocument::fromJson(s.value("config").toByteArray()).object());

        m_kind->setCurrentIndex(m_kind->findData(static_cast<int>(cfg.kind)));
        m_webdavUrl->setText(cfg.webdavUrl);
        m_webdavUser->setText(cfg.webdavUser);
        m_webdavPass->setText(cfg.webdavPass);
        m_webdavPath->setText(cfg.webdavPath);
        m_s3Endpoint->setText(cfg.s3Endpoint);
        m_s3AccessKey->setText(cfg.s3AccessKey);
        m_s3SecretKey->setText(cfg.s3SecretKey);
        m_s3Bucket->setText(cfg.s3Bucket);
        m_s3Region->setText(cfg.s3Region);
        m_s3Path->setText(cfg.s3Path);
        m_s3PathStyle->setChecked(cfg.s3UsePathStyle);
        m_s3Tls->setChecked(cfg.s3Tls);

        m_chkAutoBackup->setChecked(s.value("auto_backup", false).toBool());
        m_intervalHours->setValue(s.value("interval_hours", 24).toInt());
        QString lastBackup = s.value("last_backup").toString();
        if (!lastBackup.isEmpty())
            m_lblLastBackup->setText(lastBackup);
    }
    s.endGroup();
    onKindChanged(m_kind->currentIndex());
}

void CloudBackupPage::onKindChanged(int idx)
{
    Q_UNUSED(idx);
    const int kind = m_kind->currentData().toInt();
    const bool isWebDAV = (kind == CloudWebDAV);
    const bool isS3 = (kind == CloudS3);

    const QList<QPair<QWidget*, bool>> webdavRows = {
        { m_webdavUrl, isWebDAV }, { m_webdavUser, isWebDAV },
        { m_webdavPass, isWebDAV }, { m_webdavPath, isWebDAV },
    };
    const QList<QPair<QWidget*, bool>> s3Rows = {
        { m_s3Endpoint, isS3 }, { m_s3AccessKey, isS3 }, { m_s3SecretKey, isS3 },
        { m_s3Bucket, isS3 }, { m_s3Region, isS3 }, { m_s3Path, isS3 },
        { m_s3PathStyle, isS3 }, { m_s3Tls, isS3 },
    };
    for (auto &p : webdavRows) p.first->setVisible(p.second);
    for (auto &p : s3Rows) p.first->setVisible(p.second);
}

void CloudBackupPage::onTestConnection()
{
    const int kind = m_kind->currentData().toInt();
    if (kind == CloudNone) {
        setStatus(QStringLiteral("请先选择协议"), false);
        return;
    }
    setStatus(QStringLiteral("测试中…"), true);
    if (kind == CloudWebDAV) {
        if (m_webdavUrl->text().trimmed().isEmpty()) {
            setStatus(QStringLiteral("请填写 WebDAV URL"), false);
            return;
        }
        QNetworkRequest req(QUrl(m_webdavUrl->text().trimmed()));
        req.setRawHeader("Depth", "0");
        req.setRawHeader("User-Agent", "aw-qtui/0.1");
        if (!m_webdavUser->text().isEmpty()) {
            const QString auth = m_webdavUser->text() + ":" + m_webdavPass->text();
            req.setRawHeader("Authorization", "Basic " + auth.toUtf8().toBase64());
        }
        QNetworkReply *r = m_api->networkAccessManager()->sendCustomRequest(req, "PROPFIND");
        connect(r, &QNetworkReply::finished, this, [this, r] {
            const int status = r->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
            if (status >= 200 && status < 400) {
                setStatus(QStringLiteral("✓ WebDAV 连接成功（%1）").arg(status), true);
                log(QStringLiteral("WebDAV 测试连接成功：%1").arg(status));
            } else {
                setStatus(QStringLiteral("✗ 连接失败：%1 %2").arg(status).arg(r->errorString()), false);
                log(QStringLiteral("WebDAV 测试连接失败：%1 %2").arg(status).arg(r->errorString()));
            }
            r->deleteLater();
        });
    } else if (kind == CloudS3) {
        if (m_s3Endpoint->text().trimmed().isEmpty() || m_s3Bucket->text().trimmed().isEmpty()) {
            setStatus(QStringLiteral("请填写 Endpoint 和 Bucket"), false);
            return;
        }
        const QString ep = m_s3Endpoint->text().trimmed();
        QUrl bucketUrl(ep);
        if (m_s3PathStyle->isChecked()) {
            bucketUrl.setPath("/" + m_s3Bucket->text());
        } else {
            bucketUrl.setHost(m_s3Bucket->text() + "." + bucketUrl.host());
        }
        QNetworkRequest req(bucketUrl);
        req.setRawHeader("User-Agent", "aw-qtui/0.1");
        QNetworkReply *r = m_api->networkAccessManager()->head(req);
        connect(r, &QNetworkReply::finished, this, [this, r] {
            const int status = r->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
            if (status == 200 || status == 403) {
                setStatus(QStringLiteral("✓ S3 连接成功（%1）").arg(status), true);
                log(QStringLiteral("S3 测试连接成功：%1").arg(status));
            } else {
                setStatus(QStringLiteral("✗ 连接失败：%1 %2").arg(status).arg(r->errorString()), false);
                log(QStringLiteral("S3 测试连接失败：%1 %2").arg(status).arg(r->errorString()));
            }
            r->deleteLater();
        });
    }
}

void CloudBackupPage::onSaveConfig()
{
    QSettings s;
    s.beginGroup("cloud");
    s.setValue("auto_backup", m_chkAutoBackup->isChecked());
    s.setValue("interval_hours", m_intervalHours->value());

    CloudStorageConfig cfg;
    cfg.kind = static_cast<CloudStorageKind>(m_kind->currentData().toInt());
    cfg.webdavUrl = m_webdavUrl->text().trimmed();
    cfg.webdavUser = m_webdavUser->text().trimmed();
    cfg.webdavPass = m_webdavPass->text();
    cfg.webdavPath = m_webdavPath->text().trimmed();
    cfg.s3Endpoint = m_s3Endpoint->text().trimmed();
    cfg.s3AccessKey = m_s3AccessKey->text().trimmed();
    cfg.s3SecretKey = m_s3SecretKey->text();
    cfg.s3Bucket = m_s3Bucket->text().trimmed();
    cfg.s3Region = m_s3Region->text().trimmed();
    cfg.s3Path = m_s3Path->text().trimmed();
    cfg.s3UsePathStyle = m_s3PathStyle->isChecked();
    cfg.s3Tls = m_s3Tls->isChecked();

    CloudStorageConfig saveCfg = cfg;
    saveCfg.webdavPass.clear();
    saveCfg.s3SecretKey.clear();
    s.setValue("config", QJsonDocument(saveCfg.toJson()).toJson(QJsonDocument::Compact));
    s.endGroup();

    setStatus(QStringLiteral("✓ 配置已保存"), true);
    log(QStringLiteral("云备份配置已保存"));
}

void CloudBackupPage::onBackupNow()
{
    const int kind = m_kind->currentData().toInt();
    if (kind == CloudNone) {
        setStatus(QStringLiteral("请先选择协议"), false);
        return;
    }
    setStatus(QStringLiteral("同步中…（功能开发中）"), false);
    log(QStringLiteral("云备份同步：功能开发中"));
}

void CloudBackupPage::onAutoBackupToggled(bool on)
{
    if (on) {
        log(QStringLiteral("自动备份已启用，间隔 %1 小时").arg(m_intervalHours->value()));
    } else {
        log(QStringLiteral("自动备份已关闭"));
    }
}

void CloudBackupPage::log(const QString &line)
{
    m_log->appendPlainText(QStringLiteral("[%1] %2").arg(
        QDateTime::currentDateTime().toString(QStringLiteral("HH:mm:ss")), line));
}

void CloudBackupPage::setStatus(const QString &text, bool ok)
{
    m_lblStatus->setText(text);
    m_lblStatus->setStyleSheet(ok
        ? QStringLiteral("color: %1; font-size: 12px; font-weight: 600;").arg(kColorFgMuted)
        : QStringLiteral("color: %1; font-size: 12px; font-weight: 600;").arg(kColorWarn));
}

void CloudBackupPage::setServerUrl(const QString &url)
{
    if (m_api) {
        m_api->setBaseUrl(url);
        m_serverEdit->setText(url);
    }
}

void CloudBackupPage::applyUiScale() {}

void CloudBackupPage::refreshStatus()
{
    QSettings s;
    s.beginGroup("cloud");
    if (s.contains("config")) {
        CloudStorageConfig cfg = CloudStorageConfig::fromJson(
            QJsonDocument::fromJson(s.value("config").toByteArray()).object());
        m_kind->setCurrentIndex(m_kind->findData(static_cast<int>(cfg.kind)));
    }
    s.endGroup();
}

} // namespace awqtui
