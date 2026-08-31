// mainwindow.cpp
#include "mainwindow.h"

#include "activitypage.h"
#include "apiclient.h"
#include "appsettings.h"
#include "config.h"
#include "daypage.h"
#include "globalshortcut.h"
#include "inboxpage.h"
#include "mdnsdiscovery.h"
#include "settingsdialog.h"
#include "statspage.h"
#include "syncpage.h"
#include "tagstore.h"
#include "theme.h"
#include "timelinepage.h"

#include <QApplication>
#include <QButtonGroup>
#include <QComboBox>
#include <QEvent>
#include <QFont>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QKeySequence>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSizePolicy>
#include <QSpinBox>
#include <QTextEdit>
#include <QTimer>
#include <QToolButton>
#include <QVBoxLayout>
#include <QWheelEvent>

#include <cmath>

namespace awqtui {

MainWindow::MainWindow(const QString &serverUrl, QWidget *parent) : QMainWindow(parent)
{
    m_api = new ApiClient(this);
    if (!serverUrl.isEmpty())
        m_api->setBaseUrl(serverUrl);
    m_mdns = new MdnsDiscovery(this);
    m_tagStore = new TagStore;
    m_tagStore->load();
    buildUi();

    // 页面缩放：应用上次保存的比例（0.3~3.0），并安装全局事件过滤器拦截 Ctrl+滚轮 / +- 键
    m_zoom = loadUiZoom();
    applyUiScale();
    qApp->installEventFilter(this);

    // 全局热键：注册到主窗口 HWND，应用失焦/最小化时仍能触发（WM_HOTKEY -> nativeEvent）
    m_hotkey = new GlobalHotkey(this);
    connect(m_hotkey, &GlobalHotkey::activated, this, &MainWindow::onGlobalHotkey);
    // 窗口 show() 之后再注册：构造函数里 winId() 的 HWND 尚未完全就绪，
    // RegisterHotKey 会失败（GetLastError==0）。
    QTimer::singleShot(0, this, [this] { applyShortcuts(); });

    QTimer *t = new QTimer(this);
    connect(t, &QTimer::timeout, this, &MainWindow::updateStatus);
    t->start(30000);
    QTimer::singleShot(0, this, &MainWindow::updateStatus);
}

MainWindow::~MainWindow()
{
    delete m_tagStore;
}

void MainWindow::buildUi()
{
    auto *central = new QWidget;
    auto *root = new QHBoxLayout(central);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(0);

    // ---- 左侧导航 ----
    m_nav = new QWidget;
    m_nav->setObjectName(QStringLiteral("NavSidebar"));
    m_nav->setFixedWidth(si(200));
    auto *nav = m_nav;
    auto *navLay = new QVBoxLayout(nav);
    navLay->setContentsMargins(0, si(16), 0, si(12));
    navLay->setSpacing(si(4));

    auto *brand = new QLabel(QStringLiteral("aw · qtui"));
    brand->setObjectName(QStringLiteral("NavBrand"));
    brand->setStyleSheet(QStringLiteral("font-size: %1; font-weight: 700; color: white; padding: 0 %2 %3;")
                             .arg(sp(20), sp(16), sp(12)));
    navLay->addWidget(brand);

    // 可折叠分组：header 点击展开/收起，容器内放导航按钮
    struct NavSection {
        QToolButton *header;
        QWidget *box;
        QVBoxLayout *layout;
    };
    auto makeSection = [&navLay](const QString &title, bool expanded) {
        auto *header = new QToolButton;
        header->setObjectName(QStringLiteral("NavSection"));
        header->setText(title);
        header->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
        header->setCheckable(true);
        header->setChecked(expanded);
        header->setArrowType(expanded ? Qt::DownArrow : Qt::RightArrow);
        header->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);

        auto *box = new QWidget;
        auto *lay = new QVBoxLayout(box);
        lay->setContentsMargins(0, 0, 0, 0);
        lay->setSpacing(4);

        QObject::connect(header, &QToolButton::toggled, box, &QWidget::setVisible);
        QObject::connect(header, &QToolButton::toggled, header, [header](bool on) {
            header->setArrowType(on ? Qt::DownArrow : Qt::RightArrow);
        });

        navLay->addWidget(header);
        navLay->addWidget(box);
        box->setVisible(expanded);
        return NavSection{header, box, lay};
    };

    // ---- 分组 1：收件箱（默认展开）----
    NavSection inboxSec = makeSection(QStringLiteral("收件箱"), /*expanded*/ true);

    m_navInbox = new QPushButton(QStringLiteral("📥  收件箱"));
    m_navSync = new QPushButton(QStringLiteral("⇄  局域网同步"));
    m_navInbox->setObjectName(QStringLiteral("NavBtn"));
    m_navSync->setObjectName(QStringLiteral("NavBtn"));
    m_navInbox->setCheckable(true);
    m_navSync->setCheckable(true);
    inboxSec.layout->addWidget(m_navInbox);
    inboxSec.layout->addWidget(m_navSync);

    // ---- 分组 2：ACTIVITYWATCH（默认收起）----
    NavSection awSec = makeSection(QStringLiteral("ACTIVITYWATCH"), /*expanded*/ false);

    m_navActivity = new QPushButton(QStringLiteral("📊  Activity"));
    m_navTimeline = new QPushButton(QStringLiteral("⏱  Timeline"));
    m_navActivity->setObjectName(QStringLiteral("NavBtn"));
    m_navTimeline->setObjectName(QStringLiteral("NavBtn"));
    m_navActivity->setCheckable(true);
    m_navTimeline->setCheckable(true);
    awSec.layout->addWidget(m_navActivity);
    awSec.layout->addWidget(m_navTimeline);

    m_navDay = new QPushButton(QStringLiteral("🏷  标签 Day"));
    m_navDay->setObjectName(QStringLiteral("NavBtn"));
    m_navDay->setCheckable(true);
    awSec.layout->addWidget(m_navDay);

    m_navStats = new QPushButton(QStringLiteral("📈  统计"));
    m_navStats->setObjectName(QStringLiteral("NavBtn"));
    m_navStats->setCheckable(true);
    awSec.layout->addWidget(m_navStats);

    auto *grp = new QButtonGroup(this);
    grp->addButton(m_navActivity);
    grp->addButton(m_navTimeline);
    grp->addButton(m_navInbox);
    grp->addButton(m_navSync);
    grp->addButton(m_navDay);
    grp->addButton(m_navStats);

    connect(m_navActivity, &QPushButton::clicked, this, [this] { switchPage(0); });
    connect(m_navTimeline, &QPushButton::clicked, this, [this] { switchPage(1); });
    connect(m_navInbox, &QPushButton::clicked, this, [this] { switchPage(2); });
    connect(m_navSync, &QPushButton::clicked, this, [this] { switchPage(3); });
    connect(m_navDay, &QPushButton::clicked, this, [this] { switchPage(4); });
    connect(m_navStats, &QPushButton::clicked, this, [this] { switchPage(5); });

    navLay->addStretch(1);

    m_deviceLabel = new QLabel;
    m_deviceLabel->setWordWrap(true);
    m_deviceLabel->setStyleSheet(QStringLiteral("color: %1; font-size: 11px; padding: 0 16px;")
                                     .arg(kColorFgMuted));
    navLay->addWidget(m_deviceLabel);
    root->addWidget(nav);

    // ---- 页面堆栈 ----
    m_stack = new QStackedWidget;
    qDebug() << "[MainWindow] creating ActivityPage...";
    m_activity = new ActivityPage;
    qDebug() << "[MainWindow] ActivityPage created";
    qDebug() << "[MainWindow] creating TimelinePage...";
    m_timeline = new TimelinePage;
    qDebug() << "[MainWindow] TimelinePage created";
    qDebug() << "[MainWindow] creating InboxPage...";
    m_inbox = new InboxPage(m_api);
    qDebug() << "[MainWindow] InboxPage created";
    connect(m_inbox, &InboxPage::settingsRequested, this, &MainWindow::openSettings);
    qDebug() << "[MainWindow] creating SyncPage...";
    m_sync = new SyncPage(m_api, m_mdns);
    qDebug() << "[MainWindow] SyncPage created";
    qDebug() << "[MainWindow] creating DayPage...";
    m_day = new DayPage(m_tagStore);
    qDebug() << "[MainWindow] DayPage created";
    qDebug() << "[MainWindow] creating StatsPage...";
    m_stats = new StatsPage(m_tagStore);
    qDebug() << "[MainWindow] StatsPage created";
    m_stack->addWidget(m_activity);   // 0
    m_stack->addWidget(m_timeline);   // 1
    m_stack->addWidget(m_inbox);      // 2
    m_stack->addWidget(m_sync);       // 3
    m_stack->addWidget(m_day);        // 4
    m_stack->addWidget(m_stats);      // 5
    qDebug() << "[MainWindow] all pages added to stack";
    root->addWidget(m_stack, 1);

    // 默认落在收件箱：activitywatch 分组默认收起，启动时导航保持可见/高亮一致
    switchPage(2);

    setCentralWidget(central);

    // 右上角缩放百分比提示（缩放后短暂显示）
    m_zoomBadge = new QLabel(this);
    m_zoomBadge->setStyleSheet(
        QStringLiteral("background: rgba(0,0,0,0.72); color: white; border: 1px solid %1; "
                       "border-radius: 4px; padding: 3px 10px; font-size: 12px;")
            .arg(kColorBorder));
    m_zoomBadge->hide();

    setWindowTitle(QStringLiteral("aw-qtui — ActivityWatch 客户端"));
    resize(1280, 820);
}

void MainWindow::switchPage(int index)
{
    m_stack->setCurrentIndex(index);
    m_navActivity->setChecked(index == 0);
    m_navTimeline->setChecked(index == 1);
    m_navInbox->setChecked(index == 2);
    m_navSync->setChecked(index == 3);
    m_navDay->setChecked(index == 4);
    m_navStats->setChecked(index == 5);
    if (index == 3)
        m_sync->refreshDevices();
}

void MainWindow::updateStatus()
{
    const QString dev = QStringLiteral("设备 %1\n%2 · %3")
                            .arg(deviceId(), hostname(), platform());
    m_deviceLabel->setText(dev);
    m_sync->heartbeat();
}

QStringList MainWindow::applyShortcuts()
{
    const ShortcutConfig cfg = loadShortcuts();
    void *hwnd = reinterpret_cast<void *>(quintptr(winId()));
    qDebug() << "[MainWindow] applyShortcuts hwnd=" << quintptr(hwnd)
             << "add=" << cfg.addNote.toString() << "inbox=" << cfg.showInbox.toString();
    QStringList failed;

    auto tryRegister = [&](int id, const QKeySequence &seq, const QString &label) {
        // 空序列在 setHotKey 内按“禁用”处理并返回 true，因此这里只会收集真实失败
        if (!m_hotkey->setHotKey(id, seq, hwnd))
            failed << QStringLiteral("%1 (%2)").arg(label, seq.toString(QKeySequence::NativeText));
    };
    tryRegister(kHotkeyAddNoteId, cfg.addNote, QStringLiteral("添加记录"));
    tryRegister(kHotkeyShowInboxId, cfg.showInbox, QStringLiteral("唤醒并跳转收件箱"));

    if (!failed.isEmpty())
        qWarning() << "[MainWindow] 全局热键注册失败:" << failed.join(QLatin1String("; "));
    return failed;
}

void MainWindow::openSettings()
{
    // 编辑快捷键期间暂停全局热键：避免录入“当前已绑定”的组合时误触发动作
    m_hotkey->suspendAll();

    SettingsDialog dlg(loadShortcuts(), this);
    if (dlg.exec() != QDialog::Accepted) {
        applyShortcuts(); // 取消：恢复原注册
        return;
    }
    saveShortcuts(dlg.config());
    const QStringList failed = applyShortcuts();
    if (!failed.isEmpty()) {
        QMessageBox::warning(
            this, QStringLiteral("快捷键冲突"),
            QStringLiteral("以下快捷键未能注册（可能已被其它程序占用），已保存但暂不生效：\n\n%1\n\n"
                           "可在设置中改绑其它组合。")
                .arg(failed.join(QLatin1Char('\n'))));
    }
}

void MainWindow::wakeUpAndShow()
{
    if (isMinimized())
        showNormal();
    show();
    raise();
    activateWindow();
    raiseWindowToFront(this);
}

void MainWindow::onGlobalHotkey(int id)
{
    qDebug() << "[MainWindow] global hotkey activated id=" << id;
    switch (id) {
    case kHotkeyAddNoteId:
        // 添加记录：唤醒 → 跳到收件箱 → 打开新建笔记
        wakeUpAndShow();
        switchPage(2);
        m_inbox->openNewNote();
        break;
    case kHotkeyShowInboxId:
        // 唤醒并跳转收件箱
        wakeUpAndShow();
        switchPage(2);
        break;
    default:
        break;
    }
}

bool MainWindow::nativeEvent(const QByteArray &eventType, void *message, qintptr *result)
{
#ifdef Q_OS_WIN
    if (m_hotkey && m_hotkey->handleMessage(message)) {
        if (result)
            *result = 0;
        return true;
    }
    Q_UNUSED(eventType);
#else
    Q_UNUSED(eventType);
    Q_UNUSED(message);
    Q_UNUSED(result);
#endif
    return QMainWindow::nativeEvent(eventType, message, result);
}

void MainWindow::keyPressEvent(QKeyEvent *event)
{
    // 快捷键：1-5 切页，F5 刷新当前页
    switch (event->key()) {
    case Qt::Key_1: switchPage(0); return;
    case Qt::Key_2: switchPage(1); return;
    case Qt::Key_3: switchPage(2); return;
    case Qt::Key_4: switchPage(3); return;
    case Qt::Key_5: switchPage(4); return;
    case Qt::Key_6: switchPage(5); return;
    case Qt::Key_F5:
        if (m_stack->currentIndex() == 0) m_activity->refresh();
        else if (m_stack->currentIndex() == 1) m_timeline->refresh();
        else if (m_stack->currentIndex() == 2) m_inbox->refreshAll();
        else if (m_stack->currentIndex() == 4) m_day->refresh();
        else if (m_stack->currentIndex() == 5) m_stats->refresh();
        return;
    default: break;
    }
    QMainWindow::keyPressEvent(event);
}

bool MainWindow::eventFilter(QObject *obj, QEvent *event)
{
    // 缩放仅支持快捷键：Ctrl+加 / Ctrl+减 / Ctrl+0 复位。
    // 滚轮缩放已移除：按住 Ctrl 滚动会重建整页（Inbox 列表全部重渲染）导致卡顿。
    if (event->type() == QEvent::KeyPress && isInsideZoomable(obj)) {
        auto *ke = static_cast<QKeyEvent *>(event);
        const bool ctrl = ke->modifiers().testFlag(Qt::ControlModifier);
        const int key = ke->key();
        if (ctrl) {
            bool handled = true;
            if (key == Qt::Key_Plus || key == Qt::Key_Equal) {
                zoomBy(1.15, false);
            } else if (key == Qt::Key_Minus) {
                zoomBy(1.0 / 1.15, false);
            } else if (key == Qt::Key_0) {
                setZoom(1.0, false);
            } else {
                handled = false;
            }
            if (handled) {
                ke->accept();
                return true;
            }
        }
    }
    return QMainWindow::eventFilter(obj, event);
}

void MainWindow::zoomBy(qreal factor, bool underMouse)
{
    Q_UNUSED(underMouse);
    setZoom(m_zoom * factor, false);
}

void MainWindow::setZoom(qreal zoom, bool underMouse)
{
    Q_UNUSED(underMouse);
    zoom = qBound(0.3, zoom, 3.0);
    if (qFuzzyCompare(zoom, m_zoom))
        return;
    m_zoom = zoom;
    qDebug() << "[MainWindow] setZoom" << m_zoom;
    applyUiScale();
    saveUiZoom(m_zoom);
    showZoomBadge();
}

void MainWindow::applyUiScale()
{
    gUiScale = m_zoom;

    // 重新生成全局 QSS：把 Npx 字号/间距按缩放比放大，让控件重新布局（文字不糊）
    qApp->setStyleSheet(scaleQss(kGlobalQss));

    // 同步放大基准字体（与全局 QSS 基准 13px 一致），未内联字号的控件随之重排
    QFont f = qApp->font();
    f.setPixelSize(qRound(13 * m_zoom));
    qApp->setFont(f);

    // 左侧导航：宽度、品牌字号、间距随缩放比调整（导航按钮样式来自全局 QSS 已缩放）
    if (m_nav) {
        m_nav->setFixedWidth(si(200));
        auto *nl = qobject_cast<QVBoxLayout *>(m_nav->layout());
        if (nl) {
            nl->setContentsMargins(0, si(16), 0, si(12));
            nl->setSpacing(si(4));
        }
        if (auto *b = m_nav->findChild<QLabel *>(QStringLiteral("NavBrand")))
            b->setStyleSheet(QStringLiteral("font-size: %1; font-weight: 700; color: white; padding: 0 %2 %3;")
                                 .arg(sp(20), sp(16), sp(12)));
    }

    // 页面级缩放样式（目前重点：Inbox 页）
    if (m_inbox)
        m_inbox->applyUiScale();
}

void MainWindow::showZoomBadge()
{
    if (!m_zoomBadge)
        return;
    m_zoomBadge->setText(QStringLiteral("%1%").arg(qRound(m_zoom * 100)));
    m_zoomBadge->adjustSize();
    const QWidget *cw = centralWidget();
    const int w = cw ? cw->width() : width();
    m_zoomBadge->move(w - m_zoomBadge->width() - 16, 14);
    m_zoomBadge->show();
    m_zoomBadge->raise();
    QTimer::singleShot(1200, m_zoomBadge, [this] { m_zoomBadge->hide(); });
}

bool MainWindow::isInsideZoomable(QObject *obj) const
{
    auto *w = qobject_cast<QWidget *>(obj);
    // 主窗口内容为普通 QWidget 层级，window() 即主窗口；对话框等顶层窗口除外
    return w && w->window() == static_cast<const QWidget *>(this);
}

bool MainWindow::isTextEditingWidget(const QWidget *w) const
{
    if (!w)
        return false;
    return qobject_cast<const QLineEdit *>(w) || qobject_cast<const QTextEdit *>(w)
        || qobject_cast<const QPlainTextEdit *>(w) || qobject_cast<const QComboBox *>(w)
        || qobject_cast<const QSpinBox *>(w);
}

} // namespace awqtui
