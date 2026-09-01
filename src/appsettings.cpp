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

} // namespace awqtui
