// filterparser.h —— 活动过滤语法解析器（当日过滤与高级搜索共用）
//
// 语法子集（对齐 ManicTime Searching 文档）：
//   group:xxx       按 group（应用/文档组）匹配
//   duration>1m10s  时长比较（单位 s/m/h，可组合）
//   start>22:00     开始时间（当天内）
//   end<6:00PM      结束时间
//   label=billable  标签 label（billable）
//   note:"some"     标签备注匹配
//   or              逻辑或（and 为隐式）
//   -xxx            取反
//   ? *             通配符（单个 / 任意数量非空白字符）
//   #"regex"        正则表达式
#pragma once

#include <QList>
#include <QRegularExpression>
#include <QString>

namespace awqtui {

// 一条待匹配的活动行（Details 明细 / 标签段）
struct ActivityRow {
    QString title;
    QString group;
    qint64 startMs = 0;
    qint64 endMs = 0;
    QString notes;
    bool billable = false;
    // 扩展字段（解析支持；当前 mock 无值时不命中）
    QString workplace;
    QString desktop;
    QString gitBranch;
    QString gitRepo;
};

// 过滤表达式：AND 子句 + OR 分组
class FilterQuery
{
public:
    // 解析文本；空串 = 不过滤
    bool parse(const QString &text);
    bool isActive() const { return m_active; }
    // 判断一行是否命中
    bool matches(const ActivityRow &row) const;

private:
    struct TextToken {
        QString value;
        bool negated = false;
        bool isRegex = false;
    };
    struct Term {
        enum Type { Text, Group, DurationGt, DurationLt, StartGt, StartLt, EndGt, EndLt,
                    Label, Note, Workplace, Desktop, GitBranch, GitRepo };
        Type type = Text;
        TextToken text;
        qint64 valueMs = 0; // duration / start / end
        QString value;      // group / label / note / workplace ...
    };
    struct Clause {
        QList<Term> terms; // AND
    };
    static bool matchText(const TextToken &tok, const QString &target);
    QList<Clause> m_ors; // 任一命中即整体命中
    bool m_active = false;
};

// 工具：时长解析 "1m10s" / "1h" / "30s"
qint64 parseDurationMs(const QString &s);
// 通配匹配：? = 单个非空白字符，* = 任意数量非空白字符
bool globMatch(const QString &pattern, const QString &text);
// 时间解析：返回当天内毫秒（支持 "22:00"、"6:00PM"、"10:00 PM"）
qint64 parseClockMs(const QString &s, bool *ok);

} // namespace awqtui
