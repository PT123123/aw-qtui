// autotagengine.cpp
#include "autotagengine.h"

#include <QSet>
#include <QVector>
#include <algorithm>

namespace awqtui {

QString expandAutotagTemplate(const QString &tmpl, const TimelineEvent &ev,
                              const QRegularExpressionMatch &m)
{
    QString out = tmpl;
    out.replace(QStringLiteral("{{title}}"), ev.label);
    out.replace(QStringLiteral("{{group}}"), ev.label);
    out.replace(QStringLiteral("{{detail}}"), ev.detail);
    for (int i = 1; i <= 9; ++i) {
        const QString token = QStringLiteral("{{%1}}").arg(i);
        if (out.contains(token))
            out.replace(token, m.hasMatch() && m.lastCapturedIndex() >= i
                                   ? m.captured(i)
                                   : QString());
    }
    return out.trimmed();
}

bool ruleMatches(const AutoTagRule &rule, const TimelineEvent &ev, QRegularExpressionMatch *m)
{
    if (rule.conditions.isEmpty())
        return true;
    for (const auto &c : rule.conditions) {
        QString target;
        if (c.field == QLatin1String("title") || c.field == QLatin1String("group"))
            target = ev.label;
        else if (c.field == QLatin1String("detail"))
            target = ev.detail;
        else if (c.field == QLatin1String("url"))
            target = ev.label;
        else if (c.field == QLatin1String("domain"))
            target = ev.detail;
        else
            return false; // 未知/不支持的字段
        if (c.regex) {
            const QRegularExpression re(c.value, QRegularExpression::CaseInsensitiveOption);
            if (!re.isValid())
                continue;
            const QRegularExpressionMatch mm = re.match(target);
            if (!mm.hasMatch())
                return false;
            if (m)
                *m = mm;
        } else {
            if (!target.contains(c.value, Qt::CaseInsensitive))
                return false;
        }
    }
    return true;
}

QList<AutoTagHit> AutoTagEngine::compute(const TagStore *store, qint64 startMs, qint64 endMs,
                                         const QList<TimelineLane> &activityLanes)
{
    QList<AutoTagHit> out;
    if (!store)
        return out;

    QList<AutoTagRule> rules = store->autotagRules();
    if (rules.isEmpty())
        return out;
    std::sort(rules.begin(), rules.end(),
              [](const AutoTagRule &a, const AutoTagRule &b) {
                  if (a.order != b.order)
                      return a.order < b.order;
                  return a.id < b.id;
              });

    // 收集活动事件（window / web）
    QList<TimelineEvent> events;
    for (const auto &lane : activityLanes) {
        if (!lane.name.contains(QStringLiteral("window")) && !lane.name.contains(QStringLiteral("web")))
            continue;
        for (const auto &ev : lane.events) {
            if (ev.endMs <= startMs || ev.startMs >= endMs)
                continue;
            events.append(ev);
        }
    }
    if (events.isEmpty())
        return out;

    const bool skipRest = store->autotagSkipRest();

    // ── 第 1 步：Regular 命中（含 skip-rest） ──
    // hit: {ev, rule, match}
    struct RawHit {
        const TimelineEvent *ev = nullptr;
        const AutoTagRule *rule = nullptr;
        QRegularExpressionMatch match;
        QString label;
    };
    QList<RawHit> regular;

    // 每个事件已命中的规则集合（skip-rest）
    QVector<QSet<qint64>> hitRules(events.size());
    for (const auto &rule : rules) {
        if (!rule.enabled)
            continue;
        for (int i = 0; i < events.size(); ++i) {
            const TimelineEvent &ev = events[i];
            if (skipRest && !hitRules[i].isEmpty())
                continue;
            QRegularExpressionMatch m;
            if (!ruleMatches(rule, ev, &m))
                continue;
            if (skipRest) {
                hitRules[i].insert(rule.id);
            }
            RawHit h;
            h.ev = &ev;
            h.rule = &rule;
            h.match = m;
            h.label = expandAutotagTemplate(rule.name, ev, m);
            if (rule.type == AutoTagRule::Regular)
                regular.append(h);
            else if (rule.type == AutoTagRule::Append || rule.type == AutoTagRule::Prepend) {
                // 需要在 regular 命中上追加/前置，第 3 步处理（先记录到 extraHits）
                continue;
            }
        }
    }

    // 转成可变的 regular 命中段（用于吸收/填充/append 合并）
    struct Seg {
        qint64 startMs = 0;
        qint64 endMs = 0;
        QString label;
        bool guess = false;
        bool fromAbsorb = false;
        QString ruleName;
        qint64 anchorStart = 0; // 原始（未扩展）边界，用于吸收判断
        qint64 anchorEnd = 0;
    };
    QList<Seg> segs;
    for (const auto &h : regular) {
        Seg s;
        s.startMs = h.ev->startMs;
        s.endMs = h.ev->endMs;
        s.label = h.label;
        s.ruleName = h.rule->name;
        s.anchorStart = s.startMs;
        s.anchorEnd = s.endMs;
        segs.append(s);
    }

    // ── 第 2 步：Append / Prepend 合并（不再单独出现） ──
    for (const auto &rule : rules) {
        if (!rule.enabled || (rule.type != AutoTagRule::Append && rule.type != AutoTagRule::Prepend))
            continue;
        for (const auto &ev : events) {
            QRegularExpressionMatch m;
            if (!ruleMatches(rule, ev, &m))
                continue;
            // 找时间重叠的 regular 段
            for (auto &s : segs) {
                if (s.startMs < ev.endMs && s.endMs > ev.startMs) {
                    const QString part = expandAutotagTemplate(rule.name, ev, m);
                    if (rule.type == AutoTagRule::Append)
                        s.label = s.label + QStringLiteral(", ") + part;
                    else
                        s.label = part + QStringLiteral(", ") + s.label;
                }
            }
        }
    }

    const int gapFillSec = store->autotagGapFillSec();

    // ── 第 3 步：Absorb（被周围 autotag 吸收） ──
    for (const auto &rule : rules) {
        if (!rule.enabled || rule.type != AutoTagRule::Absorb)
            continue;
        for (const auto &ev : events) {
            QRegularExpressionMatch m;
            if (!ruleMatches(rule, ev, &m))
                continue;
            // 找左右紧邻的 regular 段（gap 小）
            Seg *best = nullptr;
            qint64 bestGap = qint64(gapFillSec) * 2000 + 1;
            for (auto &s : segs) {
                if (s.guess)
                    continue;
                qint64 gap = -1;
                if (s.anchorEnd <= ev.startMs)
                    gap = ev.startMs - s.anchorEnd;
                else if (s.anchorStart >= ev.endMs)
                    gap = s.anchorStart - ev.endMs;
                if (gap >= 0 && gap < bestGap) {
                    bestGap = gap;
                    best = &s;
                }
            }
            if (best) {
                best->startMs = qMin(best->startMs, ev.startMs);
                best->endMs = qMax(best->endMs, ev.endMs);
                best->guess = true;
                best->fromAbsorb = true;
            }
        }
    }

    // ── 第 4 步：间隙填充 ──
    std::sort(segs.begin(), segs.end(),
              [](const Seg &a, const Seg &b) { return a.startMs < b.startMs; });
    QList<Seg> merged;
    for (auto &s : segs) {
        if (!merged.isEmpty() && s.startMs <= merged.last().endMs) {
            // 重叠合并（同 label 扩展；异 label 保留后者起始）
            auto &last = merged.last();
            if (last.label == s.label) {
                last.endMs = qMax(last.endMs, s.endMs);
            } else if (s.startMs < last.endMs) {
                last.endMs = s.startMs;
                merged.append(s);
            } else {
                merged.append(s);
            }
        } else {
            merged.append(s);
        }
    }
    if (store->autotagGapFillSec() > 0) {
        for (int i = 0; i + 1 < merged.size(); ++i) {
            auto &cur = merged[i];
            const auto &nxt = merged[i + 1];
            const qint64 gap = nxt.startMs - cur.endMs;
            if (gap <= 0 || gap > qint64(gapFillSec) * 1000)
                continue;
            if (cur.label == nxt.label) {
                cur.endMs = nxt.startMs;
                cur.guess = true;
            } else {
                const qint64 half = gap / 2;
                cur.endMs = cur.endMs + half;
                merged[i + 1].startMs = merged[i + 1].startMs - (gap - half);
                // 对半填充不新增段；扩展左右边界即视为填充
                merged[i].guess = true;
                merged[i + 1].guess = true;
            }
        }
    }

    // ── 输出 ──
    for (auto &s : merged) {
        AutoTagHit h;
        h.event.startMs = s.startMs;
        h.event.endMs = s.endMs;
        h.event.label = s.label;
        h.event.color = colorForString(s.label);
        h.event.detail = QStringLiteral("autotag: %1%2%3")
                             .arg(s.ruleName)
                             .arg(s.guess ? (s.fromAbsorb ? QStringLiteral(" · 吸收")
                                                          : QStringLiteral(" · 填充"))
                                          : QString())
                             .arg(store->autotagHighlightGuesses() && s.guess
                                      ? QStringLiteral("（猜测）")
                                      : QString());
        h.guess = s.guess;
        h.ruleName = s.ruleName;
        out.append(h);
    }
    return out;
}

} // namespace awqtui
