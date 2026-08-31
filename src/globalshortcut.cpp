// globalshortcut.cpp —— 全局热键：Windows RegisterHotKey 实现
#include "globalshortcut.h"

#include <QWidget>

#ifdef Q_OS_WIN
#include <windows.h>
#endif

namespace awqtui {

unsigned int qtKeyToVk(int qtKey)
{
    // 字母 / 数字：Qt 的 Key_A..Key_Z == ASCII 'A'..'Z'，Key_0..Key_9 == '0'..'9'
    if (qtKey >= Qt::Key_A && qtKey <= Qt::Key_Z)
        return static_cast<unsigned int>('A' + (qtKey - Qt::Key_A));
    if (qtKey >= Qt::Key_0 && qtKey <= Qt::Key_9)
        return static_cast<unsigned int>('0' + (qtKey - Qt::Key_0));
    // 功能键 F1..F24（VK_F1 = 0x70）
    if (qtKey >= Qt::Key_F1 && qtKey <= Qt::Key_F24)
        return VK_F1 + static_cast<unsigned int>(qtKey - Qt::Key_F1);

    switch (qtKey) {
    case Qt::Key_Escape: return VK_ESCAPE;
    case Qt::Key_Tab:
    case Qt::Key_Backtab: return VK_TAB;
    case Qt::Key_Return:
    case Qt::Key_Enter: return VK_RETURN;
    case Qt::Key_Backspace: return VK_BACK;
    case Qt::Key_Delete: return VK_DELETE;
    case Qt::Key_Insert: return VK_INSERT;
    case Qt::Key_Home: return VK_HOME;
    case Qt::Key_End: return VK_END;
    case Qt::Key_PageUp: return VK_PRIOR;
    case Qt::Key_PageDown: return VK_NEXT;
    case Qt::Key_Left: return VK_LEFT;
    case Qt::Key_Right: return VK_RIGHT;
    case Qt::Key_Up: return VK_UP;
    case Qt::Key_Down: return VK_DOWN;
    case Qt::Key_Space: return VK_SPACE;
    case Qt::Key_CapsLock: return VK_CAPITAL;
    case Qt::Key_Pause: return VK_PAUSE;
    case Qt::Key_Print: return VK_PRINT;
    case Qt::Key_ScrollLock: return VK_SCROLL;
    case Qt::Key_NumLock: return VK_NUMLOCK;
    case Qt::Key_Minus: return VK_OEM_MINUS;
    case Qt::Key_Equal: return VK_OEM_PLUS;
    case Qt::Key_Comma: return VK_OEM_COMMA;
    case Qt::Key_Period: return VK_OEM_PERIOD;
    case Qt::Key_Slash: return VK_OEM_2;
    case Qt::Key_Semicolon: return VK_OEM_1;
    case Qt::Key_Apostrophe: return VK_OEM_7;
    case Qt::Key_BracketLeft: return VK_OEM_4;
    case Qt::Key_BracketRight: return VK_OEM_6;
    case Qt::Key_Backslash: return VK_OEM_5;
    default: break;
    }
    // 其它可打印字符用 VkKeyScanW 兜底（如非 ASCII 布局下的标点）
    if (qtKey >= 0x20 && qtKey <= 0xFFFF) {
        const SHORT sc = VkKeyScanW(static_cast<WCHAR>(qtKey));
        if (sc != static_cast<SHORT>(-1))
            return static_cast<unsigned int>(LOBYTE(sc));
    }
    return 0;
}

unsigned int qtModsToWin(int qtMods)
{
    unsigned int mod = 0;
    if (qtMods & Qt::ControlModifier)
        mod |= MOD_CONTROL;
    if (qtMods & Qt::AltModifier)
        mod |= MOD_ALT;
    if (qtMods & Qt::ShiftModifier)
        mod |= MOD_SHIFT;
    if (qtMods & Qt::MetaModifier)
        mod |= MOD_WIN;
    return mod;
}

bool parseSequence(const QKeySequence &seq, unsigned int &mod, unsigned int &vk)
{
    if (seq.isEmpty())
        return false;
    // 只取第一个组合；不支持多段序列（如 "Ctrl+K, Ctrl+P"）
    const QKeyCombination comb = seq[0];
    const int key = comb.key();
    const int mods = static_cast<int>(comb.keyboardModifiers());
    mod = qtModsToWin(mods) | MOD_NOREPEAT; // MOD_NOREPEAT 防止长按连发
    vk = qtKeyToVk(key);
    return vk != 0;
}

void raiseWindowToFront(QWidget *w)
{
    if (!w)
        return;
#ifdef Q_OS_WIN
    // 全局热键属于用户输入，此时 SetForegroundWindow 不会被系统阻止
    SetForegroundWindow(reinterpret_cast<HWND>(w->winId()));
    if (w->isMinimized())
        ShowWindow(reinterpret_cast<HWND>(w->winId()), SW_RESTORE);
#else
    Q_UNUSED(w);
#endif
}

GlobalHotkey::GlobalHotkey(QObject *parent) : QObject(parent) {}

GlobalHotkey::~GlobalHotkey()
{
    suspendAll();
}

bool GlobalHotkey::setHotKey(int id, const QKeySequence &seq, void *hwnd)
{
    clearHotKey(id);
    if (seq.isEmpty() || hwnd == nullptr)
        return true; // 空序列 = 禁用；无效句柄按禁用处理

#ifdef Q_OS_WIN
    unsigned int mod = 0, vk = 0;
    if (!parseSequence(seq, mod, vk)) {
        qWarning() << "[GlobalHotkey] 无法解析快捷键:" << seq.toString();
        return false;
    }
    if (!RegisterHotKey(reinterpret_cast<HWND>(hwnd), id, mod, vk)) {
        // 立即捕获错误码：任何 Qt 调用都可能把线程最后错误码清零
        const DWORD err = GetLastError();
        qWarning() << "[GlobalHotkey] RegisterHotKey 失败 id=" << id
                   << "seq=" << seq.toString(QKeySequence::NativeText)
                   << "err=" << err;
        return false;
    }
    Entry e;
    e.seq = seq;
    e.hwnd = hwnd;
    e.registered = true;
    m_hotkeys.insert(id, e);
    return true;
#else
    Q_UNUSED(id);
    Q_UNUSED(seq);
    Q_UNUSED(hwnd);
    return true; // 非 Windows：仅应用内使用，这里不做系统级注册
#endif
}

void GlobalHotkey::clearHotKey(int id)
{
    auto it = m_hotkeys.find(id);
    if (it == m_hotkeys.end())
        return;
#ifdef Q_OS_WIN
    if (it->registered && it->hwnd)
        UnregisterHotKey(reinterpret_cast<HWND>(it->hwnd), id);
#endif
    m_hotkeys.erase(it);
}

void GlobalHotkey::suspendAll()
{
    for (auto it = m_hotkeys.begin(); it != m_hotkeys.end(); ++it) {
#ifdef Q_OS_WIN
        if (it->registered && it->hwnd)
            UnregisterHotKey(reinterpret_cast<HWND>(it->hwnd), it.key());
#endif
        it->registered = false;
    }
}

bool GlobalHotkey::handleMessage(void *nativeMessage)
{
#ifdef Q_OS_WIN
    if (!nativeMessage)
        return false;
    const auto *msg = static_cast<const MSG *>(nativeMessage);
    if (msg->message != WM_HOTKEY)
        return false;
    const int id = static_cast<int>(msg->wParam);
    if (!m_hotkeys.contains(id))
        return false;
    emit activated(id);
    return true;
#else
    Q_UNUSED(nativeMessage);
    return false;
#endif
}

} // namespace awqtui
