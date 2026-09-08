// cloudbackuppage.cpp —— 云备份（冷备）页实现
#include "cloudbackuppage.h"
#include "ui_cloudbackuppage.h"

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

CloudBackupPage::~CloudBackupPage()
{
    delete ui;
}

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
    // 静态布局来自 Qt Designer（cloudbackuppage.ui -> ui_cloudbackuppage.h）
    ui = new Ui::CloudBackupPage;
    ui->setupUi(this);

    // ── 成员别名：业务逻辑沿用 m_* 指针，静态布局归属 .ui 文件 ──
    m_serverEdit = ui->serverEdit;
    m_tabs = ui->tabs;
    m_kind = ui->kind;
    m_webdavUrl = ui->webdavUrl;
    m_webdavUser = ui->webdavUser;
    m_webdavPass = ui->webdavPass;
    m_webdavPath = ui->webdavPath;
    m_s3Endpoint = ui->s3Endpoint;
    m_s3AccessKey = ui->s3AccessKey;
    m_s3SecretKey = ui->s3SecretKey;
    m_s3Bucket = ui->s3Bucket;
    m_s3Region = ui->s3Region;
    m_s3Path = ui->s3Path;
    m_s3PathStyle = ui->s3PathStyle;
    m_s3Tls = ui->s3Tls;
    m_btnTest = ui->btnTest;
    m_btnSave = ui->btnSave;
    m_btnBackupNow = ui->btnBackupNow;
    m_lblStatus = ui->statusLabel;
    m_chkAutoBackup = ui->chkAutoBackup;
    m_intervalHours = ui->intervalHours;
    m_lblLastBackup = ui->lblLastBackup;
    m_log = ui->log;

    // 主题样式角色（applyTheme 的 QSS 按 objectName 选择器匹配）
    ui->title->setObjectName(QStringLiteral("Heading"));
    ui->subtitle->setObjectName(QStringLiteral("SubHeading"));
    m_lblStatus->setObjectName(QStringLiteral("StatusLabel"));
    ui->btnApply->setObjectName(QStringLiteral("ToolBtn"));
    m_btnTest->setObjectName(QStringLiteral("PrimaryBtn"));
    m_btnSave->setObjectName(QStringLiteral("ToolBtn"));
    m_btnBackupNow->setObjectName(QStringLiteral("ToolBtn"));

    // uic 不便表达的运行时属性
    m_serverEdit->setText(m_api ? m_api->baseUrl() : kDefaultServerUrl);
    m_kind->setItemData(0, static_cast<int>(CloudNone));
    m_kind->setItemData(1, static_cast<int>(CloudWebDAV));
    m_kind->setItemData(2, static_cast<int>(CloudS3));
    ui->hint->setStyleSheet(QStringLiteral("color: %1; font-size: 11px;").arg(kColorFgMuted));

    // 信号连接
    connect(ui->btnApply, &QPushButton::clicked, this, [this] {
        setServerUrl(m_serverEdit->text().trimmed());
    });
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
