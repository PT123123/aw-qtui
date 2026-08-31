// untaggedview.cpp —— 未标记时间热力图实现
#include "untaggedview.h"

#include <QDate>
#include <QMap>
#include <QMouseEvent>
#include <QPainter>
#include <QSet>

#include "mockdata.h"

namespace awqtui {

UntaggedView::UntaggedView(TagStore *store, QWidget *parent)
    : QWidget(parent), m_store(store)
{
    const QDate today = QDate::currentDate();
    m_from = today.addDays(-29);
    m_to = today;
    rebuild();
    setMouseTracking(true);
}

void UntaggedView::setRange(const QDate &from, const QDate &to)
{
    m_from = from;
    m_to = to;
    rebuild();
    update();
    updateGeometry();
}

void UntaggedView::rebuild()
{
    m_stats.clear();
    m_cellRects.clear();
    if (!m_store)
        return;
    const int days = m_from.daysTo(m_to) + 1;
    if (days < 1)
        return;
    for (int i = 0; i < days && i < 400; ++i) {
        const QDate date = m_from.addDays(i);
        const qint64 dayStart = date.startOfDay().toMSecsSinceEpoch();
        const qint64 dayEnd = date.addDays(1).startOfDay().toMSecsSinceEpoch();
        DayStat st;
        st.date = date;
        // 活跃时长（mock：小时活跃秒数求和）
        const QList<qint64> hourly = generateHourlyActivity(date);
        qint64 active = 0;
        for (const qint64 h : hourly)
            active += h;
        st.activeSec = active;
        st.taggedSec = m_store->taggedTimeInRange(dayStart, dayEnd) / 1000;
        m_stats.append(st);
    }
    // 月份数
    QSet<int> months;
    for (const auto &s : m_stats)
        months.insert(s.date.year() * 100 + s.date.month());
    m_monthRows = months.size();
}

QSize UntaggedView::sizeHint() const
{
    return QSize(m_labelW + 32 * (m_cell + m_gap) + 20, m_monthRows * 56 + 20);
}

QSize UntaggedView::minimumSizeHint() const
{
    return QSize(600, m_monthRows * 56 + 10);
}

void UntaggedView::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    p.fillRect(rect(), QColor(QStringLiteral("#1b1d21")));
    p.setRenderHint(QPainter::Antialiasing, false);

    const QFont f = p.font();
    const QFont small(f.family(), f.pointSize() - 1);
    p.setFont(small);

    m_cellRects.clear();
    // 按月份分组
    QMap<int, QList<DayStat>> byMonth;
    for (const auto &s : m_stats)
        byMonth[s.date.year() * 100 + s.date.month()].append(s);

    int y = 10;
    int row = 0;
    for (auto it = byMonth.constBegin(); it != byMonth.constEnd(); ++it, ++row) {
        const int monthKey = it.key();
        const int year = monthKey / 100;
        const int month = monthKey % 100;
        const QDate monthStart(year, month, 1);
        const QRect monthRect(m_labelW, y, 32 * (m_cell + m_gap), m_cell + 22);
        paintMonth(p, monthStart, it.value(), y, monthRect);
        y += m_cell + 30;
    }
}

void UntaggedView::paintMonth(QPainter &p, const QDate &monthStart, const QList<DayStat> &stats,
                              int baseY, const QRect &)
{
    p.setPen(QColor(QStringLiteral("#c9d1d9")));
    p.drawText(QRect(0, baseY, m_labelW - 8, m_cell),
               Qt::AlignRight | Qt::AlignVCenter,
               monthStart.toString(QStringLiteral("yyyy-MM")));

    int x = m_labelW;
    for (const auto &st : stats) {
        const QRect cell(x, baseY + 22, m_cell, m_cell);
        // 颜色语义：深绿=全部已标记；浅绿=已标记；橙=未标记；白=无数据
        const bool hasActive = st.activeSec > 0;
        QColor base = QColor(QStringLiteral("#2c2e33")); // 无数据（深色主题的“白”）
        if (hasActive) {
            const qreal ratio = qBound<qreal>(0.0, static_cast<qreal>(st.taggedSec) / st.activeSec, 1.0);
            if (ratio >= 0.999) {
                base = QColor(QStringLiteral("#1f7a4d")); // 深绿
                p.fillRect(cell.adjusted(0, 0, -1, -1), base);
            } else if (ratio <= 0.001) {
                base = QColor(QStringLiteral("#c07f00")); // 橙
                p.fillRect(cell.adjusted(0, 0, -1, -1), base);
            } else {
                // 混合：左绿右橙，宽度按已标记比例
                const int greenW = static_cast<int>(cell.width() * ratio);
                p.fillRect(QRect(cell.x(), cell.y(), qMax(1, greenW), cell.height()).adjusted(0, 0, -1, -1),
                           QColor(QStringLiteral("#2ea043"))); // 浅绿
                p.fillRect(QRect(cell.x() + greenW, cell.y(), cell.width() - greenW, cell.height())
                               .adjusted(0, 0, -1, -1),
                           QColor(QStringLiteral("#c07f00"))); // 橙
                base = QColor(QStringLiteral("#2ea043"));
            }
        } else {
            p.fillRect(cell.adjusted(0, 0, -1, -1), base);
        }
        p.setPen(QColor(QStringLiteral("#1b1d21")));
        p.drawRect(cell.adjusted(0, 0, -1, -1));
        // 日期数字
        p.setPen(base.lightness() > 150 ? QColor(QStringLiteral("#111")) : QColor(QStringLiteral("#ddd")));
        p.drawText(cell.adjusted(1, 0, -1, -1), Qt::AlignCenter, QString::number(st.date.day()));
        m_cellRects.append(cell);
        x += m_cell + m_gap;
    }
}

void UntaggedView::mousePressEvent(QMouseEvent *event)
{
    for (int i = 0; i < m_cellRects.size(); ++i) {
        if (m_cellRects[i].contains(event->pos()) && i < m_stats.size()) {
            emit dayClicked(m_stats[i].date);
            return;
        }
    }
    QWidget::mousePressEvent(event);
}

} // namespace awqtui
