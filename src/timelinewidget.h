// timelinewidget.h —— 可交互多行时间线（ActivityWatch Timeline / Tockler 风格）
#pragma once

#include <QList>
#include <QPair>
#include <QWidget>
#include "mockdata.h"

namespace awqtui {

class TimelineWidget : public QWidget
{
    Q_OBJECT
public:
    explicit TimelineWidget(QWidget *parent = nullptr);

    void setLanes(const QList<TimelineLane> &lanes);
    void setTimeRange(qint64 startMs, qint64 endMs);
    void resetView(); // 回到默认当天视图

    // ── 选择模式（Day 视图用） ──
    void setSelectMode(bool on);
    bool selectMode() const { return m_selectMode; }
    // 外部联动（如明细/汇总勾选）：只重绘，不重发 selectionChanged（避免回环）
    void setSelection(const QList<QPair<qint64, qint64>> &ranges);
    QList<QPair<qint64, qint64>> selection() const { return m_selection; }
    void clearSelection();

    // 外观
    void setLaneNameWidth(int w) { m_laneNameW = w; }
    void setLaneHeight(int h) { m_laneH = h; updateGeometry(); }

    QSize sizeHint() const override;
    QSize minimumSizeHint() const override;

signals:
    void timeRangeChanged(qint64 startMs, qint64 endMs);
    void selectionChanged(const QList<QPair<qint64, qint64>> &ranges);

protected:
    void paintEvent(QPaintEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    void mouseDoubleClickEvent(QMouseEvent *event) override;
    void wheelEvent(QWheelEvent *event) override;
    void leaveEvent(QEvent *event) override;

private:
    struct HoverInfo {
        int laneIndex = -1;
        int eventIndex = -1;
    };

    // 坐标转换
    int timelineX() const { return m_laneNameW; }
    int timelineWidth() const { return qMax(10, width() - m_laneNameW); }
    int timelineTop() const { return m_axisH; }
    int timelineHeight() const { return qMax(10, height() - m_axisH); }
    double timeToX(qint64 t) const;
    qint64 xToTime(int x) const;

    // 绘制
    void paintAxis(QPainter &p);
    void paintLanes(QPainter &p);
    void paintEvents(QPainter &p, int laneIndex, const QRect &laneRect);

    // 刻度选择
    qint64 chooseTickInterval(qint64 rangeMs) const;

    // 选择辅助
    qint64 snapToBoundary(int laneIndex, qint64 t) const;
    int laneAtY(int y) const;
    void commitSelection(qint64 a, qint64 b, bool append);
    void paintSelection(QPainter &p);

    QList<TimelineLane> m_lanes;
    qint64 m_viewStartMs = 0;
    qint64 m_viewEndMs = 0;
    qint64 m_defaultStartMs = 0;
    qint64 m_defaultEndMs = 0;

    int m_laneNameW = 200;
    int m_laneH = 40;
    int m_axisH = 32;

    // 交互状态
    bool m_dragging = false;
    int m_dragLastX = 0;
    HoverInfo m_hover;

    // 选择状态
    bool m_selectMode = false;
    bool m_selecting = false;
    qint64 m_selAnchorMs = 0;  // 拖拽起点（吸附后）
    qint64 m_selCurrentMs = 0; // 拖拽当前点（吸附后）
    QList<QPair<qint64, qint64>> m_selection;
};

} // namespace awqtui
