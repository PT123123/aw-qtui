// globalshortcut.h —— 系统级全局快捷键（Windows RegisterHotKey）
//
// 与 Qt 的 QShortcut（仅应用内生效）不同，全局热键在应用失焦、最小化时依然
// 能触发。原理：把 QKeySequence 翻译成 Windows VK + 修饰掩码，用 RegisterHotKey
// 注册到主窗口 HWND，WM_HOTKEY 由 MainWindow::nativeEvent 转交 handleMessage()。
#pragma once

#include <QHash>
#include <QKeySequence>
#include <QObject>

class QWidget;

namespace awqtui {

// 把 QKeySequence 翻译成 Windows 虚拟键码（非 Windows 平台返回 0）
unsigned int qtKeyToVk(int qtKey);
// 把 Qt 修饰键掩码翻译成 Windows 修饰掩码
unsigned int qtModsToWin(int qtMods);
// 解析单个组合键 -> (mod, vk)。成功返回 true；空序列/纯修饰键返回 false。
bool parseSequence(const QKeySequence &seq, unsigned int &mod, unsigned int &vk);

// 在 Windows 上把窗口强制置前（全局热键触发时属于合法用户输入）。
// 其它平台为空操作。实现位于 globalshortcut.cpp，避免头文件泄漏 windows.h。
void raiseWindowToFront(QWidget *w);

class GlobalHotkey : public QObject
{
    Q_OBJECT
public:
    explicit GlobalHotkey(QObject *parent = nullptr);
    ~GlobalHotkey() override;

    // 注册 id 对应的快捷键（先注销该 id 的旧键）。hwnd 为接收 WM_HOTKEY 的
    // 原生窗口句柄（可传 QWidget::winId()）。空序列 = 禁用，视为成功。
    // 返回 true 表示已注册；false 通常意味着该组合被其它进程占用。
    bool setHotKey(int id, const QKeySequence &seq, void *hwnd);
    // 注销单个 id
    void clearHotKey(int id);
    // 临时注销全部热键（编辑快捷键期间调用，避免录入时误触发），
    // 之后用 setHotKey 重新注册即可恢复。
    void suspendAll();

    // 处理一条原生消息（Windows 为 MSG*，由 MainWindow::nativeEvent 传入）。
    // 命中 WM_HOTKEY 时 emit activated(id) 并返回 true。
    bool handleMessage(void *nativeMessage);

signals:
    void activated(int id);

private:
    struct Entry {
        QKeySequence seq;
        void *hwnd = nullptr;
        bool registered = false;
    };
    QHash<int, Entry> m_hotkeys;
};

} // namespace awqtui
