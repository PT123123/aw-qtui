// statspage.h —— 多日统计页（Top / Day duration / Attendance / Custom）
#pragma once

#include <QDate>
#include <QMap>
#include <QWidget>

#include "tagstore.h"

class QCheckBox;
class QComboBox;
class QDateEdit;
class QLabel;
class QPushButton;
class QStackedWidget;
class QTabWidget;
class QTableWidget;

namespace awqtui {

class HorizontalBarChart;
class StatsChartWidget;

class StatsPage : public QWidget
{
    Q_OBJECT
public:
    explicit StatsPage(TagStore *store, QWidget *parent = nullptr);
    void refresh();
    // 按当前主题重建页面内联样式与图表（主题切换时调用）
    void applyTheme();

private slots:
    void onFromChanged();
    void onToChanged();
    void onAddTab();
    void onCloseTab(int idx);
    void exportCsv();

private:
    struct TabData {
        int type = 0;                 // 0 top / 1 day duration / 2 attendance / 3 custom
        QWidget *page = nullptr;
        QComboBox *typeCombo = nullptr;
        QStackedWidget *stack = nullptr;
        HorizontalBarChart *bar = nullptr;
        StatsChartWidget *chart = nullptr;
        QTableWidget *table = nullptr;
    };
    void addTab(int type, const QString &title);
    void rebuildTab(TabData &tab);
    // 每日每应用秒数
    QMap<QDate, QMap<QString, qint64>> collectDaily() const;
    qint64 dayTotal(const QMap<QString, qint64> &d) const;

    TagStore *m_store = nullptr;
    QDateEdit *m_fromEdit = nullptr;
    QDateEdit *m_toEdit = nullptr;
    QCheckBox *m_avgCheck = nullptr;
    QTabWidget *m_tabs = nullptr;
    QLabel *m_status = nullptr;
    QList<TabData> m_tabsData;
};

} // namespace awqtui
