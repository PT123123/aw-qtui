// appsettings.h —— 应用设置：全局快捷键配置（读写 %APPDATA%\aw-qtui\aw-qtui\awqtui.ini）
#pragma once

#include <QKeySequence>
#include <QString>

namespace awqtui {

// 全局热键 ID（WM_HOTKEY 的 wParam），同时作为注册表中的唯一标识
constexpr int kHotkeyAddNoteId = 1;   // 添加记录
constexpr int kHotkeyShowInboxId = 2; // 唤醒并跳转收件箱

// 快捷键配置。为空（QKeySequence()）表示该动作未绑定全局热键。
struct ShortcutConfig {
    QKeySequence addNote;   // 默认 Alt+N
    QKeySequence showInbox; // 默认 Alt+M
};

// 配置文件路径：%APPDATA%\aw-qtui\aw-qtui\awqtui.ini（与本地数据目录一致）
QString settingsFilePath();

// 读取快捷键配置（首次运行时返回默认值 Alt+N / Alt+M）
ShortcutConfig loadShortcuts();
// 持久化快捷键配置
void saveShortcuts(const ShortcutConfig &c);

// 读取页面缩放比（0.3~3.0，默认 1.0）
double loadUiZoom();
// 持久化页面缩放比
void saveUiZoom(double zoom);

} // namespace awqtui
