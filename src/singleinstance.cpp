// singleinstance.cpp —— 跨版本单实例仲裁实现（设计说明见 singleinstance.h）
#include "singleinstance.h"
#include "config.h"

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDebug>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLocalServer>
#include <QLocalSocket>
#include <QLockFile>
#include <QStandardPaths>
#include <QThread>

#include <windows.h>

namespace awqtui {

// ---------------- 版本比较 ----------------

// 把 "0.1.10-beta" 拆成 [0,1,10]：逐段取前缀数字，非数字段记 0
static QList<int> versionSegments(const QString &v)
{
    QList<int> out;
    const QStringList parts = v.split(QLatin1Char('.'), Qt::SkipEmptyParts);
    for (const QString &p : parts) {
        int i = 0;
        while (i < p.size() && p.at(i).isDigit())
            ++i;
        out << (i > 0 ? p.left(i).toInt() : 0);
    }
    return out;
}

int compareVersion(const QString &a, const QString &b)
{
    const QList<int> sa = versionSegments(a);
    const QList<int> sb = versionSegments(b);
    const int n = qMax(sa.size(), sb.size());
    for (int i = 0; i < n; ++i) {
        const int va = i < sa.size() ? sa.at(i) : 0;
        const int vb = i < sb.size() ? sb.at(i) : 0;
        if (va != vb)
            return va > vb ? 1 : -1;
    }
    return 0;
}

// ---------------- 路径 / 命名 ----------------

// 仲裁域目录：默认 %APPDATA%/aw-qtui/aw-qtui（与历史行为、服务端数据目录一致）。
// 设了环境变量 AWQTUI_INSTANCE_DIR 则改用它 —— 用途有两个：
//   1) 隔离测试：不碰真实安装域的锁，可以在旁边跑一整套实例做协议验证；
//   2) 并行调试：让 build/ 里的开发实例与 workshop 里的发布实例同时存在，互不抢位。
static QString instanceDir()
{
    const QByteArray override = qgetenv("AWQTUI_INSTANCE_DIR");
    if (!override.isEmpty())
        return QDir::cleanPath(QString::fromLocal8Bit(override));
    return QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
}

QString SingleInstance::lockFilePath()
{
    return instanceDir() + QStringLiteral("/awqtui.lock");
}

QString SingleInstance::instanceInfoPath()
{
    return instanceDir() + QStringLiteral("/awqtui.instance.json");
}

QString SingleInstance::selfExePath()
{
    return QDir::cleanPath(QCoreApplication::applicationFilePath());
}

// 管道名按「锁文件所在目录」派生：同一用户的各版本共用一条通道（能互相让位），
// 不同用户 / 不同 AppData 互不干扰。名字长度固定，避免路径超长打爆命名管道。
QString SingleInstance::pipeName()
{
    static QString cached;
    if (cached.isEmpty()) {
        const QByteArray h = QCryptographicHash::hash(lockFilePath().toUtf8(),
                                                      QCryptographicHash::Sha1)
                                 .toHex()
                                 .left(12);
        cached = QStringLiteral("aw-qtui-single-") + QString::fromLatin1(h);
    }
    return cached;
}

// 由 pid 取进程镜像路径（需要 PROCESS_QUERY_LIMITED_INFORMATION，低权限即可）
static QString winExePathByPid(qint64 pid)
{
    if (pid <= 0)
        return QString();
    HANDLE h = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, static_cast<DWORD>(pid));
    if (!h)
        return QString();
    wchar_t buf[2048] = {0};
    DWORD n = 2048;
    QString out;
    if (QueryFullProcessImageNameW(h, 0, buf, &n))
        out = QString::fromWCharArray(buf, static_cast<int>(n));
    CloseHandle(h);
    // 统一成正斜杠，便于与 QDir 组合比较
    return out.isEmpty() ? QString() : QDir::cleanPath(out);
}

QString SingleInstance::exePathByPid(qint64 pid)
{
    return winExePathByPid(pid);
}

// 从 <parent>/aw-qtui-<ver>/awqtui.exe 反推版本号；不是同族目录则返回空。
// 这是「老版本没写 instance.json」时唯一能拿到对方版本的办法。
QString SingleInstance::versionFromExePath(const QString &exePath)
{
    if (exePath.isEmpty())
        return QString();
    const QString dirName = QFileInfo(exePath).dir().dirName();
    if (!dirName.startsWith(QStringLiteral("aw-qtui-")))
        return QString();
    const QString ver = dirName.mid(QStringLiteral("aw-qtui-").size()).trimmed();
    return ver.isEmpty() ? QString() : ver;
}

// ---------------- 持有者信息文件 ----------------

void SingleInstance::writeInfoFile()
{
    const QString path = instanceInfoPath();
    QDir().mkpath(QFileInfo(path).absolutePath());
    QJsonObject o;
    o.insert(QStringLiteral("pid"), static_cast<double>(QCoreApplication::applicationPid()));
    o.insert(QStringLiteral("version"), kAppVersion);
    o.insert(QStringLiteral("exe"), selfExePath());
    QFile f(path);
    if (f.open(QIODevice::WriteOnly | QIODevice::Truncate))
        f.write(QJsonDocument(o).toJson(QJsonDocument::Compact));
}

QString SingleInstance::readInfoVersion(qint64 pid)
{
    QFile f(instanceInfoPath());
    if (!f.open(QIODevice::ReadOnly))
        return QString();
    const QJsonDocument doc = QJsonDocument::fromJson(f.readAll());
    if (!doc.isObject())
        return QString();
    const QJsonObject o = doc.object();
    if (static_cast<qint64>(o.value(QStringLiteral("pid")).toDouble()) != pid)
        return QString(); // 陈旧的 info（pid 对不上）不可信
    return o.value(QStringLiteral("version")).toString();
}

// ---------------- IPC 服务端（持锁者侧） ----------------

void SingleInstance::startServer(bool afterTakeover)
{
    // 刚接管时，前一实例的命名管道可能还没随进程完全释放 → 重试几次
    const int attempts = afterTakeover ? 20 : 1;
    if (!m_server) {
        m_server = new QLocalServer(this);
        connect(m_server, &QLocalServer::newConnection, this, [this] {
            while (QLocalSocket *s = m_server->nextPendingConnection()) {
                connect(s, &QLocalSocket::readyRead, this, [this, s] {
                    while (s->canReadLine())
                        handleLine(s, s->readLine().trimmed());
                });
                connect(s, &QLocalSocket::disconnected, s, &QObject::deleteLater);
            }
        });
    }

    for (int i = 0; i < attempts; ++i) {
        QLocalServer::removeServer(pipeName());
        if (m_server->listen(pipeName()))
            return;
        if (i + 1 < attempts)
            QThread::msleep(150);
    }
    // 通道起不来只是失去「被让位」能力，单实例锁本身仍然有效 —— 降级但不影响启动
    qWarning() << "[single] IPC 监听失败，跨版本自动让位不可用:" << m_server->errorString();
}

void SingleInstance::handleLine(QLocalSocket *sock, const QByteArray &line)
{
    if (line.startsWith("HELLO")) {
        sock->write(QStringLiteral("HOLDER %1 %2\n").arg(kAppVersion, selfExePath()).toUtf8());
        sock->flush();
        return;
    }
    if (m_yielding) {
        // 让位进行中：本实例马上就要退出，任何进一步的请求都不该再产生副作用
        qInfo().noquote() << "[single] 正在让位，忽略请求:" << line;
        return;
    }
    if (line == "RAISE") {
        emit raiseRequested();
    } else if (line.startsWith("YIELD")) {
        m_yielding = true;
        emit yieldRequested(QString::fromUtf8(line.mid(5)).trimmed());
    }
}

// ---------------- 仲裁 ----------------

SingleInstance::SingleInstance(QObject *parent) : QObject(parent) {}

SingleInstance::~SingleInstance()
{
    if (m_server) {
        m_server->close();
        QLocalServer::removeServer(pipeName());
    }
    // m_lock 由 QObject 子对象以外的裸指针持有 → 显式释放（析构即解锁）
    delete m_lock;
    m_lock = nullptr;
}

SingleInstance::Role SingleInstance::acquire(bool enabled)
{
    if (!enabled) {
        m_detail = QStringLiteral("单实例仲裁已跳过（截图 / 提权辅助模式）");
        return Role::Primary;
    }

    const QString path = lockFilePath();
    QDir().mkpath(QFileInfo(path).absolutePath());

    m_lock = new QLockFile(path);
    m_lock->setStaleLockTime(30000); // 崩溃残留锁 30s 后可被接管

    if (m_lock->tryLock(0)) {
        writeInfoFile();
        startServer(false);
        m_primary = true;
        m_detail = QStringLiteral("本实例持锁（v%1）").arg(kAppVersion);
        return Role::Primary;
    }

    // ── 锁被占：先弄清对方是谁、什么版本 ──
    qint64 pid = 0;
    QString host;
    QString app;
    const bool gotInfo = m_lock->getLockInfo(&pid, &host, &app);
    m_holderExe = gotInfo ? exePathByPid(pid) : QString();

    QString holderVer = gotInfo ? readInfoVersion(pid) : QString();
    if (holderVer.isEmpty())
        holderVer = versionFromExePath(m_holderExe); // 老版本没写 info → 从目录名反推

    // ── 尝试连通对方的 IPC（0.1.7 起双向可用）──
    QLocalSocket sock;
    sock.connectToServer(pipeName());
    const bool connected = sock.waitForConnected(1500);

    if (connected) {
        sock.write(QStringLiteral("HELLO %1 %2\n").arg(kAppVersion, selfExePath()).toUtf8());
        sock.flush();
        if (sock.waitForReadyRead(1500)) {
            const QByteArray reply = sock.readLine().trimmed();
            if (reply.startsWith("HOLDER")) {
                const QList<QByteArray> parts = reply.split(' ');
                if (parts.size() >= 2) {
                    const QString live = QString::fromLatin1(parts.at(1)).trimmed();
                    if (!live.isEmpty())
                        holderVer = live; // 活体自报比落盘信息更可信
                }
            }
        }
    }

    const int cmp = holderVer.isEmpty() ? 0 : compareVersion(kAppVersion, holderVer);

    // 同版本 → 唤起已有窗口；对方更新 → 自己退位。都不打扰用户。
    if (cmp <= 0) {
        if (connected) {
            sock.write("RAISE\n");
            sock.flush();
            sock.waitForBytesWritten(500);
        }
        if (cmp == 0) {
            m_detail = QStringLiteral("已有同版本实例 v%1 在运行 → 已请求置前，本实例退出").arg(holderVer);
            return Role::ExitSameVersion;
        }
        m_detail = QStringLiteral("已有更新版本 v%1 在运行 → 本实例退出（不降级抢位）").arg(holderVer);
        return Role::ExitNewerExists;
    }

    // ── 本实例更新：请求旧实例让位 ──
    if (connected) {
        sock.write(QStringLiteral("YIELD %1\n").arg(selfExePath()).toUtf8());
        sock.flush();
        // 对方断开 = 它已经走完优雅退出流程（flush + 拉起新版 + quit）
        sock.waitForDisconnected(4000);
        if (sock.state() != QLocalSocket::UnconnectedState)
            sock.abort();

        for (int i = 0; i < 20; ++i) {
            if (m_lock->tryLock(500)) {
                writeInfoFile();
                startServer(true);
                m_primary = true;
                m_detail = QStringLiteral("旧实例 v%1 已让位 → 本实例 v%2 接管").arg(holderVer, kAppVersion);
                return Role::Primary;
            }
            QThread::msleep(150);
        }
        m_detail = QStringLiteral("旧实例 v%1 收到让位请求但未释放锁（可能已卡住）→ 本实例退出").arg(holderVer);
        return Role::ExitOlderBlocks;
    }

    // ── 无 IPC 通道：≤0.1.6 的老版本叫不动（不硬杀，只提示）──
    const bool sameFamily = !versionFromExePath(m_holderExe).isEmpty();
    if (sameFamily)
        m_detail = QStringLiteral("检测到旧版本实例 v%1（%2）仍在运行：该版本没有让位通道，"
                                  "请从托盘退出它一次，之后新版之间即可自动让位")
                       .arg(holderVer.isEmpty() ? QStringLiteral("?") : holderVer, m_holderExe);
    else
        m_detail = QStringLiteral("检测到已有实例（pid=%1）但无法确认其版本 → 本实例退出").arg(pid);
    return Role::ExitOlderBlocks;
}

} // namespace awqtui
