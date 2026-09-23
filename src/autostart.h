// autostart.h —— 应用本体的开机自启（HKCU Run 注册表项）
//
// 与 ServerLauncher 的「服务端自启」是两件事，各用各的注册表值名，互不覆盖：
//   aw-qtui-server —— 让本地 aw-server 随登录启动（不需要本应用在跑）
//   aw-qtui        —— 让 aw-qtui 本体随登录启动（常驻托盘，可选择不弹主窗口）
#pragma once

#include <QString>

namespace awqtui {

// HKCU Run 下的值名。测试钩子：环境变量 AWQTUI_AUTOSTART_VALUE 可覆盖，
// 探针用它写到自己的值名上，避免污染用户真实的自启项。
QString appAutostartValueName();

// 将要写入注册表的命令行：当前 exe 绝对路径（引号包裹）+ 可选的 --hidden
QString appAutostartCommand();

// 注册表里当前是否有本应用的项（只读注册表，不看 ini）
bool appAutostartRegistered();

// 开 / 关自启：写或删注册表项。开启时按当前 exe 路径幂等重写（发布目录每版一变，
// 不重写就会永远指向旧版本）。失败时返回 false，并把原因写进 error。
bool setAppAutostart(bool on, QString *error = nullptr);

// 启动时把注册表同步到 ini 里的意图（main.cpp 调）：
//   意图=开 → 按当前 exe 路径幂等重写；值已经就是当前 exe 的话不重复写；
//   意图=关 → 若注册表里还残留本应用的项就清掉，让「关」是真的关。
// 返回 true 表示这次确实改动了注册表（测试用它断言「零副作用」模式没动过）。
bool syncAppAutostartOnStartup();

} // namespace awqtui
