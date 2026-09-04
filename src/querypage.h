// querypage.h —— Query Explorer 页（手写 / 预置脚本 → /api/0/query → pretty-print JSON）
#pragma once

#include <QWidget>

class QComboBox;
class QLabel;
class QLineEdit;
class QPushButton;
class QSpinBox;
class QPlainTextEdit;

namespace awqtui {

class ApiClient;

class QueryPage : public QWidget
{
    Q_OBJECT
public:
    explicit QueryPage(ApiClient *api, QWidget *parent = nullptr);
    ~QueryPage() override;

    void refresh();
private slots:
    void onPresetChanged(int idx);
    void onRun();
    void onQueryResult();
    void onRangeChanged(int idx);
    void onTimeZoneChanged(int idx);

private:
    void buildUi();
    void applyStyle();
    void loadPresets();
    void updateScriptFromRange();

    ApiClient *m_api = nullptr;

    // 范围选择
    QComboBox *m_rangeCombo = nullptr;
    QComboBox *m_tzCombo = nullptr;
    QSpinBox *m_daySpin = nullptr;

    // 脚本编辑
    QComboBox *m_presetCombo = nullptr;
    QPlainTextEdit *m_scriptEdit = nullptr;

    // 操作
    QPushButton *m_runBtn = nullptr;
    QPushButton *m_formatBtn = nullptr;
    QPushButton *m_copyBtn = nullptr;

    // 结果
    QPlainTextEdit *m_resultEdit = nullptr;
    QLabel *m_statusLabel = nullptr;

    // 预设脚本列表
    struct Preset {
        QString name;
        QString script;
    };
    QList<Preset> m_presets;
};

} // namespace awqtui
