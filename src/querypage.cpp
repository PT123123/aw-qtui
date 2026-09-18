// querypage.cpp —— Query Explorer：手写 / 预置脚本 → /api/0/query → pretty-print JSON
#include "querypage.h"
#include "ui_querypage.h"

#include "apiclient.h"
#include "charts.h"
#include "config.h"
#include "theme.h"

#include <QApplication>
#include <QClipboard>
#include <QComboBox>
#include <QDateTime>
#include <QHBoxLayout>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSpinBox>
#include <QVBoxLayout>

namespace awqtui {

QueryPage::QueryPage(ApiClient *api, QWidget *parent)
    : QWidget(parent), m_api(api)
{
    buildUi();
    applyStyle();
    loadPresets();
}

QueryPage::~QueryPage()
{
    delete ui;
}

void QueryPage::buildUi()
{
    // 静态布局来自 Qt Designer（querypage.ui -> ui_querypage.h）；
    // 控件名直接使用主题 QSS 的 objectName 角色
    ui = new Ui::QueryPage;
    ui->setupUi(this);

    // ── 运行时取值：下拉 userData、默认隐藏的自定义天数、等宽字体 ──
    ui->rangeCombo->setItemData(0, 0);
    ui->rangeCombo->setItemData(1, 1);
    ui->rangeCombo->setItemData(2, 7);
    ui->rangeCombo->setItemData(3, 30);
    ui->rangeCombo->setItemData(4, -1);
    ui->daySpin->setVisible(false);
    QFont mono = ui->QueryEditor->font();
    mono.setFamily(QStringLiteral("Consolas"));
    ui->QueryEditor->setFont(mono);
    ui->QueryResult->setFont(mono);

    // 主题色相关样式（kColor* 随主题切换，无法烘焙进 .ui）
    m_statusLabel = ui->statusLabel;
    m_statusLabel->setStyleSheet(QStringLiteral("color: %1;").arg(kColorFgMuted));

    // ── 成员别名：业务逻辑沿用 m_* 指针 ──
    m_rangeCombo = ui->rangeCombo;
    m_tzCombo = ui->tzCombo;
    m_daySpin = ui->daySpin;
    m_presetCombo = ui->presetCombo;
    m_scriptEdit = ui->QueryEditor;
    m_runBtn = ui->PrimaryBtn;
    m_formatBtn = ui->formatBtn;
    m_copyBtn = ui->copyBtn;
    m_resultEdit = ui->QueryResult;

    // ── 信号连接 ──
    connect(m_rangeCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &QueryPage::onRangeChanged);
    connect(m_tzCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &QueryPage::onTimeZoneChanged);
    connect(m_presetCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &QueryPage::onPresetChanged);
    connect(m_runBtn, &QPushButton::clicked, this, &QueryPage::onRun);
    connect(m_formatBtn, &QPushButton::clicked, this, [this] {
        QString text = m_resultEdit->toPlainText();
        if (text.isEmpty()) return;
        QJsonParseError err;
        QJsonDocument doc = QJsonDocument::fromJson(text.toUtf8(), &err);
        if (err.error == QJsonParseError::NoError) {
            m_resultEdit->setPlainText(doc.toJson(QJsonDocument::Indented));
        }
    });
    connect(m_copyBtn, &QPushButton::clicked, this, [this] {
        QApplication::clipboard()->setText(m_resultEdit->toPlainText());
        m_statusLabel->setText(QStringLiteral("已复制到剪贴板"));
    });
}

void QueryPage::applyStyle()
{
    // 使用全局主题
}

void QueryPage::loadPresets()
{
    m_presets = {
        {"自定义", ""},
        {"所有事件（按 bucket）",
         "{\n"
         "  \"query\": [\n"
         "    \"buckets = all_buckets();\n"
         "    events = [];\"\n"
         "  ]\n"
         "}"},
        {"今日活跃应用 Top 10",
         "{\n"
         "  \"query\": [\n"
         "    \"bucket_events = query_bucket(\\\"aw-watcher-window_\\\");\n"
         "    events = filter_keyvals(bucket_events, \\\"app\\\", [\\\"Chrome\\\", \\\"Firefox\\\"]);\"\n"
         "  ]\n"
         "}"},
        {"今日每小时活跃统计",
         "{\n"
         "  \"query\": [\n"
         "    \"bucket_events = query_bucket(\\\"aw-watcher-window_\\\");\n"
         "    hours = split_events_by_hour(bucket_events);\"\n"
         "  ]\n"
         "}"},
        {"所有 bucket 列表",
         "{\n"
         "  \"query\": [\n"
         "    \"buckets = all_buckets();\",\n"
         "    \"RETURN = buckets;\"\n"
         "  ]\n"
         "}"},
        {"所有事件（全量）",
         "{\n"
         "  \"query\": [\n"
         "    \"events = query_all_events();\n"
         "    RETURN = events;\"\n"
         "  ]\n"
         "}"},
    };

    for (const auto &p : m_presets) {
        m_presetCombo->addItem(p.name);
    }
}

void QueryPage::onPresetChanged(int idx)
{
    if (idx < 0 || idx >= m_presets.size()) return;
    if (!m_presets[idx].script.isEmpty()) {
        m_scriptEdit->setPlainText(m_presets[idx].script);
    }
}

void QueryPage::onRun()
{
    if (!m_api) {
        m_statusLabel->setText(QStringLiteral("API 客户端不可用"));
        return;
    }

    QString script = m_scriptEdit->toPlainText().trimmed();
    if (script.isEmpty()) {
        m_statusLabel->setText(QStringLiteral("请输入查询脚本"));
        return;
    }

    // 校验 JSON
    QJsonParseError err;
    QJsonDocument doc = QJsonDocument::fromJson(script.toUtf8(), &err);
    if (err.error != QJsonParseError::NoError) {
        m_statusLabel->setText(QStringLiteral("JSON 语法错误：%1").arg(err.errorString()));
        return;
    }

    // 自动注入时间范围（如果脚本里有 timeperiods 占位）
    QJsonObject payload = doc.object();
    if (!payload.contains("timeperiods") || payload["timeperiods"].toArray().isEmpty()) {
        // 根据范围选择计算时间范围
        int days = 1;
        int rangeIdx = m_rangeCombo->currentData().toInt();
        if (rangeIdx == 0) days = 1;
        else if (rangeIdx == 1) days = 1;
        else if (rangeIdx == 7) days = 7;
        else if (rangeIdx == 30) days = 30;
        else days = m_daySpin->value();

        QJsonArray timeperiods;
        QDateTime now = QDateTime::currentDateTime();
        QString end = now.toString(Qt::ISODate);
        QString start = now.addDays(-days).toString(Qt::ISODate);
        timeperiods.append(start + "/" + end);
        payload["timeperiods"] = timeperiods;
    }

    m_statusLabel->setText(QStringLiteral("执行中…"));
    m_runBtn->setEnabled(false);

    QNetworkReply *r = m_api->postQuery(payload);
    connect(r, &QNetworkReply::finished, this, &QueryPage::onQueryResult);
}

void QueryPage::onQueryResult()
{
    QNetworkReply *r = qobject_cast<QNetworkReply *>(sender());
    if (!r) return;
    r->deleteLater();

    m_runBtn->setEnabled(true);

    QJsonDocument doc;
    QString err;
    if (!ApiClient::parseReply(r, &doc, &err)) {
        m_statusLabel->setText(QStringLiteral("查询失败：%1").arg(err));
        m_resultEdit->setPlainText(QStringLiteral("{\n  \"error\": \"%1\"\n}").arg(err));
        return;
    }

    m_resultEdit->setPlainText(doc.toJson(QJsonDocument::Indented));
    m_statusLabel->setText(QStringLiteral("查询完成"));
}

void QueryPage::onRangeChanged(int idx)
{
    Q_UNUSED(idx);
    bool custom = m_rangeCombo->currentData().toInt() == -1;
    m_daySpin->setVisible(custom);
}

void QueryPage::onTimeZoneChanged(int idx)
{
    Q_UNUSED(idx);
}

void QueryPage::releaseWeight()
{
    m_resultEdit->clear();
    if (m_statusLabel)
        m_statusLabel->clear();
}

void QueryPage::refresh()
{
    m_resultEdit->clear();
}

} // namespace awqtui
