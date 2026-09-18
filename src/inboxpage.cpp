// inboxpage.cpp
#include "inboxpage.h"
#include "ui_inboxpage.h"

#include "apiclient.h"
#include "globalshortcut.h" // raiseWindowToFront：热键弹窗抢前台
#include "theme.h"
#include "widgets.h"

#include <QApplication>
#include <QClipboard>
#include <QColor>
#include <QComboBox>
#include <QCursor>
#include <QGraphicsDropShadowEffect>
#include <QHBoxLayout>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMap>
#include <QMessageBox>
#include <QPointer>
#include <QPushButton>
#include <QRegularExpression>
#include <QScreen>
#include <QScrollBar>
#include <QSet>
#include <QSignalBlocker>
#include <QStackedLayout>
#include <QTimer>
#include <QTreeWidget>
#include <QVBoxLayout>

#include <algorithm>
#include <functional>

namespace awqtui {

namespace {
// 连接层失败（服务端不可达）才把应用判为离线；HTTP 4xx（如评论端点不存在）
// 不应误判离线——保留本地待同步即可
bool isConnectionError(QNetworkReply *r)
{
    switch (r->error()) {
    case QNetworkReply::ConnectionRefusedError:
    case QNetworkReply::RemoteHostClosedError:
    case QNetworkReply::HostNotFoundError:
    case QNetworkReply::TimeoutError:
    case QNetworkReply::NetworkSessionFailedError:
    case QNetworkReply::SslHandshakeFailedError:
        return true;
    default:
        return false;
    }
}
} // namespace

// 笔记转待办的标题提取（对齐 Android NoteTodoConverter.extractTitle）：
// 取首个非空行，剥掉标题井号/列表/引用/有序列表与行内强调标记，截 50 字
static QString extractTodoTitle(const QString &content)
{
    QString line;
    for (const QString &l : content.split(QLatin1Char('\n'))) {
        if (!l.trimmed().isEmpty()) {
            line = l;
            break;
        }
    }
    line = line.trimmed();
    static const QRegularExpression heading(QStringLiteral("^#{1,6}\\s+"));
    static const QRegularExpression bullet(QStringLiteral("^[-*+>]\\s+"));
    static const QRegularExpression ordered(QStringLiteral("^\\d+\\.\\s+"));
    line.remove(heading);
    line.remove(bullet);
    line.remove(ordered);
    line.remove(QLatin1String("**"));
    line.remove(QLatin1String("~~"));
    line.remove(QLatin1Char('`'));
    line.remove(QLatin1Char('*'));
    line = line.trimmed();
    if (line.isEmpty())
        line = content.trimmed();
    if (line.isEmpty())
        return QStringLiteral("（无标题）");
    if (line.size() > 50)
        line = line.left(50).trimmed() + QStringLiteral("…");
    return line;
}

InboxPage::InboxPage(ApiClient *api, QWidget *parent) : QWidget(parent), m_api(api)
{
    // 先加载本地缓存：即使服务端没起，历史数据也在
    m_store.load();

    m_reconnect = new QTimer(this);
    m_reconnect->setInterval(10000);
    connect(m_reconnect, &QTimer::timeout, this, &InboxPage::tryReconnect);

    buildUi();
    QTimer::singleShot(0, this, [this] { refreshAll(); });
}

InboxPage::~InboxPage()
{
    delete ui;
}

QString InboxPage::searchTerm() const
{
    return m_search->text();
}

void InboxPage::buildUi()
{
    // 静态布局来自 Qt Designer（inboxpage.ui -> ui_inboxpage.h），
    // .ui 中的边距/间距为基准值，si() 缩放几何在此重设；内联主题样式统一在 applyStyles()
    ui = new Ui::InboxPage;
    ui->setupUi(this);

    // ── 运行时缩放几何（随 UI 缩放变化，无法烘焙进 .ui） ──
    ui->TagPanel->setFixedWidth(si(m_sidebarWidth));
    ui->tagLay->setContentsMargins(si(10), si(12), si(10), si(12));
    ui->tagLay->setSpacing(si(8));
    ui->toolbar->setContentsMargins(si(20), si(16), si(16), si(12));
    ui->toolbar->setSpacing(si(10));
    ui->filterLay->setContentsMargins(si(20), 0, si(16), si(6));
    ui->filterLay->setSpacing(si(6));
    ui->emptyLay->setSpacing(si(8));
    ui->emptyLay->setAlignment(Qt::AlignCenter);
    ui->fabLay->setContentsMargins(0, 0, si(20), si(20));
    ui->search->setFixedWidth(si(240));
    ui->sortBox->setFixedWidth(si(108));
    ui->btnSidebar->setFixedSize(si(30), si(30));
    ui->btnRefresh->setFixedSize(si(30), si(30));
    ui->Fab->setFixedSize(si(56), si(56));
    ui->stack->setCurrentWidget(ui->InboxList);
    ui->filterBar->setVisible(false);

    // ── 成员别名：业务逻辑沿用 m_* 指针，静态布局归属 .ui 文件 ──
    m_tagPanel = ui->TagPanel;
    m_tagTitle = ui->TagTitle;
    m_tagTree = ui->TagTree;
    m_btnClear = ui->btnClear;
    m_title = ui->InboxTitle;
    // 主体「收件箱」大字去掉，界面更贴近背景
    if (m_title)
        m_title->setVisible(false);
    m_search = ui->search;
    m_btnSidebar = ui->btnSidebar;
    m_sort = ui->sortBox;
    m_btnCopy = ui->btnCopy;
    m_btnRefresh = ui->btnRefresh;
    m_badge = ui->badge;
    m_filterBar = ui->filterBar;
    m_filterText = ui->filterText;
    m_btnFilterUp = ui->btnFilterUp;
    m_btnFilterClear = ui->btnFilterClear;
    m_list = ui->InboxList;
    m_stack = ui->stack;
    m_emptyIcon = ui->emptyIcon;
    m_emptyText = ui->emptyText;
    m_emptyHint = ui->emptyHint;
    m_fab = ui->Fab;

    // ── 排序下拉 userData（对齐 API 字段：created / updated / content） ──
    m_sort->setItemData(0, QStringLiteral("created"));
    m_sort->setItemData(1, QStringLiteral("updated"));
    m_sort->setItemData(2, QStringLiteral("content"));

    // 卡片列表行跟随视口宽度重排（退出全屏/还原窗口时卡片右侧「⋯」不被顶出可视区）
    new ItemWidgetRelayoutFilter(m_list, 60, m_list);

    // ── 信号连接 ──
    connect(m_tagTree, &QTreeWidget::itemClicked, this, &InboxPage::onTagTreeItemClicked);
    connect(m_btnClear, &QPushButton::clicked, this, [this] { applyTagFilterPath(QString()); });
    connect(m_search, &QLineEdit::textChanged, this, &InboxPage::onSearchChanged);
    connect(m_btnSidebar, &QPushButton::clicked, this, [this] {
        m_sidebarVisible = !m_sidebarVisible;
        m_tagPanel->setVisible(m_sidebarVisible);
    });
    connect(m_sort, &QComboBox::currentIndexChanged, this, [this](int) { loadNotes(true); });
    connect(m_btnCopy, &QPushButton::clicked, this, &InboxPage::onCopyAll);
    connect(m_btnRefresh, &QPushButton::clicked, this, &InboxPage::onRefresh);
    connect(m_btnFilterUp, &QPushButton::clicked, this, [this] {
        const int slash = m_currentTag.lastIndexOf(QLatin1Char('/'));
        applyTagFilterPath(slash > 0 ? m_currentTag.left(slash) : QString());
    });
    connect(m_btnFilterClear, &QPushButton::clicked, this, [this] { applyTagFilterPath(QString()); });
    connect(m_list->verticalScrollBar(), &QScrollBar::valueChanged, this, &InboxPage::onScroll);

    // 悬浮 + 投影（受全局阴影强度控制）
    m_fabShadow = makeDropShadow(m_fab);
    connect(m_fab, &QPushButton::clicked, this, &InboxPage::onNewNote);

    applyStyles();
}

// 内联主题样式：buildUi 与 applyUiScale 共用（scaleQss/glassBg 需运行时按缩放/主题生成，
// 因此不放进 .ui）；几何尺寸（固定宽度等）在 applyUiScale 中单独重设
void InboxPage::applyStyles()
{
    // 标签侧栏
    if (m_tagPanel)
        m_tagPanel->setStyleSheet(scaleQss(QStringLiteral(
            "QWidget#TagPanel { background: transparent; }")));
    if (m_tagTitle)
        m_tagTitle->setStyleSheet(scaleQss(QStringLiteral(
            "color: %1; font-size: 10px; font-weight: 800; letter-spacing: 1.5px;"
            " padding: 0 4px 2px;")
                                               .arg(kColorFgMuted)));
    if (m_tagTree)
        m_tagTree->setStyleSheet(scaleQss(QStringLiteral(
            "QTreeWidget { background: transparent; border: none; outline: none; }"
            "QTreeWidget::branch { background: transparent; }"
            "QTreeWidget::item { padding: 5px 8px; border: none; border-radius: 8px; color: %1; }"
            "QTreeWidget::item:hover { background: %2; color: %3; }"
            "QTreeWidget::item:selected { background: %4; color: %5; }")
                                             .arg(kColorFg, kColorBgElev2, kColorFg,
                                                  withAlpha(kColorAccent, 0.16), kColorAccent)));
    if (m_filterText)
        m_filterText->setStyleSheet(scaleQss(QStringLiteral(
            "color: %1; font-size: 12px; background: %2; border: 1px solid %3;"
            " border-radius: 6px; padding: 3px 8px;")
                                               .arg(kColorAccent, glassBg(kColorBgElev), withAlpha(kColorBorder, 0.45))));
    {   // 筛选条与侧栏底部按钮：统一 hover 浮起的小圆角块（chip）
        const QString chipBtn = scaleQss(QStringLiteral(
            "QPushButton { background: transparent; border: none; border-radius: 6px;"
            " color: %1; padding: 5px 8px; font-size: 11px; text-align: left; }"
            "QPushButton:hover { background: %2; color: %3; }")
                                             .arg(kColorFgMuted, kColorBgElev2, kColorFg));
        for (QPushButton *b : {m_btnFilterUp, m_btnFilterClear})
            if (b)
                b->setStyleSheet(scaleQss(QStringLiteral(
                    "QPushButton { background: transparent; border: none; border-radius: 6px;"
                    " color: %1; padding: 5px 10px; font-size: 12px; }"
                    "QPushButton:hover { background: %2; color: %3; }")
                                                     .arg(kColorFgMuted, kColorBgElev2, kColorFg)));
        if (m_btnClear)
            m_btnClear->setStyleSheet(chipBtn);
    }

    // 工具栏
    if (m_title)
        m_title->setStyleSheet(scaleQss(QStringLiteral("font-size: 22px; font-weight: 700; color: %1;").arg(kColorFg)));
    // 搜索框：胶囊化，聚焦时 accent 描边
    if (m_search)
        m_search->setStyleSheet(scaleQss(QStringLiteral(
            "QLineEdit { background: %1; border: 1px solid transparent; border-radius: 9px;"
            " padding: 5px 12px; color: %2; font-size: 12px;"
            " selection-background-color: %3; }"
            "QLineEdit:focus { border-color: %3; background: %4; }"
            "QLineEdit:hover { border-color: %5; }")
                                         .arg(kColorBgElev2, kColorFg, kColorAccent,
                                              glassBg(kColorBgElev), withAlpha(kColorBorder, 0.7))));
    // 排序下拉：圆角无边框化，匹配搜索框胶囊；下拉列表同样圆角
    if (m_sort)
        m_sort->setStyleSheet(scaleQss(QStringLiteral(
            "QComboBox { background: %1; border: 1px solid transparent; border-radius: 9px;"
            " padding: 4px 6px 4px 12px; color: %2; font-size: 12px; }"
            "QComboBox:hover { border-color: %3; }"
            "QComboBox:focus { border-color: %4; }"
            "QComboBox::drop-down { border: none; width: 18px; }"
            "QComboBox QAbstractItemView { background: %5; border: 1px solid %3;"
            " border-radius: 8px; color: %2; outline: none;"
            " selection-background-color: %6; selection-color: %4; padding: 4px; }")
                                         .arg(kColorBgElev2, kColorFg, withAlpha(kColorBorder, 0.7),
                                              kColorAccent, glassBg(kColorBgElev), withAlpha(kColorAccent, 0.16))));
    const QString subtleBtn = QStringLiteral(
        "QPushButton { background: transparent; border: none; border-radius: 6px;"
        " color: %1; padding: 5px 10px; font-size: 12px; }"
        "QPushButton:hover { background: %2; color: %3; }");
    const QString subtleStyle = scaleQss(subtleBtn.arg(kColorFgMuted, kColorBgElev2, kColorFg));
    for (QPushButton *b : {m_btnSidebar, m_btnRefresh, m_btnCopy}) {
        if (b)
            b->setStyleSheet(subtleStyle);
    }

    // 空状态
    if (m_emptyIcon)
        m_emptyIcon->setStyleSheet(scaleQss(QStringLiteral("font-size: 42px;")));
    if (m_emptyText)
        m_emptyText->setStyleSheet(scaleQss(QStringLiteral("color: %1; font-size: 15px;").arg(kColorFgMuted)));
    if (m_emptyHint)
        m_emptyHint->setStyleSheet(scaleQss(QStringLiteral("color: %1; font-size: 12px;").arg(kColorFgMuted)));

    // 卡片列表
    if (m_list)
        m_list->setStyleSheet(scaleQss(QStringLiteral(
            "QListWidget { background: transparent; border: none; }"
            "QListWidget::item { background: transparent; border: none; padding: 0; margin: 0; }")));

    // 悬浮 +
    if (m_fab) {
        m_fab->setStyleSheet(scaleQss(QStringLiteral(
            "QPushButton#Fab { background: %1; color: white; border: none; border-radius: 28px;"
            " font-size: 28px; font-weight: 400; }"
            "QPushButton#Fab:hover { background: %2; }")
                                         .arg(kColorAccent, kColorAccentHover)));
        // 阴影随强度增删（受全局阴影强度控制）
        clearDropShadow(m_fab, m_fabShadow);
        m_fabShadow = makeDropShadow(m_fab);
    }
}

void InboxPage::applyUiScale()
{
    // 固定几何随缩放重设
    if (m_tagPanel)
        m_tagPanel->setFixedWidth(si(m_sidebarWidth));
    if (m_search)
        m_search->setFixedWidth(si(240));
    if (m_sort)
        m_sort->setFixedWidth(si(108));
    for (QPushButton *b : {m_btnSidebar, m_btnRefresh})
        if (b)
            b->setFixedSize(si(30), si(30));
    if (m_fab)
        m_fab->setFixedSize(si(56), si(56));

    applyStyles();

    if (m_badge)
        m_badge->applyUiScale();

    // 重渲染列表：卡片（NoteCard）在创建时按当前缩放比取样式（缩放变化必须无条件重建）
    applyClientFilter(true);
}

// ------------------------------------------------------------------ //

void InboxPage::refreshAll()
{
    // 刷新/初次加载：给本次重建的卡片加入场淡入（过滤、翻页不触发）
    m_animateCards = true;
    if (isOffline()) {
        // 服务端不可用：直接渲染本地缓存，并尝试重连
        rebuildTagsFromLocal();
        renderLocal();
        startReconnect();
        return;
    }
    // 后台静默触发局域网拉取（与列表加载并行，成功后追加一轮加载，远端变更即刷即现）
    triggerLanPull();
    loadTagTree();
    loadNotes(true);
}

// 对齐 Android 8a2d694 LanPull.syncAllPairedNow：GET /devices → 逐台
// paired && 非本机 → POST /devices/<id>/sync（双向拉合推）；单台失败不影响其余；
// 有成功台数时再 loadNotes(true) 重载一次。全程静默，10 秒节流防 F5 连打。
void InboxPage::triggerLanPull()
{
    if (isOffline() || m_lanPullInflight)
        return;
    if (m_lanPullThrottle.isValid() && m_lanPullThrottle.elapsed() < 10000)
        return;
    m_lanPullThrottle.start();
    m_lanPullInflight = true;
    QNetworkReply *r = m_api->getSyncDevices();
    connect(r, &QNetworkReply::finished, this, [this, r] {
        QJsonDocument doc;
        QString err;
        if (!ApiClient::parseReply(r, &doc, &err)) {
            m_lanPullInflight = false;
            return;
        }
        QList<SyncDevice> targets;
        const auto arr = doc.array();
        for (const auto &v : arr) {
            if (!v.isObject())
                continue;
            const SyncDevice d = SyncDevice::fromJson(v.toObject());
            if (d.paired && !d.isSelf)
                targets << d;
        }
        if (targets.isEmpty()) {
            m_lanPullInflight = false;
            return;
        }
        auto remaining = new int(targets.size());
        auto okCount = new int(0);
        for (const SyncDevice &d : targets) {
            QNetworkReply *rs = m_api->triggerSync(d.id);
            connect(rs, &QNetworkReply::finished, this, [this, rs, remaining, okCount] {
                QJsonDocument dd;
                QString e2;
                if (ApiClient::parseReply(rs, &dd, &e2))
                    ++(*okCount);
                if (--(*remaining) > 0)
                    return;
                const int ok = *okCount;
                delete remaining;
                delete okCount;
                m_lanPullInflight = false;
                if (ok > 0 && m_online) {
                    setStatus(StatusBadge::State::Syncing,
                              QStringLiteral("已从局域网 %1 台设备拉取变更…").arg(ok));
                    // 用 loadNotes 而非 refreshAll：不递归触发下一轮局域网拉取
                    loadNotes(true);
                }
            });
        }
    });
}

// ------------------------------------------------------------------ //
// 离线优先：本地存储
// ------------------------------------------------------------------ //

void InboxPage::renderLocal()
{
    ++m_reqGen; // 使在途的服务端请求回包过期，避免它们覆盖本地渲染
    m_notes.clear();
    const QList<Note> all = m_store.notes();
    const QString search = m_search->text().trimmed().toLower();
    const QString sortBy = m_sort ? m_sort->currentData().toString() : QStringLiteral("created");

    // 客户端过滤：搜索 + 层级标签路径（段边界前缀匹配，与服务的 ?tag= 语义一致）
    QList<Note> visible;
    for (const Note &n : all) {
        if (!search.isEmpty() && !n.content.toLower().contains(search))
            continue;
        if (!tagPathMatches(n.tags, m_currentTag))
            continue;
        visible << n;
    }

    // 注入本地置顶标记（置顶优先排序在 applyClientFilter 里做）
    for (Note &n : visible)
        n.pinned = m_store.isPinned(n.id);

    // 客户端排序（与服务端语义一致：新在前；content 按字母升序）
    std::sort(visible.begin(), visible.end(), [sortBy](const Note &a, const Note &b) {
        if (sortBy == QLatin1String("content"))
            return a.content.toLower() < b.content.toLower();
        const QString ka = (sortBy == QLatin1String("updated")) ? a.updatedAt : a.createdAt;
        const QString kb = (sortBy == QLatin1String("updated")) ? b.updatedAt : b.createdAt;
        if (ka != kb)
            return ka > kb;
        return a.id > b.id;
    });

    m_notes = visible;
    m_hasMore = false;
    m_loading = false;
    applyClientFilter();
    emit noteCountChanged(m_notes.size());
    updateOfflineBadge();
}

void InboxPage::rebuildTagsFromLocal()
{
    QMap<QString, int> counts;
    QMap<QString, QString> last;
    for (const Note &n : m_store.notes()) {
        for (const QString &t : n.tags) {
            counts[t] += 1;
            if (n.updatedAt > last.value(t))
                last[t] = n.updatedAt;
        }
    }
    m_tags.clear();
    for (auto it = counts.constBegin(); it != counts.constEnd(); ++it)
        m_tags << DetailedTag{it.key(), it.value(), last.value(it.key())};
    // 层级标签树（本地构建：精确计数 → 前缀含子孙计数）
    QMap<QString, qint64> exact;
    for (auto it = counts.constBegin(); it != counts.constEnd(); ++it)
        exact.insert(it.key(), it.value());
    m_tagRoots = buildTagTreeFromExact(exact);
    rebuildTagTree();
}

// 在线模式只拉标签树（refreshAll -> loadTagTree），此前从不填 m_tags，导致新建笔记
// 对话框的 #tag 联想池永远为空（离线反而有 rebuildTagsFromLocal 兜底）。这里把树
// 拍平回 m_tags，两种模式的联想行为保持一致。
void InboxPage::rebuildFlatTagsFromTree()
{
    m_tags.clear();
    std::function<void(const QList<TagNode> &)> walk = [&](const QList<TagNode> &nodes) {
        for (const TagNode &n : nodes) {
            if (!n.path.isEmpty())
                m_tags << DetailedTag{n.path, n.count, QString()};
            walk(n.children);
        }
    };
    walk(m_tagRoots);
}

void InboxPage::createLocal(const QString &content, const QStringList &tags)
{
    const qint64 id = m_store.insertLocal(content, tags, m_api->deviceId());
    m_store.save();
    // 渲染完成后定位并高亮新笔记（离线路径与在线创建后的定位一致）
    m_pendingJumpId = id;
    rebuildTagsFromLocal();
    renderLocal();
}

void InboxPage::updateLocal(qint64 id, const QString &content, const QStringList &tags)
{
    m_store.updateLocal(id, content, tags);
    m_store.save();
    m_pendingJumpId = id;
    rebuildTagsFromLocal();
    renderLocal();
}

void InboxPage::deleteLocal(qint64 id)
{
    m_store.markDeleted(id);
    m_store.save();
    rebuildTagsFromLocal();
    renderLocal();
}

void InboxPage::pushDirty()
{
    const QList<Note> dirty = m_store.dirtyNotes();
    if (dirty.isEmpty()) {
        // 没有笔记改动时，若还有待同步评论则直接补推评论
        if (m_store.pendingCommentCount() > 0)
            pushComments();
        return;
    }
    m_pendingPush = dirty.size();
    setStatus(StatusBadge::State::Syncing,
              QStringLiteral("补推 %1 条离线改动…").arg(m_pendingPush));

    for (const Note &n : dirty) {
        if (n.pendingOp == QLatin1String("create")) {
            QNetworkReply *r = m_api->createNote(n.content, n.tags);
            connect(r, &QNetworkReply::finished, this, [this, r, localId = n.id] {
                QJsonDocument doc;
                QString err;
                if (!ApiClient::parseReply(r, &doc, &err)) {
                    pushFailed();
                    return;
                }
                const qint64 serverId = doc.object().value(QLatin1String("id")).toVariant().toLongLong();
                m_store.remapId(localId, serverId);
                m_store.clearPending(serverId);
                m_store.save();
                pushDone();
            });
        } else if (n.pendingOp == QLatin1String("update")) {
            QNetworkReply *r = m_api->updateNote(n.id, n.content, n.tags);
            connect(r, &QNetworkReply::finished, this, [this, r, id = n.id] {
                QJsonDocument doc;
                QString err;
                if (!ApiClient::parseReply(r, &doc, &err)) {
                    pushFailed();
                    return;
                }
                m_store.clearPending(id);
                m_store.save();
                pushDone();
            });
        } else if (n.pendingOp == QLatin1String("delete")) {
            QNetworkReply *r = m_api->deleteNote(n.id);
            connect(r, &QNetworkReply::finished, this, [this, r, id = n.id] {
                QJsonDocument doc;
                QString err;
                if (!ApiClient::parseReply(r, &doc, &err)) {
                    pushFailed();
                    return;
                }
                m_store.clearPending(id);
                m_store.save();
                pushDone();
            });
        }
    }
}

void InboxPage::pushDone()
{
    if (--m_pendingPush <= 0) {
        m_pendingPush = 0;
        // 笔记已推完，若还有待同步评论则继续补推，全部完成后再回在线态重拉
        if (m_store.pendingCommentCount() > 0) {
            pushComments();
        } else {
            refreshAll();
        }
    }
}

void InboxPage::pushFailed()
{
    fprintf(stderr, "[awqtui-push] pushFailed -> offline, keep local\n");
    // 推送失败：保留脏数据，回到离线态，等下次重连再补推
    m_pendingPush = 0;
    m_online = false;
    setStatus(StatusBadge::State::Disconnected, QStringLiteral("离线改动推送失败，已保留本地"));
    renderLocal();
    startReconnect();
}

void InboxPage::tryReconnect()
{
    if (m_online || m_loading)
        return;
    setStatus(StatusBadge::State::Syncing, QStringLiteral("正在重新连接…"));
    QNetworkReply *r = m_api->getNotes(1, 0, QString(), QString(), QString());
    // 本机“连接被拒”回报慢，2.5s 没回就放弃本次探测（abort 会触发 finished）
    // 注意：探测请求可能在 2.5s 内已完成（成功或连接被拒）并被 parseReply 里 deleteLater
    // 销毁，定时器回调若再访问裸指针 r 就是 use-after-free。必须用 QPointer 防悬垂。
    QPointer<QNetworkReply> guard(r);
    QTimer::singleShot(2500, this, [guard] {
        if (guard && !guard->isFinished())
            guard->abort();
    });
    connect(r, &QNetworkReply::finished, this, [this, r] {
        QJsonDocument doc;
        QString err;
        if (!ApiClient::parseReply(r, &doc, &err)) {
            // 仍未恢复，保持离线；定时器继续跑
            updateOfflineBadge();
            return;
        }
        m_online = true;
        m_reconnect->stop();
        if (m_store.pendingCount() > 0)
            pushDirty();
        else
            refreshAll();
    });
}

void InboxPage::startReconnect()
{
    if (!m_online)
        m_reconnect->start();
}

void InboxPage::updateOfflineBadge()
{
    const int p = m_store.pendingCount();
    if (m_online) {
        // 在线但仍有推不动的改动（如服务端无评论端点）：保持已连接，仅提示待同步
        setStatus(p > 0 ? StatusBadge::State::Connected : StatusBadge::State::Connected,
                  p > 0 ? QStringLiteral("待同步 %1 条").arg(p) : QString());
        return;
    }
    if (p > 0)
        setStatus(StatusBadge::State::Disconnected,
                  QStringLiteral("已离线 · 本地待同步 %1 条").arg(p));
    else
        setStatus(StatusBadge::State::Disconnected, QStringLiteral("已离线 · 本地已存"));
}

void InboxPage::loadDetailedTags()
{
    QNetworkReply *r = m_api->getDetailedTags();
    if (!r)
        return;
    connect(r, &QNetworkReply::finished, this, [this, r] {
        QJsonDocument doc;
        QString err;
        if (!ApiClient::parseReply(r, &doc, &err)) {
            setStatus(StatusBadge::State::Error, err);
            return;
        }
        m_tags.clear();
        for (const auto &v : doc.array()) {
            if (v.isObject())
                m_tags << DetailedTag::fromJson(v.toObject());
        }
        // 树端点不可用（旧服务端）时的回退：用扁平精确计数自建层级树
        QMap<QString, qint64> exact;
        for (const DetailedTag &t : m_tags)
            exact.insert(t.name, t.count);
        m_tagRoots = buildTagTreeFromExact(exact);
        rebuildTagTree();
    });
}

void InboxPage::loadTagTree()
{
    QNetworkReply *r = m_api->getTagTree();
    if (!r) {
        loadTags();
        return;
    }
    connect(r, &QNetworkReply::finished, this, [this, r] {
        QJsonDocument doc;
        QString err;
        if (!ApiClient::parseReply(r, &doc, &err)) {
            // 服务端过旧（无 /inbox/tags/tree）：回退到 detailed 标签自建树
            loadDetailedTags();
            return;
        }
        m_tagRoots.clear();
        const auto arr = doc.object().value(QLatin1String("tags")).toArray();
        for (const auto &v : arr) {
            if (v.isObject())
                m_tagRoots << TagNode::fromJson(v.toObject());
        }
        rebuildFlatTagsFromTree();
        rebuildTagTree();
    });
}

void InboxPage::loadTags()
{
    QNetworkReply *r = m_api->getTags();
    if (!r)
        return;
    connect(r, &QNetworkReply::finished, this, [this, r] {
        QJsonDocument doc;
        QString err;
        if (!ApiClient::parseReply(r, &doc, &err))
            return;
        // tags 端点可能只返回名字数组；detailed 返回 count。这里以 detailed 为主
        Q_UNUSED(doc);
        loadDetailedTags();
    });
}

// 与服务端 get_tag_tree_db 同构：按「/」分段累计每个前缀路径的含子孙计数；
// 根节点取所有无「/」的前缀路径（纯中间节点也会出现）；children 按路径排序
QList<TagNode> InboxPage::buildTagTreeFromExact(const QMap<QString, qint64> &exact)
{
    QMap<QString, qint64> incl;                 // 前缀路径 → 含子孙计数
    QMap<QString, QStringList> childrenMap;     // 父路径 → 直接子路径
    for (auto it = exact.constBegin(); it != exact.constEnd(); ++it) {
        const QString tag = it.key();
        QString path;
        const QStringList segs = tag.split(QLatin1Char('/'), Qt::SkipEmptyParts);
        for (const QString &segRaw : segs) {
            const QString seg = segRaw.trimmed();
            if (seg.isEmpty())
                continue;
            const QString parent = path;
            path = path.isEmpty() ? seg : path + QLatin1Char('/') + seg;
            childrenMap[parent] << path;
            incl[path] += it.value();
        }
    }
    std::function<QList<TagNode>(const QString &)> build =
        [&build, &incl, &childrenMap](const QString &parent) -> QList<TagNode> {
        QList<TagNode> out;
        for (const QString &child : childrenMap.value(parent)) {
            TagNode n;
            n.path = child;
            n.count = incl.value(child);
            n.children = build(child);
            out << n;
        }
        std::sort(out.begin(), out.end(), [](const TagNode &a, const TagNode &b) {
            return a.path < b.path;
        });
        return out;
    };
    // 根节点：parent 为空串的 children（即无「/」的路径），从 childrenMap 里直接取
    return build(QString());
}

void InboxPage::rebuildTagTree()
{
    // 标签树内容签名（路径 + 计数，含当前筛选路径）：一致就不重建。
    // 重建会 clear() 掉整棵树并重新展开，侧栏会明显闪一下（同步轮询每次都会走到这里）。
    QString sig = m_currentTag + QLatin1Char('\n');
    std::function<void(const QList<TagNode> &)> appendSig = [&](const QList<TagNode> &nodes) {
        for (const TagNode &n : nodes) {
            sig += n.path + QLatin1Char('\x1f') + QString::number(n.count) + QLatin1Char('\n');
            appendSig(n.children);
        }
    };
    appendSig(m_tagRoots);
    if (sig == m_tagTreeSig)
        return;
    m_tagTreeSig = sig;

    m_tagTree->blockSignals(true);
    m_tagTree->clear();
    std::function<void(QTreeWidgetItem *, const QList<TagNode> &)> addNodes =
        [this, &addNodes](QTreeWidgetItem *parentItem, const QList<TagNode> &nodes) {
            for (const TagNode &n : nodes) {
                // 顶层项必须以树为父（QTreeWidgetItem(nullptr) 是孤儿，不会出现在树里）
                auto *item = parentItem ? new QTreeWidgetItem(parentItem) : new QTreeWidgetItem(m_tagTree);
                const int slash = n.path.lastIndexOf(QLatin1Char('/'));
                const QString lastSeg = slash >= 0 ? n.path.mid(slash + 1) : n.path;
                item->setText(0, QStringLiteral("#%1 (%2)").arg(lastSeg).arg(n.count));
                item->setToolTip(0, n.path);
                item->setData(0, Qt::UserRole, n.path);
                addNodes(item, n.children);
            }
        };
    addNodes(nullptr, m_tagRoots);
    m_tagTree->expandAll();
    // 恢复当前筛选路径的选中态
    if (!m_currentTag.isEmpty()) {
        std::function<QTreeWidgetItem *(QTreeWidgetItem *, const QString &)> find =
            [&find, this](QTreeWidgetItem *root, const QString &path) -> QTreeWidgetItem * {
            const int count = root ? root->childCount() : m_tagTree->topLevelItemCount();
            for (int i = 0; i < count; ++i) {
                QTreeWidgetItem *it = root ? root->child(i) : m_tagTree->topLevelItem(i);
                if (!it)
                    continue;
                if (it->data(0, Qt::UserRole).toString() == path)
                    return it;
                if (QTreeWidgetItem *sub = find(it, path))
                    return sub;
            }
            return nullptr;
        };
        if (QTreeWidgetItem *cur = find(nullptr, m_currentTag))
            m_tagTree->setCurrentItem(cur);
    }
    m_tagTree->blockSignals(false);
}

void InboxPage::onTagTreeItemClicked(QTreeWidgetItem *item, int column)
{
    const QString path = item->data(column, Qt::UserRole).toString();
    // 再点当前筛选中的标签 = 取消筛选（与 Android 再点同标签/✕ 取消一致）
    applyTagFilterPath(path == m_currentTag ? QString() : path);
}

void InboxPage::updateFilterBar()
{
    if (!m_filterBar)
        return;
    m_filterBar->setVisible(!m_currentTag.isEmpty());
    if (m_currentTag.isEmpty())
        return;
    const QStringList segs = m_currentTag.split(QLatin1Char('/'), Qt::SkipEmptyParts);
    m_filterText->setText(QStringLiteral("仅显示 #%1").arg(segs.join(QStringLiteral(" / "))));
    if (m_btnFilterUp)
        m_btnFilterUp->setVisible(segs.size() > 1);
}

void InboxPage::applyTagFilterPath(const QString &path)
{
    m_currentTag = path;
    updateFilterBar();
    loadNotes(true);
}

void InboxPage::loadNotes(bool reset)
{
    if (m_loading)
        return;
    if (isOffline()) {
        renderLocal();
        return;
    }
    if (reset) {
        ++m_reqGen;
        m_offset = 0;
        m_hasMore = true;
        // 这里刻意不清空 m_list / m_notes：等回包落地后整体替换。
        // 提前清空会让列表在请求往返期间空白一下（同步轮询每 15s 就触发一次，表现为闪动）。
    }
    if (!m_hasMore)
        return;
    m_loading = true;
    setStatus(StatusBadge::State::Syncing, QStringLiteral("加载中…"));

    // 层级标签路径筛选：交给服务端 ?tag= 段边界前缀匹配（bc2647b）
    const QString sortBy = m_sort->currentData().toString();
    QNetworkReply *r = m_api->getNotes(m_limit, m_offset, m_currentTag, m_search->text(), sortBy);
    const int gen = m_reqGen;

    // 兜底：本机“连接被拒”可能要数秒才回报，超过阈值直接判离线，避免界面长时间卡在“加载中”
    QTimer::singleShot(3000, this, [this, gen] {
        if (gen != m_reqGen)
            return; // 已被新请求或本地渲染取代
        if (!m_loading)
            return; // 请求已正常完成
        m_loading = false;
        m_online = false;
        ++m_reqGen; // 使在途回包过期
        setStatus(StatusBadge::State::Disconnected, QStringLiteral("服务端无响应，改用本地缓存"));
        rebuildTagsFromLocal();
        renderLocal();
        startReconnect();
    });

    connect(r, &QNetworkReply::finished, this, [this, r, gen, reset] {
        if (gen != m_reqGen) {
            r->deleteLater();
            return; // 过期回包：期间已切离线/已重新加载
        }
        QJsonDocument doc;
        QString err;
        if (!ApiClient::parseReply(r, &doc, &err)) {
            m_loading = false;
            m_online = false;
            setStatus(StatusBadge::State::Disconnected,
                      QStringLiteral("服务端不可用，改用本地缓存"));
            rebuildTagsFromLocal();
            renderLocal();
            startReconnect();
            return;
        }
        m_loading = false;
        m_online = true;
        QList<Note> batch;
        for (const auto &v : doc.array()) {
            if (v.isObject())
                batch << Note::fromJson(v.toObject());
        }
        // 拉到的服务端数据并入本地镜像（本地脏改动不会被覆盖）
        m_store.applyServerNotes(batch);
        m_store.save();
        m_hasMore = (batch.size() >= m_limit);
        // 翻页推进：下一页跳过本次已取回的条目。此前只在 reset 时清零、从不递增，
        // 导致滚动加载永远重复请求第一页（列表里出现重复卡片且越翻越多）
        m_offset += batch.size();
        appendNotes(batch, reset);
        setStatus(StatusBadge::State::Connected);
        emit noteCountChanged(m_notes.size());

        // 若本地还有积压的离线改动且没有正在补推，则推送到服务端
        if (m_pendingPush == 0 && m_store.pendingCount() > 0)
            pushDirty();
    });
}

// reset=true：整批替换（首屏/刷新/筛选变更）；false：追加下一页。
// 列表控件的清空与重建统一交给 applyClientFilter，避免请求期间出现空白帧。
void InboxPage::appendNotes(const QList<Note> &notes, bool reset)
{
    if (reset)
        m_notes.clear();
    // offset 分页在两页请求之间服务端数据变动时会错位（新笔记插到前面会把
    // 上一页末尾的笔记挤进下一页），按 id 去重避免同一笔记出现两张卡片
    QSet<qint64> seen;
    if (!reset) {
        for (const Note &e : m_notes)
            seen.insert(e.id);
    }
    for (Note n : notes) {
        if (seen.contains(n.id))
            continue;
        seen.insert(n.id);
        n.pinned = m_store.isPinned(n.id);
        // 服务端笔记 JSON 不携带 comment_parent_id（applyServerNotes 仅在本地镜像保留）：
        // 从本地镜像补回，否则评论笔记在收件箱里不会显示「被评论笔记」的引用预览
        if (const Note *local = m_store.find(n.id))
            n.commentParentId = local->commentParentId;
        m_notes << n;
    }
    // 多标签客户端 OR 过滤后重新渲染
    applyClientFilter();
}

void InboxPage::applyClientFilter(bool force)
{
    // 防重入：循环内 addItem 会触发 verticalScrollBar::valueChanged → onScroll →
    // loadNotes → 离线时 renderLocal → 本函数重入，内层 m_list->clear() 会删除外层
    // 刚 addItem 的 item，外层继续 setItemWidget 即 use-after-free 崩溃（三个 dump 证实）。
    if (m_rebuilding)
        return;
    // 标签/搜索过滤已在数据源头完成（在线 ?tag= 服务端过滤、离线 renderLocal 客户端过滤）
    QList<Note> visible = m_notes;
    // 置顶优先（稳定分区：置顶笔记排在最前，其余保持原顺序）
    QList<Note> ordered;
    for (const Note &n : visible)
        if (n.pinned)
            ordered << n;
    for (const Note &n : visible)
        if (!n.pinned)
            ordered << n;
    visible = ordered;

    // 内容签名：与上次渲染完全一致说明画面上不会有任何变化 → 不重建。
    // 同步轮询/局域网拉取会周期性回到这里，但绝大多数轮次数据并没有变，
    // 重建会清空列表（闪一下）并把滚动位置、卡片顺序全部推倒重来。
    // 注：签名前缀固定非空，保证「空列表」也能正常渲染空状态页。
    QString sig = QStringLiteral("inbox\n");
    for (const Note &n : visible) {
        sig += QString::number(n.id) + QLatin1Char('\x1f') + n.content + QLatin1Char('\x1f')
               + n.tags.join(QLatin1Char('\x1e')) + QLatin1Char('\x1f')
               + (n.pinned ? QLatin1Char('1') : QLatin1Char('0')) + QLatin1Char('\x1f')
               + (n.deleted ? QLatin1Char('1') : QLatin1Char('0')) + QLatin1Char('\x1f')
               + QString::number(n.commentParentId) + QLatin1Char('\x1f') + n.updatedAt + QLatin1Char('\x1f')
               + (n.commentParentId != 0 ? parentPreview(n.commentParentId) : QString())
               + QLatin1Char('\n');
    }
    if (!force && m_pendingJumpId == 0 && sig == m_renderSig) {
        // 本次「刷新」没有可见变化：顺手清掉待消费的入场动画，避免下次真正的重建莫名淡入
        m_animateCards = false;
        return;
    }
    m_renderSig = sig;

    m_rebuilding = true;
    m_visibleIds.clear();
    m_list->clear();
    for (const Note &n : visible) {
        m_visibleIds << n.id;
        QWidget *card = makeCard(n);
        // Inset 分组：卡片左右缩进、上下留缝，模拟 iOS InsetGroupedListStyle
        auto *item = new QListWidgetItem;
        auto *wrap = new QWidget;
        auto *wrapLay = new QVBoxLayout(wrap);
        wrapLay->setContentsMargins(si(20), si(5), si(20), si(6));
        wrapLay->setSpacing(0);
        wrapLay->addWidget(card);
        item->setSizeHint(QSize(0, wrap->sizeHint().height()));
        m_list->addItem(item);
        m_list->setItemWidget(item, wrap);
        // 入场淡入：仅刷新/初次加载时（阴影在卡片上、透明度在包裹层上，互不冲突）
        if (m_animateCards)
            fadeInWidget(wrap, 180);
    }
    m_animateCards = false;
    if (visible.isEmpty())
        m_stack->setCurrentIndex(1);
    else
        m_stack->setCurrentIndex(0);

    // 渲染完成后，若有待跳转目标（此前被搜索/标签过滤），滚动定位并高亮
    if (m_pendingJumpId != 0) {
        const qint64 target = m_pendingJumpId;
        m_pendingJumpId = 0;
        jumpToNote(target);
    }
    m_rebuilding = false;
}

QWidget *InboxPage::makeCard(const Note &n)
{
    auto *card = new NoteCard(n, n.pinned);
    connect(card, &NoteCard::editRequested, this, &InboxPage::onEditNote);
    connect(card, &NoteCard::deleteRequested, this, &InboxPage::onDeleteNote);
    connect(card, &NoteCard::commentRequested, this, &InboxPage::onComment);
    connect(card, &NoteCard::togglePinnedRequested, this, &InboxPage::onTogglePinned);
    connect(card, &NoteCard::detailsRequested, this, &InboxPage::onNoteDetails);
    connect(card, &NoteCard::convertToTodoRequested, this, &InboxPage::onConvertToTodo);
    connect(card, &NoteCard::taskToggled, this, &InboxPage::onTaskToggled);
    connect(card, &NoteCard::parentReferenceClicked, this, &InboxPage::onParentReferenceClicked);
    // 点击正文里的 #标签（层级 tag 每段可点）→ 按路径筛选；再点同路径取消
    connect(card, &NoteCard::tagClicked, this, [this](const QString &path) {
        applyTagFilterPath(path == m_currentTag ? QString() : path);
    });
    // 评论笔记：在内容下方展示被评论笔记的引用预览
    if (n.commentParentId != 0) {
        const QString preview = parentPreview(n.commentParentId);
        if (!preview.isEmpty())
            card->setParentReference(n.commentParentId, preview);
    }
    return card;
}

QString InboxPage::parentPreview(qint64 parentId) const
{
    const Note *p = m_store.find(parentId);
    if (!p || p->deleted)
        return QString();
    QString s = p->content;
    // 轻量去除常见 markdown 标记，保留可读文本（# 标签保留）
    s.remove(QRegularExpression(QStringLiteral("[`*_~>]")));
    s.remove(QRegularExpression(QStringLiteral("^#{1,6}\\s+")));
    // 折叠换行/空白为单个空格
    s = s.simplified();
    // 100 字截断
    if (s.size() > 100)
        s = s.left(100).trimmed() + QStringLiteral("…");
    return s;
}

void InboxPage::jumpToNote(qint64 id)
{
    const int idx = m_visibleIds.indexOf(id);
    if (idx < 0 || idx >= m_list->count())
        return;
    QListWidgetItem *item = m_list->item(idx);
    m_list->scrollToItem(item, QAbstractItemView::PositionAtCenter);
    QWidget *wrap = m_list->itemWidget(item);
    if (!wrap)
        return;
    if (auto *card = wrap->findChild<NoteCard *>())
        card->flashHighlight();
}

void InboxPage::onParentReferenceClicked(qint64 parentId)
{
    if (m_visibleIds.contains(parentId)) {
        jumpToNote(parentId);
        return;
    }
    // 目标不在当前可见列表（被搜索/标签过滤，或尚未加载）：本地也不存在则无法跳转
    if (!m_store.find(parentId))
        return;
    // 清除搜索与标签筛选并重载，重载完成后跳转
    m_pendingJumpId = parentId;
    {
        const QSignalBlocker bSearch(m_search);
        m_search->clear();
    }
    applyTagFilterPath(QString());
}

void InboxPage::onScroll()
{
    auto *bar = m_list->verticalScrollBar();
    if (bar->value() >= bar->maximum() - 40)
        // 延迟到事件循环再加载：避免在 applyClientFilter 循环内（addItem 引发的
        // valueChanged 同步回调）同步重入 loadNotes → renderLocal → 重建列表，
        // 从而清除外层循环刚 addItem 的 item 造成 use-after-free。
        QTimer::singleShot(0, this, [this] { loadNotes(false); });
}

void InboxPage::onSearchChanged()
{
    QTimer::singleShot(200, this, [this] { loadNotes(true); });
}

void InboxPage::onSortChanged()
{
    loadNotes(true);
}

void InboxPage::onRefresh()
{
    refreshAll();
}

void InboxPage::onNewNote()
{
    // 单例：已有新建笔记窗口时，直接置前并返回，不重复创建
    if (m_newNoteDialog && m_newNoteDialog->isVisible()) {
        raiseWindowToFront(m_newNoteDialog);
        return;
    }

    QStringList existing;
    for (const DetailedTag &t : m_tags)
        existing << t.name;
    m_newNoteDialog = new NoteEditorDialog(QString(), existing, QStringLiteral("新建笔记"), this);

    // 把对话框定位到鼠标所在的屏幕
    const QPoint cursorPos = QCursor::pos();
    if (QScreen *screen = QGuiApplication::screenAt(cursorPos)) {
        const QRect avail = screen->availableGeometry();
        const QSize dlgSize = m_newNoteDialog->size();
        const int x = avail.x() + (avail.width() - dlgSize.width()) / 2;
        const int y = avail.y() + (avail.height() - dlgSize.height()) / 2;
        m_newNoteDialog->move(x, y);
    }

    // 全局热键触发时主窗口多半隐藏/失焦，Windows 前台锁会把 exec() 弹出的模态框
    // 压到当前前台窗口后面且不给键盘焦点（表现为 Alt+N 要按两次才出来）。先 show，
    // 借热键属于用户输入的窗口期 SetForegroundWindow 抢前台，再进模态事件循环。
    m_newNoteDialog->show();
    raiseWindowToFront(m_newNoteDialog);

    // 对话框销毁前记下所在屏：发送结果气泡要弹回同一块屏
    QScreen *dlgScreen = m_newNoteDialog->screen();
    if (m_newNoteDialog->exec() != QDialog::Accepted) {
        m_newNoteDialog->deleteLater();
        m_newNoteDialog.clear();
        return;
    }
    const QString text = m_newNoteDialog->text();
    m_newNoteDialog->deleteLater();
    m_newNoteDialog.clear();
    if (text.isEmpty())
        return;
    const QStringList tags = extractTags(text);

    if (isOffline()) {
        // 服务端不可用：直接写入本地，标记待同步
        createLocal(text, tags);
        showToast(QStringLiteral("✓ 已保存 · 待同步"), dlgScreen);
        return;
    }
    QNetworkReply *r = m_api->createNote(text, tags);
    connect(r, &QNetworkReply::finished, this, [this, r, text, tags, dlgScreen] {
        QJsonDocument doc;
        QString err;
        if (!ApiClient::parseReply(r, &doc, &err)) {
            // 请求失败（服务端可能刚挂）：落本地并切离线
            m_online = false;
            startReconnect();
            createLocal(text, tags);
            showToast(QStringLiteral("✓ 已保存 · 待同步"), dlgScreen);
            return;
        }
        m_store.applyServerNotes({Note::fromJson(doc.object())});
        m_store.save();
        showToast(QStringLiteral("✓ 已发送"), dlgScreen);
        // 新建完成：刷新后定位并高亮新笔记（Android refreshAndScrollToNote 语义；
        // 若新笔记不属于当前筛选/搜索，refreshAll 重载后不在列表里则静默跳过）
        m_pendingJumpId = Note::fromJson(doc.object()).id;
        refreshAll();
    });
}

void InboxPage::openNewNote()
{
    onNewNote();
}

void InboxPage::onEditNote(qint64 id)
{
    Note note;
    for (const Note &n : m_notes) {
        if (n.id == id) {
            note = n;
            break;
        }
    }
    QStringList existing;
    for (const DetailedTag &t : m_tags)
        existing << t.name;
    NoteEditorDialog dlg(note.content, existing, QStringLiteral("编辑笔记"), this);
    if (dlg.exec() != QDialog::Accepted)
        return;
    applyContent(id, dlg.text());
}

void InboxPage::onTogglePinned(qint64 id)
{
    m_store.setPinned(id, !m_store.isPinned(id));
    m_store.save();
    // 即时更新内存列表并重排（置顶是本地行为，无需网络往返）
    for (Note &n : m_notes) {
        if (n.id == id) {
            n.pinned = !n.pinned;
            break;
        }
    }
    applyClientFilter();
}

void InboxPage::onNoteDetails(qint64 id)
{
    // 元信息来自本地镜像/内存列表（含 createdAt/deviceId 等），立即可显示
    Note note;
    bool found = false;
    for (const Note &n : m_notes) {
        if (n.id == id) {
            note = n;
            found = true;
            break;
        }
    }
    if (!found) {
        if (const Note *p = m_store.find(id))
            note = *p;
        else
            return;
    }

    auto *dlg = new NoteDetailsDialog(note, this);
    dlg->setAttribute(Qt::WA_DeleteOnClose);
    // 恢复 = 把历史版本内容当作一次普通编辑提交（走 applyContent，离线也能兜底）
    connect(dlg, &NoteDetailsDialog::restoreRequested, this,
            [this](qint64 noteId, const QString &content) { applyContent(noteId, content); });
    // 点击标签面包屑某一段 → 关闭详情并按路径筛选
    connect(dlg, &NoteDetailsDialog::tagClicked, this, [this, dlg](const QString &path) {
        dlg->close();
        applyTagFilterPath(path == m_currentTag ? QString() : path);
    });
    // 「转为待办」：关闭详情后执行（先建 Todo 再删原笔记）
    connect(dlg, &NoteDetailsDialog::convertRequested, this, [this, dlg](qint64 noteId) {
        dlg->close();
        onConvertToTodo(noteId);
    });
    dlg->show();

    // 对话框先弹出（元信息立即可见），历史版本异步回填；QPointer 防止提前关闭后悬空访问
    QPointer<NoteDetailsDialog> guard(dlg);

    // 历史版本由服务端维护：离线或本地尚未同步的新建（负 id）没有可查的历史
    if (id < 0 || isOffline()) {
        dlg->setHistoryUnavailable(id < 0
                                       ? QStringLiteral("本地新建的笔记尚未同步到服务端，暂无历史版本。")
                                       : QStringLiteral("当前离线，无法获取服务端的历史版本。"));
    } else {
        QNetworkReply *r = m_api->getNoteHistory(id);
        connect(r, &QNetworkReply::finished, this, [r, guard] {
            QJsonDocument doc;
            QString err;
            if (!ApiClient::parseReply(r, &doc, &err)) {
                if (guard)
                    guard->setHistoryUnavailable(QStringLiteral("获取历史版本失败：%1").arg(err));
                return;
            }
            QList<NoteHistory> items;
            const auto arr = doc.isArray() ? doc.array() : QJsonArray();
            for (const auto &v : arr)
                items << NoteHistory::fromJson(v.toObject());
            if (guard)
                guard->setHistory(items);
        });
    }

    // 来源设备名解析（best-effort）：device_id → 已配对设备的别名/名称；失败则保留原始 id
    if (id >= 0 && !isOffline() && !note.deviceId.isEmpty()) {
        QNetworkReply *rd = m_api->getSyncDevices();
        connect(rd, &QNetworkReply::finished, this, [rd, guard, note] {
            QJsonDocument doc;
            QString err;
            if (!ApiClient::parseReply(rd, &doc, &err))
                return;
            const auto arr = doc.isArray() ? doc.array() : QJsonArray();
            for (const auto &v : arr) {
                if (!v.isObject())
                    continue;
                const SyncDevice d = SyncDevice::fromJson(v.toObject());
                if (d.id != note.deviceId)
                    continue;
                QString name = d.alias;
                if (name.isEmpty())
                    name = d.name;
                if (d.isSelf)
                    name += QStringLiteral("（本机）");
                if (!name.isEmpty() && guard)
                    guard->setDeviceName(name);
                break;
            }
        });
    }
}

void InboxPage::onConvertToTodo(qint64 id)
{
    // 找到笔记（内存列表 → 本地镜像）
    Note note;
    bool found = false;
    for (const Note &n : m_notes) {
        if (n.id == id) {
            note = n;
            found = true;
            break;
        }
    }
    if (!found) {
        if (const Note *p = m_store.find(id)) {
            note = *p;
            found = true;
        }
    }
    if (!found)
        return;

    // 转换走服务端两步调用（先建 Todo 再删笔记），离线/本地未同步无法执行
    if (isOffline() || id < 0) {
        QMessageBox::information(this, QStringLiteral("转为待办"),
                                 QStringLiteral("当前离线（或笔记尚未同步），暂不支持转为待办。"));
        return;
    }

    const auto ret = QMessageBox::question(
        this, QStringLiteral("转为待办"),
        QStringLiteral("把该笔记原样转为一条待办，并删除原笔记？"),
        QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
    if (ret != QMessageBox::Yes)
        return;

    const QStringList tags = note.tags;
    QNetworkReply *r = m_api->createTodo(extractTodoTitle(note.content), note.content, tags);
    connect(r, &QNetworkReply::finished, this, [this, r, id] {
        QJsonDocument doc;
        QString err;
        if (!ApiClient::parseReply(r, &doc, &err)) {
            // 步骤 1 失败：不删笔记（防数据丢失），保留原文
            if (isConnectionError(r)) {
                m_online = false;
                startReconnect();
                updateOfflineBadge();
            }
            QMessageBox::warning(this, QStringLiteral("转为待办"),
                                 QStringLiteral("转换失败：%1").arg(err));
            return;
        }
        // 步骤 2：Todo 已建，删除原笔记；删除失败则保留笔记并提示
        QNetworkReply *rd = m_api->deleteNote(id);
        connect(rd, &QNetworkReply::finished, this, [this, rd] {
            QJsonDocument doc2;
            QString err2;
            if (!ApiClient::parseReply(rd, &doc2, &err2)) {
                setStatus(StatusBadge::State::Connected);
                QMessageBox::warning(this, QStringLiteral("转为待办"),
                                     QStringLiteral("已转为待办，原笔记删除失败（%1）").arg(err2));
            } else {
                setStatus(StatusBadge::State::Connected);
            }
            // 保留当前筛选上下文整体刷新（loadNotes 自带 m_currentTag / 搜索）
            refreshAll();
        });
    });
}

void InboxPage::onTaskToggled(qint64 id, const QString &content)
{
    applyContent(id, content);
}

void InboxPage::applyContent(qint64 id, const QString &text)
{
    const QStringList tags = extractTags(text);

    if (isOffline() || id < 0) {
        // 离线，或编辑的是本地未同步的新建（负 id）：只改本地
        updateLocal(id, text, tags);
        return;
    }
    QNetworkReply *r = m_api->updateNote(id, text, tags);
    connect(r, &QNetworkReply::finished, this, [this, r, id, text, tags] {
        QJsonDocument doc;
        QString err;
        if (!ApiClient::parseReply(r, &doc, &err)) {
            m_online = false;
            startReconnect();
            updateLocal(id, text, tags);
            return;
        }
        // 编辑保存（含详情页版本恢复、任务清单勾选）成功：刷新后定位并高亮该笔记
        m_pendingJumpId = id;
        refreshAll();
    });
}

void InboxPage::onDeleteNote(qint64 id)
{
    const auto ret = QMessageBox::question(this, QStringLiteral("删除笔记"),
                                           QStringLiteral("确定删除这条笔记？"));
    if (ret != QMessageBox::Yes)
        return;

    if (isOffline() || id < 0) {
        // 离线，或本地未同步的新建：本地删除（新建会直接消失，服务端笔记留 tombstone）
        deleteLocal(id);
        return;
    }
    QNetworkReply *r = m_api->deleteNote(id);
    connect(r, &QNetworkReply::finished, this, [this, r, id] {
        QJsonDocument doc;
        QString err;
        if (!ApiClient::parseReply(r, &doc, &err)) {
            m_online = false;
            startReconnect();
            deleteLocal(id);
            return;
        }
        m_store.drop(id);
        m_store.save();
        refreshAll();
    });
}

void InboxPage::onComment(qint64 id)
{
    auto *dlg = new CommentsDialog(id, this);
    // 离线优先：先显示本地缓存的评论（服务端不可用也能看已加载的评论）
    dlg->setComments(m_store.commentsFor(id));
    if (!isOffline()) {
        // 在线时再异步拉取服务端评论，成功后合并刷新（失败静默，保留本地缓存，不弹连接错误）
        QNetworkReply *r = m_api->getComments(id);
        QPointer<CommentsDialog> guard(dlg);
        connect(r, &QNetworkReply::finished, this, [this, r, guard, id] {
            QJsonDocument doc;
            QString err;
            if (ApiClient::parseReply(r, &doc, &err)) {
                QList<Comment> comments;
                for (const auto &v : doc.array()) {
                    if (v.isObject())
                        comments << Comment::fromJson(v.toObject());
                }
                m_store.setComments(id, comments); // 覆盖服务端数据，保留本地待同步评论
                m_store.save();
                if (guard)
                    guard->setComments(m_store.commentsFor(id));
            }
        });
    }
    if (dlg->exec() != QDialog::Accepted) {
        dlg->deleteLater();
        return;
    }
    const QString text = dlg->commentText();
    dlg->deleteLater();
    if (text.isEmpty())
        return;
    submitComment(id, text);
}

void InboxPage::submitComment(qint64 noteId, const QString &text)
{
    // 离线优先：先落本地 —— 创建一条本地评论笔记（收件箱立即可见）+ 评论缓存 + 入待同步队列
    const QString ts = m_store.addLocalComment(noteId, text, m_api->deviceId());
    m_store.save();

    if (isOffline()) {
        // 服务端不可用：本地保存并立即渲染（评论笔记出现在收件箱），重连后自动补推
        renderLocal();
        setStatus(StatusBadge::State::Disconnected, QStringLiteral("评论已保存，离线待同步"));
        QTimer::singleShot(2000, this, [this] { updateOfflineBadge(); });
        return;
    }

    // 在线：直接推送；期间标记在途，避免 loadNotes 触发的 pushComments 重复补推同一条
    m_inflightComments.insert(ts);
    QNetworkReply *rr = m_api->addComment(noteId, text);
    connect(rr, &QNetworkReply::finished, this, [this, rr, noteId, text, ts] {
        m_inflightComments.remove(ts);
        QJsonDocument doc;
        QString err;
        const bool connErr = isConnectionError(rr);
        if (!ApiClient::parseReply(rr, &doc, &err)) {
            // 连接类失败（服务端刚挂）：切离线等待重连补推
            if (connErr) {
                m_online = false;
                startReconnect();
            }
            // 服务端未接受：评论笔记保留在本地（带待同步标记），渲染到收件箱
            renderLocal();
            setStatus(StatusBadge::State::Connected, QStringLiteral("评论已本地保存，待同步"));
            QTimer::singleShot(2000, this, [this] { updateOfflineBadge(); });
            return;
        }
        // 成功：本地评论笔记转正（重映射到服务端 id 并清 pending），再拉回全量刷新
        // 服务端把评论也建成一条笔记并返回，因此刷新后评论笔记会出现在收件箱
        const qint64 serverNoteId = doc.object().value(QLatin1String("id")).toVariant().toLongLong();
        m_store.confirmComment(noteId, text, ts, serverNoteId);
        m_store.save();
        setStatus(StatusBadge::State::Connected, QStringLiteral("评论已发表"));
        // 刷新后定位并高亮新评论笔记（Android 快速发送后跳转定位语义）
        m_pendingJumpId = serverNoteId;
        QTimer::singleShot(2000, this, [this] { updateOfflineBadge(); });
        refreshAll();
    });
}

void InboxPage::pushComments()
{
    const QList<PendingComment> pending = m_store.pendingComments();
    if (pending.isEmpty())
        return;

    // 过滤掉正在由 submitComment 直接推送的同一条评论，避免重复 POST
    QList<PendingComment> toPush;
    for (const PendingComment &c : pending) {
        if (!m_inflightComments.contains(c.createdAt))
            toPush << c;
    }
    if (toPush.isEmpty()) {
        m_pendingPush = 0;
        return;
    }

    m_pendingPush = toPush.size();
    setStatus(StatusBadge::State::Syncing,
              QStringLiteral("补推 %1 条离线评论…").arg(m_pendingPush));

    for (const PendingComment &c : toPush) {
        QNetworkReply *r = m_api->addComment(c.noteId, c.content);
        connect(r, &QNetworkReply::finished, this, [this, r, c] {
            QJsonDocument doc;
            QString err;
            const bool connErr = isConnectionError(r);
            if (!ApiClient::parseReply(r, &doc, &err)) {
                // 连接类失败才整机切离线；4xx（无评论端点）保留待同步并继续
                if (connErr) {
                    pushFailed();
                } else {
                    if (--m_pendingPush <= 0) {
                        m_pendingPush = 0;
                        updateOfflineBadge();
                    }
                }
                return;
            }
            // 成功：本地评论笔记转正（重映射到服务端 id），收件箱保留该评论笔记
            const qint64 serverNoteId = doc.object().value(QLatin1String("id")).toVariant().toLongLong();
            m_store.confirmComment(c.noteId, c.content, c.createdAt, serverNoteId);
            m_store.save();
            pushDone();
        });
    }
}

void InboxPage::onCopyAll()
{
    QStringList lines;
    for (const Note &n : m_notes) {
        // 与 aw-webui「复制全部」同款格式：修改时间∣内容
        lines << QStringLiteral("%1∣%2").arg(formatLocal(n.updatedAt.isEmpty() ? n.createdAt : n.updatedAt),
                                              n.content);
    }
    QApplication::clipboard()->setText(lines.join(QLatin1Char('\n')));
    setStatus(StatusBadge::State::Connected, QStringLiteral("已复制 %1 条").arg(lines.size()));
    QTimer::singleShot(1500, this, [this] { setStatus(StatusBadge::State::Connected); });
}

void InboxPage::setStatus(StatusBadge::State s, const QString &text)
{
    m_badge->setState(s, text);
}

} // namespace awqtui
