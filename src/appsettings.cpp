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

} // namespace awqtui
