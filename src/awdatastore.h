// awdatastore.h —— ActivityWatch /api/0 数据转换层
// 把 aw-server 返回的 JSON buckets/events 转换成页面用的 TimelineLane / TimelineEvent / BarItem
#pragma once

#include "mockdata.h"

#include <QDate>
#include <QHash>
#include <QJsonArray>
#include <QList>
#include <QString>

namespace awqtui {

struct BucketInfo {
    QString id;
    QString type;      // currentwindow / afkstatus / web.tab ...
    QString client;    // aw-watcher-window / aw-watcher-afk / aw-watcher-web ...
    QString hostname;
};

// 解析 GET /api/0/buckets 返回的 JSON 对象（key=bucket_id, value=bucket_info）
QList<BucketInfo> parseBuckets(const QJsonObject &bucketsObj);

// 解析 GET /api/0/buckets/<id>/events 返回的 JSON 数组，转换成 TimelineEvent
QList<TimelineEvent> parseEvents(const QJsonArray &events, const BucketInfo &bucket);

// 把多 bucket 的 events 组装成 TimelineLane 列表
// eventsMap: bucketId -> events JSON array
QList<TimelineLane> buildLanes(const QList<BucketInfo> &buckets,
                                const QHash<QString, QJsonArray> &eventsMap);

// ── 从 lanes 计算统计（替代 mockdata 里的 generate* 函数） ──
QList<BarItem> topAppsFromLanes(const QList<TimelineLane> &lanes, int limit);
QList<BarItem> topTitlesFromLanes(const QList<TimelineLane> &lanes, int limit);
QList<BarItem> topCategoriesFromLanes(const QList<TimelineLane> &lanes, int limit);
QList<qint64> hourlyFromLanes(const QList<TimelineLane> &lanes);
QList<BarItem> categoryTreeFromLanes(const QList<TimelineLane> &lanes);

// 从 aw-watcher-web lane 里提取 Top 域名 / URL
QList<BarItem> topDomainsFromLanes(const QList<TimelineLane> &lanes, int limit);
QList<BarItem> topUrlsFromLanes(const QList<TimelineLane> &lanes, int limit);

} // namespace awqtui
