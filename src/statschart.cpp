// statschart.cpp
#include "statschart.h"

#include "theme.h"

#include <QPainter>

namespace awqtui {

StatsChartWidget::StatsChartWidget(QWidget *parent) : QWidget(parent) {}

void StatsChartWidget::setSeries(const QList<StatsSeries> &series)
{
    m_series = series;
    update();
}

void StatsChartWidget::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    p.fillRect(rect(), QColor(kColorChartBg));
    p.setRenderHint(QPainter::Antialiasing, true);

    const int left = 70;
    const int right = 16;
    const int top = 40;
    const int bottom = 46;
    const QRect plot(left, top, qMax(10, width() - left - right),
                     qMax(10, height() - top - bottom));

    // 标题
    if (!m_title.isEmpty()) {
        p.setPen(QColor(kColorFg));
        QFont f = p.font();
        f.setBold(true);
        p.setFont(f);
        p.drawText(QRect(0, 6, width(), 24), Qt::AlignCenter, m_title);
        f.setBold(false);
        p.setFont(f);
    }

    if (m_series.isEmpty()) {
        p.setPen(QColor(kColorMuted2));
        p.drawText(plot, Qt::AlignCenter, QStringLiteral("（无数据）"));
        return;
    }

    // x 标签（取第一序列；多序列要求相同 x）
    const auto &xLabels = m_series.first().points;
    const int n = xLabels.size();
    if (n == 0)
        return;

    // y 范围
    double yMax = 1;
    for (const auto &s : m_series)
        for (const auto &pt : s.points)
            yMax = qMax(yMax, pt.second);
    yMax *= 1.1;

    // 网格 + y 轴
    p.setPen(QPen(QColor(kColorBgElev2), 1));
    const int gridY = 4;
    for (int i = 0; i <= gridY; ++i) {
        const double v = yMax * i / gridY;
        const int y = plot.bottom() - int(plot.height() * v / yMax);
        p.setPen(QPen(QColor(kColorBgElev2), 1));
        p.drawLine(plot.left(), y, plot.right(), y);
        p.setPen(QColor(kColorFgMuted));
        p.drawText(QRect(0, y - 8, left - 8, 16), Qt::AlignRight | Qt::AlignVCenter,
                   QString::number(v, 'f', v < 10 ? 1 : 0));
    }

    // 柱状模式：单序列、按组绘制
    if (m_barMode) {
        const double groupW = double(plot.width()) / n;
        for (int i = 0; i < n; ++i) {
            const double v = xLabels[i].second;
            const int barH = int(plot.height() * v / yMax);
            const QRect bar(int(plot.left() + groupW * i + groupW * 0.2),
                            plot.bottom() - barH, int(groupW * 0.6), barH);
            p.fillRect(bar, m_series.first().color);
            p.setPen(QColor(kColorChartBg));
            p.drawRect(bar);
        }
    } else {
        // 折线
        for (const auto &s : m_series) {
            p.setPen(QPen(s.color, 2));
            QPointF prev;
            bool have = false;
            for (int i = 0; i < n; ++i) {
                const double v = s.points[i].second;
                const double x = plot.left() + (n <= 1 ? plot.width() / 2.0
                                                       : double(plot.width()) * i / (n - 1));
                const double y = plot.bottom() - plot.height() * v / yMax;
                const QPointF pt(x, y);
                if (have)
                    p.drawLine(prev, pt);
                else
                    p.drawEllipse(pt, 3, 3);
                prev = pt;
                have = true;
            }
            // 点
            for (int i = 0; i < n; ++i) {
                const double v = s.points[i].second;
                const double x = plot.left() + (n <= 1 ? plot.width() / 2.0
                                                       : double(plot.width()) * i / (n - 1));
                const double y = plot.bottom() - plot.height() * v / yMax;
                p.setPen(Qt::NoPen);
                p.setBrush(s.color);
                p.drawEllipse(QPointF(x, y), 2.5, 2.5);
            }
            // 平均线
            if (m_showAvg && !s.points.isEmpty()) {
                double sum = 0;
                for (const auto &pt : s.points)
                    sum += pt.second;
                const double avg = sum / s.points.size();
                const int y = int(plot.bottom() - plot.height() * avg / yMax);
                QPen pen(s.color, 1, Qt::DashLine);
                p.setPen(pen);
                p.drawLine(plot.left(), y, plot.right(), y);
            }
        }
    }

    // x 轴标签（抽样显示）
    p.setPen(QColor(kColorFgMuted));
    const int step = qMax(1, n / 8);
    for (int i = 0; i < n; i += step) {
        const double x = plot.left() + (n <= 1 ? plot.width() / 2.0
                                               : double(plot.width()) * i / (n - 1));
        p.drawText(QRect(int(x - 40), plot.bottom() + 6, 80, 16), Qt::AlignCenter,
                   xLabels[i].first);
    }

    // 图例
    int lx = plot.right() - 60;
    for (auto it = m_series.constEnd(); it != m_series.constBegin();) {
        --it;
        const auto &s = *it;
        p.setPen(Qt::NoPen);
        p.setBrush(s.color);
        p.drawRect(lx, top - 22, 10, 10);
        p.setPen(QColor(kColorFgSoft));
        const int w = p.fontMetrics().horizontalAdvance(s.name) + 6;
        p.drawText(QRect(lx + 14, top - 28, w, 20), Qt::AlignLeft | Qt::AlignVCenter, s.name);
        lx -= (w + 14 + 10);
    }
}

} // namespace awqtui
