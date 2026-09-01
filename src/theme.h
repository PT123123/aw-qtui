// theme.h —— 多主题 + 全局 QSS + emoji 程序图标（纯代码，无外部资源文件）
#pragma once

#include <QAbstractAnimation>
#include <QColor>
#include <QEasingCurve>
#include <QFont>
#include <QGraphicsDropShadowEffect>
#include <QGraphicsOpacityEffect>
#include <QIcon>
#include <QLinearGradient>
#include <QPainter>
#include <QPainterPath>
#include <QPixmap>
#include <QPropertyAnimation>
#include <QRegularExpression>
#include <QString>
#include <QWidget>

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

// ---------------------------------------------------------------- //
// 主题预设
// ---------------------------------------------------------------- //
struct Theme {
    const char *id;
    const char *name;    // 中文名（设置界面显示）
    const char *desc;    // 一句话说明
    const char *emoji;   // 图标 / 预览用 emoji
    bool light;          // 是否为浅色主题（影响派生色方向）
    const char *bg;      // 主背景
    const char *bgElev;  // 侧栏 / 浮层 / 输入框背景
    const char *bgElev2; // 按钮 / 列表悬浮背景
    const char *border;  // 边框
    const char *fg;      // 前景文字
    const char *fgMuted; // 弱化文字
    const char *accent;  // 强调色
    const char *accentHover;
    const char *danger;
    const char *ok;
    const char *warn;
    const char *tagBg;
    const char *tagFg;
};

inline const Theme kThemes[] = {
    // 1 暗夜蓝（默认）
    { "midnight", "暗夜蓝", "默认主题，深蓝基调、蓝色点缀", "🌙", false,
      "#1a1d21", "#22262c", "#2a2f37", "#343a44", "#e6e6e6", "#9aa4b0",
      "#4c8bf5", "#5f99f7", "#e5534b", "#3fb950", "#d29922", "#3a5c9e", "#cfe0ff" },
    // 2 石墨（GitHub 风格中性灰）
    { "graphite", "石墨灰", "GitHub 风格中性灰", "🪨", false,
      "#161b22", "#1f242c", "#282e36", "#363d46", "#e6edf3", "#8b949e",
      "#2f81f7", "#58a6ff", "#f85149", "#3fb950", "#d29922", "#1f6feb", "#c9d1d9" },
    // 3 紫罗兰
    { "violet", "紫罗兰", "柔和的紫色系", "💜", false,
      "#17151f", "#211e2d", "#2b2738", "#3a3550", "#e8e6f0", "#9d97b5",
      "#a78bfa", "#c4b5fd", "#f87171", "#34d399", "#fbbf24", "#6d28d9", "#e9d5ff" },
    // 4 森林绿
    { "emerald", "森林绿", "深绿护眼、青绿点缀", "🌲", false,
      "#0f1a16", "#16241e", "#1d2f27", "#2a4238", "#e2efe7", "#93b3a4",
      "#34d399", "#4ade80", "#f87171", "#22c55e", "#f59e0b", "#0e9f6e", "#c9f2e2" },
    // 5 琥珀暖
    { "amber", "琥珀暖", "暖棕底色、琥珀点缀", "🔥", false,
      "#1c1712", "#241d16", "#2e251b", "#45392a", "#f0e6d6", "#b8a68a",
      "#f59e0b", "#fbbf24", "#f87171", "#34d399", "#fb923c", "#b45309", "#fde9c8" },
    // 6 海洋青
    { "ocean", "海洋青", "深海蓝青、青色点缀", "🌊", false,
      "#0b1a22", "#10222c", "#16303c", "#20404f", "#d8eef7", "#86b3c4",
      "#22d3ee", "#67e8f9", "#fb7185", "#2dd4bf", "#facc15", "#0e7490", "#cffafe" },
    // 7 珊瑚红
    { "rose", "珊瑚红", "低饱和红粉、暖色点缀", "🌹", false,
      "#1a1216", "#221820", "#2c2029", "#43313a", "#f0e4e9", "#bb9fa9",
      "#fb7185", "#fda4af", "#f43f5e", "#34d399", "#fbbf24", "#be123c", "#ffe4e6" },
    // 8 明亮浅色
    { "light", "明亮", "浅色护眼主题", "☀️", true,
      "#f5f6f8", "#ffffff", "#eceff3", "#d9dee5", "#24292f", "#6b7280",
      "#2f6fed", "#1f5fd0", "#d13438", "#1a7f37", "#9a6700", "#d7e5ff", "#1a3f7a" },
};

inline const Theme *gTheme = &kThemes[0];

// 程序 / 托盘图标固定用的 emoji（独立于主题。🌿 香草 + 白色圆形背景，flomo 风格）
inline const char *kAppEmoji = "🌿";

// ---------------------------------------------------------------- //
// 语义色：主色直接指向主题字符串常量；派生色在 applyThemeColors() 中计算
// ---------------------------------------------------------------- //
inline const char *kColorBg = "#1a1d21";
inline const char *kColorBgElev = "#22262c";
inline const char *kColorBgElev2 = "#2a2f37";
inline const char *kColorBorder = "#343a44";
inline const char *kColorFg = "#e6e6e6";
inline const char *kColorFgMuted = "#9aa4b0";
inline const char *kColorAccent = "#4c8bf5";
inline const char *kColorAccentHover = "#5f99f7";
inline const char *kColorDanger = "#e5534b";
inline const char *kColorOk = "#3fb950";
inline const char *kColorWarn = "#d29922";
inline const char *kColorTagBg = "#3a5c9e";
inline const char *kColorTagFg = "#cfe0ff";
// 派生色（随主题计算，QString 保证生命周期）
inline QString kColorHover;   // 控件悬浮背景
inline QString kColorPressed; // 控件按下背景
inline QString kColorMuted2;  // 更弱的弱化色（导航分组小标题等）
inline QString kColorFgSoft;  // 介于前景与弱化之间的柔和文字
inline QString kColorRowAlt;  // 表格交替行背景
inline QString kColorChartBg; // 自绘图表 / 自定义视图背景
inline QString kColorAxis;    // 坐标轴 / 网格线
inline QString kColorNavSel;  // 导航选中项背景（accent 带透明）
inline QString kColorListSel; // 列表选中项背景（accent 带透明）
// 当前主题生成的全局 QSS（未缩放；缩放时套 scaleQss）
inline QString gGlobalQss;

// ---------------------------------------------------------------- //
// 界面效果全局配置（由「设置 → 界面效果」控制；影响阴影 / 玻璃 / 动画 / DWM）
// ---------------------------------------------------------------- //
inline int gShadowLevel = 2;      // 阴影强度：0关 1弱 2中 3强
inline int gGlassLevel = 2;       // 玻璃材质强度：0关(纯色) 1弱 2中 3强
inline bool gFxAnimations = true; // 动画：切页淡入 / 卡片入场 / 高亮过渡
inline bool gDwmBackdrop = false; // Windows DWM 系统背景（Mica/Acrylic）

// ---------------------------------------------------------------- //
// 颜色工具
// ---------------------------------------------------------------- //
// 明度微调：amt>0 变亮、amt<0 变暗
inline QString shade(const char *hex, qreal amt)
{
    QColor c(QString::fromLatin1(hex));
    if (!c.isValid())
        return QString::fromLatin1(hex);
    auto ch = [&](int v) {
        if (amt >= 0)
            return v + qRound((255 - v) * amt);
        return v + qRound(v * amt);
    };
    return QColor(qBound(0, ch(c.red()), 255), qBound(0, ch(c.green()), 255),
                  qBound(0, ch(c.blue()), 255))
        .name();
}
// 两色线性混合：t=0 -> a，t=1 -> b
inline QString mix(const char *a, const char *b, qreal t)
{
    QColor ca(QString::fromLatin1(a)), cb(QString::fromLatin1(b));
    auto ch = [&](int x, int y) { return qRound(x + (y - x) * t); };
    return QColor(qBound(0, ch(ca.red(), cb.red()), 255),
                  qBound(0, ch(ca.green(), cb.green()), 255),
                  qBound(0, ch(ca.blue(), cb.blue()), 255))
        .name();
}
// 附加透明度，输出 rgba(r,g,b,a)
inline QString withAlpha(const char *hex, qreal alpha)
{
    QColor c(QString::fromLatin1(hex));
    return QStringLiteral("rgba(%1,%2,%3,%4)")
        .arg(c.red())
        .arg(c.green())
        .arg(c.blue())
        .arg(QString::number(qBound(0.0, alpha, 1.0), 'f', 2));
}

// ---------------------------------------------------------------- //
// 界面效果辅助：玻璃材质 / 投影阴影 / 入场淡入
// 全部受 gShadowLevel / gGlassLevel / gFxAnimations 控制；
// 关闭时退化为纯色 / 无效果，便于对比与省性能。
// ---------------------------------------------------------------- //

// 阴影规格：按强度档位返回模糊半径 / 垂直偏移 / 透明度
struct ShadowSpec {
    int blur;
    int dy;
    int alpha;
};
inline ShadowSpec shadowSpec(int level)
{
    switch (level) {
    case 1: return {12, 2, 80};    // 弱
    case 2: return {20, 4, 130};   // 中
    case 3: return {32, 6, 180};   // 强
    default: return {0, 0, 0};      // 关
    }
}
inline bool shadowsEnabled() { return gShadowLevel > 0; }
inline bool glassEnabled() { return gGlassLevel > 0; }

// 玻璃面板背景：半透明底色 + 顶部高光渐变，模拟毛玻璃质感。
// level 0 返回纯色；level 越高透明度越低（越透）、高光越强。
// 深色主题顶部微亮、底部微暗；浅色主题相反。
inline QString glassBg(const char *hex, int level = -1)
{
    if (level < 0)
        level = gGlassLevel;
    if (level <= 0)
        return QString::fromLatin1(hex);
    const qreal alpha = level == 1 ? 0.86 : level == 2 ? 0.74 : 0.60;
    const qreal d = (gTheme && gTheme->light) ? -1.0 : 1.0;
    const QString base = withAlpha(hex, alpha);
    // 顶部高光：比底色更亮的半透明层，模拟玻璃上沿反光
    const QString hi = withAlpha(shade(hex, 0.10 * d).toUtf8().constData(), alpha * 0.55);
    // 底部微暗：增加层次
    const QString lo = withAlpha(shade(hex, -0.03 * d).toUtf8().constData(), alpha);
    return QStringLiteral(
        "qlineargradient(x1:0,y1:0,x2:0,y2:1, stop:0 %1, stop:0.10 %2, stop:0.85 %2, stop:1 %3)")
        .arg(hi, base, lo);
}

// 玻璃边框颜色：半透明亮色描边，模拟玻璃边缘反光。
// level 0 返回普通边框色。
inline QString glassBorder(int level = -1)
{
    if (level < 0)
        level = gGlassLevel;
    if (level <= 0)
        return QString::fromLatin1(kColorBorder);
    const qreal a = level == 1 ? 0.14 : level == 2 ? 0.22 : 0.32;
    const qreal d = (gTheme && gTheme->light) ? -1.0 : 1.0;
    // 深色主题用白色半透明高光边；浅色主题用深色半透明边
    const char *edge = (gTheme && gTheme->light) ? "#000000" : "#ffffff";
    return withAlpha(edge, a * (d > 0 ? 1.0 : 0.6));
}

// 兼容旧名：materialBg 现在等同于 glassBg（保留以便旧代码编译）
inline QString materialBg(const char *hex, qreal /*tint*/ = 0.0)
{
    return glassBg(hex, gGlassLevel);
}

// 创建并附加投影阴影（受全局阴影强度控制；关闭时返回 nullptr 且不附加）。
// level < 0 时使用全局 gShadowLevel。
inline QGraphicsDropShadowEffect *makeDropShadow(QWidget *w, int level = -1, int dx = 0)
{
    if (level < 0)
        level = gShadowLevel;
    if (level <= 0 || !w)
        return nullptr;
    const ShadowSpec s = shadowSpec(level);
    auto *e = new QGraphicsDropShadowEffect(w);
    e->setBlurRadius(si(s.blur));
    e->setOffset(si(dx), si(s.dy));
    e->setColor(QColor(0, 0, 0, s.alpha));
    w->setGraphicsEffect(e);
    return e;
}

// 移除某个投影阴影（用于运行时开关）。
// 注意：setGraphicsEffect(nullptr) 会同步删除原效果，此处切勿再对 effect 做 deleteLater
inline void clearDropShadow(QWidget *w, QGraphicsDropShadowEffect *&effect)
{
    if (w && effect)
        w->setGraphicsEffect(nullptr); // 同步删除，指针随即失效
    effect = nullptr;
}

// 入场淡入（受全局动画开关控制；关闭时不做任何处理）
inline void fadeInWidget(QWidget *w, int duration = 200)
{
    if (!gFxAnimations || !w)
        return;
    auto *effect = new QGraphicsOpacityEffect(w);
    effect->setOpacity(0.0);
    w->setGraphicsEffect(effect);
    auto *anim = new QPropertyAnimation(effect, "opacity", w);
    anim->setDuration(duration);
    anim->setStartValue(0.0);
    anim->setEndValue(1.0);
    anim->setEasingCurve(QEasingCurve::OutCubic);
    anim->start(QAbstractAnimation::DeleteWhenStopped);
}

inline const Theme *findTheme(const QString &id)
{
    for (const Theme &t : kThemes)
        if (QLatin1String(t.id) == id)
            return &t;
    return &kThemes[0];
}

// ---------------------------------------------------------------- //
// 全局 QSS 生成（美化版）
// ---------------------------------------------------------------- //
inline void substIn(QString &s, const char *token, const QString &val)
{
    s.replace(QStringLiteral("@") + QLatin1String(token) + QStringLiteral("@"), val);
}

inline QString themeQss(const Theme &t)
{
    const qreal d = t.light ? -1.0 : 1.0;
    const QString hover = shade(t.bgElev2, 0.06 * d);
    const QString pressed = shade(t.bgElev2, -0.05 * d);
    const QString muted2 = mix(t.fgMuted, t.bg, 0.45);
    const QString fgSoft = mix(t.fg, t.fgMuted, 0.55);
    const QString rowAlt = shade(t.bg, 0.025 * d);
    const QString chartBg = shade(t.bg, 0.012 * d);
    const QString axis = shade(t.border, 0.06 * d);
    const QString navSel = withAlpha(t.accent, t.light ? 0.12 : 0.16);
    const QString navSelText = t.light ? QString::fromLatin1(t.accent) : QStringLiteral("white");
    const QString listSel = withAlpha(t.accent, t.light ? 0.10 : 0.14);
    const QString dangerA = withAlpha(t.danger, 0.12);
    const QString scrollHover = shade(t.bgElev2, 0.10 * d);

    QString q = QStringLiteral(R"(
        * {
            font-family: "Segoe UI", "Microsoft YaHei", sans-serif;
            font-size: 13px;
            color: @FG@;
        }
        QMainWindow, QWidget { background: @BG@; }
        QToolTip { background: @BGL2@; color: @FG@; border: 1px solid @BORDER@; border-radius: 6px; padding: 4px 8px; }
        QDialog { background: @BG@; }
        QGroupBox { border: 1px solid @BORDER@; border-radius: 8px; margin-top: 12px; padding-top: 10px; }
        QGroupBox::title { subcontrol-origin: margin; left: 10px; padding: 0 6px; color: @MUTED@; }
        QWidget#NavSidebar {
            background: @BGEL@;
            border-right: 1px solid @BORDER@;
        }
        QPushButton {
            background: @BGL2@; border: 1px solid @BORDER@; border-radius: 6px;
            padding: 6px 14px; outline: none;
        }
        QPushButton:hover { background: @HOVER@; border-color: @ACCENT@; }
        QPushButton:pressed { background: @PRESSED@; }
        QPushButton:focus { border-color: @ACCENT@; }
        QPushButton:disabled { color: @MUTED@; background: @BGEL@; border-color: @BORDER@; }
        QPushButton#PrimaryBtn {
            background: qlineargradient(x1:0,y1:0,x2:0,y2:1, stop:0 @ACCENT@, stop:1 @ACCENTH@);
            border: none; color: white; font-weight: 600; padding: 7px 16px;
        }
        QPushButton#PrimaryBtn:hover {
            background: qlineargradient(x1:0,y1:0,x2:0,y2:1, stop:0 @ACCENTH@, stop:1 @ACCENT@);
        }
        QPushButton#PrimaryBtn:pressed { background: @ACCENTH@; }
        QPushButton#DangerBtn { background: transparent; border: 1px solid @DANGER@; color: @DANGER@; }
        QPushButton#DangerBtn:hover { background: @DANGERA@; }
        QPushButton#NavBtn {
            text-align: left; padding: 8px 12px; border: none; border-radius: 0;
            background: transparent; color: @MUTED@; font-size: 14px;
        }
        QPushButton#NavBtn:hover { background: @BGL2@; color: @FG@; }
        QPushButton#NavBtn:checked {
            background: @NAVSEL@; color: @NAVSELTXT@;
            border-left: 3px solid @ACCENT@;
        }
        QToolButton#NavSection {
            text-align: left; color: @MUTED2@; font-size: 10px; font-weight: 700;
            letter-spacing: 0.5px; padding: 6px 12px 4px;
            border: none; background: transparent;
        }
        QToolButton#NavSection:hover { color: @FGSOFT@; }
        QToolButton#NavSection:checked { background: transparent; }
        QLineEdit, QTextEdit, QPlainTextEdit, QComboBox, QSpinBox {
            background: @BGEL@; border: 1px solid @BORDER@; border-radius: 6px;
            padding: 6px 10px; selection-background-color: @ACCENT@; selection-color: white;
        }
        QLineEdit:focus, QTextEdit:focus, QPlainTextEdit:focus, QComboBox:focus, QSpinBox:focus {
            border-color: @ACCENT@; background: @BGL2@;
        }
        QComboBox::drop-down { border: none; width: 22px; }
        QComboBox QAbstractItemView {
            background: @BGL2@; border: 1px solid @BORDER@; border-radius: 6px;
            selection-background-color: @ACCENT@; selection-color: white; padding: 4px;
        }
        QListWidget, QTableWidget, QTreeWidget {
            background: @BG@; border: 1px solid @BORDER@; border-radius: 8px; outline: none;
            alternate-background-color: @ROWALT@;
        }
        QListWidget::item { border: none; border-bottom: 1px solid @BORDER@; padding: 10px 12px; }
        QListWidget::item:hover { background: @LISTSEL@; }
        QListWidget::item:selected { background: @LISTSEL@; }
        QTableWidget::item, QTreeWidget::item { padding: 4px 8px; }
        QTableWidget::item:selected, QTreeWidget::item:selected { background: @LISTSEL@; }
        QHeaderView::section {
            background: @BGEL@; border: none; border-bottom: 1px solid @BORDER@;
            padding: 8px; font-weight: 600; color: @MUTED@;
        }
        QScrollBar:vertical { background: transparent; width: 12px; margin: 0; }
        QScrollBar::handle:vertical { background: @BGL2@; border-radius: 6px; min-height: 30px; margin: 2px; }
        QScrollBar::handle:vertical:hover { background: @SCROLLH@; }
        QScrollBar:horizontal { background: transparent; height: 12px; margin: 0; }
        QScrollBar::handle:horizontal { background: @BGL2@; border-radius: 6px; min-width: 30px; margin: 2px; }
        QScrollBar::handle:horizontal:hover { background: @SCROLLH@; }
        QScrollBar::add-line, QScrollBar::sub-line { width: 0; height: 0; }
        QScrollBar::add-page, QScrollBar::sub-page { background: transparent; }
        QStatusBar { background: @BGEL@; color: @MUTED@; border-top: 1px solid @BORDER@; }
        QCheckBox { spacing: 6px; }
        QCheckBox::indicator { width: 16px; height: 16px; border: 1px solid @BORDER@; border-radius: 4px; background: @BGEL@; }
        QCheckBox::indicator:hover { border-color: @ACCENT@; }
        QCheckBox::indicator:checked { background: @ACCENT@; border-color: @ACCENT@; }
        QRadioButton { spacing: 6px; }
        QRadioButton::indicator { width: 15px; height: 15px; border: 1px solid @BORDER@; border-radius: 8px; background: @BGEL@; }
        QRadioButton::indicator:hover { border-color: @ACCENT@; }
        QRadioButton::indicator:checked { background: @ACCENT@; border-color: @ACCENT@; }
        QSplitter::handle { background: @BORDER@; }
        QSplitter::handle:hover { background: @ACCENT@; }
        QMenu { background: @BGL2@; border: 1px solid @BORDER@; border-radius: 8px; padding: 4px; }
        QMenu::item { padding: 6px 24px 6px 12px; border-radius: 5px; }
        QMenu::item:selected { background: @ACCENT@; color: white; }
        QMenu::separator { height: 1px; background: @BORDER@; margin: 4px 8px; }
    )");

    substIn(q, "FG", t.fg);
    substIn(q, "BG", t.bg);
    substIn(q, "BGEL", t.bgElev);
    substIn(q, "BGL2", t.bgElev2);
    substIn(q, "BORDER", t.border);
    substIn(q, "MUTED", t.fgMuted);
    substIn(q, "ACCENT", t.accent);
    substIn(q, "ACCENTH", t.accentHover);
    substIn(q, "DANGER", t.danger);
    substIn(q, "HOVER", hover);
    substIn(q, "PRESSED", pressed);
    substIn(q, "MUTED2", muted2);
    substIn(q, "FGSOFT", fgSoft);
    substIn(q, "ROWALT", rowAlt);
    substIn(q, "NAVSEL", navSel);
    substIn(q, "NAVSELTXT", navSelText);
    substIn(q, "LISTSEL", listSel);
    substIn(q, "SCROLLH", scrollHover);
    substIn(q, "DANGERA", dangerA);
    Q_UNUSED(chartBg);
    Q_UNUSED(axis);
    return q;
}

// 应用主题：更新全部语义色全局变量 + 重新生成全局 QSS
inline void applyThemeColors(const Theme &t)
{
    kColorBg = t.bg;
    kColorBgElev = t.bgElev;
    kColorBgElev2 = t.bgElev2;
    kColorBorder = t.border;
    kColorFg = t.fg;
    kColorFgMuted = t.fgMuted;
    kColorAccent = t.accent;
    kColorAccentHover = t.accentHover;
    kColorDanger = t.danger;
    kColorOk = t.ok;
    kColorWarn = t.warn;
    kColorTagBg = t.tagBg;
    kColorTagFg = t.tagFg;

    const qreal d = t.light ? -1.0 : 1.0;
    kColorHover = shade(t.bgElev2, 0.06 * d);
    kColorPressed = shade(t.bgElev2, -0.05 * d);
    kColorMuted2 = mix(t.fgMuted, t.bg, 0.45);
    kColorFgSoft = mix(t.fg, t.fgMuted, 0.55);
    kColorRowAlt = shade(t.bg, 0.025 * d);
    kColorChartBg = shade(t.bg, 0.012 * d);
    kColorAxis = shade(t.border, 0.06 * d);
    kColorNavSel = withAlpha(t.accent, t.light ? 0.12 : 0.16);
    kColorListSel = withAlpha(t.accent, t.light ? 0.10 : 0.14);

    gGlobalQss = themeQss(t);
}

// ---------------------------------------------------------------- //
// emoji 程序图标：多尺寸、纯代码、无外部资源文件。
// badge 控制背景：None=透明(纯 emoji)；Gradient=原渐变圆角徽章；WhiteCircle=白色圆形底 + emoji。
// 默认 WhiteCircle：白色圆形背景 + emoji（flomo 风格，干净、托盘辨识度高）。
// ---------------------------------------------------------------- //
enum class IconBadge { None, Gradient, WhiteCircle };

inline QIcon makeEmojiIcon(const QString &emoji,
                           const QColor &c = Qt::white,
                           IconBadge badge = IconBadge::WhiteCircle)
{
    QIcon icon;
    const int sizes[] = {256, 128, 64, 48, 32, 16};
    for (const int px : sizes) {
        QPixmap pm(px, px);
        pm.fill(Qt::transparent);
        QPainter p(&pm);
        p.setRenderHint(QPainter::Antialiasing);

        if (badge != IconBadge::None) {
            QPainterPath path;
            path.addEllipse(0, 0, px, px);
            if (badge == IconBadge::Gradient) {
                QLinearGradient g(0, 0, 0, px);
                g.setColorAt(0, c.lighter(115));
                g.setColorAt(1, c.darker(140));
                p.fillPath(path, g);
                p.setBrush(Qt::NoBrush);
                p.setPen(QPen(QColor(255, 255, 255, 46), qMax(1, px / 80)));
                p.drawPath(path);
            } else { // WhiteCircle
                p.setBrush(Qt::white);
                p.setPen(Qt::NoPen);
                p.drawPath(path);
                // 极淡描边：浅色 / 白色任务栏上也能看出圆形轮廓（白圆本身会融进白底）
                p.setPen(QPen(QColor(0, 0, 0, 26), qMax(1, px / 64)));
                p.drawPath(path);
            }
        }

        QFont f;
        f.setPixelSize(qMax(8, int(px * 0.55)));
#ifdef Q_OS_WIN
        f.setFamilies({QStringLiteral("Segoe UI Emoji"), QStringLiteral("Segoe UI Symbol")});
#endif
        p.setFont(f);
        p.drawText(pm.rect(), Qt::AlignCenter, emoji);
        p.end();
        icon.addPixmap(pm);
    }
    return icon;
}

} // namespace awqtui
