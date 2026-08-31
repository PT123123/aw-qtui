// mdrender.h —— 完整 Markdown -> 富文本 HTML 渲染（收件箱卡片内容）
#pragma once

#include <QString>

namespace awqtui {

struct MarkdownRenderResult {
    QString html;
    int taskCount = 0; // 任务清单项数量，用于 awtask://N 点击索引
};

// 把 markdown 渲染为富文本 HTML：
//   - 块级：标题 / 段落 / 引用 / 无序·有序列表 / 任务清单 / 围栏代码块 / 分隔线
//   - 行内：**粗体** *斜体* ~~删除线~~ `行内代码` [文本](链接) ![alt](图片) 裸 URL
//   - #标签 内联高亮（# 后无空格，避免与标题冲突）
//   - 任务 checkbox 渲染为 <a href="awtask://N"> 可点击锚点（由卡片捕获后切换）
// 超长内容在 600 字符内的行边界截断。
MarkdownRenderResult renderMarkdown(const QString &markdown);

} // namespace awqtui
