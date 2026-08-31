// tagstore.h —— 时间标签本地存储（离线优先，原子写，独立于收件箱 inbox_local.json）
#pragma once

#include <QColor>
#include <QList>
#include <QMap>
#include <QString>
#include <QStringList>

namespace awqtui {

// ── 一条时间标签段 ──────────────────────────────────────────
struct TagSegment {
    qint64 id = 0;
    qint64 startMs = 0;
    qint64 endMs = 0;
    QStringList tags;          // 逗号层级拍平为数组，顺序即层级顺序（Client,Project,Activity）
    QString notes;
    bool billable = false;
    QString colorOverride;     // 空 = 按首标签颜色计算
    bool deleted = false;      // tombstone（本地库本期不对外同步，预留）
    QString createdAt;
    QString updatedAt;
};

// ── 单个标签的元信息（Tag editor / Tag picker 用） ─────────
struct TagMeta {
    QString name;
    QString color;             // 空 = colorForString(name) 派生
    bool skipColor = false;    // 颜色计算时跳过该标签
    bool billableDefault = false;
    QString lastUsed;
    qint64 useCount = 0;
    qint64 taggedMs = 0;       // 该标签在全部组合中的累计时长(ms)
};

// ── 自动标签规则（Autotagging） ─────────────────────────────
struct AutoTagCondition {
    QString field;             // title / group / detail / url / domain / note
    QString value;
    bool regex = false;
};

struct AutoTagRule {
    qint64 id = 0;
    QString name;              // 可含 {{title}} {{group}} {{1}} {{2}} {{3}}
    enum Type { Regular = 0, Append = 1, Prepend = 2, Absorb = 3 };
    int type = Regular;
    QString notes;
    bool billable = false;
    bool enabled = true;
    int order = 0;             // 越小优先级越高
    QList<AutoTagCondition> conditions;
};

// ── 时间标签库 ──────────────────────────────────────────────
class TagStore
{
public:
    TagStore();
    ~TagStore();

    // 从磁盘加载；失败视为空库
    bool load();
    // 原子写回磁盘
    void save() const;

    // ── 段 CRUD ──
    QList<TagSegment> segments() const;                            // 未删除
    QList<TagSegment> segmentsInRange(qint64 startMs, qint64 endMs) const;
    const TagSegment *find(qint64 id) const;
    qint64 addSegment(qint64 startMs, qint64 endMs, const QStringList &tags,
                      const QString &notes, bool billable);
    bool updateSegment(qint64 id, qint64 startMs, qint64 endMs, const QStringList &tags,
                       const QString &notes, bool billable);
    bool removeSegment(qint64 id);

    // ── 标签字典 / 组合 ──
    QStringList allTagNames() const;                  // 去重、稳定排序
    QStringList allCombinations() const;              // 按最后使用倒序
    QList<TagMeta> tagMetas() const;                  // 单个标签
    QList<TagMeta> combinationMetas() const;          // 组合级统计
    QList<TagSegment> segmentsOfCombination(const QStringList &tags) const;

    // 区间内已标签总时长（用于未标记时间计算）
    qint64 taggedTimeInRange(qint64 startMs, qint64 endMs) const;

    // ── 颜色模型（首标签色 / Skip / 组合覆盖） ──
    QColor tagColor(const QString &name) const;
    QColor segmentColor(const TagSegment &seg) const;
    void setTagColor(const QString &name, const QString &colorHex);
    void setTagSkip(const QString &name, bool skip);
    void setCombinationColor(const QStringList &tags, const QString &colorHex);
    void clearCombinationColor(const QStringList &tags);

    // ── 重命名 / 删除 ──
    void renameTag(const QString &oldName, const QString &newName);   // 所有组合中的该标签一并改名
    void renameCombination(const QStringList &oldTags, const QStringList &newTags);
    void deleteTag(const QString &name);   // 组合自动降级；只含该标签的段被删除
    void deleteCombination(const QStringList &tags); // 删除该组合的所有段

    // ── 导入 / 导出（.txt，每行一个组合，逗号分隔） ──
    QString exportText() const;
    int importText(const QString &text);   // 部分标签笛卡尔展开；返回写入的字典组合数

    // ── 标签快捷键（key -> 标签组合） ──
    QMap<QString, QString> shortcuts() const { return m_shortcuts; }
    void setShortcut(const QString &key, const QString &combo);
    void removeShortcut(const QString &key);
    QString shortcutTag(const QString &key) const;

    // ── 全局设置 ──
    bool newTagsBillableByDefault() const { return m_billableDefault; }
    void setNewTagsBillableByDefault(bool on);

    // ── 自动标签规则与设置 ──
    QList<AutoTagRule> autotagRules() const { return m_autotagRules; }
    void setAutotagRules(const QList<AutoTagRule> &rules);
    qint64 nextRuleId() const;
    int autotagGapFillSec() const { return m_gapFillSec; }
    void setAutotagGapFillSec(int s);
    bool autotagSkipRest() const { return m_skipRest; }
    void setAutotagSkipRest(bool b);
    bool autotagHighlightGuesses() const { return m_highlightGuesses; }
    void setAutotagHighlightGuesses(bool b);
    bool autotagAppendRuleData() const { return m_appendRuleData; }
    void setAutotagAppendRuleData(bool b);

    // 便捷：是否为空
    bool isEmpty() const { return m_segments.isEmpty(); }

private:
    void touch();          // 置脏并刷新统计
    void rebuildStats();
    QString nextId() const;

    QList<TagSegment> m_segments;
    QMap<QString, TagMeta> m_tags;        // 单标签字典
    QMap<QString, QString> m_comboColors; // 组合键("a,b,c") -> 颜色 hex
    QMap<QString, QString> m_shortcuts;   // 快捷键 key -> 标签组合
    bool m_billableDefault = false;
    mutable bool m_dirty = false;
    QList<AutoTagRule> m_autotagRules;
    int m_gapFillSec = 60;
    bool m_skipRest = true;
    bool m_highlightGuesses = true;
    bool m_appendRuleData = false;

    static QString filePath();
};

} // namespace awqtui
