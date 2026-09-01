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

// 读取主题 ID（默认 "midnight" 暗夜蓝）
QString loadThemeId();
// 持久化主题 ID
void saveThemeId(const QString &id);

// 界面效果配置（阴影 / 玻璃 / 动画 / DWM 背景），供「设置 → 界面效果」控制
struct UiEffects {
    // 阴影强度：0=关, 1=弱, 2=中, 3=强
    int shadowLevel = 2;
    // 玻璃材质强度：0=关(纯色), 1=弱, 2=中, 3=强（半透明 + 顶部高光 + 亮边）
    int glassLevel = 2;
    // 动画：切页淡入 / 卡片入场 / 高亮过渡
    bool animations = true;
    // Windows DWM 系统背景（Mica / Acrylic 真模糊），仅 Win11 22H2+ 有效，失败静默回退
    bool dwmBackdrop = false;

    // 效果预设
    enum Preset {
        Custom = -1,
        Refined = 0,    // 精致玻璃：强阴影 + 强玻璃 + 动画 + DWM
        Standard = 1,   // 标准：中阴影 + 中玻璃 + 动画
        Minimal = 2,    // 简约：弱阴影 + 弱玻璃 + 无动画
        Performance = 3 // 性能优先：全关
    };

    // 从预设生成配置
    static UiEffects fromPreset(Preset p);
    // 当前配置匹配哪个预设（不匹配返回 Custom）
    Preset preset() const;

    bool operator==(const UiEffects &o) const
    {
        return shadowLevel == o.shadowLevel && glassLevel == o.glassLevel
               && animations == o.animations && dwmBackdrop == o.dwmBackdrop;
    }
    bool operator!=(const UiEffects &o) const { return !(*this == o); }
};

// 读取界面效果配置（首次运行时默认 Standard）
UiEffects loadUiEffects();
// 持久化界面效果配置
void saveUiEffects(const UiEffects &e);

// 本地服务端自动管理（探测->拉起->看护），默认开启
bool loadServerAutoManage();
void saveServerAutoManage(bool on);
// 开机自启（Task Scheduler ONLOGON），默认开启
bool loadServerAutostart();
void saveServerAutostart(bool on);

} // namespace awqtui
