// statschart.h —— 统计图表控件（折线/柱状、多序列、平均线）
#pragma once

#include <QColor>
#include <QList>
#include <QPair>
#include <QString>
#include <QWidget>

namespace awqtui {

struct StatsSeries {
    QString name;
    QList<QPair<QString, double>> points; // (x 标签, y 值)
    QColor color;
};

class StatsChartWidget : public QWidget
{
    Q_OBJECT
public:
    explicit StatsChartWidget(QWidget *parent = nullptr);

    void setSeries(const QList<StatsSeries> &series);
    void setTitle(const QString &t) { m_title = t; update(); }
    void setShowAverage(bool on) { m_showAvg = on; update(); }
    void setBarMode(bool on) { m_barMode = on; update(); }
    void setUnit(const QString &u) { m_unit = u; update(); }

    QSize sizeHint() const override { return {760, 380}; }
    QSize minimumSizeHint() const override { return {480, 260}; }

protected:
    void paintEvent(QPaintEvent *event) override;

private:
    QString m_title;
    QString m_unit;
    QList<StatsSeries> m_series;
    bool m_showAvg = true;
    bool m_barMode = false;
};

} // namespace awqtui
