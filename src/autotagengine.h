// autotagengine.h —— 自动标签计算引擎
#pragma once

#include <QList>
#include <QRegularExpression>
#include <QString>

#include "mockdata.h"
#include "tagstore.h"

namespace awqtui {

// 一条自动标签命中（AutoTags lane 事件）
struct AutoTagHit {
    TimelineEvent event;
    bool guess = false;   // 由间隙填充 / 吸收产生（猜测）
    QString ruleName;     // 产生它的规则名（诊断）
};

// 模板展开：{{title}} {{group}} {{detail}} {{1}}..{{9}}
QString expandAutotagTemplate(const QString &tmpl, const TimelineEvent &ev,
                              const QRegularExpressionMatch &m);
// 规则条件匹配（AND）；m 返回最后一条正则捕获
bool ruleMatches(const AutoTagRule &rule, const TimelineEvent &ev,
                 QRegularExpressionMatch *m = nullptr);

class AutoTagEngine
{
public:
    // 对给定时间范围内活动事件计算自动标签（规则来自 store，含优先级/append/prepend/absorb/间隙填充）
    static QList<AutoTagHit> compute(const TagStore *store, qint64 startMs, qint64 endMs,
                                     const QList<TimelineLane> &activityLanes);
};

} // namespace awqtui
