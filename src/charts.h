// charts.h —— 自绘图表控件：横向条形图 + 24 小时活跃柱状图
#pragma once

#include <QWidget>
#include "mockdata.h"

namespace awqtui {

// ── 横向条形图（Top Applications / Top Window Titles / Top Categories）──
class HorizontalBarChart : public QWidget
{
    Q_OBJECT
public:
    explicit HorizontalBarChart(const QString &title = QString(), QWidget *parent = nullptr);

    void setItems(const QList<BarItem> &items);
    void setTitle(const QString &title);
    // label 列宽度（默认 150）
    void setLabelWidth(int w) { m_labelWidth = w; }

    QSize sizeHint() const override;
    QSize minimumSizeHint() const override;

protected:
    void paintEvent(QPaintEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void leaveEvent(QEvent *event) override;

private:
    QString m_title;
    QList<BarItem> m_items;
    int m_labelWidth = 150;
    int m_hoverIndex = -1;
};

// ── 24 小时活跃柱状图（Activity 页顶部） ───────────────────
class HourlyActivityBars : public QWidget
{
    Q_OBJECT
public:
    explicit HourlyActivityBars(QWidget *parent = nullptr);

    void setData(const QList<qint64> &secondsPerHour); // size 24

    QSize sizeHint() const override { return {800, 120}; }
    QSize minimumSizeHint() const override { return {400, 90}; }

protected:
    void paintEvent(QPaintEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void leaveEvent(QEvent *event) override;

private:
    QList<qint64> m_data;
    qint64 m_maxVal = 1;
    int m_hoverHour = -1;
};

// ── 分类彩色时间柱（Activity 页 Timeline Barchart） ────────
// 24 个柱子，颜色 = 该小时最主要分类，高度 = 活跃时长
class CategoryBars : public QWidget
{
    Q_OBJECT
public:
    explicit CategoryBars(QWidget *parent = nullptr);
    // hourlyCategories: size 24，每个小时的主分类名
    void setData(const QList<qint64> &secondsPerHour, const QStringList &hourlyCategories);
    QSize sizeHint() const override { return {800, 140}; }
    QSize minimumSizeHint() const override { return {400, 100}; }
protected:
    void paintEvent(QPaintEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void leaveEvent(QEvent *event) override;
private:
    QList<qint64> m_data;
    QStringList m_cats;
    qint64 m_maxVal = 1;
    int m_hoverHour = -1;
};

// ── 环形分类图（Category Sunburst 简化版） ─────────────────
class DonutChart : public QWidget
{
    Q_OBJECT
public:
    explicit DonutChart(QWidget *parent = nullptr);
    void setItems(const QList<BarItem> &items);
    QSize sizeHint() const override { return {260, 260}; }
    QSize minimumSizeHint() const override { return {180, 180}; }
protected:
    void paintEvent(QPaintEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void leaveEvent(QEvent *event) override;
private:
    QList<BarItem> m_items;
    qint64 m_total = 1;
    int m_hoverIndex = -1;
};

// ── 时长格式化 ──────────────────────────────────────────────
QString formatDuration(qint64 totalSeconds);

} // namespace awqtui
