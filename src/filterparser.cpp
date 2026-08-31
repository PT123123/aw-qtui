// filterparser.cpp
#include "filterparser.h"

#include <QDateTime>
#include <QRegularExpression>
#include <QSet>
#include <QStringList>

namespace awqtui {

qint64 parseDurationMs(const QString &s)
{
    // 形如 "1h30m10s"、"30s"、"1m"；纯数字按秒
    qint64 total = 0;
    const QRegularExpression re(QStringLiteral("(\\d+)([smh])"));
    auto it = re.globalMatch(s);
    bool any = false;
    while (it.hasNext()) {
        const auto m = it.next();
        const qint64 n = m.captured(1).toLongLong();
        const QChar unit = m.captured(2).at(0);
        total += (unit == QLatin1Char('s')) ? n * 1000
                : (unit == QLatin1Char('m')) ? n * 60000
                                             : n * 3600000;
        any = true;
    }
    if (!any) {
        bool ok = false;
        const qint64 n = s.trimmed().toLongLong(&ok);
        if (ok)
            return n * 1000;
        return -1;
    }
    return total;
}

qint64 parseClockMs(const QString &s, bool *ok)
{
    if (ok)
        *ok = false;
    QString t = s.trimmed();
    if (t.isEmpty())
        return -1;

    int hour = 0;
    int minute = 0;
    const QString up = t.toUpper();
    bool pm = false;
    if (up.contains(QLatin1String("PM")) || up.contains(QLatin1String("AM"))) {
        pm = up.contains(QLatin1String("PM"));
        t = t.left(t.indexOf(QRegularExpression(QStringLiteral("[AP]M")))).trimmed();
    }
    const QStringList parts = t.split(QLatin1Char(':'));
    if (parts.isEmpty())
        return -1;
    bool hok = false;
    const int h = parts[0].trimmed().toInt(&hok);
    if (!hok || h < 0 || h > 23)
        return -1;
    hour = h;
    if (parts.size() >= 2) {
        bool mok = false;
        const int m = parts[1].trimmed().toInt(&mok);
        if (!mok || m < 0 || m > 59)
            return -1;
        minute = m;
    }
    if (pm && hour < 12)
        hour += 12;
    if (up.contains(QLatin1String("AM")) && hour == 12)
        hour = 0;
    if (ok)
        *ok = true;
    return (hour * 3600LL + minute * 60LL) * 1000;
}

static QRegularExpression globToRegex(const QString &glob)
{
    QString re;
    re.reserve(glob.size() * 2 + 8);
    for (const QChar c : glob) {
        if (c == QLatin1Char('?'))
            re += QStringLiteral("\\S");
        else if (c == QLatin1Char('*'))
            re += QStringLiteral("\\S*");
        else
            re += QRegularExpression::escape(QString(c));
    }
    return QRegularExpression(QStringLiteral("^") + re + QStringLiteral("$"),
                              QRegularExpression::CaseInsensitiveOption);
}

bool globMatch(const QString &pattern, const QString &text)
{
    if (!pattern.contains(QLatin1Char('?')) && !pattern.contains(QLatin1Char('*')))
        return text.contains(pattern, Qt::CaseInsensitive);
    return globToRegex(pattern).match(text).hasMatch();
}

// ── token 切分：保留引号内空格 ──────────────────────────────
static QStringList tokenize(const QString &text)
{
    QStringList out;
    QString cur;
    bool inQuote = false;
    for (const QChar c : text) {
        if (c == QLatin1Char('"')) {
            inQuote = !inQuote;
            cur += c;
            continue;
        }
        if (c.isSpace() && !inQuote) {
            if (!cur.isEmpty()) {
                out << cur;
                cur.clear();
            }
            continue;
        }
        cur += c;
    }
    if (!cur.isEmpty())
        out << cur;
    return out;
}

static QString stripQuotes(const QString &s)
{
    if (s.size() >= 2 && s.startsWith(QLatin1Char('"')) && s.endsWith(QLatin1Char('"')))
        return s.mid(1, s.size() - 2);
    return s;
}

static bool tryRegex(const QString &s, bool *isRegex, QString *value)
{
    if (s.size() >= 3 && s.startsWith(QStringLiteral("#\"")) && s.endsWith(QLatin1Char('"'))) {
        *isRegex = true;
        *value = s.mid(2, s.size() - 3);
        return true;
    }
    *isRegex = false;
    *value = stripQuotes(s);
    return true;
}

bool FilterQuery::parse(const QString &text)
{
    m_ors.clear();
    m_active = false;

    const QStringList tokens = tokenize(text);
    if (tokens.isEmpty())
        return true;

    Clause clause;
    auto pushClause = [&] {
        if (!clause.terms.isEmpty() || m_ors.isEmpty())
            m_ors.append(clause);
        clause = Clause();
    };

    for (const QString &tok : tokens) {
        QString t = tok;
        if (t == QLatin1String("or")) {
            pushClause();
            continue;
        }

        Term term;
        QString body = t;
        // 处理 - 取反（-xxx / -group:xxx / -label=billable）
        if (body.startsWith(QLatin1Char('-'))) {
            term.text.negated = true;
            body = body.mid(1);
            if (body.isEmpty())
                continue;
        }

        if (body.startsWith(QStringLiteral("group:"))) {
            term.type = Term::Group;
            tryRegex(body.mid(6), &term.text.isRegex, &term.text.value);
        } else if (body.startsWith(QStringLiteral("duration>"))) {
            term.type = Term::DurationGt;
            term.valueMs = parseDurationMs(body.mid(9));
            if (term.valueMs < 0)
                continue;
        } else if (body.startsWith(QStringLiteral("duration<"))) {
            term.type = Term::DurationLt;
            term.valueMs = parseDurationMs(body.mid(9));
            if (term.valueMs < 0)
                continue;
        } else if (body.startsWith(QStringLiteral("start>"))) {
            bool ok = false;
            term.type = Term::StartGt;
            term.valueMs = parseClockMs(body.mid(6), &ok);
            if (!ok)
                continue;
        } else if (body.startsWith(QStringLiteral("start<"))) {
            bool ok = false;
            term.type = Term::StartLt;
            term.valueMs = parseClockMs(body.mid(6), &ok);
            if (!ok)
                continue;
        } else if (body.startsWith(QStringLiteral("end>"))) {
            bool ok = false;
            term.type = Term::EndGt;
            term.valueMs = parseClockMs(body.mid(4), &ok);
            if (!ok)
                continue;
        } else if (body.startsWith(QStringLiteral("end<"))) {
            bool ok = false;
            term.type = Term::EndLt;
            term.valueMs = parseClockMs(body.mid(4), &ok);
            if (!ok)
                continue;
        } else if (body.startsWith(QStringLiteral("label="))) {
            term.type = Term::Label;
            term.value = stripQuotes(body.mid(6));
        } else if (body.startsWith(QStringLiteral("note:"))) {
            term.type = Term::Note;
            tryRegex(body.mid(5), &term.text.isRegex, &term.text.value);
        } else if (body.startsWith(QStringLiteral("workplace:"))) {
            term.type = Term::Workplace;
            term.value = stripQuotes(body.mid(10));
        } else if (body.startsWith(QStringLiteral("desktop:"))) {
            term.type = Term::Desktop;
            term.value = stripQuotes(body.mid(8));
        } else if (body.startsWith(QStringLiteral("git-branch:"))) {
            term.type = Term::GitBranch;
            term.value = stripQuotes(body.mid(11));
        } else if (body.startsWith(QStringLiteral("git-repository:"))) {
            term.type = Term::GitRepo;
            term.value = stripQuotes(body.mid(15));
        } else {
            term.type = Term::Text;
            tryRegex(body, &term.text.isRegex, &term.text.value);
        }
        clause.terms.append(term);
    }
    pushClause();
    m_active = !m_ors.isEmpty();
    return true;
}

bool FilterQuery::matchText(const FilterQuery::TextToken &tok, const QString &target)
{
    if (tok.isRegex) {
        const QRegularExpression re(tok.value, QRegularExpression::CaseInsensitiveOption);
        const bool hit = re.match(target).hasMatch();
        return tok.negated ? !hit : hit;
    }
    const bool hit = globMatch(tok.value, target);
    return tok.negated ? !hit : hit;
}

bool FilterQuery::matches(const ActivityRow &row) const
{
    if (m_ors.isEmpty())
        return true;
    for (const Clause &clause : m_ors) {
        if (clause.terms.isEmpty())
            continue;
        bool all = true;
        for (const Term &term : clause.terms) {
            bool hit = false;
            switch (term.type) {
            case Term::Text:
                hit = matchText(term.text, row.title);
                break;
            case Term::Group:
                hit = matchText(term.text, row.group);
                break;
            case Term::DurationGt:
                hit = (row.endMs - row.startMs) > term.valueMs;
                break;
            case Term::DurationLt:
                hit = (row.endMs - row.startMs) < term.valueMs;
                break;
            case Term::StartGt:
            case Term::StartLt: {
                const qint64 t = QDateTime::fromMSecsSinceEpoch(row.startMs, Qt::LocalTime)
                                     .time()
                                     .msecsSinceStartOfDay();
                hit = (term.type == Term::StartGt) ? (t > term.valueMs) : (t < term.valueMs);
                break;
            }
            case Term::EndGt:
            case Term::EndLt: {
                const qint64 t = QDateTime::fromMSecsSinceEpoch(row.endMs, Qt::LocalTime)
                                     .time()
                                     .msecsSinceStartOfDay();
                hit = (term.type == Term::EndGt) ? (t > term.valueMs) : (t < term.valueMs);
                break;
            }
            case Term::Label:
                hit = (term.value == QLatin1String("billable")) ? row.billable : (!row.billable);
                break;
            case Term::Note:
                hit = matchText(term.text, row.notes);
                break;
            case Term::Workplace:
                hit = !row.workplace.isEmpty() && globMatch(term.value, row.workplace);
                break;
            case Term::Desktop:
                hit = !row.desktop.isEmpty() && globMatch(term.value, row.desktop);
                break;
            case Term::GitBranch:
                hit = !row.gitBranch.isEmpty() && globMatch(term.value, row.gitBranch);
                break;
            case Term::GitRepo:
                hit = !row.gitRepo.isEmpty() && globMatch(term.value, row.gitRepo);
                break;
            }
            if (!hit) {
                all = false;
                break;
            }
        }
        if (all)
            return true;
    }
    return false;
}

} // namespace awqtui
