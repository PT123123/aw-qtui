// singleinstance.h —— 跨版本单实例仲裁（aw-qtui 客户端同时只留一个实例，且版本新者优先）
//
// 背景：workshop 目录下会并存 aw-qtui-0.1.6 / aw-qtui-0.1.7 ... 多份发布。
// 用户直接双击新版时，占着锁的**旧版必须让位** —— 而不是像老实现（main.cpp 的裸 QLockFile）
// 那样让新版自己弹框退出、旧版继续霸着。
//
// 仲裁规则（以版本号大小为准，比较按数字段，见 compareVersion）：
//   持锁者更旧 → 通过 IPC 请求对方优雅让位（对方 flush 后退出 + 拉起本实例），本实例接管
//   持锁者同版 → 请求对方把窗口置前，本实例静默退出（双击同一版本 = 唤起已有窗口，不打扰）
//   持锁者更新 → 本实例静默退出（不降级抢位）
//
// 兜底：≤0.1.6 的老版本没有 IPC 通道，叫不动。此时从锁文件取对方 pid →
// 用进程镜像路径反推版本（<parent>/aw-qtui-<ver>/awqtui.exe）→ 确认更旧也只提示、
// **不硬杀**（这是用户的明确要求：优雅关闭，不硬杀）。0.1.7 起双向自动让位。
//
// 线程模型：全部在 GUI 线程同步完成。acquire() 会阻塞至多数秒（等对方退出），
// 必须在 QApplication::exec() 之前调用。
#pragma once

#include <QObject>
#include <QString>

class QLockFile;
class QLocalServer;
class QLocalSocket;

namespace awqtui {

// 语义化版本比较：a>b → 1，a==b → 0，a<b → -1。
// 按「数字段」逐段比，保证 "0.1.10" > "0.1.7"（直接字符串比较会判反）。
// 非数字段（如 "0.1.7-beta"）取前缀数字，缺失段按 0 处理。
int compareVersion(const QString &a, const QString &b);

class SingleInstance : public QObject
{
    Q_OBJECT

public:
    // 仲裁结果。除 Primary 外调用方都应干净退出（return 0）
    enum class Role {
        Primary,          // 锁到手 → 正常启动
        ExitSameVersion,  // 已有同版本实例（已请求它置前）→ 本实例退出
        ExitNewerExists,  // 已有更新版本实例 → 本实例退出
        ExitOlderBlocks,  // 已有更旧实例，但它不支持让位 → 本实例退出（提示用户手动关一次）
    };
    Q_ENUM(Role)

    explicit SingleInstance(QObject *parent = nullptr);
    ~SingleInstance() override;

    // 单实例仲裁。enabled=false（截图 / 提权辅助模式）直接返回 Primary：
    // 这类实例跑完就退，不该被已有实例挡掉，也不该抢锁。
    Role acquire(bool enabled);

    bool isPrimary() const { return m_primary; }
    // 最后一次仲裁的人话描述（写日志 / 提示用）
    QString lastDetail() const { return m_detail; }
    // 冲突方（持锁者）的 exe 路径，取不到为空
    QString holderExe() const { return m_holderExe; }

    // 跨版本共用的锁文件（默认 %APPDATA%/aw-qtui/aw-qtui/awqtui.lock）。外部脚本也用这个探活。
    // 环境变量 AWQTUI_INSTANCE_DIR 可覆盖仲裁域目录：隔离测试 / 开发实例与发布实例并行调试。
    static QString lockFilePath();
    // 本进程 exe 绝对路径（正斜杠）
    static QString selfExePath();

signals:
    // 本实例是旧版：收到更新版的让位请求 → 应 flush 待提交编辑后退出。
    // newerExe 为请求方 exe 路径（可能为空），退出前用它拉起新版。
    void yieldRequested(const QString &newerExe);
    // 已有实例被要求置前（同版本再次启动时唤起原窗口）
    void raiseRequested();

private:
    void startServer(bool afterTakeover);
    void handleLine(QLocalSocket *sock, const QByteArray &line);

    QLockFile *m_lock = nullptr;     // 由本对象持有，必须活到进程结束（析构即释放锁）
    QLocalServer *m_server = nullptr;
    QString m_detail;
    QString m_holderExe;
    bool m_primary = false;
    // 已开始让位 → 忽略后续请求。没有这道闸，旧实例在退出途中又收到一次 YIELD
    // （新版拉起的第二个副本会连回来）就会重复「拉起新版」，形成进程爆发。
    bool m_yielding = false;

    static QString instanceInfoPath();
    static QString pipeName();
    static QString exePathByPid(qint64 pid);
    static QString versionFromExePath(const QString &exePath);
    static QString readInfoVersion(qint64 pid);
    static void writeInfoFile();
};

} // namespace awqtui
