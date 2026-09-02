// appsettings.cpp —— 快捷键配置读写
#include "appsettings.h"

#include <QDir>
#include <QSettings>
#include <QStandardPaths>
#include <QtGlobal>

namespace awqtui {

QString settingsFilePath()
{
    const QString dir = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    if (!dir.isEmpty())
        QDir().mkpath(dir);
    return dir + QStringLiteral("/awqtui.ini");
}

ShortcutConfig loadShortcuts()
{
    ShortcutConfig cfg;
    // 默认值
    cfg.addNote = QKeySequence(QStringLiteral("Alt+N"));
    cfg.showInbox = QKeySequence(QStringLiteral("Alt+M"));

    QSettings s(settingsFilePath(), QSettings::IniFormat);
    const QString add = s.value(QStringLiteral("hotkey/addNote")).toString();
    const QString inbox = s.value(QStringLiteral("hotkey/showInbox")).toString();
    if (!add.isEmpty())
        cfg.addNote = QKeySequence::fromString(add, QKeySequence::PortableText);
    if (!inbox.isEmpty())
        cfg.showInbox = QKeySequence::fromString(inbox, QKeySequence::PortableText);
    return cfg;
}

void saveShortcuts(const ShortcutConfig &c)
{
    QSettings s(settingsFilePath(), QSettings::IniFormat);
    s.setValue(QStringLiteral("hotkey/addNote"), c.addNote.toString(QKeySequence::PortableText));
    s.setValue(QStringLiteral("hotkey/showInbox"), c.showInbox.toString(QKeySequence::PortableText));
    s.sync();
}

double loadUiZoom()
{
    QSettings s(settingsFilePath(), QSettings::IniFormat);
    bool ok = false;
    const double v = s.value(QStringLiteral("ui/zoom"), 1.0).toDouble(&ok);
    return (ok && v > 0.0) ? qBound(0.3, v, 3.0) : 1.0;
}

void saveUiZoom(double zoom)
{
    QSettings s(settingsFilePath(), QSettings::IniFormat);
    s.setValue(QStringLiteral("ui/zoom"), zoom);
    s.sync();
}

bool loadNavCollapsed()
{
    QSettings s(settingsFilePath(), QSettings::IniFormat);
    return s.value(QStringLiteral("ui/navCollapsed"), true).toBool();
}

void saveNavCollapsed(bool collapsed)
{
    QSettings s(settingsFilePath(), QSettings::IniFormat);
    s.setValue(QStringLiteral("ui/navCollapsed"), collapsed);
    s.sync();
}

QString loadThemeId()
{
    QSettings s(settingsFilePath(), QSettings::IniFormat);
    return s.value(QStringLiteral("ui/theme"), QStringLiteral("midnight")).toString();
}

void saveThemeId(const QString &id)
{
    QSettings s(settingsFilePath(), QSettings::IniFormat);
    s.setValue(QStringLiteral("ui/theme"), id);
    s.sync();
}

UiEffects UiEffects::fromPreset(Preset p)
{
    UiEffects e;
    switch (p) {
    case Refined:
        e.shadowLevel = 3;
        e.glassLevel = 3;
        e.animations = true;
        e.dwmBackdrop = true;
        break;
    case Standard:
        e.shadowLevel = 2;
        e.glassLevel = 2;
        e.animations = true;
        e.dwmBackdrop = false;
        break;
    case Minimal:
        e.shadowLevel = 1;
        e.glassLevel = 1;
        e.animations = false;
        e.dwmBackdrop = false;
        break;
    case Performance:
        e.shadowLevel = 0;
        e.glassLevel = 0;
        e.animations = false;
        e.dwmBackdrop = false;
        break;
    default:
        break;
    }
    return e;
}

UiEffects::Preset UiEffects::preset() const
{
    const UiEffects refined = fromPreset(Refined);
    const UiEffects standard = fromPreset(Standard);
    const UiEffects minimal = fromPreset(Minimal);
    const UiEffects performance = fromPreset(Performance);
    if (*this == refined)
        return Refined;
    if (*this == standard)
        return Standard;
    if (*this == minimal)
        return Minimal;
    if (*this == performance)
        return Performance;
    return Custom;
}

UiEffects loadUiEffects()
{
    QSettings s(settingsFilePath(), QSettings::IniFormat);
    UiEffects e;
    // 兼容旧版布尔字段：shadows/material → 强度档位
    const bool oldShadows = s.value(QStringLiteral("ui/shadows"), true).toBool();
    const bool oldMaterial = s.value(QStringLiteral("ui/material"), true).toBool();
    e.shadowLevel = s.value(QStringLiteral("ui/shadowLevel"), oldShadows ? 2 : 0).toInt();
    e.glassLevel = s.value(QStringLiteral("ui/glassLevel"), oldMaterial ? 2 : 0).toInt();
    e.animations = s.value(QStringLiteral("ui/animations"), true).toBool();
    e.dwmBackdrop = s.value(QStringLiteral("ui/dwmBackdrop"), false).toBool();
    // 边缘修复开关：缺省 = 开启（采用修复后的观感；关掉即恢复旧行为，便于 A/B 对比）
    e.fixEdgeLowContrast = s.value(QStringLiteral("ui/fixEdgeLowContrast"), true).toBool();
    e.fixGlassOpaque = s.value(QStringLiteral("ui/fixGlassOpaque"), true).toBool();
    e.fixSnapZoom = s.value(QStringLiteral("ui/fixSnapZoom"), true).toBool();
    e.fixShadowAdaptive = s.value(QStringLiteral("ui/fixShadowAdaptive"), true).toBool();
    e.shadowLevel = qBound(0, e.shadowLevel, 3);
    e.glassLevel = qBound(0, e.glassLevel, 3);
    return e;
}

void saveUiEffects(const UiEffects &e)
{
    QSettings s(settingsFilePath(), QSettings::IniFormat);
    s.setValue(QStringLiteral("ui/shadowLevel"), e.shadowLevel);
    s.setValue(QStringLiteral("ui/glassLevel"), e.glassLevel);
    s.setValue(QStringLiteral("ui/animations"), e.animations);
    s.setValue(QStringLiteral("ui/dwmBackdrop"), e.dwmBackdrop);
    s.setValue(QStringLiteral("ui/fixEdgeLowContrast"), e.fixEdgeLowContrast);
    s.setValue(QStringLiteral("ui/fixGlassOpaque"), e.fixGlassOpaque);
    s.setValue(QStringLiteral("ui/fixSnapZoom"), e.fixSnapZoom);
    s.setValue(QStringLiteral("ui/fixShadowAdaptive"), e.fixShadowAdaptive);
    // 清理旧版字段
    s.remove(QStringLiteral("ui/shadows"));
    s.remove(QStringLiteral("ui/material"));
    s.sync();
}

bool loadServerAutoManage()
{
    QSettings s(settingsFilePath(), QSettings::IniFormat);
    return s.value(QStringLiteral("server/autoManage"), true).toBool();
}

void saveServerAutoManage(bool on)
{
    QSettings s(settingsFilePath(), QSettings::IniFormat);
    s.setValue(QStringLiteral("server/autoManage"), on);
    s.sync();
}

bool loadServerAutostart()
{
    QSettings s(settingsFilePath(), QSettings::IniFormat);
    return s.value(QStringLiteral("server/autostart"), true).toBool();
}

void saveServerAutostart(bool on)
{
    QSettings s(settingsFilePath(), QSettings::IniFormat);
    s.setValue(QStringLiteral("server/autostart"), on);
    s.sync();
}

FocusModules loadFocusModules()
{
    FocusModules m;
    QSettings s(settingsFilePath(), QSettings::IniFormat);
    m.timer = s.value(QStringLiteral("todo/mod_timer"), true).toBool();
    m.overview = s.value(QStringLiteral("todo/mod_overview"), true).toBool();
    m.detail = s.value(QStringLiteral("todo/mod_detail"), true).toBool();
    m.week = s.value(QStringLiteral("todo/mod_week"), true).toBool();
    m.heatmap = s.value(QStringLiteral("todo/mod_heatmap"), true).toBool();
    m.best = s.value(QStringLiteral("todo/mod_best"), true).toBool();
    m.calendar = s.value(QStringLiteral("todo/mod_calendar"), true).toBool();
    m.memorial = s.value(QStringLiteral("todo/mod_memorial"), true).toBool();
    return m;
}

void saveFocusModules(const FocusModules &m)
{
    QSettings s(settingsFilePath(), QSettings::IniFormat);
    s.setValue(QStringLiteral("todo/mod_timer"), m.timer);
    s.setValue(QStringLiteral("todo/mod_overview"), m.overview);
    s.setValue(QStringLiteral("todo/mod_detail"), m.detail);
    s.setValue(QStringLiteral("todo/mod_week"), m.week);
    s.setValue(QStringLiteral("todo/mod_heatmap"), m.heatmap);
    s.setValue(QStringLiteral("todo/mod_best"), m.best);
    s.setValue(QStringLiteral("todo/mod_calendar"), m.calendar);
    s.setValue(QStringLiteral("todo/mod_memorial"), m.memorial);
    s.sync();
}

} // namespace awqtui
