// tagstore.cpp —— 时间标签本地存储实现
#include "tagstore.h"

#include "mockdata.h" // colorForString

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>
#include <QSet>
#include <QStandardPaths>

namespace awqtui {

static QString nowIso()
{
    return QDateTime::currentDateTime().toString(Qt::ISODateWithMs);
}

static QString comboKey(const QStringList &tags)
{
    return tags.join(QLatin1Char(','));
}

TagStore::TagStore() = default;
TagStore::~TagStore() = default;

QString TagStore::filePath()
{
    const QString dir = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    if (!dir.isEmpty())
        QDir().mkpath(dir);
    return dir + QStringLiteral("/timetags_local.json");
}

bool TagStore::load()
{
    m_segments.clear();
    m_tags.clear();
    m_comboColors.clear();

    QFile f(filePath());
    if (!f.open(QIODevice::ReadOnly))
        return false;
    const QJsonDocument doc = QJsonDocument::fromJson(f.readAll());
    f.close();
    if (!doc.isObject())
        return false;

    const QJsonObject root = doc.object();
    const QJsonArray segArr = root.value(QLatin1String("segments")).toArray();
    for (const auto &v : segArr) {
        const QJsonObject o = v.toObject();
        TagSegment s;
        s.id = o.value(QLatin1String("id")).toVariant().toLongLong();
        s.startMs = o.value(QLatin1String("start_ms")).toVariant().toLongLong();
        s.endMs = o.value(QLatin1String("end_ms")).toVariant().toLongLong();
        const QJsonArray t = o.value(QLatin1String("tags")).toArray();
        for (const auto &tv : t)
            s.tags << tv.toString();
        s.notes = o.value(QLatin1String("notes")).toString();
        s.billable = o.value(QLatin1String("billable")).toBool();
        s.colorOverride = o.value(QLatin1String("color_override")).toString();
        s.deleted = o.value(QLatin1String("deleted")).toBool();
        s.createdAt = o.value(QLatin1String("created_at")).toString();
        s.updatedAt = o.value(QLatin1String("updated_at")).toString();
        m_segments.append(s);
    }
    const QJsonObject tagObj = root.value(QLatin1String("tags")).toObject();
    for (auto it = tagObj.constBegin(); it != tagObj.constEnd(); ++it) {
        const QJsonObject o = it.value().toObject();
        TagMeta m;
        m.name = it.key();
        m.color = o.value(QLatin1String("color")).toString();
        m.skipColor = o.value(QLatin1String("skip")).toBool();
        m.billableDefault = o.value(QLatin1String("billable_default")).toBool();
        m.lastUsed = o.value(QLatin1String("last_used")).toString();
        m.useCount = o.value(QLatin1String("use_count")).toVariant().toLongLong();
        m.taggedMs = o.value(QLatin1String("tagged_ms")).toVariant().toLongLong();
        m_tags.insert(m.name, m);
    }
    const QJsonObject comboObj = root.value(QLatin1String("combo_colors")).toObject();
    for (auto it = comboObj.constBegin(); it != comboObj.constEnd(); ++it)
        m_comboColors.insert(it.key(), it.value().toString());
    const QJsonObject scObj = root.value(QLatin1String("shortcuts")).toObject();
    for (auto it = scObj.constBegin(); it != scObj.constEnd(); ++it)
        m_shortcuts.insert(it.key(), it.value().toString());
    m_billableDefault = root.value(QLatin1String("billable_default")).toBool();
    const QJsonArray autArr = root.value(QLatin1String("autotags")).toArray();
    for (const auto &v : autArr) {
        const QJsonObject o = v.toObject();
        AutoTagRule r;
        r.id = o.value(QLatin1String("id")).toVariant().toLongLong();
        r.name = o.value(QLatin1String("name")).toString();
        r.type = o.value(QLatin1String("type")).toInt(0);
        r.notes = o.value(QLatin1String("notes")).toString();
        r.billable = o.value(QLatin1String("billable")).toBool();
        r.enabled = o.value(QLatin1String("enabled")).toBool(true);
        r.order = o.value(QLatin1String("order")).toInt(0);
        const QJsonArray ca = o.value(QLatin1String("conditions")).toArray();
        for (const auto &cv : ca) {
            const QJsonObject co = cv.toObject();
            AutoTagCondition c;
            c.field = co.value(QLatin1String("field")).toString();
            c.value = co.value(QLatin1String("value")).toString();
            c.regex = co.value(QLatin1String("regex")).toBool();
            r.conditions.append(c);
        }
        m_autotagRules.append(r);
    }
    m_gapFillSec = root.value(QLatin1String("autotag_gap_fill_sec")).toInt(60);
    m_skipRest = root.value(QLatin1String("autotag_skip_rest")).toBool(true);
    m_highlightGuesses = root.value(QLatin1String("autotag_highlight_guesses")).toBool(true);
    m_appendRuleData = root.value(QLatin1String("autotag_append_rule_data")).toBool(false);
    rebuildStats();
    return true;
}

void TagStore::save() const
{
    QJsonArray segArr;
    for (const auto &s : m_segments) {
        QJsonObject o;
        o.insert(QLatin1String("id"), static_cast<double>(s.id));
        o.insert(QLatin1String("start_ms"), static_cast<double>(s.startMs));
        o.insert(QLatin1String("end_ms"), static_cast<double>(s.endMs));
        QJsonArray t;
        for (const auto &tag : s.tags)
            t.append(tag);
        o.insert(QLatin1String("tags"), t);
        o.insert(QLatin1String("notes"), s.notes);
        o.insert(QLatin1String("billable"), s.billable);
        o.insert(QLatin1String("color_override"), s.colorOverride);
        o.insert(QLatin1String("deleted"), s.deleted);
        o.insert(QLatin1String("created_at"), s.createdAt);
        o.insert(QLatin1String("updated_at"), s.updatedAt);
        segArr.append(o);
    }
    QJsonObject tagObj;
    for (auto it = m_tags.constBegin(); it != m_tags.constEnd(); ++it) {
        const TagMeta &m = it.value();
        QJsonObject o;
        o.insert(QLatin1String("color"), m.color);
        o.insert(QLatin1String("skip"), m.skipColor);
        o.insert(QLatin1String("billable_default"), m.billableDefault);
        o.insert(QLatin1String("last_used"), m.lastUsed);
        o.insert(QLatin1String("use_count"), static_cast<double>(m.useCount));
        o.insert(QLatin1String("tagged_ms"), static_cast<double>(m.taggedMs));
        tagObj.insert(it.key(), o);
    }
    QJsonObject comboObj;
    for (auto it = m_comboColors.constBegin(); it != m_comboColors.constEnd(); ++it)
        comboObj.insert(it.key(), it.value());
    QJsonObject scObj;
    for (auto it = m_shortcuts.constBegin(); it != m_shortcuts.constEnd(); ++it)
        scObj.insert(it.key(), it.value());

    QJsonObject root;
    root.insert(QLatin1String("segments"), segArr);
    root.insert(QLatin1String("tags"), tagObj);
    root.insert(QLatin1String("combo_colors"), comboObj);
    root.insert(QLatin1String("shortcuts"), scObj);
    root.insert(QLatin1String("billable_default"), m_billableDefault);
    QJsonArray autArr;
    for (const auto &r : m_autotagRules) {
        QJsonObject o;
        o.insert(QLatin1String("id"), static_cast<double>(r.id));
        o.insert(QLatin1String("name"), r.name);
        o.insert(QLatin1String("type"), r.type);
        o.insert(QLatin1String("notes"), r.notes);
        o.insert(QLatin1String("billable"), r.billable);
        o.insert(QLatin1String("enabled"), r.enabled);
        o.insert(QLatin1String("order"), r.order);
        QJsonArray ca;
        for (const auto &c : r.conditions) {
            QJsonObject co;
            co.insert(QLatin1String("field"), c.field);
            co.insert(QLatin1String("value"), c.value);
            co.insert(QLatin1String("regex"), c.regex);
            ca.append(co);
        }
        o.insert(QLatin1String("conditions"), ca);
        autArr.append(o);
    }
    root.insert(QLatin1String("autotags"), autArr);
    root.insert(QLatin1String("autotag_gap_fill_sec"), m_gapFillSec);
    root.insert(QLatin1String("autotag_skip_rest"), m_skipRest);
    root.insert(QLatin1String("autotag_highlight_guesses"), m_highlightGuesses);
    root.insert(QLatin1String("autotag_append_rule_data"), m_appendRuleData);

    QSaveFile f(filePath());
    if (f.open(QIODevice::WriteOnly)) {
        f.write(QJsonDocument(root).toJson(QJsonDocument::Indented));
        f.commit();
    }
}

void TagStore::rebuildStats()
{
    QMap<QString, TagMeta> fresh;
    for (auto it = m_tags.constBegin(); it != m_tags.constEnd(); ++it) {
        TagMeta m = it.value();
        m.useCount = 0;
        m.taggedMs = 0;
        m.lastUsed.clear();
        fresh.insert(m.name, m);
    }
    for (const auto &s : m_segments) {
        if (s.deleted)
            continue;
        for (const auto &tag : s.tags) {
            TagMeta &m = fresh[tag]; // 若字典没有则创建
            ++m.useCount;
            m.taggedMs += (s.endMs - s.startMs);
            if (s.updatedAt > m.lastUsed)
                m.lastUsed = s.updatedAt;
        }
    }
    m_tags = fresh;
}

QString TagStore::nextId() const
{
    qint64 mx = 0;
    for (const auto &s : m_segments)
        mx = qMax(mx, s.id);
    return QString::number(mx + 1);
}

// ── 段 CRUD ─────────────────────────────────────────────────
QList<TagSegment> TagStore::segments() const
{
    QList<TagSegment> out;
    for (const auto &s : m_segments)
        if (!s.deleted)
            out.append(s);
    return out;
}

QList<TagSegment> TagStore::segmentsInRange(qint64 startMs, qint64 endMs) const
{
    QList<TagSegment> out;
    for (const auto &s : m_segments) {
        if (s.deleted)
            continue;
        if (s.endMs <= startMs || s.startMs >= endMs)
            continue;
        out.append(s);
    }
    return out;
}

const TagSegment *TagStore::find(qint64 id) const
{
    for (const auto &s : m_segments)
        if (s.id == id && !s.deleted)
            return &s;
    return nullptr;
}

qint64 TagStore::addSegment(qint64 startMs, qint64 endMs, const QStringList &tags,
                            const QString &notes, bool billable)
{
    if (endMs <= startMs || tags.isEmpty())
        return 0;
    TagSegment s;
    s.id = nextId().toLongLong();
    s.startMs = startMs;
    s.endMs = endMs;
    s.tags = tags;
    s.notes = notes;
    s.billable = billable;
    s.createdAt = nowIso();
    s.updatedAt = s.createdAt;
    m_segments.append(s);
    touch();
    return s.id;
}

bool TagStore::updateSegment(qint64 id, qint64 startMs, qint64 endMs, const QStringList &tags,
                             const QString &notes, bool billable)
{
    for (auto &s : m_segments) {
        if (s.id == id && !s.deleted) {
            s.startMs = startMs;
            s.endMs = endMs;
            s.tags = tags;
            s.notes = notes;
            s.billable = billable;
            s.updatedAt = nowIso();
            touch();
            return true;
        }
    }
    return false;
}

bool TagStore::removeSegment(qint64 id)
{
    for (int i = 0; i < m_segments.size(); ++i) {
        if (m_segments[i].id == id && !m_segments[i].deleted) {
            m_segments[i].deleted = true;
            m_segments[i].updatedAt = nowIso();
            touch();
            return true;
        }
    }
    return false;
}

void TagStore::touch()
{
    m_dirty = true;
    rebuildStats();
    save();
}

// ── 字典 / 组合 ─────────────────────────────────────────────
QStringList TagStore::allTagNames() const
{
    QStringList names = m_tags.keys();
    names.sort();
    return names;
}

QStringList TagStore::allCombinations() const
{
    // 从段中提取组合，按最后使用倒序
    QMap<QString, QString> lastUsed; // comboKey -> lastUsed
    QMap<QString, qint64> combos;    // comboKey -> count
    for (const auto &s : m_segments) {
        if (s.deleted || s.tags.isEmpty())
            continue;
        const QString k = comboKey(s.tags);
        combos[k] += 1;
        if (s.updatedAt > lastUsed.value(k))
            lastUsed[k] = s.updatedAt;
    }
    QStringList keys = combos.keys();
    std::sort(keys.begin(), keys.end(), [&lastUsed](const QString &a, const QString &b) {
        return lastUsed.value(a) > lastUsed.value(b);
    });
    return keys;
}

QList<TagMeta> TagStore::tagMetas() const
{
    QList<TagMeta> out;
    for (const auto &m : m_tags)
        out.append(m);
    std::sort(out.begin(), out.end(), [](const TagMeta &a, const TagMeta &b) {
        return QString::localeAwareCompare(a.name, b.name) < 0;
    });
    return out;
}

QList<TagMeta> TagStore::combinationMetas() const
{
    QMap<QString, TagMeta> map;
    for (const auto &s : m_segments) {
        if (s.deleted || s.tags.isEmpty())
            continue;
        const QString k = comboKey(s.tags);
        TagMeta &m = map[k];
        m.name = k;
        m.useCount += 1;
        m.taggedMs += (s.endMs - s.startMs);
        if (s.updatedAt > m.lastUsed)
            m.lastUsed = s.updatedAt;
    }
    QList<TagMeta> out = map.values();
    std::sort(out.begin(), out.end(), [](const TagMeta &a, const TagMeta &b) {
        return a.lastUsed > b.lastUsed;
    });
    return out;
}

QList<TagSegment> TagStore::segmentsOfCombination(const QStringList &tags) const
{
    QList<TagSegment> out;
    for (const auto &s : m_segments) {
        if (s.deleted)
            continue;
        if (s.tags == tags)
            out.append(s);
    }
    return out;
}

qint64 TagStore::taggedTimeInRange(qint64 startMs, qint64 endMs) const
{
    qint64 total = 0;
    for (const auto &s : m_segments) {
        if (s.deleted)
            continue;
        const qint64 a = qMax(startMs, s.startMs);
        const qint64 b = qMin(endMs, s.endMs);
        if (b > a)
            total += (b - a);
    }
    return total;
}

// ── 颜色模型 ────────────────────────────────────────────────
QColor TagStore::tagColor(const QString &name) const
{
    auto it = m_tags.constFind(name);
    if (it != m_tags.constEnd() && !it->color.isEmpty())
        return QColor(it->color);
    return colorForString(name);
}

QColor TagStore::segmentColor(const TagSegment &seg) const
{
    if (!seg.colorOverride.isEmpty())
        return QColor(seg.colorOverride);
    for (const auto &tag : seg.tags) {
        auto it = m_tags.constFind(tag);
        const bool skip = it != m_tags.constEnd() && it->skipColor;
        if (!skip)
            return tagColor(tag);
    }
    return QColor(QStringLiteral("#8a919c"));
}

void TagStore::setTagColor(const QString &name, const QString &colorHex)
{
    TagMeta &m = m_tags[name];
    m.color = colorHex;
    touch();
}

void TagStore::setTagSkip(const QString &name, bool skip)
{
    TagMeta &m = m_tags[name];
    m.skipColor = skip;
    touch();
}

void TagStore::setCombinationColor(const QStringList &tags, const QString &colorHex)
{
    if (colorHex.isEmpty())
        m_comboColors.remove(comboKey(tags));
    else
        m_comboColors.insert(comboKey(tags), colorHex);
    touch();
}

void TagStore::clearCombinationColor(const QStringList &tags)
{
    m_comboColors.remove(comboKey(tags));
    touch();
}

// ── 重命名 / 删除 ───────────────────────────────────────────
void TagStore::renameTag(const QString &oldName, const QString &newName)
{
    if (oldName.isEmpty() || newName.isEmpty() || oldName == newName)
        return;
    for (auto &s : m_segments) {
        if (s.deleted)
            continue;
        for (auto &t : s.tags) {
            if (t == oldName)
                t = newName;
        }
        s.updatedAt = nowIso();
    }
    auto it = m_tags.constFind(oldName);
    if (it != m_tags.constEnd()) {
        TagMeta m = it.value();
        m.name = newName;
        m_tags.remove(oldName);
        m_tags.insert(newName, m);
    }
    // 组合颜色键迁移
    QStringList toMove;
    for (auto c = m_comboColors.constBegin(); c != m_comboColors.constEnd(); ++c) {
        if (c.key().split(QLatin1Char(',')).contains(oldName))
            toMove << c.key();
    }
    for (const auto &k : toMove) {
        QStringList parts = k.split(QLatin1Char(','));
        for (auto &p : parts)
            if (p == oldName)
                p = newName;
        m_comboColors.insert(parts.join(QLatin1Char(',')), m_comboColors.take(k));
    }
    touch();
}

void TagStore::renameCombination(const QStringList &oldTags, const QStringList &newTags)
{
    if (oldTags.isEmpty() || newTags.isEmpty())
        return;
    for (auto &s : m_segments) {
        if (s.deleted)
            continue;
        if (s.tags == oldTags) {
            s.tags = newTags;
            s.updatedAt = nowIso();
        }
    }
    const QString oldKey = comboKey(oldTags);
    if (m_comboColors.contains(oldKey))
        m_comboColors.insert(comboKey(newTags), m_comboColors.take(oldKey));
    touch();
}

void TagStore::deleteTag(const QString &name)
{
    // 从所有段中移除该标签；段空则删除
    QList<qint64> toDelete;
    for (auto &s : m_segments) {
        if (s.deleted)
            continue;
        s.tags.removeAll(name);
        s.updatedAt = nowIso();
        if (s.tags.isEmpty())
            toDelete << s.id;
    }
    for (const auto &id : toDelete) {
        for (auto &s : m_segments)
            if (s.id == id)
                s.deleted = true;
    }
    m_tags.remove(name);
    // 组合颜色清理
    QStringList toRemove;
    for (auto c = m_comboColors.constBegin(); c != m_comboColors.constEnd(); ++c) {
        if (c.key().split(QLatin1Char(',')).contains(name))
            toRemove << c.key();
    }
    for (const auto &k : toRemove)
        m_comboColors.remove(k);
    touch();
}

void TagStore::deleteCombination(const QStringList &tags)
{
    for (auto &s : m_segments) {
        if (!s.deleted && s.tags == tags)
            s.deleted = true;
    }
    m_comboColors.remove(comboKey(tags));
    touch();
}

// ── 导入 / 导出 ─────────────────────────────────────────────
QString TagStore::exportText() const
{
    QStringList lines;
    QSet<QString> seen;
    for (const auto &s : m_segments) {
        if (s.deleted || s.tags.isEmpty())
            continue;
        const QString line = s.tags.join(QStringLiteral(", "));
        if (!seen.contains(line)) {
            seen.insert(line);
            lines << line;
        }
    }
    return lines.join(QLatin1Char('\n'));
}

int TagStore::importText(const QString &text)
{
    // 支持部分标签笛卡尔展开：如 "Project X," + ",Design" -> "Project X, Design"
    QStringList prefixes;
    QStringList suffixes;
    int importCount = 0;

    const QStringList rawLines = text.split(QLatin1Char('\n'));
    for (const QString &raw : rawLines) {
        const QString line = raw.trimmed();
        if (line.isEmpty())
            continue;
        // 以逗号开头的行是部分标签的「后缀」定义
        if (line.startsWith(QLatin1Char(','))) {
            suffixes << line.mid(1).trimmed();
            continue;
        }
        // 以逗号结尾的行是「前缀」定义
        const QString stripped = line.endsWith(QLatin1Char(',')) ? line.left(line.size() - 1).trimmed() : line;
        prefixes << stripped;
    }
    // 笛卡尔展开
    QStringList combos;
    if (!prefixes.isEmpty() && suffixes.isEmpty()) {
        for (const auto &p : prefixes)
            combos << p;
    } else if (prefixes.isEmpty() && !suffixes.isEmpty()) {
        for (const auto &s : suffixes)
            combos << s;
    } else {
        for (const auto &p : prefixes)
            for (const auto &s : suffixes)
                combos << (p.isEmpty() ? s : (s.isEmpty() ? p : p + QStringLiteral(", ") + s));
    }
    for (const auto &c : combos) {
        if (c.isEmpty())
            continue;
        const QStringList parts = c.split(QStringLiteral(","), Qt::SkipEmptyParts);
        QStringList cleaned;
        for (auto &p : parts) {
            const QString t = p.trimmed();
            if (!t.isEmpty())
                cleaned << t;
        }
        if (cleaned.isEmpty())
            continue;
        TagMeta &m = m_tags[cleaned.join(QLatin1Char(','))];
        m.name = cleaned.join(QLatin1Char(','));
        ++importCount;
    }
    if (importCount > 0)
        touch();
    return importCount;
}

void TagStore::setNewTagsBillableByDefault(bool on)
{
    m_billableDefault = on;
    save();
}

void TagStore::setShortcut(const QString &key, const QString &combo)
{
    if (combo.trimmed().isEmpty())
        m_shortcuts.remove(key);
    else
        m_shortcuts.insert(key, combo.trimmed());
    save();
}

void TagStore::removeShortcut(const QString &key)
{
    m_shortcuts.remove(key);
    save();
}

QString TagStore::shortcutTag(const QString &key) const
{
    return m_shortcuts.value(key);
}

// ── 自动标签规则 ────────────────────────────────────────────
void TagStore::setAutotagRules(const QList<AutoTagRule> &rules)
{
    m_autotagRules = rules;
    save();
}

qint64 TagStore::nextRuleId() const
{
    qint64 mx = 0;
    for (const auto &r : m_autotagRules)
        mx = qMax(mx, r.id);
    return mx + 1;
}

void TagStore::setAutotagGapFillSec(int s)
{
    m_gapFillSec = s;
    save();
}

void TagStore::setAutotagSkipRest(bool b)
{
    m_skipRest = b;
    save();
}

void TagStore::setAutotagHighlightGuesses(bool b)
{
    m_highlightGuesses = b;
    save();
}

void TagStore::setAutotagAppendRuleData(bool b)
{
    m_appendRuleData = b;
    save();
}

} // namespace awqtui
