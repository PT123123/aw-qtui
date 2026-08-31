// config.cpp —— 设备身份生成/持久化
#include "config.h"

#include <QDir>
#include <QFile>
#include <QStandardPaths>
#include <QSysInfo>
#include <QTextStream>

#include <cstdio>

#ifdef Q_OS_WIN
#include <winsock2.h> // 必须先于 windows.h（AF_UNSPEC / GetAdaptersAddresses 依赖）
#include <windows.h>
#include <iphlpapi.h>
#pragma comment(lib, "iphlpapi.lib")
#endif

namespace awqtui {

static QString macBasedId()
{
#ifdef Q_OS_WIN
    ULONG size = 0;
    if (GetAdaptersAddresses(AF_UNSPEC, 0, nullptr, nullptr, &size) == ERROR_BUFFER_OVERFLOW && size > 0) {
        QByteArray buf(int(size), 0);
        auto *adapters = reinterpret_cast<PIP_ADAPTER_ADDRESSES>(buf.data());
        if (GetAdaptersAddresses(AF_UNSPEC, 0, nullptr, adapters, &size) == NO_ERROR) {
            for (auto *p = adapters; p; p = p->Next) {
                if (p->IfType == IF_TYPE_SOFTWARE_LOOPBACK || p->IfType == IF_TYPE_TUNNEL)
                    continue;
                if (p->PhysicalAddressLength >= 6) {
                    QString hex;
                    for (ULONG i = 0; i < 6; ++i)
                        hex += QString::asprintf("%02x", p->PhysicalAddress[i]);
                    return hex;
                }
            }
        }
    }
    return QString();
#else
    return QString();
#endif
}

static QString randomId()
{
    QString hex;
    for (int i = 0; i < 12; ++i) {
        const int v = std::rand() % 16; // NOLINT
        hex += QChar(v < 10 ? '0' + v : 'a' + v - 10);
    }
    return hex;
}

QString deviceId()
{
    // 环境变量可覆盖，便于多实例联调
    if (qEnvironmentVariableIsSet("AW_QTUI_DEVICE_ID"))
        return qEnvironmentVariable("AW_QTUI_DEVICE_ID");

    const QString dir = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    if (!dir.isEmpty())
        QDir().mkpath(dir);
    const QString path = dir + QStringLiteral("/device_id");

    QFile f(path);
    if (f.open(QIODevice::ReadOnly | QIODevice::Text)) {
        const QString id = QString::fromUtf8(f.readAll()).trimmed();
        f.close();
        if (!id.isEmpty())
            return id;
    }
    QString id = macBasedId();
    if (id.isEmpty())
        id = randomId();
    if (f.open(QIODevice::WriteOnly | QIODevice::Text)) {
        f.write(id.toUtf8());
        f.close();
    }
    return id;
}

QString hostname()
{
    return QSysInfo::machineHostName().isEmpty() ? QStringLiteral("unknown") : QSysInfo::machineHostName();
}

QString platform()
{
#ifdef Q_OS_WIN
    return QStringLiteral("windows");
#elif defined(Q_OS_MACOS)
    return QStringLiteral("darwin");
#elif defined(Q_OS_LINUX)
    return QStringLiteral("linux");
#else
    return QStringLiteral("unknown");
#endif
}

} // namespace awqtui
