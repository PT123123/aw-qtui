// mockdata.h —— ActivityWatch/Tockler 风格页面的数据模型 + 模拟数据生成
#pragma once

#include <QColor>
#include <QDate>
#include <QList>
#include <QString>

namespace awqtui {

// ── 时间线事件 ──────────────────────────────────────────────
struct TimelineEvent {
    qint64 startMs = 0;   // 自 epoch 毫秒
    qint64 endMs = 0;
    QString label;         // 显示名（应用名 / 网址 / afk 状态）
    QColor color;
    QString category;      // 可选：分类名（Work / Play / Neutral ...）
    QString detail;        // hover 详情（窗口标题 / URL）
};

struct TimelineLane {
    QString name;          // lane 名，如 "afk-status"、"aw-watcher-window"
    QList<TimelineEvent> events;
};

// ── 横向条形图项 ────────────────────────────────────────────
struct BarItem {
    QString label;
    qint64 valueSeconds = 0;
    QColor color;
    QString subLabel;      // 可选：第二行文字（如窗口标题、域名）
};

// ── 工具：根据字符串生成稳定柔和颜色 ────────────────────────
QColor colorForString(const QString &s);

// 分类颜色（对齐 ActivityWatch 默认分类）
QColor colorForCategory(const QString &category);

// ── Mock 数据生成 ───────────────────────────────────────────
// 生成指定日期的多行时间线（afk-status / aw-watcher-window / aw-watcher-web）
QList<TimelineLane> generateTimelineLanes(const QDate &date);

// Top Applications（按使用时长降序，取前 N）
QList<BarItem> generateTopApps(const QDate &date, int limit = 8);

// Top Window Titles
QList<BarItem> generateTopTitles(const QDate &date, int limit = 8);

// Top Categories（带分类颜色）
QList<BarItem> generateTopCategories(const QDate &date, int limit = 6);

// 24 小时活跃秒数（index 0 = 00:00 小时段）
QList<qint64> generateHourlyActivity(const QDate &date);

// 分类树（Category Tree 用，扁平 label + 时长）
QList<BarItem> generateCategoryTree(const QDate &date);

} // namespace awqtui
