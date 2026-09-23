// autostart.cpp —— 应用本体开机自启（HKCU Run）
//
// 为什么走 HKCU Run：
//   - 无需 admin（HKCU 可写，而计划任务在受限/非提升上下文里创建会失败）；
//   - 任意会话上下文都生效，且能被「任务管理器 → 启动」统一管理（用户看得见、关得掉）；
//   - 与服务端自启（awserver.cpp）同一套机制，少一套行为差异。
//
// 注意：值里存的是**绝对路径**。发布目录每版一个（c:/workshop/aw-qtui-<ver>），
// 所以 main.cpp 每次启动都会幂等重写一次，否则自启会停在被删掉的旧目录上。
#include "autostart.h"

#include "appsettings.h"

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QSettings>

namespace awqtui {

namespace {

const char *kRunKeyPath = "HKEY_CURRENT_USER\\Software\\Microsoft\\Windows\\CurrentVersion\\Run";
const char *kValueName = "aw-qtui";
const char *kValueNameEnv = "AWQTUI_AUTOSTART_VALUE"; // 测试钩子，见 autostart.h

} // namespace

QString appAutostartValueName()
{
    const QByteArray over = qgetenv(kValueNameEnv);
    return over.isEmpty() ? QString::fromLatin1(kValueName) : QString::fromLocal8Bit(over);
}

QString appAutostartCommand()
{
    // 原生分隔符：注册表里存的是 Windows 路径，统一用 \ 更可读，也免去别的工具解析歧义
    const QString exe = QDir::toNativeSeparators(QCoreApplication::applicationFilePath());
    QString cmd = QStringLiteral("\"%1\"").arg(exe);
    if (loadAppAutostartHidden())
        cmd += QStringLiteral(" --hidden");
    return cmd;
}

bool appAutostartRegistered()
{
    QSettings s(QString::fromLatin1(kRunKeyPath), QSettings::NativeFormat);
    return s.contains(appAutostartValueName());
}

bool setAppAutostart(bool on, QString *error)
{
    QSettings s(QString::fromLatin1(kRunKeyPath), QSettings::NativeFormat);
    const QString name = appAutostartValueName();

    if (on) {
        const QString exe = QCoreApplication::applicationFilePath();
        if (exe.isEmpty() || !QFileInfo::exists(exe)) {
            if (error)
                *error = QStringLiteral("找不到当前程序路径（%1）").arg(exe);
            return false;
        }
        s.setValue(name, appAutostartCommand());
    } else {
        s.remove(name);
    }
    s.sync();

    if (s.status() != QSettings::NoError) {
        if (error)
            *error = QStringLiteral("注册表写入被拒绝（错误码 %1）").arg(int(s.status()));
        qWarning() << "[autostart] HKCU Run 写入失败:" << name;
        return false;
    }
    qInfo().noquote() << "[autostart]" << (on ? "已注册开机自启:" : "已移除开机自启:")
                      << (on ? appAutostartCommand() : name);
    return true;
}

bool syncAppAutostartOnStartup()
{
    const QString name = appAutostartValueName();
    QSettings s(QString::fromLatin1(kRunKeyPath), QSettings::NativeFormat);
    const bool registered = s.contains(name);
    const QString current = registered ? s.value(name).toString() : QString();

    if (loadAppAutostart()) {
        // 已经是当前 exe + 当前偏好：不用再写（同一发布目录下反复启动走的就是这条路）
        if (registered && current == appAutostartCommand())
            return false;
        QString err;
        if (!setAppAutostart(true, &err)) {
            qWarning().noquote() << "[autostart] 开机自启注册失败：" << err;
            return false;
        }
        return true;
    }

    // 意图=关：清掉残留（正常情况下取消勾选时就已经删掉了，这里兜底）
    if (!registered)
        return false;
    return setAppAutostart(false);
}

} // namespace awqtui
