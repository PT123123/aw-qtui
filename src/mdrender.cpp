// mdrender.cpp —— Markdown -> HTML 渲染器（无第三方依赖，QLabel 可直接显示）
#include "mdrender.h"

#include "theme.h"

#include <QRegularExpression>
#include <QStringList>

namespace awqtui {

namespace {

QString escapeHtml(QStringView s)
{
    QString out;
    out.reserve(s.size());
    for (const QChar c : s) {
        switch (c.unicode()) {
        case L'&': out += QStringLiteral("&amp;"); break;
        case L'<': out += QStringLiteral("&lt;"); break;
        case L'>': out += QStringLiteral("&gt;"); break;
        case L'"': out += QStringLiteral("&quot;"); break;
        default: out += c; break;
        }
    }
    return out;
}

// 行内渲染：粗体/斜体/删除线/行内代码/链接/图片/#标签/转义
QString inlineToHtml(const QString &text, int &taskNo)
{
    QString out;
    out.reserve(text.size() * 2);
    int i = 0;
    const int n = text.size();
    while (i < n) {
        const QChar c = text.at(i);
        // 图片 ![alt](url) —— 无内联加载，渲染为可点击占位
        if (c == QLatin1Char('!') && i + 1 < n && text.at(i + 1) == QLatin1Char('[')) {
            const int close = text.indexOf(QLatin1Char(']'), i + 2);
            if (close > i && close + 1 < n && text.at(close + 1) == QLatin1Char('(')) {
                const int end = text.indexOf(QLatin1Char(')'), close + 2);
                if (end > close) {
                    const QString url = escapeHtml(QStringView(text).mid(close + 2, end - close - 2));
                    const QString alt = escapeHtml(QStringView(text).mid(i + 2, close - i - 2));
                    out += QStringLiteral("<a href=\"") + url
                           + QStringLiteral("\" style='color:%1;text-decoration:none;'>🖼 ").arg(kColorAccent) + alt
                           + QStringLiteral("</a>");
                    i = end + 1;
                    continue;
                }
            }
        }
        // 行内代码 `...`
        if (c == QLatin1Char('`')) {
            const int j = text.indexOf(QLatin1Char('`'), i + 1);
            if (j > i) {
                out += QStringLiteral("<code style='background:%1;color:%2;border-radius:4px;"
                                      "padding:1px 5px;font-family:Consolas,monospace;'>")
                           .arg(kColorBgElev2, kColorWarn)
                       + escapeHtml(QStringView(text).mid(i + 1, j - i - 1)) + QStringLiteral("</code>");
                i = j + 1;
                continue;
            }
        }
        // 删除线 ~~...~~
        if (c == QLatin1Char('~') && i + 1 < n && text.at(i + 1) == QLatin1Char('~')) {
            const int j = text.indexOf(QStringLiteral("~~"), i + 2);
            if (j > i) {
                out += QStringLiteral("<s>") + escapeHtml(QStringView(text).mid(i + 2, j - i - 2))
                       + QStringLiteral("</s>");
                i = j + 2;
                continue;
            }
        }
        // 粗体 **...**（开口后、闭口前不能是空白，避免 2 ** 3 误判）
        if (c == QLatin1Char('*') && i + 1 < n && text.at(i + 1) == QLatin1Char('*')
            && (i + 2 >= n || !text.at(i + 2).isSpace())) {
            const int j = text.indexOf(QStringLiteral("**"), i + 2);
            if (j > i + 2 && j - 1 >= 0 && !text.at(j - 1).isSpace()) {
                out += QStringLiteral("<b>") + escapeHtml(QStringView(text).mid(i + 2, j - i - 2))
                       + QStringLiteral("</b>");
                i = j + 2;
                continue;
            }
        }
        // 斜体 *...*（开口后、闭口前不能是空白，且不能是 ** 开头）
        if (c == QLatin1Char('*') && (i + 1 >= n || text.at(i + 1) != QLatin1Char('*'))
            && (i + 1 < n && !text.at(i + 1).isSpace())) {
            const int j = text.indexOf(QLatin1Char('*'), i + 1);
            if (j > i + 1 && !text.at(j - 1).isSpace()) {
                out += QStringLiteral("<i>") + escapeHtml(QStringView(text).mid(i + 1, j - i - 1))
                       + QStringLiteral("</i>");
                i = j + 1;
                continue;
            }
        }
        // 链接 [文本](url)
        if (c == QLatin1Char('[')) {
            const int close = text.indexOf(QLatin1Char(']'), i + 1);
            if (close > i && close + 1 < n && text.at(close + 1) == QLatin1Char('(')) {
                const int end = text.indexOf(QLatin1Char(')'), close + 2);
                if (end > close) {
                    const QString url = escapeHtml(QStringView(text).mid(close + 2, end - close - 2));
                    const QString label = escapeHtml(QStringView(text).mid(i + 1, close - i - 1));
                    out += QStringLiteral("<a href=\"") + url
                           + QStringLiteral("\" style='color:%1;text-decoration:none;'>").arg(kColorAccent) + label
                           + QStringLiteral("</a>");
                    i = end + 1;
                    continue;
                }
            }
        }
        // #标签（# 前不是字母数字、# 后非空白/非 #，避免把标题或 C# 误伤）
        if (c == QLatin1Char('#')
            && (i == 0 || !text.at(i - 1).isLetterOrNumber())) {
            int j = i + 1;
            while (j < n && !text.at(j).isSpace() && text.at(j) != QLatin1Char('#'))
                ++j;
            if (j > i + 1) {
                out += QStringLiteral("<span style='color:#7fb3ff;'>#")
                       + escapeHtml(QStringView(text).mid(i + 1, j - i - 1)) + QStringLiteral("</span>");
                i = j;
                continue;
            }
        }
        // 换行（引用内拼接的多行）
        if (c == QLatin1Char('\n')) {
            out += QStringLiteral("<br>");
            ++i;
            continue;
        }
        out += escapeHtml(QStringView(text).mid(i, 1));
        ++i;
    }
    return out;
}

} // namespace

MarkdownRenderResult renderMarkdown(const QString &markdown)
{
    MarkdownRenderResult res;
    if (markdown.trimmed().isEmpty()) {
        res.html = QStringLiteral("<i style='color:%1;'>（空笔记）</i>").arg(kColorFgMuted);
        return res;
    }

    // 截断：600 字符，尽量对齐行边界
    QString src = markdown;
    if (src.size() > 600) {
        int cut = src.lastIndexOf(QLatin1Char('\n'), 600);
        if (cut < 300)
            cut = 600;
        src = src.left(cut) + QStringLiteral("…");
    }

    const QStringList lines = src.split(QLatin1Char('\n'));
    const int n = lines.size();
    QString out;
    out.reserve(src.size() * 2);

    QStringList para; // 段落缓冲（跨行合并为 <p>）
    int listType = 0; // 0 无 / 1 ul / 2 ol
    auto flushPara = [&] {
        if (para.isEmpty())
            return;
        out += QStringLiteral("<p style='margin:2px 0;'>") + para.join(QStringLiteral("<br>"))
               + QStringLiteral("</p>");
        para.clear();
    };
    auto closeList = [&] {
        if (listType == 1)
            out += QStringLiteral("</ul>");
        else if (listType == 2)
            out += QStringLiteral("</ol>");
        listType = 0;
    };
    auto openList = [&](int t) {
        if (listType != t) {
            closeList();
            out += (t == 1) ? QStringLiteral("<ul style='margin:2px 0;padding-left:20px;'>")
                            : QStringLiteral("<ol style='margin:2px 0;padding-left:20px;'>");
            listType = t;
        }
    };

    static const QRegularExpression ulRe(QStringLiteral("^\\s*([-*+])\\s+(.*)$"));
    static const QRegularExpression taskRe(QStringLiteral("^\\s*([-*+])\\s+\\[([ xX])\\]\\s+(.*)$"));
    static const QRegularExpression olRe(QStringLiteral("^\\s*\\d+[.)]\\s+(.*)$"));

    int i = 0;
    while (i < n) {
        const QString line = lines[i];

        // 空行：收段落与列表
        if (line.trimmed().isEmpty()) {
            flushPara();
            closeList();
            ++i;
            continue;
        }

        // 围栏代码块 ``` / ~~~
        if (line.startsWith(QStringLiteral("```")) || line.startsWith(QStringLiteral("~~~"))) {
            flushPara();
            closeList();
            const QString fence = line.left(3);
            const QString lang = line.mid(3).trimmed();
            QStringList code;
            ++i;
            while (i < n) {
                if (lines[i].trimmed().startsWith(fence)) {
                    ++i;
                    break;
                }
                code << lines[i];
                ++i;
            }
            out += QStringLiteral("<pre style='background:%1;border-radius:8px;padding:8px 10px;"
                                  "margin:4px 0;'><code").arg(kColorBgElev2)
                   + (lang.isEmpty() ? QString() : QStringLiteral(" class=\"lang-%1\"").arg(escapeHtml(lang)))
                   + QStringLiteral(">") + escapeHtml(code.join(QLatin1Char('\n')))
                   + QStringLiteral("</code></pre>");
            continue;
        }

        // 标题（# 后必须有空格，避免 #标签 被当标题）
        if (line.startsWith(QLatin1Char('#'))) {
            int h = 0;
            while (h < line.size() && h < 6 && line.at(h) == QLatin1Char('#'))
                ++h;
            if (h < line.size() && line.at(h).isSpace()) {
                flushPara();
                closeList();
                const int px = qMax(14, 20 - h * 2);
                out += QStringLiteral("<h%1 style='margin:8px 0 4px;font-size:%2;font-weight:700;'>")
                           .arg(h)
                           .arg(sp(px))
                       + inlineToHtml(line.mid(h).trimmed(), res.taskCount)
                       + QStringLiteral("</h%1>").arg(h);
                ++i;
                continue;
            }
        }

        // 分隔线
        {
            const QString t = line.trimmed();
            if (t == QStringLiteral("---") || t == QStringLiteral("***") || t == QStringLiteral("___")) {
                flushPara();
                closeList();
                out += QStringLiteral("<hr style='border:none;border-top:1px solid %1;margin:8px 0;'>")
                           .arg(kColorBorder);
                ++i;
                continue;
            }
        }

        // 引用
        if (line.trimmed().startsWith(QLatin1Char('>'))) {
            flushPara();
            closeList();
            QStringList quote;
            while (i < n && lines[i].trimmed().startsWith(QLatin1Char('>'))) {
                QString q = lines[i].trimmed();
                q.remove(0, 1);
                if (q.startsWith(QLatin1Char(' ')))
                    q.remove(0, 1);
                quote << q;
                ++i;
            }
            out += QStringLiteral("<blockquote style='margin:6px 0;padding:2px 12px;"
                                  "border-left:3px solid %1;color:%2;'>")
                       .arg(kColorAccent, kColorFgMuted)
                   + inlineToHtml(quote.join(QLatin1Char('\n')), res.taskCount)
                   + QStringLiteral("</blockquote>");
            continue;
        }

        // 无序列表（含任务清单）
        {
            const auto um = ulRe.match(line);
            if (um.hasMatch()) {
                flushPara();
                while (i < n) {
                    const QString cl = lines[i];
                    const auto tm = taskRe.match(cl);
                    if (tm.hasMatch()) {
                        openList(1);
                        const QString box = (tm.captured(2) == QLatin1Char('x')
                                             || tm.captured(2) == QLatin1Char('X'))
                                                ? QStringLiteral("☑")
                                                : QStringLiteral("☐");
                        out += QStringLiteral("<li style='margin:2px 0;'>")
                               + QStringLiteral("<a href=\"awtask://%1\" style='text-decoration:none;"
                                                "color:%2;'>%3</a> ")
                                     .arg(res.taskCount)
                                     .arg(kColorWarn)
                                     .arg(box)
                               + inlineToHtml(tm.captured(3), res.taskCount)
                               + QStringLiteral("</li>");
                        ++res.taskCount;
                        ++i;
                        continue;
                    }
                    const auto um2 = ulRe.match(cl);
                    if (um2.hasMatch()) {
                        openList(1);
                        out += QStringLiteral("<li style='margin:2px 0;'>")
                               + inlineToHtml(um2.captured(2), res.taskCount) + QStringLiteral("</li>");
                        ++i;
                        continue;
                    }
                    break;
                }
                continue;
            }
        }

        // 有序列表
        {
            const auto om = olRe.match(line);
            if (om.hasMatch()) {
                flushPara();
                while (i < n) {
                    const auto om2 = olRe.match(lines[i]);
                    if (!om2.hasMatch())
                        break;
                    openList(2);
                    out += QStringLiteral("<li style='margin:2px 0;'>")
                           + inlineToHtml(om2.captured(1), res.taskCount) + QStringLiteral("</li>");
                    ++i;
                }
                continue;
            }
        }

        // 普通段落行
        para << inlineToHtml(line, res.taskCount);
        ++i;
    }
    flushPara();
    closeList();

    res.html = out;
    return res;
}

} // namespace awqtui
