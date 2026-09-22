// mem_baseline.cpp —— 拆解「这个应用起步就得吃多少内存」。
//
// 逐级构造**真实控件**（不是玩具 widget），每级打一次进程私有内存 / 工作集，用来回答
// 「常驻托盘的 Qt 应用，base 内存多少算合理」以及「某次改动把地板抬高了没有」。
//
// 编译运行：just mem-baseline            （会先自动构建 build-verify 的一份目标文件）
//     或：just mem-baseline offscreen    （离屏平台，跑得快但绝对值偏低，只能看增量）
//
// 两个平台的差别很重要：
//   real（默认）用真实 windows 平台插件 —— 系统字体库、完整 polish/paint 都在，最接近实机；
//              顶层窗口设 WA_DontShowOnScreen，屏幕上不会弹任何窗口。
//   offscreen  快，但没有真实窗口/后备存储，且不加载系统字体库（少 30MB 私有 / 70MB 工作集），
//              绝对值不可比，只看「每级增量」。
//
// 零副作用：不连服务端（base url 指向死端口，强制走离线路径）、数据目录经
// QStandardPaths::setTestModeEnabled 隔离到 %APPDATA%\qttest\，不碰真实 inbox.db / todo.db。
//
// 判读要点（README「内存与性能基线」有完整表格）：
//   * 工作集（任务管理器「内存」列）里含映射进来的字体文件，光字体库一次性初始化就 +69MB ——
//     评估自己的开销看**私有内存**，别拿工作集当 KPI。
//   * 第 3b 级必须单独量：不把它拆出来，这 30MB 会被算到下一个页面的头上。
#include <QApplication>
#include <QLabel>
#include <QPixmap>
#include <QStandardPaths>
#include <QVBoxLayout>
#include <QWidget>
#include <cstdio>

#include <windows.h>
#include <psapi.h>

#include "apiclient.h"
#include "inboxpage.h"
#include "theme.h"
#include "todopage.h"
#include "todostore.h"

using namespace awqtui;

static size_t g_base = 0;

static void stage(const char *label)
{
    PROCESS_MEMORY_COUNTERS_EX c;
    c.cb = sizeof(c);
    GetProcessMemoryInfo(GetCurrentProcess(), (PROCESS_MEMORY_COUNTERS *)&c, sizeof(c));
    const double priv = c.PrivateUsage / 1024.0 / 1024.0;
    const double ws = c.WorkingSetSize / 1024.0 / 1024.0;
    if (g_base == 0)
        g_base = c.PrivateUsage;
    std::printf("%-34s 私有 %7.1f MB  (Delta %+6.1f)   工作集 %7.1f MB\n", label, priv,
                (c.PrivateUsage - g_base) / 1024.0 / 1024.0, ws);
    std::fflush(stdout);
}

// real 模式：真实平台插件 + WA_DontShowOnScreen（不往用户屏幕上弹窗）
static bool g_real = false;

static void reveal(QWidget *w)
{
    if (g_real)
        w->setAttribute(Qt::WA_DontShowOnScreen, true);
}

int main(int argc, char **argv)
{
    QStandardPaths::setTestModeEnabled(true);
    QApplication::setApplicationName(QStringLiteral("awqtui-mem-baseline"));
    QApplication::setOrganizationName(QStringLiteral("aw-qtui"));

    for (int i = 1; i < argc; ++i) {
        const QString a = QString::fromLocal8Bit(argv[i]);
        if (a == QLatin1String("real"))
            g_real = true;
        else if (a == QLatin1String("offscreen"))
            g_real = false;
    }

    QApplication app(argc, argv);

    stage(g_real ? "1 QApplication（真实平台）" : "1 QApplication（离屏）");

    QWidget *win = new QWidget;
    win->resize(1200, 800);
    reveal(win);
    win->show();
    QCoreApplication::processEvents();
    stage("2 + 空窗口 show 1200x800");

    // 全局 QSS（生产是 themeQss(theme) 生成的整份样式）
    const Theme *t = findTheme(QStringLiteral("dark")) ? findTheme(QStringLiteral("dark"))
                                                       : &kThemes[0];
    gGlobalQss = themeQss(*t);
    app.setStyleSheet(scaleQss(gGlobalQss));
    QCoreApplication::processEvents();
    stage("3 + 全局 QSS 套到 window 上");

    // 逼出「字体库一次性初始化」：真实平台上第一次画文字才建 DirectWrite 字体缓存。
    // 不单独量出来，这 30MB 私有 / 69MB 工作集会被算到下一个阶段的头上。
    {
        QWidget *host = new QWidget;
        host->resize(400, 200);
        auto *lay = new QVBoxLayout(host);
        auto *lb = new QLabel(QStringLiteral("内存测试 memory test 0123456789"));
        lb->setWordWrap(true);
        lay->addWidget(lb);
        reveal(host);
        host->show();
        QCoreApplication::processEvents();
        QPixmap pm(host->size());
        host->render(&pm); // 强制真正绘制一遍
    }
    stage("3b + 带文字的窗口（逼出字体库）");

    auto *api = new ApiClient;
    api->setBaseUrl(QStringLiteral("http://127.0.0.1:9")); // 死端口：强制离线路径
    auto *inbox = new InboxPage(api);
    inbox->resize(1200, 800);
    stage("4a InboxPage 已构造（未 show）");
    reveal(inbox);
    inbox->show();
    QCoreApplication::processEvents();
    stage("4b InboxPage 已 show+polish");

    auto *store = new TodoStore;
    auto *todo = new TodoPage(store);
    todo->setApiClient(api);
    todo->resize(1200, 800);
    reveal(todo);
    todo->show();
    QCoreApplication::processEvents();
    stage("5 + TodoPage（空数据）");

    std::printf("注：这里量的是「空数据地板」。实机 base 高出的部分来自真实数据（卡片/行控件）、\n"
                "    CardPool 常驻池与页面重建 churn —— 判据见 README「内存与性能基线」。\n");
    return 0;
}
