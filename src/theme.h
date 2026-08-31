// theme.h —— 深色主题 QSS（气质对齐 aw-webui 暗色界面）
#pragma once

#include <QRegularExpression>
#include <QString>

namespace awqtui {

// 全局 UI 缩放因子（1.0 = 100%）。B 方案：清晰重排式缩放——
// 按该因子放大基准字号并重新生成 Npx 样式，让控件重新布局，而非像素等比拉伸。
inline qreal gUiScale = 1.0;

// 样式表内 Npx -> 缩放后 Npx（仅匹配 "数字px"，不会误伤 #rrggbb / rgba 颜色）
inline QString scaleQss(const QString &base)
{
    if (qAbs(gUiScale - 1.0) < 0.0001)
        return base;
    static const QRegularExpression re(QStringLiteral("(\\d+)px"));
    QString out;
    out.reserve(base.size() + 32);
    int last = 0;
    auto it = re.globalMatch(base);
    while (it.hasNext()) {
        const auto m = it.next();
        out += base.mid(last, m.capturedStart() - last);
        out += QString::number(qRound(m.captured(1).toInt() * gUiScale));
        out += QLatin1String("px");
        last = m.capturedEnd();
    }
    out += base.mid(last);
    return out;
}

// 缩放后的 px 字符串（用于拼样式表），如 sp(14) -> "21px"（scale=1.5）
inline QString sp(qreal px)
{
    return QString::number(qRound(px * gUiScale)) + QLatin1String("px");
}
// 缩放后的整数值（用于 setFixedWidth / setFixedSize 等）
inline int si(qreal px)
{
    return qRound(px * gUiScale);
}

// 调色板
inline constexpr const char *kColorBg = "#1a1d21";
inline constexpr const char *kColorBgElev = "#22262c";
inline constexpr const char *kColorBgElev2 = "#2a2f37";
inline constexpr const char *kColorBorder = "#343a44";
inline constexpr const char *kColorFg = "#e6e6e6";
inline constexpr const char *kColorFgMuted = "#9aa4b0";
inline constexpr const char *kColorAccent = "#4c8bf5";
inline constexpr const char *kColorAccentHover = "#5f99f7";
inline constexpr const char *kColorDanger = "#e5534b";
inline constexpr const char *kColorOk = "#3fb950";
inline constexpr const char *kColorWarn = "#d29922";
inline constexpr const char *kColorTagBg = "#3a5c9e";
inline constexpr const char *kColorTagFg = "#cfe0ff";

inline const QString kGlobalQss = QStringLiteral(R"(
* {
    font-family: "Segoe UI", "Microsoft YaHei", sans-serif;
    font-size: 13px;
    color: #e6e6e6;
}
QMainWindow, QWidget { background: #1a1d21; }
QWidget#NavSidebar {
    background: #22262c;
    border-right: 1px solid #343a44;
}
QPushButton {
    background: #2a2f37;
    border: 1px solid #343a44;
    border-radius: 6px;
    padding: 6px 14px;
}
QPushButton:hover { background: #30363f; border-color: #4c8bf5; }
QPushButton:pressed { background: #262b33; }
QPushButton:disabled { color: #9aa4b0; background: #22262c; }
QPushButton#PrimaryBtn { background: #4c8bf5; border: none; color: white; font-weight: 600; }
QPushButton#PrimaryBtn:hover { background: #5f99f7; }
QPushButton#DangerBtn { background: transparent; border: 1px solid #e5534b; color: #e5534b; }
QPushButton#DangerBtn:hover { background: rgba(229, 83, 75, 0.12); }
QPushButton#NavBtn {
    text-align: left; padding: 10px 16px; border: none; border-radius: 0;
    background: transparent; color: #9aa4b0; font-size: 14px;
}
QPushButton#NavBtn:hover { background: #2a2f37; color: #e6e6e6; }
QPushButton#NavBtn:checked {
    background: rgba(76, 139, 245, 0.16); color: white;
    border-left: 3px solid #4c8bf5;
}
QToolButton#NavSection {
    text-align: left; color: #6b7280; font-size: 10px; font-weight: 700;
    letter-spacing: 1px; padding: 8px 16px 4px;
    border: none; background: transparent;
}
QToolButton#NavSection:hover { color: #9ca3af; }
QToolButton#NavSection:checked { background: transparent; }
QLineEdit, QTextEdit, QPlainTextEdit, QComboBox, QSpinBox {
    background: #22262c; border: 1px solid #343a44; border-radius: 6px;
    padding: 6px 10px; selection-background-color: #4c8bf5;
}
QLineEdit:focus, QTextEdit:focus, QPlainTextEdit:focus, QComboBox:focus, QSpinBox:focus { border-color: #4c8bf5; }
QComboBox::drop-down { border: none; width: 22px; }
QComboBox QAbstractItemView { background: #2a2f37; border: 1px solid #343a44; selection-background-color: #4c8bf5; }
QListWidget, QTableWidget, QTreeWidget {
    background: #1a1d21; border: 1px solid #343a44; border-radius: 8px; outline: none;
}
QListWidget::item { border: none; border-bottom: 1px solid #343a44; padding: 10px 12px; }
QListWidget::item:selected { background: rgba(76, 139, 245, 0.14); }
QHeaderView::section {
    background: #22262c; border: none; border-bottom: 1px solid #343a44;
    padding: 8px; font-weight: 600; color: #9aa4b0;
}
QScrollBar:vertical { background: transparent; width: 10px; }
QScrollBar::handle:vertical { background: #2a2f37; border-radius: 5px; min-height: 30px; }
QScrollBar::handle:vertical:hover { background: #3a414b; }
QScrollBar::add-line, QScrollBar::sub-line { height: 0; }
QScrollBar:horizontal { background: transparent; height: 10px; }
QScrollBar::handle:horizontal { background: #2a2f37; border-radius: 5px; min-width: 30px; }
QScrollBar::add-line:horizontal, QScrollBar::sub-line:horizontal { width: 0; }
QToolTip { background: #2a2f37; color: #e6e6e6; border: 1px solid #343a44; }
QGroupBox { border: 1px solid #343a44; border-radius: 8px; margin-top: 12px; padding-top: 8px; }
QGroupBox::title { subcontrol-origin: margin; left: 10px; padding: 0 6px; color: #9aa4b0; }
QStatusBar { background: #22262c; color: #9aa4b0; border-top: 1px solid #343a44; }
QCheckBox { spacing: 6px; }
QSplitter::handle { background: #343a44; }
QMenu { background: #2a2f37; border: 1px solid #343a44; }
QMenu::item:selected { background: #4c8bf5; color: white; }
QDialog { background: #1a1d21; }
)");

} // namespace awqtui
