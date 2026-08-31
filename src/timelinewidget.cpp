// timelinewidget.cpp
#include "timelinewidget.h"

#include "charts.h"

#include <QDateTime>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QToolTip>
#include <QWheelEvent>
#include <cmath>

namespace awqtui {

TimelineWidget::TimelineWidget(QWidget *parent) : QWidget(parent)
{
    setMouseTracking(true);
    setMinimumHeight(160);
    // 默认当天
    const QDate today = QDate::currentDate();
    m_defaultStartMs = QDateTime(today, QTime(0, 0), Qt::LocalTime).toMSecsSinceEpoch();
    m_defaultEndMs = m_defaultStartMs + 86400000LL;
    m_viewStartMs = m_defaultStartMs;
    m_viewEndMs = m_defaultEndMs;
}

void TimelineWidget::setLanes(const QList<TimelineLane> &lanes)
{
    m_lanes = lanes;
    updateGeometry();
    update();
}

// ── 选择模式 ────────────────────────────────────────────────
void TimelineWidget::setSelectMode(bool on)
{
    m_selectMode = on;
    if (!on)
        m_selecting = false;
    setCursor(Qt::ArrowCursor);
    update();
}

void TimelineWidget::clearSelection()
{
    if (m_selection.isEmpty())
        return;
    m_selection.clear();
    update();
    emit selectionChanged(m_selection);
}

void TimelineWidget::setSelection(const QList<QPair<qint64, qint64>> &ranges)
{
    m_selection = ranges;
    update();
}

int TimelineWidget::laneAtY(int y) const
{
    const int top = timelineTop();
    if (y < top)
        return -1;
    return (y - top) / m_laneH;
}

qint64 TimelineWidget::snapToBoundary(int laneIndex, qint64 t) const
{
    constexpr qint64 kSnap = 90 * 1000LL; // 吸附阈值 90s
    if (laneIndex < 0 || laneIndex >= m_lanes.size())
        return t;
    qint64 best = t;
    qint64 bestDiff = kSnap + 1;
    for (const auto &ev : m_lanes[laneIndex].events) {
        const qint64 d1 = qAbs(ev.startMs - t);
        if (d1 < bestDiff) {
            bestDiff = d1;
            best = ev.startMs;
        }
        const qint64 d2 = qAbs(ev.endMs - t);
        if (d2 < bestDiff) {
            bestDiff = d2;
            best = ev.endMs;
        }
    }
    return best;
}

void TimelineWidget::commitSelection(qint64 a, qint64 b, bool append)
{
    if (b - a < 1000) // 不足 1s 视为单击
        return;
    if (!append)
        m_selection.clear();
    m_selection.append({a, b});
    update();
    emit selectionChanged(m_selection);
}

void TimelineWidget::paintSelection(QPainter &p)
{
    QList<QPair<qint64, qint64>> ranges = m_selection;
    if (m_selecting && m_selCurrentMs != m_selAnchorMs)
        ranges.append({qMin(m_selAnchorMs, m_selCurrentMs), qMax(m_selAnchorMs, m_selCurrentMs)});
    if (ranges.isEmpty())
        return;

    const int top = timelineTop();
    for (int i = 0; i < m_lanes.size(); ++i) {
        const int y = top + i * m_laneH;
        const QRect laneRect(timelineX(), y, timelineWidth(), m_laneH);
        for (const auto &r : ranges) {
            const qint64 a = r.first;
            const qint64 b = r.second;
            if (b <= m_viewStartMs || a >= m_viewEndMs)
                continue;
            const int x0 = qMax(laneRect.left(), static_cast<int>(timeToX(a)));
            const int x1 = qMin(laneRect.right(), static_cast<int>(timeToX(b)));
            if (x1 <= x0)
                continue;
            const QRect sel(x0, laneRect.y() + 1, x1 - x0, laneRect.height() - 2);
            p.fillRect(sel, QColor(76, 139, 245, 60));
            p.setPen(QPen(QColor(255, 255, 255, 180), 1));
            p.drawRect(sel);
        }
    }
}

void TimelineWidget::setTimeRange(qint64 startMs, qint64 endMs)
{
    m_viewStartMs = startMs;
    m_viewEndMs = endMs;
    m_defaultStartMs = startMs;
    m_defaultEndMs = endMs;
    update();
    emit timeRangeChanged(m_viewStartMs, m_viewEndMs);
}

void TimelineWidget::resetView()
{
    m_viewStartMs = m_defaultStartMs;
    m_viewEndMs = m_defaultEndMs;
    update();
    emit timeRangeChanged(m_viewStartMs, m_viewEndMs);
}

QSize TimelineWidget::sizeHint() const
{
    return {900, m_axisH + qMax(2, static_cast<int>(m_lanes.size())) * m_laneH + 4};
}

QSize TimelineWidget::minimumSizeHint() const
{
    return {400, 160};
}

// ── 坐标转换 ────────────────────────────────────────────────
double TimelineWidget::timeToX(qint64 t) const
{
    const qint64 range = m_viewEndMs - m_viewStartMs;
    if (range <= 0) return timelineX();
    return timelineX() + (t - m_viewStartMs) / double(range) * timelineWidth();
}

qint64 TimelineWidget::xToTime(int x) const
{
    const qint64 range = m_viewEndMs - m_viewStartMs;
    const double ratio = (x - timelineX()) / double(timelineWidth());
    return m_viewStartMs + static_cast<qint64>(ratio * range);
}

// ── 刻度间隔选择 ────────────────────────────────────────────
qint64 TimelineWidget::chooseTickInterval(qint64 rangeMs) const
{
    static const qint64 options[] = {
        60000LL,       // 1 min
        300000LL,      // 5 min
        600000LL,      // 10 min
        900000LL,      // 15 min
        1800000LL,     // 30 min
        3600000LL,     // 1 h
        7200000LL,     // 2 h
        10800000LL,    // 3 h
        21600000LL,    // 6 h
        43200000LL,    // 12 h
        86400000LL,    // 24 h
    };
    for (qint64 opt : options) {
        if (rangeMs / opt <= 14) return opt;
    }
    return 86400000LL;
}

// ── 绘制：时间轴 ────────────────────────────────────────────
void TimelineWidget::paintAxis(QPainter &p)
{
    const int tx = timelineX();
    const int tw = timelineWidth();
    const int axisH = m_axisH;

    // 背景
    p.fillRect(QRect(0, 0, width(), axisH), QColor(QStringLiteral("#22262c")));
    p.setPen(QPen(QColor(QStringLiteral("#343a44")), 1));
    p.drawLine(0, axisH - 1, width(), axisH - 1);
    p.drawLine(tx - 1, 0, tx - 1, axisH);

    const qint64 range = m_viewEndMs - m_viewStartMs;
    const qint64 interval = chooseTickInterval(range);

    // 对齐到刻度起点
    qint64 firstTick = (m_viewStartMs / interval) * interval;
    if (firstTick < m_viewStartMs) firstTick += interval;

    p.setPen(QColor(QStringLiteral("#9aa4b0")));
    QFont f = p.font();
    f.setPointSize(8);
    p.setFont(f);
    QFontMetrics fm(f);

    const bool showDate = interval >= 86400000LL;
    const bool showSeconds = interval < 60000LL;

    for (qint64 t = firstTick; t <= m_viewEndMs; t += interval) {
        const int x = static_cast<int>(timeToX(t));
        if (x < tx - 1 || x > tx + tw + 1) continue;

        // 刻度线
        p.setPen(QPen(QColor(QStringLiteral("#3a414b")), 1));
        p.drawLine(x, axisH - 7, x, axisH - 1);

        // 标签
        const QDateTime dt = QDateTime::fromMSecsSinceEpoch(t, Qt::LocalTime);
        QString label;
        if (showDate)
            label = dt.toString(QStringLiteral("MM-dd"));
        else if (showSeconds)
            label = dt.toString(QStringLiteral("HH:mm:ss"));
        else
            label = dt.toString(QStringLiteral("HH:mm"));

        p.setPen(QColor(QStringLiteral("#9aa4b0")));
        const int textW = fm.horizontalAdvance(label);
        p.drawText(QRect(x - textW / 2, 4, textW + 4, axisH - 10),
                   Qt::AlignCenter, label);
    }
}

// ── 绘制：lane 背景 + 名称 ──────────────────────────────────
void TimelineWidget::paintLanes(QPainter &p)
{
    const int tx = timelineX();
    const int tw = timelineWidth();
    const int top = timelineTop();

    for (int i = 0; i < m_lanes.size(); ++i) {
        const int y = top + i * m_laneH;
        const QRect laneRect(tx, y, tw, m_laneH);

        // 交替背景
        const QColor bg = (i % 2 == 0) ? QColor(QStringLiteral("#1e2228"))
                                         : QColor(QStringLiteral("#22262c"));
        p.fillRect(laneRect, bg);

        // lane 名称区域
        p.fillRect(QRect(0, y, m_laneNameW, m_laneH), QColor(QStringLiteral("#22262c")));
        p.setPen(QColor(QStringLiteral("#c8cdd4")));
        QFont f = p.font();
        f.setPointSize(9);
        f.setBold(true);
        p.setFont(f);
        QFontMetrics fm(f);
        const QString name = fm.elidedText(m_lanes[i].name, Qt::ElideRight, m_laneNameW - 12);
        p.drawText(QRect(8, y, m_laneNameW - 12, m_laneH),
                   Qt::AlignLeft | Qt::AlignVCenter, name);

        // 分隔线
        p.setPen(QPen(QColor(QStringLiteral("#343a44")), 1));
        p.drawLine(0, y + m_laneH - 1, width(), y + m_laneH - 1);

        // 事件
        paintEvents(p, i, laneRect);
    }

    // lane 名称区右边框
    p.setPen(QPen(QColor(QStringLiteral("#343a44")), 1));
    p.drawLine(tx - 1, top, tx - 1, top + m_lanes.size() * m_laneH);
}

// ── 绘制：事件矩形 ──────────────────────────────────────────
void TimelineWidget::paintEvents(QPainter &p, int laneIndex, const QRect &laneRect)
{
    const TimelineLane &lane = m_lanes[laneIndex];
    const int eventY = laneRect.y() + 5;
    const int eventH = m_laneH - 10;

    QFont f = p.font();
    f.setPointSize(8);
    p.setFont(f);
    QFontMetrics fm(f);

    for (int ei = 0; ei < lane.events.size(); ++ei) {
        const TimelineEvent &ev = lane.events[ei];
        if (ev.endMs <= m_viewStartMs || ev.startMs >= m_viewEndMs) continue;

        const double x0 = timeToX(ev.startMs);
        const double x1 = timeToX(ev.endMs);
        const int ex = qMax(laneRect.left(), static_cast<int>(x0));
        const int ew = qMin(laneRect.right(), static_cast<int>(x1)) - ex + 1;
        if (ew < 1) continue;

        // 事件矩形
        QPainterPath path;
        const qreal radius = qMin(3.0, ew / 4.0);
        path.addRoundedRect(QRectF(ex, eventY, ew, eventH), radius, radius);
        p.fillPath(path, ev.color);

        // hover 高亮边框
        if (m_hover.laneIndex == laneIndex && m_hover.eventIndex == ei) {
            p.setPen(QPen(QColor(255, 255, 255, 200), 1.5));
            p.drawPath(path);
        }

        // 事件文字（宽度足够时）
        if (ew > 40) {
            p.setPen(QColor(255, 255, 255, 220));
            const QString text = fm.elidedText(ev.label, Qt::ElideRight, ew - 8);
            p.drawText(QRect(ex + 4, eventY, ew - 8, eventH),
                       Qt::AlignLeft | Qt::AlignVCenter, text);
        }
    }
}

// ── paintEvent ───────────────────────────────────────────────
void TimelineWidget::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, true);

    // 整体背景
    p.fillRect(rect(), QColor(QStringLiteral("#1a1d21")));

    paintAxis(p);
    paintLanes(p);
    paintSelection(p);

    // 如果没有 lanes
    if (m_lanes.isEmpty()) {
        p.setPen(QColor(QStringLiteral("#9aa4b0")));
        p.drawText(rect(), Qt::AlignCenter, QStringLiteral("暂无时间线数据"));
    }

    // 拖拽时光标提示已通过 setCursor 处理
}

// ── 交互：鼠标按下 ──────────────────────────────────────────
void TimelineWidget::mousePressEvent(QMouseEvent *event)
{
    if (event->button() != Qt::LeftButton || event->pos().x() < timelineX())
        return;

    if (m_selectMode) {
        m_selecting = true;
        const int lane = laneAtY(event->pos().y());
        m_selAnchorMs = snapToBoundary(lane, xToTime(event->pos().x()));
        m_selCurrentMs = m_selAnchorMs;
        if (!(event->modifiers() & Qt::ControlModifier))
            m_selection.clear();
        setCursor(Qt::CrossCursor);
        update();
        return;
    }

    m_dragging = true;
    m_dragLastX = event->pos().x();
    setCursor(Qt::ClosedHandCursor);
}

void TimelineWidget::mouseMoveEvent(QMouseEvent *event)
{
    if (m_selecting) {
        const int lane = laneAtY(event->pos().y());
        m_selCurrentMs = snapToBoundary(lane, xToTime(event->pos().x()));
        update();
        return;
    }

    if (m_dragging) {
        const int dx = event->pos().x() - m_dragLastX;
        const qint64 range = m_viewEndMs - m_viewStartMs;
        const qint64 deltaMs = static_cast<qint64>(-dx / double(timelineWidth()) * range);
        m_viewStartMs += deltaMs;
        m_viewEndMs += deltaMs;
        m_dragLastX = event->pos().x();
        update();
        emit timeRangeChanged(m_viewStartMs, m_viewEndMs);
        return;
    }

    // hover 检测
    HoverInfo newHover;
    const int mx = event->pos().x();
    const int my = event->pos().y();
    const int top = timelineTop();

    if (mx >= timelineX() && my >= top) {
        const int laneIdx = (my - top) / m_laneH;
        if (laneIdx >= 0 && laneIdx < m_lanes.size()) {
            const qint64 t = xToTime(mx);
            const TimelineLane &lane = m_lanes[laneIdx];
            for (int ei = 0; ei < lane.events.size(); ++ei) {
                if (t >= lane.events[ei].startMs && t < lane.events[ei].endMs) {
                    newHover.laneIndex = laneIdx;
                    newHover.eventIndex = ei;
                    break;
                }
            }
        }
    }

    if (newHover.laneIndex != m_hover.laneIndex || newHover.eventIndex != m_hover.eventIndex) {
        m_hover = newHover;
        update();
    }

    if (m_hover.laneIndex >= 0 && m_hover.eventIndex >= 0) {
        const TimelineEvent &ev = m_lanes[m_hover.laneIndex].events[m_hover.eventIndex];
        const QDateTime sdt = QDateTime::fromMSecsSinceEpoch(ev.startMs, Qt::LocalTime);
        const QDateTime edt = QDateTime::fromMSecsSinceEpoch(ev.endMs, Qt::LocalTime);
        const qint64 durSec = (ev.endMs - ev.startMs) / 1000;
        QString tip = QStringLiteral("<b>%1</b><br>%2 — %3<br>时长：%4")
                          .arg(ev.label.toHtmlEscaped(),
                               sdt.toString(QStringLiteral("HH:mm:ss")),
                               edt.toString(QStringLiteral("HH:mm:ss")),
                               formatDuration(durSec));
        if (!ev.detail.isEmpty() && ev.detail != ev.label)
            tip += QStringLiteral("<br>%1").arg(ev.detail.toHtmlEscaped());
        if (!ev.category.isEmpty())
            tip += QStringLiteral("<br><span style='color:#9aa4b0'>分类：%1</span>").arg(ev.category);
        QToolTip::showText(event->globalPos(), tip, this);
    } else {
        QToolTip::hideText();
    }

    // 光标
    if (event->pos().x() >= timelineX())
        setCursor(Qt::OpenHandCursor);
    else
        setCursor(Qt::ArrowCursor);
}

void TimelineWidget::mouseReleaseEvent(QMouseEvent *event)
{
    if (event->button() != Qt::LeftButton)
        return;

    if (m_selecting) {
        m_selecting = false;
        const int lane = laneAtY(event->pos().y());
        m_selCurrentMs = snapToBoundary(lane, xToTime(event->pos().x()));
        const qint64 a = qMin(m_selAnchorMs, m_selCurrentMs);
        const qint64 b = qMax(m_selAnchorMs, m_selCurrentMs);
        if (b - a >= 1000)
            commitSelection(a, b, event->modifiers() & Qt::ControlModifier);
        else
            update();
        setCursor(Qt::ArrowCursor);
        return;
    }

    if (m_dragging) {
        m_dragging = false;
        setCursor(Qt::OpenHandCursor);
    }
}

void TimelineWidget::mouseDoubleClickEvent(QMouseEvent *event)
{
    if (event->button() != Qt::LeftButton || event->pos().x() < timelineX())
        return;
    const int laneIdx = laneAtY(event->pos().y());
    if (laneIdx < 0 || laneIdx >= m_lanes.size())
        return;
    const qint64 t = xToTime(event->pos().x());
    const TimelineLane &lane = m_lanes[laneIdx];
    for (const auto &ev : lane.events) {
        if (t >= ev.startMs && t < ev.endMs) {
            commitSelection(ev.startMs, ev.endMs, event->modifiers() & Qt::ControlModifier);
            return;
        }
    }
}

void TimelineWidget::wheelEvent(QWheelEvent *event)
{
    const int delta = event->angleDelta().y();
    if (delta == 0) return;

    const int mx = event->position().x();
    const qint64 pivotTime = xToTime(mx);
    const qint64 range = m_viewEndMs - m_viewStartMs;

    // 缩放因子：向上滚（delta>0）= 放大 = 范围变小
    const double factor = std::exp(-delta * 0.0015);
    qint64 newRange = static_cast<qint64>(range * factor);

    // 限制范围
    const qint64 minRange = 5 * 60 * 1000LL;    // 5 分钟
    const qint64 maxRange = 7 * 86400000LL;      // 7 天
    newRange = qBound(minRange, newRange, maxRange);

    // 保持 pivot 位置不变
    const double pivotRatio = (pivotTime - m_viewStartMs) / double(range);
    m_viewStartMs = pivotTime - static_cast<qint64>(pivotRatio * newRange);
    m_viewEndMs = m_viewStartMs + newRange;

    update();
    emit timeRangeChanged(m_viewStartMs, m_viewEndMs);
    event->accept();
}

void TimelineWidget::leaveEvent(QEvent *)
{
    if (m_hover.laneIndex >= 0) {
        m_hover = HoverInfo();
        update();
    }
    if (!m_dragging) setCursor(Qt::ArrowCursor);
}

} // namespace awqtui
