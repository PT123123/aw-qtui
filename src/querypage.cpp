// querypage.cpp —— Query Explorer：手写 / 预置脚本 → /api/0/query → pretty-print JSON
#include "querypage.h"

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

QueryPage::~QueryPage() = default;

void QueryPage::buildUi()
{
    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(24, 24, 24, 24);
    root->setSpacing(12);

    // 标题
    auto *title = new QLabel(QStringLiteral("Query Explorer"));
    title->setObjectName(QStringLiteral("PageTitle"));
    root->addWidget(title);

    // 时间范围
    auto *rangeRow = new QHBoxLayout;
    rangeRow->addWidget(new QLabel(QStringLiteral("范围")));
    m_rangeCombo = new QComboBox;
    m_rangeCombo->addItem(QStringLiteral("今日"), 0);
    m_rangeCombo->addItem(QStringLiteral("昨日"), 1);
    m_rangeCombo->addItem(QStringLiteral("近 7 天"), 7);
    m_rangeCombo->addItem(QStringLiteral("近 30 天"), 30);
    m_rangeCombo->addItem(QStringLiteral("自定义（天）"), -1);
    connect(m_rangeCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &QueryPage::onRangeChanged);
    rangeRow->addWidget(m_rangeCombo);

    m_daySpin = new QSpinBox;
    m_daySpin->setMinimum(1);
    m_daySpin->setMaximum(365);
    m_daySpin->setValue(7);
    m_daySpin->setVisible(false);
    rangeRow->addWidget(m_daySpin);

    rangeRow->addWidget(new QLabel(QStringLiteral("时区")));
    m_tzCombo = new QComboBox;
    m_tzCombo->addItem(QStringLiteral("本机"), 0);
    m_tzCombo->addItem(QStringLiteral("UTC"), 1);
    connect(m_tzCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &QueryPage::onTimeZoneChanged);
    rangeRow->addWidget(m_tzCombo);
    rangeRow->addStretch(1);
    root->addLayout(rangeRow);

    // 预设脚本
    auto *presetRow = new QHBoxLayout;
    presetRow->addWidget(new QLabel(QStringLiteral("预设")));
    m_presetCombo = new QComboBox;
    connect(m_presetCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &QueryPage::onPresetChanged);
    presetRow->addWidget(m_presetCombo, 1);
    root->addLayout(presetRow);

    // 脚本编辑
    auto *scriptLabel = new QLabel(QStringLiteral("查询脚本（JSON）"));
    scriptLabel->setObjectName(QStringLiteral("SectionTitle"));
    root->addWidget(scriptLabel);

    m_scriptEdit = new QPlainTextEdit;
    m_scriptEdit->setObjectName(QStringLiteral("QueryEditor"));
    m_scriptEdit->setPlaceholderText(QStringLiteral(
        "{\n"
        "  \"query\": [\n"
        "    \"bucket_events = query_bucket(\\\"aw-watcher-window_\\\");\n"
        "    RETURN = bucket_events;\"\n"
        "  ],\n"
        "  \"timeperiods\": []\n"
        "}"));
    m_scriptEdit->setMinimumHeight(200);
    QFont mono = m_scriptEdit->font();
    mono.setFamily("Consolas");
    m_scriptEdit->setFont(mono);
    root->addWidget(m_scriptEdit, 1);

    // 操作行
    auto *btnRow = new QHBoxLayout;
    m_runBtn = new QPushButton(QStringLiteral("▶ 执行查询"));
    m_runBtn->setObjectName(QStringLiteral("PrimaryBtn"));
    connect(m_runBtn, &QPushButton::clicked, this, &QueryPage::onRun);
    m_formatBtn = new QPushButton(QStringLiteral("格式化"));
    connect(m_formatBtn, &QPushButton::clicked, this, [this] {
        QString text = m_resultEdit->toPlainText();
        if (text.isEmpty()) return;
        QJsonParseError err;
        QJsonDocument doc = QJsonDocument::fromJson(text.toUtf8(), &err);
        if (err.error == QJsonParseError::NoError) {
            m_resultEdit->setPlainText(doc.toJson(QJsonDocument::Indented));
        }
    });
    m_copyBtn = new QPushButton(QStringLiteral("复制结果"));
    connect(m_copyBtn, &QPushButton::clicked, this, [this] {
        QApplication::clipboard()->setText(m_resultEdit->toPlainText());
        m_statusLabel->setText(QStringLiteral("已复制到剪贴板"));
    });
    btnRow->addWidget(m_runBtn);
    btnRow->addWidget(m_formatBtn);
    btnRow->addWidget(m_copyBtn);
    btnRow->addStretch(1);
    root->addLayout(btnRow);

    // 结果
    auto *resultLabel = new QLabel(QStringLiteral("结果"));
    resultLabel->setObjectName(QStringLiteral("SectionTitle"));
    root->addWidget(resultLabel);

    m_resultEdit = new QPlainTextEdit;
    m_resultEdit->setReadOnly(true);
    m_resultEdit->setObjectName(QStringLiteral("QueryResult"));
    m_resultEdit->setPlaceholderText(QStringLiteral("查询结果将显示在这里…"));
    m_resultEdit->setMinimumHeight(200);
    m_resultEdit->setFont(mono);
    root->addWidget(m_resultEdit, 2);

    m_statusLabel = new QLabel(QStringLiteral("就绪"));
    m_statusLabel->setStyleSheet(QStringLiteral("color: %1;").arg(kColorFgMuted));
    root->addWidget(m_statusLabel);

    root->addStretch(0);
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

void QueryPage::refresh()
{
    m_resultEdit->clear();
}

} // namespace awqtui
