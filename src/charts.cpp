// charts.cpp
#include "charts.h"

#include "theme.h"

#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QToolTip>
#include <cmath>

namespace {
constexpr double kPi = 3.14159265358979323846;
}

namespace awqtui {

QString formatDuration(qint64 totalSeconds)
{
    if (totalSeconds < 60)
        return QStringLiteral("%1s").arg(totalSeconds);
    if (totalSeconds < 3600)
        return QStringLiteral("%1m %2s").arg(totalSeconds / 60).arg(totalSeconds % 60);
    const int h = static_cast<int>(totalSeconds / 3600);
    const int m = static_cast<int>((totalSeconds % 3600) / 60);
    return QStringLiteral("%1h %2m").arg(h).arg(m);
}

// ═══════════════════════════════════════════════════════════
// HorizontalBarChart
// ═══════════════════════════════════════════════════════════
HorizontalBarChart::HorizontalBarChart(const QString &title, QWidget *parent)
    : QWidget(parent), m_title(title)
{
    setMouseTracking(true);
    setMinimumHeight(80);
}

void HorizontalBarChart::setItems(const QList<BarItem> &items)
{
    m_items = items;
    updateGeometry();
    update();
}

void HorizontalBarChart::setTitle(const QString &title)
{
    m_title = title;
    update();
}

QSize HorizontalBarChart::sizeHint() const
{
    const int rowH = 30;
    const int titleH = m_title.isEmpty() ? 8 : 32;
    return {400, titleH + static_cast<int>(m_items.size()) * rowH + 8};
}

QSize HorizontalBarChart::minimumSizeHint() const
{
    return {280, qMax(80, sizeHint().height())};
}

void HorizontalBarChart::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, true);

    const int w = width();
    const int rowH = 30;
    const int titleH = m_title.isEmpty() ? 8 : 32;
    const int valueWidth = 70;
    const int barX = m_labelWidth + 8;
    const int barW = qMax(40, w - barX - valueWidth - 8);

    // 标题
    if (!m_title.isEmpty()) {
        p.setPen(QColor(kColorFg));
        QFont f = p.font();
        f.setBold(true);
        f.setPointSize(10);
        p.setFont(f);
        p.drawText(QRect(0, 0, w, 28), Qt::AlignLeft | Qt::AlignVCenter, m_title);
    }

    if (m_items.isEmpty()) {
        p.setPen(QColor(kColorFgMuted));
        p.drawText(rect(), Qt::AlignCenter, QStringLiteral("无数据"));
        return;
    }

    qint64 maxVal = 1;
    for (const auto &it : m_items) maxVal = qMax(maxVal, it.valueSeconds);

    for (int i = 0; i < m_items.size(); ++i) {
        const BarItem &it = m_items.at(i);
        const int y = titleH + i * rowH;

        // hover 背景
        if (i == m_hoverIndex) {
            p.fillRect(QRect(0, y, w, rowH - 2), QColor(255, 255, 255, 12));
        }

        // label
        p.setPen(QColor(kColorFg));
        QFont lf = p.font();
        lf.setPointSize(9);
        p.setFont(lf);
        const QString lbl = it.subLabel.isEmpty() ? it.label : it.subLabel;
        QFontMetrics fm(lf);
        const QString elided = fm.elidedText(lbl, Qt::ElideRight, m_labelWidth - 4);
        p.drawText(QRect(4, y, m_labelWidth - 4, rowH - 2), Qt::AlignLeft | Qt::AlignVCenter, elided);

        // 条形背景
        const int barY = y + 7;
        const int barH = rowH - 16;
        p.fillRect(QRect(barX, barY, barW, barH), QColor(kColorBgElev2));

        // 条形前景
        const double ratio = it.valueSeconds / double(maxVal);
        const int fw = qMax(2, static_cast<int>(barW * ratio));
        QPainterPath path;
        path.addRoundedRect(QRectF(barX, barY, fw, barH), 3, 3);
        p.fillPath(path, it.color);

        // 时长文字
        p.setPen(QColor(kColorFgMuted));
        p.drawText(QRect(barX + barW + 4, y, valueWidth - 4, rowH - 2),
                   Qt::AlignLeft | Qt::AlignVCenter, formatDuration(it.valueSeconds));
    }
}

void HorizontalBarChart::mouseMoveEvent(QMouseEvent *event)
{
    const int rowH = 30;
    const int titleH = m_title.isEmpty() ? 8 : 32;
    const int idx = (event->pos().y() - titleH) / rowH;
    const int newHover = (idx >= 0 && idx < m_items.size()) ? idx : -1;
    if (newHover != m_hoverIndex) {
        m_hoverIndex = newHover;
        update();
    }
    if (m_hoverIndex >= 0) {
        const BarItem &it = m_items.at(m_hoverIndex);
        QToolTip::showText(event->globalPos(),
            QStringLiteral("%1\n%2").arg(it.label, formatDuration(it.valueSeconds)), this);
    }
}

void HorizontalBarChart::leaveEvent(QEvent *)
{
    m_hoverIndex = -1;
    update();
}

// ═══════════════════════════════════════════════════════════
// HourlyActivityBars
// ═══════════════════════════════════════════════════════════
HourlyActivityBars::HourlyActivityBars(QWidget *parent) : QWidget(parent)
{
    setMouseTracking(true);
    m_data = QList<qint64>(24, 0);
}

void HourlyActivityBars::setData(const QList<qint64> &secondsPerHour)
{
    m_data = secondsPerHour;
    if (m_data.size() < 24) m_data += QList<qint64>(24 - m_data.size(), 0);
    m_maxVal = 1;
    for (qint64 v : m_data) m_maxVal = qMax(m_maxVal, v);
    update();
}

void HourlyActivityBars::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, true);

    const int w = width();
    const int h = height();
    const int axisH = 20;
    const int topPad = 8;
    const int chartH = h - axisH - topPad;
    const int barAreaW = w - 8;
    const double barW = barAreaW / 24.0;

    // 基线
    p.setPen(QPen(QColor(kColorBorder), 1));
    p.drawLine(4, topPad + chartH, w - 4, topPad + chartH);

    // 柱子
    for (int i = 0; i < 24; ++i) {
        const double ratio = m_data[i] / double(m_maxVal);
        const int bh = qMax(1, static_cast<int>(chartH * ratio));
        const int bx = static_cast<int>(4 + i * barW);
        const int by = topPad + chartH - bh;
        const int bw = qMax(2, static_cast<int>(barW - 3));

        // 颜色：活跃越高越亮蓝
        const int lightness = 110 + static_cast<int>(ratio * 70);
        QColor barColor = QColor::fromHsl(215, 180, lightness);
        if (i == m_hoverHour) {
            barColor = barColor.lighter(130);
        }
        p.fillRect(QRect(bx, by, bw, bh), barColor);
    }

    // x 轴标签
    p.setPen(QColor(kColorFgMuted));
    QFont f = p.font();
    f.setPointSize(8);
    p.setFont(f);
    const int ticks[] = {0, 3, 6, 9, 12, 15, 18, 21, 23};
    for (int t : ticks) {
        const int tx = static_cast<int>(4 + t * barW + barW / 2);
        p.drawText(QRect(tx - 20, topPad + chartH + 2, 40, axisH - 2),
                   Qt::AlignHCenter | Qt::AlignTop, QStringLiteral("%1:00").arg(t, 2, 10, QChar('0')));
    }
}

void HourlyActivityBars::mouseMoveEvent(QMouseEvent *event)
{
    const int w = width();
    const int barAreaW = w - 8;
    const double barW = barAreaW / 24.0;
    const int h = (event->pos().x() - 4) / static_cast<int>(barW);
    const int newHover = (h >= 0 && h < 24) ? h : -1;
    if (newHover != m_hoverHour) {
        m_hoverHour = newHover;
        update();
    }
    if (m_hoverHour >= 0) {
        QToolTip::showText(event->globalPos(),
            QStringLiteral("%1:00 — %2:00\n活跃 %3")
                .arg(m_hoverHour, 2, 10, QChar('0'))
                .arg((m_hoverHour + 1) % 24, 2, 10, QChar('0'))
                .arg(formatDuration(m_data[m_hoverHour])),
            this);
    }
}

void HourlyActivityBars::leaveEvent(QEvent *)
{
    m_hoverHour = -1;
    update();
}

// ═══════════════════════════════════════════════════════════
// CategoryBars
// ═══════════════════════════════════════════════════════════
CategoryBars::CategoryBars(QWidget *parent) : QWidget(parent)
{
    setMouseTracking(true);
    m_data = QList<qint64>(24, 0);
    m_cats = QStringList(24, QStringLiteral("Uncategorized"));
}

void CategoryBars::setData(const QList<qint64> &secondsPerHour, const QStringList &hourlyCategories)
{
    m_data = secondsPerHour;
    m_cats = hourlyCategories;
    while (m_data.size() < 24) m_data.append(0);
    while (m_cats.size() < 24) m_cats.append(QStringLiteral("Uncategorized"));
    m_maxVal = 1;
    for (qint64 v : m_data) m_maxVal = qMax(m_maxVal, v);
    update();
}

void CategoryBars::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, true);

    const int w = width();
    const int h = height();
    const int axisH = 20;
    const int topPad = 8;
    const int chartH = h - axisH - topPad;
    const double barW = (w - 8) / 24.0;

    p.setPen(QPen(QColor(kColorBorder), 1));
    p.drawLine(4, topPad + chartH, w - 4, topPad + chartH);

    for (int i = 0; i < 24; ++i) {
        const double ratio = m_data[i] / double(m_maxVal);
        const int bh = qMax(1, static_cast<int>(chartH * ratio));
        const int bx = static_cast<int>(4 + i * barW);
        const int by = topPad + chartH - bh;
        const int bw = qMax(2, static_cast<int>(barW - 3));

        QColor col = colorForCategory(m_cats.value(i, QStringLiteral("Uncategorized")));
        if (i == m_hoverHour) col = col.lighter(130);
        p.fillRect(QRect(bx, by, bw, bh), col);
    }

    p.setPen(QColor(kColorFgMuted));
    QFont f = p.font();
    f.setPointSize(8);
    p.setFont(f);
    const int ticks[] = {0, 6, 12, 18, 23};
    for (int t : ticks) {
        const int tx = static_cast<int>(4 + t * barW + barW / 2);
        p.drawText(QRect(tx - 20, topPad + chartH + 2, 40, axisH - 2),
                   Qt::AlignHCenter | Qt::AlignTop, QStringLiteral("%1:00").arg(t, 2, 10, QChar('0')));
    }
}

void CategoryBars::mouseMoveEvent(QMouseEvent *event)
{
    const double barW = (width() - 8) / 24.0;
    const int h = (event->pos().x() - 4) / static_cast<int>(barW);
    const int newHover = (h >= 0 && h < 24) ? h : -1;
    if (newHover != m_hoverHour) {
        m_hoverHour = newHover;
        update();
    }
    if (m_hoverHour >= 0) {
        QToolTip::showText(event->globalPos(),
            QStringLiteral("%1:00 — %2:00\n分类：%3\n活跃 %4")
                .arg(m_hoverHour, 2, 10, QChar('0'))
                .arg((m_hoverHour + 1) % 24, 2, 10, QChar('0'))
                .arg(m_cats.value(m_hoverHour, QStringLiteral("-")))
                .arg(formatDuration(m_data[m_hoverHour])),
            this);
    }
}

void CategoryBars::leaveEvent(QEvent *)
{
    m_hoverHour = -1;
    update();
}

// ═══════════════════════════════════════════════════════════
// DonutChart
// ═══════════════════════════════════════════════════════════
DonutChart::DonutChart(QWidget *parent) : QWidget(parent)
{
    setMouseTracking(true);
}

void DonutChart::setItems(const QList<BarItem> &items)
{
    m_items = items;
    m_total = 1;
    for (const auto &it : m_items) m_total += it.valueSeconds;
    update();
}

void DonutChart::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, true);

    const int side = qMin(width(), height()) - 20;
    const int cx = width() / 2;
    const int cy = height() / 2;
    const QRectF outerRect(cx - side / 2.0, cy - side / 2.0, side, side);
    const QRectF innerRect(cx - side / 4.0, cy - side / 4.0, side / 2.0, side / 2.0);

    if (m_items.isEmpty()) {
        p.setPen(QColor(kColorFgMuted));
        p.drawText(rect(), Qt::AlignCenter, QStringLiteral("无数据"));
        return;
    }

    double startAngle = 90.0; // 从顶部开始
    for (int i = 0; i < m_items.size(); ++i) {
        const BarItem &it = m_items[i];
        const double span = 360.0 * it.valueSeconds / double(m_total);
        QColor col = it.color;
        if (i == m_hoverIndex) col = col.lighter(125);

        QPainterPath path;
        path.moveTo(cx, cy);
        path.arcTo(outerRect, startAngle, -span);
        path.closeSubpath();
        // 挖空内圈
        QPainterPath hole;
        hole.addEllipse(innerRect);
        path = path.subtracted(hole);
        p.fillPath(path, col);

        startAngle -= span;
    }

    // 内圈背景
    p.fillRect(innerRect, QColor(kColorBg));

    // 中心文字
    p.setPen(QColor(kColorFg));
    QFont f = p.font();
    f.setPointSize(10);
    f.setBold(true);
    p.setFont(f);
    p.drawText(innerRect, Qt::AlignCenter, QStringLiteral("Categories"));

    // 图例（右侧）
    const int legendX = cx + side / 2 + 12;
    int ly = cy - m_items.size() * 18 / 2;
    QFont lf = p.font();
    lf.setPointSize(8);
    lf.setBold(false);
    p.setFont(lf);
    for (int i = 0; i < m_items.size(); ++i) {
        p.fillRect(QRect(legendX, ly + 3, 10, 10), m_items[i].color);
        p.setPen(QColor(kColorFgSoft));
        const QString txt = m_items[i].label.left(18);
        p.drawText(QRect(legendX + 16, ly, 120, 16), Qt::AlignLeft | Qt::AlignVCenter, txt);
        ly += 18;
    }
}

void DonutChart::mouseMoveEvent(QMouseEvent *event)
{
    // 简化：根据角度判断 hover 扇区
    const int side = qMin(width(), height()) - 20;
    const int cx = width() / 2;
    const int cy = height() / 2;
    const double dx = event->pos().x() - cx;
    const double dy = event->pos().y() - cy;
    const double dist = std::sqrt(dx * dx + dy * dy);
    int newHover = -1;
    if (dist > side / 4.0 && dist < side / 2.0) {
        double angle = std::atan2(-dy, dx) * 180.0 / kPi;
        if (angle < 0) angle += 360.0;
        // 从 90 度（顶部）开始顺时针
        double rel = angle - 90.0;
        if (rel < 0) rel += 360.0;
        double acc = 0;
        for (int i = 0; i < m_items.size(); ++i) {
            const double span = 360.0 * m_items[i].valueSeconds / double(m_total);
            if (rel >= acc && rel < acc + span) {
                newHover = i;
                break;
            }
            acc += span;
        }
    }
    if (newHover != m_hoverIndex) {
        m_hoverIndex = newHover;
        update();
    }
    if (m_hoverIndex >= 0) {
        const BarItem &it = m_items[m_hoverIndex];
        const int pct = static_cast<int>(100 * it.valueSeconds / double(m_total));
        QToolTip::showText(event->globalPos(),
            QStringLiteral("%1\n%2 (%3%)").arg(it.label, formatDuration(it.valueSeconds)).arg(pct),
            this);
    }
}

void DonutChart::leaveEvent(QEvent *)
{
    m_hoverIndex = -1;
    update();
}

} // namespace awqtui
