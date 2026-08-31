// config.h —— 全局配置：服务端地址与设备身份（对齐 aw-inbox Rust 服务端）
#pragma once

#include <QString>

namespace awqtui {

// 服务端默认地址（aw-inbox 监听 0.0.0.0:5600）
inline const QString kDefaultServerUrl = QStringLiteral("http://127.0.0.1:5600");
// mDNS 服务类型（对齐 aw-sync-transport/src/discovery.rs）
inline const QString kMdnsServiceType = QStringLiteral("_activitywatch._tcp.local.");
inline const QString kAppName = QStringLiteral("aw-qtui");
inline const QString kAppVersion = QStringLiteral("0.1.0");

// 稳定的设备 ID：优先读持久化文件，否则按 MAC 生成并缓存
QString deviceId();
// 主机名
QString hostname();
// 平台名 windows/linux/darwin
QString platform();

} // namespace awqtui
