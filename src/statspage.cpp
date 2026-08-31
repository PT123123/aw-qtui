// statspage.cpp
#include "statspage.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDateEdit>
#include <QDateTime>
#include <QFile>
#include <QFileDialog>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QInputDialog>
#include <QLabel>
#include <QMessageBox>
#include <QPushButton>
#include <QSet>
#include <QStackedWidget>
#include <QTabWidget>
#include <QTableWidget>
#include <QVBoxLayout>

#include <algorithm>

#include "charts.h"
#include "mockdata.h"
#include "statschart.h"
#include "theme.h"

namespace awqtui {

static const char *kStatTypeNames[] = {"Top 统计", "Day duration", "Attendance", "Custom"};

StatsPage::StatsPage(TagStore *store, QWidget *parent)
    : QWidget(parent), m_store(store)
{
    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(14, 12, 14, 12);
    root->setSpacing(10);

    // 顶部：范围 + 操作
    auto *top = new QHBoxLayout;
    m_fromEdit = new QDateEdit;
    m_fromEdit->setCalendarPopup(true);
    m_fromEdit->setDisplayFormat(QStringLiteral("yyyy-MM-dd"));
    m_toEdit = new QDateEdit;
    m_toEdit->setCalendarPopup(true);
    m_toEdit->setDisplayFormat(QStringLiteral("yyyy-MM-dd"));
    auto *weekBtn = new QPushButton(QStringLiteral("本周"));
    weekBtn->setObjectName(QStringLiteral("ToolBtn"));
    auto *monthBtn = new QPushButton(QStringLiteral("本月"));
    monthBtn->setObjectName(QStringLiteral("ToolBtn"));
    m_avgCheck = new QCheckBox(QStringLiteral("平均值"));
    m_avgCheck->setChecked(true);
    auto *refreshBtn = new QPushButton(QStringLiteral("刷新"));
    refreshBtn->setObjectName(QStringLiteral("PrimaryBtn"));
    auto *exportBtn = new QPushButton(QStringLiteral("导出 CSV"));
    exportBtn->setObjectName(QStringLiteral("ToolBtn"));
    auto *addTabBtn = new QPushButton(QStringLiteral("＋ 新建统计"));
    addTabBtn->setObjectName(QStringLiteral("ToolBtn"));
    top->addWidget(new QLabel(QStringLiteral("From")));
    top->addWidget(m_fromEdit);
    top->addWidget(new QLabel(QStringLiteral("To")));
    top->addWidget(m_toEdit);
    top->addWidget(weekBtn);
    top->addWidget(monthBtn);
    top->addWidget(m_avgCheck);
    top->addStretch(1);
    top->addWidget(addTabBtn);
    top->addWidget(refreshBtn);
    top->addWidget(exportBtn);
    root->addLayout(top);

    m_tabs = new QTabWidget;
    m_tabs->setTabsClosable(true);
    root->addWidget(m_tabs, 1);

    m_status = new QLabel;
    m_status->setStyleSheet(QStringLiteral("color:%1;font-size:11px;").arg(kColorMuted2));
    root->addWidget(m_status);

    // 默认本周
    const QDate today = QDate::currentDate();
    m_fromEdit->setDate(today.addDays(-(today.dayOfWeek() - 1)));
    m_toEdit->setDate(today);

    connect(m_fromEdit, &QDateEdit::dateChanged, this, &StatsPage::onFromChanged);
    connect(m_toEdit, &QDateEdit::dateChanged, this, &StatsPage::onToChanged);
    connect(weekBtn, &QPushButton::clicked, this, [this] {
        const QDate today = QDate::currentDate();
        m_fromEdit->setDate(today.addDays(-(today.dayOfWeek() - 1)));
        m_toEdit->setDate(today);
        refresh();
    });
    connect(monthBtn, &QPushButton::clicked, this, [this] {
        const QDate today = QDate::currentDate();
        m_fromEdit->setDate(QDate(today.year(), today.month(), 1));
        m_toEdit->setDate(today);
        refresh();
    });
    connect(m_avgCheck, &QCheckBox::toggled, this, [this](bool) { refresh(); });
    connect(refreshBtn, &QPushButton::clicked, this, &StatsPage::refresh);
    connect(exportBtn, &QPushButton::clicked, this, &StatsPage::exportCsv);
    connect(addTabBtn, &QPushButton::clicked, this, &StatsPage::onAddTab);
    connect(m_tabs, &QTabWidget::tabCloseRequested, this, &StatsPage::onCloseTab);
    connect(m_tabs, &QTabWidget::currentChanged, this, [this](int) {
        const int idx = m_tabs->currentIndex();
        if (idx >= 0 && idx < m_tabsData.size()) {
            const auto &tab = m_tabsData[idx];
            if (tab.chart)
                tab.chart->setShowAverage(m_avgCheck->isChecked());
        }
    });

    // 默认两个 tab
    addTab(1, QStringLiteral("Day duration"));
    addTab(0, QStringLiteral("Top 统计"));
    refresh();
}

void StatsPage::onFromChanged()
{
    if (m_fromEdit->date() > m_toEdit->date())
        m_toEdit->setDate(m_fromEdit->date());
    refresh();
}

void StatsPage::onToChanged()
{
    if (m_toEdit->date() < m_fromEdit->date())
        m_fromEdit->setDate(m_toEdit->date());
    refresh();
}

void StatsPage::onAddTab()
{
    bool ok = false;
    const QString item = QInputDialog::getItem(this, QStringLiteral("新建统计"),
                                               QStringLiteral("统计类型："),
                                               {QStringLiteral("Top 统计"),
                                                QStringLiteral("Day duration"),
                                                QStringLiteral("Attendance"),
                                                QStringLiteral("Custom")},
                                               0, false, &ok);
    if (!ok)
        return;
    int type = 0;
    for (int i = 0; i < 4; ++i)
        if (item == QString::fromUtf8(kStatTypeNames[i]))
            type = i;
    addTab(type, item);
    refresh();
}

void StatsPage::onCloseTab(int idx)
{
    if (idx < 0 || idx >= m_tabsData.size())
        return;
    QWidget *page = m_tabsData[idx].page;
    m_tabs->removeTab(idx);
    m_tabsData.removeAt(idx);
    delete page;
}

void StatsPage::addTab(int type, const QString &title)
{
    TabData tab;
    tab.type = type;
    tab.page = new QWidget;
    auto *lay = new QVBoxLayout(tab.page);
    lay->setContentsMargins(6, 6, 6, 6);

    tab.typeCombo = new QComboBox;
    for (int i = 0; i < 4; ++i)
        tab.typeCombo->addItem(QString::fromUtf8(kStatTypeNames[i]));
    tab.typeCombo->setCurrentIndex(type);
    lay->addWidget(tab.typeCombo);

    tab.stack = new QStackedWidget;
    tab.bar = new HorizontalBarChart;
    tab.chart = new StatsChartWidget;
    tab.stack->addWidget(tab.bar);
    tab.stack->addWidget(tab.chart);
    lay->addWidget(tab.stack, 1);

    tab.table = new QTableWidget;
    tab.table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    tab.table->setSelectionBehavior(QAbstractItemView::SelectRows);
    lay->addWidget(tab.table);
    lay->setStretch(2, 2);
    tab.table->setMaximumHeight(240);

    connect(tab.typeCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
            [this, idx = m_tabsData.size()](int) {
                if (idx < m_tabsData.size())
                    rebuildTab(m_tabsData[idx]);
            });
    m_tabsData.append(tab);
    m_tabs->addTab(tab.page, title);
}

QMap<QDate, QMap<QString, qint64>> StatsPage::collectDaily() const
{
    QMap<QDate, QMap<QString, qint64>> out;
    const QDate from = m_fromEdit->date();
    const QDate to = m_toEdit->date();
    const int days = from.daysTo(to) + 1;
    if (days < 1 || days > 400)
        return out;
    for (int i = 0; i < days; ++i) {
        const QDate date = from.addDays(i);
        auto &day = out[date];
        const QList<TimelineLane> lanes = generateTimelineLanes(date);
        for (const auto &lane : lanes) {
            if (!lane.name.contains(QStringLiteral("window")) &&
                !lane.name.contains(QStringLiteral("web")))
                continue;
            for (const auto &ev : lane.events)
                day[ev.label] += (ev.endMs - ev.startMs) / 1000;
        }
    }
    return out;
}

qint64 StatsPage::dayTotal(const QMap<QString, qint64> &d) const
{
    qint64 t = 0;
    for (auto it = d.constBegin(); it != d.constEnd(); ++it)
        t += it.value();
    return t;
}

void StatsPage::rebuildTab(TabData &tab)
{
    const QMap<QDate, QMap<QString, qint64>> daily = collectDaily();
    const QDate from = m_fromEdit->date();
    const QDate to = m_toEdit->date();

    if (tab.type == 0) {
        // Top 统计：跨日期聚合应用时长
        QMap<QString, qint64> agg;
        for (auto it = daily.constBegin(); it != daily.constEnd(); ++it)
            for (auto jt = it.value().constBegin(); jt != it.value().constEnd(); ++jt)
                agg[jt.key()] += jt.value();
        QList<QPair<QString, qint64>> list;
        for (auto it = agg.constBegin(); it != agg.constEnd(); ++it)
            list.append({it.key(), it.value()});
        std::sort(list.begin(), list.end(),
                  [](const QPair<QString, qint64> &a, const QPair<QString, qint64> &b) {
                      return a.second > b.second;
                  });
        const int n = qMin<int>(list.size(), 10);
        QList<BarItem> items;
        for (int i = 0; i < n; ++i) {
            BarItem bi;
            bi.label = list[i].first;
            bi.valueSeconds = list[i].second;
            bi.color = colorForString(list[i].first);
            items.append(bi);
        }
        tab.bar->setTitle(QStringLiteral("Top %1 · %2 — %3")
                              .arg(n)
                              .arg(from.toString(QStringLiteral("yyyy-MM-dd")),
                                   to.toString(QStringLiteral("yyyy-MM-dd"))));
        tab.bar->setItems(items);
        tab.table->setRowCount(n);
        tab.table->setColumnCount(2);
        tab.table->setHorizontalHeaderLabels({QStringLiteral("应用"), QStringLiteral("时长")});
        for (int i = 0; i < n; ++i) {
            tab.table->setItem(i, 0, new QTableWidgetItem(list[i].first));
            tab.table->setItem(i, 1,
                               new QTableWidgetItem(formatDuration(list[i].second)));
        }
        tab.stack->setCurrentWidget(tab.bar);
    } else {
        // 折线/柱状类：x = 日期
        QList<QPair<QString, double>> pts;
        for (QDate date = from; date <= to; date = date.addDays(1)) {
            const auto it = daily.constFind(date);
            const qint64 total = it == daily.constEnd() ? 0 : dayTotal(it.value());
            pts.append({date.toString(QStringLiteral("MM-dd")), total / 60.0});
        }
        if (tab.type == 1) {
            // Day duration
            StatsSeries s;
            s.name = QStringLiteral("每日活跃时长");
            s.color = QColor(kColorAccent);
            s.points = pts;
            tab.chart->setTitle(QStringLiteral("Day duration · 每日活跃时长（分钟）"));
            tab.chart->setUnit(QStringLiteral("min"));
            tab.chart->setBarMode(false);
            tab.chart->setShowAverage(m_avgCheck->isChecked());
            tab.chart->setSeries({s});
            tab.stack->setCurrentWidget(tab.chart);
            tab.table->setColumnCount(2);
            tab.table->setHorizontalHeaderLabels({QStringLiteral("日期"), QStringLiteral("活跃(分钟)")});
            tab.table->setRowCount(pts.size());
            for (int i = 0; i < pts.size(); ++i) {
                tab.table->setItem(i, 0, new QTableWidgetItem(
                    from.addDays(i).toString(QStringLiteral("yyyy-MM-dd"))));
                tab.table->setItem(i, 1,
                                   new QTableWidgetItem(QString::number(pts[i].second, 'f', 1)));
            }
        } else if (tab.type == 2) {
            // Attendance：柱状，绿=≥1h 活跃，红=<1h
            StatsSeries s;
            s.name = QStringLiteral("每日活跃分钟");
            s.color = QColor(kColorOk);
            s.points = pts;
            tab.chart->setTitle(QStringLiteral("Attendance · 每日活跃时长（绿=≥1h）"));
            tab.chart->setUnit(QStringLiteral("min"));
            tab.chart->setBarMode(true);
            tab.chart->setSeries({s});
            tab.stack->setCurrentWidget(tab.chart);
            tab.table->setColumnCount(2);
            tab.table->setHorizontalHeaderLabels({QStringLiteral("日期"), QStringLiteral("活跃(分钟)")});
            tab.table->setRowCount(pts.size());
            for (int i = 0; i < pts.size(); ++i) {
                tab.table->setItem(i, 0, new QTableWidgetItem(
                    from.addDays(i).toString(QStringLiteral("yyyy-MM-dd"))));
                tab.table->setItem(i, 1,
                                   new QTableWidgetItem(QString::number(pts[i].second, 'f', 1)));
            }
        } else {
            // Custom：Top 5 应用的多序列折线
            QMap<QString, QVector<double>> appSeries;
            QMap<QString, QColor> colors;
            // 收集 Top5
            QMap<QString, qint64> agg;
            for (auto it = daily.constBegin(); it != daily.constEnd(); ++it)
                for (auto jt = it.value().constBegin(); jt != it.value().constEnd(); ++jt)
                    agg[jt.key()] += jt.value();
            QList<QPair<QString, qint64>> list;
            for (auto it = agg.constBegin(); it != agg.constEnd(); ++it)
                list.append({it.key(), it.value()});
            std::sort(list.begin(), list.end(),
                      [](const QPair<QString, qint64> &a, const QPair<QString, qint64> &b) {
                          return a.second > b.second;
                      });
            const int topN = qMin<int>(list.size(), 5);
            QSet<QString> keep;
            for (int i = 0; i < topN; ++i)
                keep.insert(list[i].first);

            QList<StatsSeries> series;
            for (auto it = agg.constBegin(); it != agg.constEnd(); ++it) {
                if (!keep.contains(it.key()))
                    continue;
                StatsSeries s;
                s.name = it.key();
                s.color = colorForString(it.key());
                for (QDate date = from; date <= to; date = date.addDays(1)) {
                    const auto di = daily.constFind(date);
                    const qint64 v = (di != daily.constEnd()) ? di->value(it.key()) : 0;
                    s.points.append({date.toString(QStringLiteral("MM-dd")), v / 60.0});
                }
                series.append(s);
            }
            tab.chart->setTitle(QStringLiteral("Custom · Top %1 应用每日时长对比").arg(topN));
            tab.chart->setBarMode(false);
            tab.chart->setShowAverage(m_avgCheck->isChecked());
            tab.chart->setSeries(series);
            tab.stack->setCurrentWidget(tab.chart);
            tab.table->setColumnCount(1 + topN);
            QStringList headers{QStringLiteral("日期")};
            for (int i = 0; i < topN; ++i)
                headers << list[i].first;
            tab.table->setHorizontalHeaderLabels(headers);
            const int days = from.daysTo(to) + 1;
            tab.table->setRowCount(days);
            for (int i = 0; i < days; ++i) {
                const QDate date = from.addDays(i);
                tab.table->setItem(i, 0,
                                   new QTableWidgetItem(date.toString(QStringLiteral("yyyy-MM-dd"))));
                for (int k = 0; k < topN; ++k) {
                    const auto di = daily.constFind(date);
                    const qint64 v =
                        (di != daily.constEnd()) ? di->value(list[k].first) : 0;
                    tab.table->setItem(i, 1 + k,
                                       new QTableWidgetItem(QString::number(v / 60.0, 'f', 1)));
                }
            }
        }
    }
}

void StatsPage::refresh()
{
    for (auto &tab : m_tabsData)
        rebuildTab(tab);
    const int days = m_fromEdit->date().daysTo(m_toEdit->date()) + 1;
    m_status->setText(QStringLiteral("范围 %1 — %2 · 共 %3 天")
                          .arg(m_fromEdit->date().toString(QStringLiteral("yyyy-MM-dd")),
                               m_toEdit->date().toString(QStringLiteral("yyyy-MM-dd")))
                          .arg(days));
}

void StatsPage::applyTheme()
{
    if (m_status)
        m_status->setStyleSheet(QStringLiteral("color:%1;font-size:11px;").arg(kColorMuted2));
    for (auto &tab : m_tabsData)
        rebuildTab(tab);
}

void StatsPage::exportCsv()
{
    const int idx = m_tabs->currentIndex();
    if (idx < 0 || idx >= m_tabsData.size())
        return;
    TabData &tab = m_tabsData[idx];
    if (tab.table->rowCount() == 0) {
        QMessageBox::information(this, QStringLiteral("导出"), QStringLiteral("当前统计没有数据"));
        return;
    }
    const QString path = QFileDialog::getSaveFileName(this, QStringLiteral("导出 CSV"),
                                                      QStringLiteral("stats.csv"),
                                                      QStringLiteral("CSV (*.csv)"));
    if (path.isEmpty())
        return;
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Text))
        return;
    QStringList lines;
    QStringList header;
    for (int c = 0; c < tab.table->columnCount(); ++c)
        header << tab.table->horizontalHeaderItem(c)->text();
    lines << header.join(QLatin1Char(','));
    for (int r = 0; r < tab.table->rowCount(); ++r) {
        QStringList row;
        for (int c = 0; c < tab.table->columnCount(); ++c) {
            const QTableWidgetItem *it = tab.table->item(r, c);
            row << (it ? it->text() : QString());
        }
        lines << row.join(QLatin1Char(','));
    }
    f.write(lines.join(QLatin1Char('\n')).toUtf8());
    QMessageBox::information(this, QStringLiteral("导出"),
                             QStringLiteral("已导出到 %1").arg(path));
}

} // namespace awqtui
